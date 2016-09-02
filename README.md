# **gcpp**: A GC heap allocation library for C++

Herb Sutter -- Updated 2016-09-01

## Overview, goals, and non-goals

The gcpp library is an experiment at adding tracing garbage collection as a library to the C++ toolbox. It provides a `gc_heap` type that encapsulates a bubble of memory containing objects accessed via `gc_ptr<T>`.

- _**gcpp is**_ opt-in, deterministic, accurate, type-safe including calling real destructors, scoped and composable including allowing multiple little separately collected `gc_heap`s, and adheres to C++'s zero-overhead abstraction principle (a.k.a. "you don't pay for what you don't use and you usually couldn't write it more efficiently by hand") -- space and time cost is always proportional to how much GC allocation your code performs, including zero cost if you never perform a GC allocation.

- _**gcpp is not yet**_ attempting to be scalable or production quality. The goal for now is to try out a proof of concept to validate whether the general approach and interface are right.

- _**gcpp will not ever**_ trace the whole C++ heap, incur uncontrollable or global GC pauses, add a "finalizer" concept, permit object "resurrection," be recommended as a default allocator, or replace `unique_ptr` and `shared_ptr` -- we are very happy with C++'s current lifetime model, and the aim here is only to add a fourth fallback when today's options are insufficient.

This project aims to continue C++'s long tradition of being a great language for building libraries, including specialized memory allocation libraries such as regions, arenas, pools, and allocators.


## Two target use cases

Gcpp aims to address two primary use cases. Initial work has also indicated a possible third use case to explore, but it is speculative.

### 1. gc_ptr for potentially-cyclic data structures

The first target use case is automatic by-construction memory management for data structures with cycles, which cannot be expressed with fully automated lifetime using C++17 facilities only.

- Encapsulated example: A `Graph` type whose `Node`s point to each other but should stay alive as long as they are transitively reachable from the enclosing `Graph` object or from some object such as a `Graph::iterator` that is handed out by the `Graph` object.

- Unencapsulated example: A group of objects of different types that refer to each other in a potentially-cyclic way but should stay alive as long as they are transitively reachable from some root outside their `gc_heap`.

- In C++17, both examples can be partly automated (e.g., having `Graph` contain a `vector<unique_ptr<Node>> my_nodes;` so that all nodes are guaranteed to be destroyed at least when the `Graph` is destroyed), but require at minimum manual liveness tracing today (e.g., traversal logic to discover unreachable `Node` objects, which essentially implements a tracing collection algorithm by hand for the nodes, and then `my_nodes.erase(unreachable_node);` to remove each unreachable node which is manual and morally equivalent to `delete unreachable_node;`).

### 2. atomic_gc_ptr for lock-free data structures (with or without cycles)

The second target use case is to support scalable and concurrent lock-free data structures that encounter ABA and deletion problems and cannot be written efficiently or at all in portable Standard C++ today. As a litmus test: If a lock-free library today resorts to hazard pointers or transactional memory to solve ABA and deletion problems, it is probably a candidate for using `atomic_gc_ptr` instead.

- Acyclic example: A lock-free queue that supports both traversal and node deletion. (Note: C++17 `atomic_shared_ptr`, also written by me, also addresses this problem. However, making it truly lock-free requires at least some additional complexity in the implementation; thanks to Anthony Williams for contributing implementation experience with `atomic_shared_ptr` including demonstrating a lock-free implementation. Finally, some experts still question its lock-free property.)

- Cyclic example: A lock-free graph that can contain cycles.

### Possible bonus use case (speculative): gc_allocator for STL containers 

Finally, `gc_allocator` wraps up a `gc_heap` as an STL allocator. This was not an original use case, and is not a primary target, but it's an interesting case to explore because it might just fall out once a `gc_heap` abstraction is available and it may have some interesting properties.

- When using a `container<T, gc_allocator<T>>`, iterators can’t dangle (point to a destroyed or deallocated object) because they keep objects alive. This turns dereferencing an invalidated iterator from an undefined behavior problem into (mostly) a stale data problem.

