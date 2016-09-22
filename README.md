# **gcpp**: Deferred and unordered destruction

Herb Sutter -- Updated 2016-09-21

## Motivation, goals, and non-goals

gcpp is a personal project to try an experiment: Can we take the deferred and unordered destruction patterns with custom reachability tracing logic that we find ourselves writing by hand today, and automate some parts of that work as a reusable C++ library that delivers it as a zero-overhead abstraction?

This is a demo of a potential additional fallback option for the rare cases where `unique_ptr`, `shared_ptr`, and `weak_ptr` aren't quite enough, notably when you have objects that refer to each other in local owning cycles, or when you need to defer destructor execution to meet real-time deadlines or to bound destructor stack cost. The goal is to illustrate ideas that others can draw from, that you may find useful even if you never use types like the ones below but just continue to use existing smart pointers and write your destructor-deferral and tracing code by hand.

Disclaimers: This is a demo, not a production quality library. I have only tried it on one compiler and STL implementation, Visual Studio 2015 Update 3 (`deferred_allocator` does not work on Update 2 which had only partial support for C++11 allocators with fancy pointers); if you have success with others, please report it by opening an Issue to update this README. See [FAQ: So there are no disadvantages?](#so-there-are-no-disadvantages).

## Overview

### deferred_heap

A `deferred_heap` owns a "bubble" of objects that can be safely accessed via `deferred_ptr<T>`, and can point to each other arbitrarily within the same heap. It has three main functions:

- `.make<T>()` allocates and constructs a new `T` object and returns a `deferred_ptr<T>`. If `T` is not trivially destructible, it will also record the destructor to be eventually invoked.

- `.collect()` is called explicitly by default and traces this local heap in isolation; it never traces outside this heap and its roots, including that it does not touch the memory owned by any other `deferred_heap`s or any other program data structures. Any unreachable objects will have their deferred destructors run before their memory is deallocated. Cycles of `deferred_ptr`s within the same heap are destroyed when the cycle is no longer reachable.

- `~deferred_heap()` runs any remaining deferred destructors and resets any `deferred_ptr`s that outlive this heap to null, then releases its memory all at once [like a region](#is-deferred_heap-equivalent-to-a-region).

Because `.collect()` and the destructor are explicit, the program can choose when (at a convenient time) and where (e.g., on what thread or processor) to run destructors.

Local small heaps are encouraged. This keeps tracing isolated and composable; combining libraries that each use `deferred_heap`s internally will not directly affect each other's performance.

### deferred_ptr<T>

A `deferred_ptr<T>` refers to a `T` object in a `deferred_heap`. You can construct a `deferred_ptr` as a copy of an existing one (including one returned by `deferred_heap::make`) or by default-constructing it to null.

It has the same functions as any normal C++ smart pointer, including derived-to-base and const conversions. Here are the main additional features and semantics:

- Assigning a `deferred_ptr` is not allowed across heaps. The pointer must continue pointing into whichever heap it initially pointed into. A default-constructed pointer does not point into any heap, but once assigned to point into a given heap it must continue to point there.

- A `deferred_ptr` member is null in a deferred destructor. This enables safe unordered destruction by enforcing known best practices in other environments, in particular:

   - It enforces that the otherwise-normal deferred destructor cannot access another deferred-destruction object that could possibly be destroyed in the same collection cycle (which might already have been destroyed). This eliminates the "accessing a disposed/finalized object" class of bugs that is possible in environments like Java, .NET, and Go, and eliminates the "leaking cycles of objects that use finalizers" class of bugs that is possible in Go.

   - It enforces that a destructor cannot "resurrect" another deferred-lifetime object (or itself) by storing a `deferred_ptr` to it somewhere that would make that object reachable again. This eliminates the "double dispose" class of bugs that is possible in environments like Java, .NET, and Go, as well closes as a second route to the "accessing a disposed/finalized object" class of bugs mentioned in the previous bullet.

- Like `shared_ptr`'s aliasing constructor, `deferred_ptr` supports creating a smart pointer to a data member subobject of a deferred object. However, I'm currently doing it in a different way from `shared_ptr`. Whereas `shared_ptr`'s aliasing constructor can accept any pointer value and so isn't type-safe or memory-safe, `deferred_ptr<T>` instead provides a `.ptr_to<U>(U T::*)` function that takes a pointer to member of `T` and so type- and memory-safely guarantees at compile time that you can only use it to form a `deferred_ptr<U>` to a valid `U` subobject of a deferred `T` object via a valid `deferred_ptr<T>`.

### deferred_allocator

Finally, `deferred_allocator` is a C++11 STL allocator that you can use to have an STL container store its elements in a `deferred_heap`. This is useful when a deferred-lifetime object contains a container of `deferred_ptr`s to other objects in its heap; using `deferred_allocator` expresses that the container's `deferred_ptr`s are inside the heap so that any cycles they participate it can be automatically cleaned up, whereas not using `deferred_allocator` expresses that the container's `deferred_ptr`s are outside the heap (i.e., roots).

Convenience aliases are provided; for example, `deferred_vector<T>` is an alias for `std::vector<T, gcpp::deferred_allocator<T>>`.

Note: 

## Example

Here is a `Graph` type that has its own local heap shared by all `Graph` objects:

~~~
// possibly-cyclic N-ary Graph, one heap for all graph nodes
class Graph {
    struct Node {
        deferred_vector<deferred_ptr<Node>> outbound_edges{ my_heap };  // keeps all edges in the heap
        /*... data ...*/
    };

    static deferred_heap my_heap;
    vector<deferred_ptr<Node>> roots;

public:
    void SampleFunction() {
        roots.emplace_back(my_heap.make<Node>());
    }

    // ... etc.
};
~~~

Here is a variation where each `Graph` object has its own private local heap: 

~~~
// possibly-cyclic N-ary Graph, one heap per graph object
class Graph {
    struct Node {
        /*... data ...*/
        deferred_vector<deferred_ptr<Node>> outbound_edges;  // keeps all edges in the heap
        Node(deferred_heap& h) : outbound_edges{h} { }
    };

    deferred_heap my_heap;
    vector<deferred_ptr<Node>> roots;

public:
    void SampleFunction() {
        roots.emplace_back(my_heap.make<Node>(my_heap));
    }

    // ... etc.
};
~~~

Note that `deferred_allocator` requires C++11 allocator support for fancy pointers; on MSVC, it requires Visual Studio 2015 Update 3 or later.

## Target use cases

gcpp aims to address three main issues, and a speculative use case.

### 1. Ownership cycles, preventing leaks

The first target use case is automatic by-construction memory management for data structures with cycles, which cannot be expressed with fully automated lifetime using C++17 facilities only.

- Encapsulated example: A `Graph` type whose `Node`s point to each other but should stay alive as long as they are transitively reachable from the enclosing `Graph` object or from some object such as a `Graph::iterator` that is handed out by the `Graph` object.

- Unencapsulated example: A group of objects of different types that refer to each other in a potentially-cyclic way but should stay alive as long as they are transitively reachable from some root outside their `deferred_heap`.

- In C++17, both examples can be partly automated (e.g., having `Graph` contain a `vector<unique_ptr<Node>> my_nodes;` so that all nodes are guaranteed to be destroyed at least when the `Graph` is destroyed), but require at minimum manual liveness tracing today (e.g., traversal logic to discover unreachable `Node` objects, which essentially implements a custom tracing algorithm by hand for the nodes, and then `my_nodes.erase(unreachable_node);` to remove each unreachable node which is manual and morally equivalent to `delete unreachable_node;`).

### 2. Real time systems, bounding the cost of pointer assignment

Using `shared_ptr` can be problematic in real time systems code, because any simple `shared_ptr` pointer assignment or destruction could cause an arbitrary number of objects to be destroyed, and therefore have unbounded cost. This is a rare case of where prompt destruction, usually a wonderful property, is actually bad for performance. (Note: This is less of a problem with `unique_ptr` because `unique_ptr` assignment necessarily causes destruction and so tends to be treated more carefully, whereas `shared_ptr` assignment *might* cause destruction.)

Today, deadline-driven code must either avoid manipulating `shared_ptr`s or take defensive measures:

- Banning `shared_ptr` assignment or destruction entirely in deadline-driven code, such as by deferring the pointer assignment until after the end of the critical code region, is not always an option because the code may need to change which object a pointer refers to while still inside the critical region.

- Bounding the number of objects reachable from a `shared_ptr` can make the assignment cost have an upper bound to fit within the deadline, but requires ongoing care during maintenance. Adding an additional object, or changing the cost of a destructor, can exceed the bound.

- Deferring destruction by storing additional strong references in a separate "keepalive" data structure allows tracing to be performed later, outside the critical code region, to identify and destroy those objects no longer referred to from outside the keepalive structure. However, this amounts to another form of manually implemented liveness tracing.

By design, `deferred_ptr` assignment cost is bounded and unrelated to destructors, and `deferred_heap` gives full control over when and where `collect()` runs deferred destructors. This makes it a candidate for being appropriate for real-time code in situations where using `shared_ptr` may be problematic.

### 3. Constrained systems, bounding stack depth of destruction

Using `unique_ptr` and `shared_ptr` can be problematic in systems with constrained stack space and deep ownership. Because destructors are always run nested (recursively), the thread that releases an object must have sufficient stack space for the call depth required to destroy the tree of objects being released. If the tree can be arbitrarily deep, an arbitrary about of stack space may be needed.

Today, systems with constrained stacks use similar techniques to those mentioned in #2 above, with similar limitations and tradeoffs.

By design, `deferred_heap` runs deferred destructors iteratively, not recursively. Of course, an *individual* deferred object may still own a tree of resources that may use `shared_ptr`s and be freed recursively, but any two deferred objects are destroyed iteratively even if they referred to each other and their destructors never nest. This makes it a candidate for being appropriate for real-time code in situations where using `shared_ptr` may be problematic.

### (speculative) Possible bonus use case: deferred_allocator for STL containers 

Besides being useful to have containers of `deferred_ptr`s whose pointers live in a `deferred_heap`, using just a `container<T, deferred_allocator<T>>` by itself without explicit `deferred_ptr`s may have some interesting properties that could be useful for safer use of STL in domains where allowing iterator dereference errors to have undefined behavior is not tolerable.

- When using a `container<T, deferred_allocator<T>>`, iterators can’t dangle (point to a destroyed or deallocated object) because iterators keep objects alive and destructors are deferred. This turns dereferencing an invalidated iterator from an undefined behavior problem into a stale data problem.

- See [Implementation notes](#implementation-notes) for limitations on iterator navigation using invalidated iterators.

- Note: `deferred_allocator` relies on C++11's allocator extensions to support "fancy" user-defined pointer types. It does not work with pre-C++11 standard libraries, which required `allocator::pointer` to be a raw pointer type. If you are using Microsoft Visual C++, the current implementation of gcpp requires Visual Studio 2015 Update 3 (or later); it does not work on Update 2 which did not yet have enough fancy pointer support.

## Object lifetime guidance: For C++17 and for the gcpp library

The following summarizes the best practices we should already teach for expressing object lifetimes in C++17, and at the end adds a potential new fallback option to consider something along these lines.

| Guidance / library | What it automates | Efficiency | Clarity and correctness |
|--------------------|-------------------|------------|-------------------------|
| [C++17] **1. Where possible, prefer scoped lifetime** by default (e.g., locals, members) | Expressing that this object's lifetime is tied to some other lifetime that is already well defined, such as a block scope (`auto` storage duration) or another object (member lifetime) | Zero additional lifetime management overhead for this object | |
| [C++17] **2. Else prefer `make_unique` and `unique_ptr`**, if the object must have its own lifetime (i.e., heap) and ownership can be unique without ownership cycles | Single-ownership heap object lifetime | Usually identical cost as correctly written `new`+`delete` | Clearer and more robust than explicit `delete` (declarative, uses are correct by construction) |
| [C++17] **3. Else prefer `make_shared` and `shared_ptr`**, if the object must have its own lifetime (i.e., heap) and shared ownership, without ownership cycles | Acyclic shared heap object lifetime managed with reference counting | Usually identical cost as correctly written manual reference counting | Clearer and more robust than manual/custom reference count logic (declarative, uses are correct by construction) |
| [experimental] **4. Else use similar techniques as `deferred_heap` and `deferred_ptr`**, if the object must have its own lifetime (i.e., heap) and there can be ownership cycles | Potentially-cyclic shared heap object lifetime managed with liveness tracing<br><br>Real-time code with bounded pointer assignment cost requirements<br><br>Constrained stacks with bounded call depth requirements | (conjecture) Usually identical cost as correctly written manual tracing | (conjecture) Clearer and more robust than manual/custom tracing logic (declarative, uses are correct by construction) |


# FAQs

## Is this [garbage collection](https://en.wikipedia.org/wiki/Garbage_collection_(computer_science))?

Of course, and remember so is reference counting.

## I meant, is this just [tracing garbage collection](https://en.wikipedia.org/wiki/Tracing_garbage_collection) (GC)?

Not the tracing GC most people are familiar with.

Most importantly, **`deferred_heap` collects objects, not garbage**:

- Most mainstream tracing GC is about *managing raw memory*, which can turn into meaningless "garbage" bytes. Tearing down the real objects that live in that memory is at best a brittle afterthought not designed as an integrated part of the language runtime; see the restrictions on ["finalizers"](https://en.wikipedia.org/wiki/Finalizer) in Java, C#, D, Go, etc. (in C# what are called "destructors" are finalizers) -- all the major GC-based environments I know of that have more than 10 years' field experience with finalizers now recommend avoiding finalizers outright in released code ([example](http://www.hboehm.info/gc/finalization.html)), for the same reasons: they are not guaranteed to be executed, they are fragile because a finalizer should not (but can) access other possibly-finalized objects, and they lead to unmanageable complications like [object resurrection](https://en.wikipedia.org/wiki/Object_resurrection) (the worst case of which is making a *finalized* object reachable again).

- `deferred_heap` is fundamentally about *managing objects*, and is focused on deferring real destructors. So although it does perform liveness tracing, the most important way it differs from the mainstream tracing GC designs is that it tracks and collects constructed objects, and runs their deferred destructors. And because of that emphasis on running real destructors safely, `deferred_heap` makes all of a collectable object's `deferred_ptr`s null before running its destructor, which entirely prevents accessing possibly-destroyed objects and object resurrection. 

    - Note: gcpp is a demo for people to draw ideas from. That includes other languages; I encourage other GC-based languages to consider just nulling object references *that point to other finalizable objects* before running a round of finalizers. That should nearly always turn a latent bug into a NullPointerException/NullReferenceException/nil-pointer-panic/etc. Although it could be a breaking change in behavior for some programs, it is very likely to "break" only code that is already living on the edge and doing things your language's experts already recommend they not do. In C++, when we discuss changes that would break suspicious code, Bjarne Stroustrup likes to call it "code that deserves to be broken." So, think about it, and consider whether it makes sense to "break" code deliberately in your language for this case.

The other important difference is that **`deferred_heap` meets C++'s zero-overhead principle by being opt-in and granular, not default and global**:

- Most mainstream languages that assume GC make the garbage-collected allocator the primary or only way to create heap objects, and the program shares a single global heap. Performing something other than GC allocation requires fighting with, and avoiding large parts of, the language and its standard library; for example, by resorting to writing your own allocator by allocating a large array and using unsafe pointers, or by calling out to native code. Further, tracing collection performance (e.g., GC pauses) of one part of the program can depend on allocation done by an unrelated library linked into the program.

- `deferred_heap` is intended to be used tactically as another fallback in situations where options like `unique_ptr` and `shared_ptr` are insufficient, and even then in a granular way with a distinct `deferred_heap` within a class (or at most module). You don't pay for what you don't use: If you never perform a deferred allocation then there is zero cost, and if you do perform a deferred allocation then the cost is always proportional to the amount of deferred allocation you do. Tracing is performed only within each individual `deferred_heap` bubble, and cycles must stay within the same heap in order to be collected. (Yes, it's possible to instantiate and share a global `deferred_heap`, but that isn't the way I intend this to be used, and certainly the current implementation won't scale well to millions of pointers.)


## Is this related/similar to the [Boehm collector](http://www.hboehm.info/gc/)?"

No. That is a fine collector, but with different aims: It doesn't run destructors; even for registered finalizers, cycles of finalizable objects are never finalized. Its tracing is [conservative](http://stackoverflow.com/questions/7629446/conservative-garbage-collector) and more global (touches memory beyond the actual GC allocations and roots, such as to discover roots conservatively).

`deferred_heap` runs destructors, and the tracing is accurate (not conservative) and scoped to an individual granular heap.


## Is deferred_heap equivalent to a region?

It's a strict superset of [region-based memory management](https://en.wikipedia.org/wiki/Region-based_memory_management).

The key idea of a region is to efficiently release the region's memory in one shot when the region is destroyed, with deallocate-at-once semantics. `deferred_heap` does that, but goes further in three ways:

- It adds  optional **collection** via `.collect()` to reclaim unused parts of the region without waiting for the whole region to be destroyed. If you don't call `.collect()`, then when the heap is destroyed the heap's memory is released all at once, exactly like a region.

- It owns **objects**, not just raw memory, and always correctly destroys any pending destructors for objects that are still in the heap when it is destroyed (or, if `.collect()` is called, that are unreachable). If you allocate only trivially destructible objects, then this adds zero work -- when the heap is destroyed, no additional destructors need to be run.

- It knows its **roots**, and safely resets any outstanding `deferred_ptr`s that outlive the heap they point into. If you don't let any `deferred_ptr`s outlive the heap they point into, then this adds zero work -- when the heap is destroyed, no additional nulling needs to be performed.

The only work performed in the `deferred_heap` destructor is to run pending destructors, null out any `deferred_ptr`s that outlive the heap, and release the heap's memory pages. So if you use a `deferred_heap` and never call `.collect()`, never allocate a non-trivially destructible object, and never let a `deferred_ptr` outlive the heap, then destroying the heap does exactly nothing beyond efficiently dropping any memory it owns with deallocate-at-once semantics -- and then, yes, it's a region. But, unlike a region, you *can* do any of those things, and if you do then they are safe.


## So there are no disadvantages?

Sure there are disadvantages, especially in this demo implementation.

- It does more work and therefore has more overhead than `unique_ptr` and `shared_ptr`. Prefer `unique_ptr` and `shared_ptr` in that order where possible, as usual.

- The current implementation is not production-quality. In particular, it's a pure library solution that requires no compiler support, it's single-threaded, it registers every `deferred_ptr`, and it doesn't try to optimize its marking algorithm. The GC literature and experience is full of ways to make this faster; for example, a compiler optimizer that is aware of `deferred_ptr` could optimize away all registration of stack-based `deferred_ptr`s by generating stack maps. (GC experts, feel free to plug in your favorite real GC implementation under the `deferred_heap` interface and let us know how it goes. I've factored out the destructor tracking to keep it separate from the heap implementation, to make it easier to plug in just the GC memory and tracing management implementation.)


## Why create another smart pointer? another allocator?

Because that's how we roll in C++.

gcpp aims to continue C++'s long tradition of being a great language for building libraries, including for memory allocation and lifetime management.

- We already have `make_unique` and `unique_ptr` to automate managing the lifetime of a single uniquely owned heap object. Using them is as efficient as (and considerably easier and less brittle than) writing `new` and `delete` by hand. Prefer this first when putting an object in a local stack scope or as a data member of another object is not sufficient.  

- Similarly, `make_shared` and `shared_ptr` automate managing the lifetime of a single shared heap object. Using them is usually as efficient as (and always easier and less brittle than) managing reference counts by hand. Prefer this next when `unique_ptr` is not sufficient.

- In gcpp, `deferred_heap` and `deferred_ptr` take a stab at how we might automate managing the lifetime of a **group of related shared heap objects** that (a) may contain cycles and/or (b) needs deterministic pointer manipulation space and time cost. The goal is that using them be usually as efficient as (and easier and more robust than) managing ownership and writing custom tracing logic by hand to discover and perform destruction of unreachable objects. Because reachability is a property of the whole group, not of a single object or subgroup, an abstraction that owns the whole group is needed. Consider this, or write similar logic by hand, when you have a situation where neither `unique_ptr` nor `shared_ptr` are sufficient.


# Implementation notes

## deferred_allocator

`deferred_allocator` is an experiment to wrap a `deferred_heap` up as a C++11 allocator. It appears to work with unmodified current STL containers, but I'm still exploring how well and exploring the limits.

- `deallocate()` is a no-op, but performs checking in debug builds. It does not need to actually deallocate because memory-safe deallocation will happen at the next `.collect()` after the memory becomes unreachable.

- `destroy()` is a no-op, but performs checking in debug builds. It does not need to remember the destructor because that was already correctly recorded when `construct()` was called; see next point. It does not need to call the destructor because the destructor will be called type-safely when the object is unreachable (or, for `vector` only, when the container in-place constructs a new object in the same location; see next subpoint).

- The in-place `construct()` function remembers the type-correct destructor -- if needed, which means only if the object has a nontrivial destructor.

   - `construct()` is available via `deferred_allocator` only, and adds special sauce for handling `vector::pop_back` followed by `push_back`: A pending destructor is also run before constructing an object in a location for which the destructor is pending. This is the only situation where a destructor runs sooner than at `.collect()` time, and only happens when using `vector<T, deferred_allocator<T>>`.
   
   - Note: We assume the container implementation is not malicious. To my knowledge, `deferred_allocator::construct()` is the only operation in gcpp that could be abused in a type-unsafe way, ignoring code that resorts to undefined behavior like `reinterpret_cast`ing `deferred_ptr`s.

- `container<T, deferred_allocator<T>>` iterators keep objects (not just memory) alive. This makes **dereferencing** an invalidated iterator type-safe, as well as memory-safe.

   - The iterator stores `deferred_ptr`, which makes the iterator a strong owner. When dereferencing invalidated iterators, this turns an undefined behavior problem into "just" a stale data problem.

   - For all containers, an invalidated iterator points to a valid object. Note that the object may have a different value than the (buggy) program expects, including that it may be in a moved-from state; also, reading the object via the invalidated iterator is not guaranteed to see changes made to the container, and vice versa.

- However, **using navigation** (e.g., incrementing) invalidated iterators is not much better than today.

   - For all random access iterators that use pointer arithmetic, any use of an iterator to navigate beyond the end of the allocation that the iterator actually points into will fire an assert in debug builds.

   - For a `vector`, an invalidated iterator will keep an outgrown-and-discarded `vector` buffer alive and can still be compared correctly with other iterators into the same actual buffer (i.e., iterators of the same vintage == when the container had the same capacity). In particular, an invalidated iterator obtained before the `vector`'s last expansion cannot be correctly compared to the `vector`'s current `.begin()` or `.end()`, and loops that do that with an invalidated iterator will fire an assert in debug builds (because they perform `deferred_ptr` checked pointer arithmetic).

   - For a node-based container or `deque`, an invalidated iterator refers to a node whose pointers have been destroyed (not just reset). Incrementing an invalidated iterator to a node-based container is still undefined behavior, as today.


# Acknowledgments

I would like to particularly thank the following people for their help:

- Thanks to Casey Carter, Jonathan Caves, Gabriel Dos Reis, Howard Hinnant, Stephan T. Lavavej, and Neil MacIntosh for their feedback on the C++-specific parts and/or help with various detailed C++ language and standard library questions.

- Thanks to Pavel Curtis, Daniel Frampton, Kathryn S McKinley, and Mads Torgersen for their review and suggestions regarding the tracing GC parts.
