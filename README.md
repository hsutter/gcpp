# **gcpp**: Demo of "deferred object lifetime as a library" for C++

Herb Sutter -- Updated 2016-09-15

## Overview

The gcpp library is an experiment at how we might automate some support for deferred object lifetime as a library for the C++ toolbox. It aims to continue C++'s long tradition of being a great language for building libraries, including for memory allocation and lifetime management.

- We already have `make_unique` and `unique_ptr` to automate managing the lifetime of a single uniquely owned heap object. Using them is as efficient as (and considerably easier and less brittle than) writing `new` and `delete` by hand.

- Similarly, `make_shared` and `shared_ptr` automate managing the lifetime of a single shared heap object. Using them is usually as efficient as (and always easier and less brittle than) managing reference counts by hand.

- In gcpp, `deferred_heap` and `deferred_ptr` take a stab at how we might automate managing the lifetime of a group of shared heap objects that (a) may contain cycles or (b) need deterministic pointer manipulation cost. The goal is that using them be usually as efficient as (and easier and more robuse than) managing ownership and writing custom tracing logic to discover and destroy unreachable objects by hand, including deferring and invoking destructors.

## Goals and non-goals

_**gcpp is**_:

- a demo of another fallback option for the rare cases where `unique_ptr`, `shared_ptr`, and `weak_ptr` aren't quite enough, notably when you have objects that refer to each other in owning cycles or when you need to defer destructor execution to meet real-time deadlines;

- designed to encourage tactical isolated use, where each `deferred_heap` is its own little self-contained island of memory and objects;

- strict about meeting C++'s zero-overhead abstraction principle (a.k.a. "you don't pay for what you don't use and you usually couldn't write it more efficiently by hand") -- space and time cost is always proportional to how much `deferred_heap` allocation and `deferred_ptr` use your code performs, including zero cost if your code never does any allocation; and

- a fun project to try out and demo some ideas you might borrow to write your own similar facility.

_**gcpp is not**_:

- production quality, so don't email me for support;

- scalable, so don't try having millions of pointers; or

- well tested, so expect bugs.

_**gcpp will not ever**_ trace the whole C++ heap, incur uncontrollable or global pauses, add a "finalizer" concept, permit object "resurrection," be recommended as a default allocator, or replace `unique_ptr` and `shared_ptr` -- we are very happy with C++'s current lifetime model, and the aim here is only to see if we can add a fourth fallback when today's options are insufficient to replace code we would otherwise have to write by hand in a custom way for each place we need it.

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

Finally, `deferred_allocator` wraps up a `deferred_heap` as an STL allocator. This was not an original use case, and is not a primary target at the moment, but it's an interesting case to explore because it might just fall out once a `deferred_heap` abstraction is available and it may have some interesting properties that are useful for safer use of STL in domains where allowing iterator dereference errors to have undefined behavior is not tolerable.

- When using a `container<T, deferred_allocator<T>>`, iterators can’t dangle (point to a destroyed or deallocated object) because iterators keep objects alive. This turns dereferencing an invalidated iterator from an undefined behavior problem into a stale data problem.

- See notes below for limitations on iterator navigation using invalidated iterators.

- Note: `deferred_allocator` relies on C++11's allocator extensions to support "fancy" user-defined pointer types. It does not work with pre-C++11 standard libraries, which required `allocator::pointer` to be a raw pointer type. If you are using Microsoft Visual C++, the current implementation of gcpp requires Visual Studio 2015 Update 3 (or later); it does not work on Update 2 which did not yet have enough fancy pointer support.

## Object lifetime guidance: For C++17 and for the gcpp library

The following summarizes the best practices we should already teach for expressing object lifetimes in C++17, and at the end adds a potential new fallback option to consider something along these lines.

| Guidance / library | What it automates | Efficiency | Clarity and correctness |
|--------------------|-------------------|------------|-------------------------|
| [C++17] **1. Where possible, prefer scoped lifetime** by default (e.g., locals, members) | Expressing that this object's lifetime is tied to some other lifetime that is already well defined, such as a block scope (`auto` storage duration) or another object (member lifetime) | Zero additional lifetime management overhead for this object | |
| [C++17] **2. Else prefer `make_unique` and `unique_ptr`**, if the object must have its own lifetime (i.e., heap) and ownership can be unique without ownership cycles | Single-ownership heap object lifetime | Usually identical cost as correctly written `new`+`delete` | Clearer and more robust than explicit `delete` (declarative, uses are correct by construction) |
| [C++17] **3. Else prefer `make_shared` and `shared_ptr`**, if the object must have its own lifetime (i.e., heap) and shared ownership, without ownership cycles | Acyclic shared heap object lifetime managed with reference counting | Usually identical cost as correctly written manual reference counting | Clearer and more robust than manual/custom reference count logic (declarative, uses are correct by construction) |
| [experimental] **4. Else use similar techniques as `deferred_heap` and `deferred_ptr`**, if the object must have its own lifetime (i.e., heap) and there can be ownership cycles | Potentially-cyclic shared heap object lifetime managed with liveness tracing<br><br>Real-time code with bounded pointer assignment cost requirements<br><br>Constrained stacks with bounded call depth requirements | (conjecture) Usually identical cost as correctly written manual tracing | (conjecture) Clearer and more robust than manual/custom tracing logic (declarative, uses are correct by construction) |


# Implementation notes

## deferred_heap and deferred_ptr

`deferred_heap` encapsulates memory containing objects that can be safely accessed via `deferred_ptr<T>`.

- `collect()` is local: It traces only `deferred_heap`-allocated objects, and only this `deferred_heap`.

- `collect()` is explicit: It runs only when called, under program control.

    - If you never call `collect()`, a `deferred_heap` behaves like a [region](https://en.wikipedia.org/wiki/Region-based_memory_management) that deallocates all memory efficiently at once. Unlike most regions which just let go of the memory, it will first  run any pending destructors and only then efficiently just let go of the memory.

- `make<T>()` allocates and constructs a new `T` object and returns a `deferred_ptr<T>`.

   - If `T` has a nontrivial destructor, the destructor is remembered and run during `.collect()` if the object has become unreachable. This is necessary if we want to try to make using `deferred_ptr` also type-safe, in addition to memory-safe.

`deferred_ptr` refers to an object in a `deferred_heap`.

- Additional rule: A `deferred_ptr` member is null in a deferred destructor. This enables safe unordered destruction by enforcing known best practices in other environments, in particular:

   - The deferred destructor runs normally, but cannot access another deferred-destruction object that could possibly be destroyed in the same collection cycle (which might already have been destroyed). This eliminates the "accessing a disposed/finalized object" class of bugs that is possible in environments like Java, .NET, and Go, and eliminates the "leaking cycles of objects that use finalizers" class of bugs that is possible in Go.

   - It is not possible to "resurrect" another deferred-lifetime object by storing a `deferred_ptr` to it somewhere that would make that object reachable again (and an object cannot make itself reachable again because it cannot form a `deferred_ptr` to itself). This eliminates the "double dispose" class of bugs that is possible in environments like Java, .NET, and Go.

## deferred_allocator (speculative)

`deferred_allocator` is an experiment to wrap a `deferred_heap` up as a C++11 allocator. It appears to work with unmodified current STL containers, but I'm still exploring how well and exploring the limits. Note that `deferred_allocator` requires C++11 allocator support for fancy pointers; on MSVC, it requires Visual Studio 2015 Update 3 or later.

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