- See notes below for limitations on iterator navigation using invalidated iterators.

- Note: `gc_allocator` relies on C++11's allocator extensions to support "fancy" user-defined pointer types. It does not work with pre-C++11 standard libraries, which required `allocator::pointer` to be a raw pointer type. If you are using Microsoft Visual C++, the current implementation requires MSVC 2015 Update 3; it does not work on Update 2 which did not yet have enough fancy pointer support.

## Usage guidance: C++17 and the gcpp library

| Guidance / library | What it automates | Efficiency | Clarity and correctness |
|--------------------|-------------------|------------|-------------------------|
| [C++17] **1. Where possible, prefer scoped lifetime** by default (e.g., locals, members) | Expressing that this object's lifetime is tied to some other lifetime that is already well defined, such as a block scope (`auto` storage duration) or another object (member lifetime) | Zero additional lifetime management overhead for this object | |
| [C++17] **2. Else prefer `make_unique` and `unique_ptr`**, if the object must have its own lifetime (i.e., heap) and ownership can be unique without ownership cycles | Single-ownership heap object lifetime | Usually identical cost as correctly written `new`+`delete` | Clearer and more robust than explicit `delete` (declarative, uses are correct by construction) |
| [C++17] **3. Else prefer `make_shared` and `shared_ptr`**, if the object must have its own lifetime (i.e., heap) and shared ownership, without ownership cycles | Acyclic shared heap object lifetime managed with reference counting | Usually identical cost as correctly written manual reference counting | Clearer and more robust than manual/custom reference count logic (declarative, uses are correct by construction) |
| [gcpp, experimental] **4. Else consider `gc_heap` and `gc_ptr`**, if the object must have its own lifetime (i.e., heap) and there can be ownership cycles | Potentially-cyclic shared heap object lifetime managed with liveness tracing<br><br>Lock-free data structures that perform general node deletion and/or have cycles | (conjecture) Usually identical cost as correctly written manual tracing | (conjecture) Clearer and more robust than manual/custom tracing logic (declarative, uses are correct by construction) |


# Implementation notes

## gc_heap and gc_ptr

`gc_heap` encapsulates memory containing objects accessed via `gc_ptr<T>`.

- `collect()` is local: It traces only GC-allocated objects, and only this `gc_heap`.

- `collect()` is explicit: It runs only when called. There are opt-in automatic `collect()` options (TODO), such as on `allocate()` failure before allocating a new page.

- Note: The prototype collector is not intended to be scalable or production-quality. The prototype focuses on interface design, and uses a proof-of-concept collector. I invite GC experts to suggest/code their own GC implementations under this interface. However, even this prototype collector implementation was written with a view to avoiding unreasonable space overhead and time complexity

- `make<T>()` allocates and constructs a new `T` object and returns a `gc_ptr<T>`.

   - If `T` has a nontrivial destructor, the destructor is remembered and run during `.collect()` when the object has become unreachable. This is necessary to make using `gc_ptr` also type-safe, in addition to memory-safe.

   - The current collector implementation stores the following data:

      - Per nontrivially destructible object, stores two pointers: {T* obj, void (*dtor)(void*)}.

      - Per array of nontrivially destructible objects, stores two pointers plus the number of objects.

      - Per type, usually stores one 1-instruction dtor wrapper: Clang -O1 generates a function containing just *jmp*, Clang -O2 inlines the destructor entirely where that’s usual.

`gc_ptr` refers to an object in a `gc_heap`.

- Additional rule: A `gc_ptr` member is null in a GC'd object’s destructor. This enables safe unordered destruction by enforcing known best practices in GC environments, in particular:

   - The destructor of a GC'd object runs normally, but cannot access another GC'd object possibly being destroyed in the same collection cycle (which might already have been destroyed). This eliminates the "accessing a disposed/finalized object" class of bugs that is possible in environments like Java, .NET, and Go, and eliminates the "leaking cycles of objects that use finalizers" class of bugs that is possible in Go.

   - It is not possible to "resurrect" another GC'd object by storing a `gc_ptr` to it somewhere that would make that object reachable again (and an object cannot make itself reachable again because it cannot form a `gc_ptr` to itself). This eliminates the "double dispose" class of bugs that is possible in environments like Java, .NET, and Go.

- A `gc_ptr<T>` is identical to `T*` in size and assignment. Copy assignment is trivial.

- Constructing or destroying a `gc_ptr` enregisters/deregisters its location.

   - The proof-of-concept collector just registers all `gc_ptr`s; other collectors might register only `gc_ptr`s that are external roots.

## atomic_gc_ptr

`atomic_gc_ptr` is designed to be safe for lock-free use, and have its uses be lock-free.

- Remember that construction and destruction are special: We never need to think about races on an object in the object’s constructor or destructor.

- Atomic operations (e.g., `compare_exchange`) operate on existing objects, just like assignment. Because `gc_ptr<T>` wraps a `T*` with trivial assignment, it makes it easy to let `atomic_gc_ptr<T>` wrap `atomic<T*>` and expose `compare_exchange` etc.


## gc_allocator (speculative)

**Note: Not a primary target use case.** `gc_allocator` is an experiment to wrap a `gc_heap` up as a C++11 allocator. It appears to work with unmodified current STL containers, but I'm still exploring how well and exploring the limits. Note that `gc_allocator` requires C++11 allocator support for fancy pointers; on MSVC, it requires Visual Studio 2015 Update 3 or greater.

- `deallocate()` is a no-op, but performs checking in debug builds. It does not need to actually deallocate because deallocation will happen at the next `.collect()` after the memory becomes unreachable.

- `destroy()` is a no-op, but perform checking in debug builds. It does not need to remember the destructor because that was already recorded when `construct()` was called; see next point. It does not need to call the destructor because the destructor will be called when the object is unreachable (or, for `vector` only, when the container in-place constructs a new object in the same location; see next subpoint).

- The in-place `construct()` function remembers the correct destructor if needed, which is only if the object has a nontrivial destructor.

   - `construct()` is available via `gc_allocator` only, and adds special sauce for handling `vector::pop_back` followed by `push_back`: A pending destructor is also run before constructing an object in a location for which the destructor is pending. This is the only situation where a destructor runs other than at `.collect()` time, and only happens when using `vector<T, gc_allocator<T>>`.
   
   - Note: To my knowledge, this is the only gcpp operation that could be abused type-unsafely, without resorting to `reinterpret_cast`ing `gc_ptr`s. We assume the container implementation is not malicious.

- `container<T, gc_allocator<T>>` iterators keep object (not just memory) alive.

   - The iterator stores `gc_ptr`, which makes the iterator a strong owner. Turns an undefined behavior problem into stale data problem.

   - For all containers, an invalidated iterator points to a valid object. However, the object may have a different value, including being in a moved-from state; also, reading the object via the invalidated iterator is not guaranteed to see changes made to the container, and vice versa.

   - For a node-based container or `deque`, an invalidated iterator could still navigate to other erased/current nodes that the erased node happens to be pointing to, if the container did not change the logically-deleted node in a way that breaks its iterator increment/decrement logic. Note: This is not guaranteed to work, but appears to work in practice in the `set` implementations I've tried so far.

   - For a `vector`, any use of an iterator to navigate beyond the end of the allocation that the iterator actually points into will fire an assert in debug builds. An invalidated iterator will keep an outgrown-and-discarded `vector` buffer alive and can still be compared correctly with other iterators into the same actual buffer (i.e., iterators of the same vintage == when the vector had the same capacity). In particular, an invalidated iterator obtained before the vector's last expansion cannot be correctly compared to the vector's current `.begin()` or `.end()`, and loops that do that with an invalidated iterator will fire an assert in debug builds.

