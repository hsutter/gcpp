# **gcpp**: A GC heap allocation library for C++

The gcpp library is an experiment at adding tracing garbage collection as a library to the C++ toolbox. 


## Overview and context

C++ is a great language for building libraries, including memory allocation libraries which were greatly extended in C++11 and C++14. This experiment is an attempt to extend that tradition, and to extend -- not replace -- the existing C++ guidance for object lifetime management:

| Guidance / library | What it automates | Comparison to manual lifetime management style |
|---------|-----------|--------------------------|
| **1. Where possible, prefer scoped lifetime** by default (e.g., locals, members) | Expressing that this object's lifetime is tied to some other lifetime that is already well defined, such as a block scope (`auto` storage duration) or another object (member lifetime) | Zero additional lifetime management overhead for this object |
| **2. Else prefer `make_unique` and `unique_ptr`**, if the object must have its own lifetime (i.e., heap) and ownership can be unique without ownership cycles [Boost, then C++11] | Single-ownership heap object lifetime | Efficiency: Usually identical cost as correctly written `new`+`delete`<br><br>Correctness: Clearer and more robust (declarative, uses are correct by construction) |
| **3. Else prefer `make_shared` and `shared_ptr`**, if the object must have its own lifetime (i.e., heap) and shared ownership, without ownership cycles [Boost, then C++11] | Acyclic shared heap object lifetime managed with reference counting | Efficiency: Usually identical cost as correctly written manual reference counting<br><br>Correctness: Clearer and more robust (declarative, uses are correct by construction) |
| (experimental) **4. Else consider `gc_heap` and `gc_ptr`**, if the object must have its own lifetime (i.e., heap) and there can be ownership cycles [this project] | Potentially-cyclic shared heap object lifetime managed with liveness tracing<br><br>Lock-free data structures that perform general node deletion | Efficiency: Usually identical cost as correctly written manual tracing<br><br>Correctness: Clearer and more robust (declarative, uses are correct by construction) |


## Goals

Gcpp aims to address the following scenarios, in order starting with the most important:

**(1) `gc_ptr` for potentially-cyclic data structures.** Automatic by-construction memory management for data structures with cycles.

- Encapsulated example: A `Graph` type whose `Node`s point to each other but should stay alive as long as they are transitively reachable from the enclosing `Graph` object or from some object such as a `Graph::iterator` that is handed out by the `Graph` object. 

- Unencapsulated example: A group of objects of different types that refer to each other in a potentially-cyclic way but should stay alive as long as they are transitively reachable from some root outside their `gc_heap`.

**(2) `atomic_gc_ptr` for lock-free data structures (with or without cycles).** Support scalable and concurrent lock-free data structures that encounter ABA and deletion problems and cannot be written efficiently or at all in portable Standard C++ today. A litmus test: If a lock-free library today resorts to hazard pointers or transactional memory to solve ABA and deletion problems, it is probably a candidate for using `atomic_gc_ptr` instead.

- Acyclic example: A lock-free queue that supports both traversal and node deletion. (Note: C++17 `atomic_shared_ptr`, also written by me, also addresses this problem. However, making it truly lock-free requires at least some additional complexity in the implementation; thanks to Anthony Williams for contributing implementation experience with `atomic_shared_ptr` including demonstrating a lock-free implementation. Finally, some experts still question its lock-free property.)

- Cyclic example: A lock-free graph that can contain cycles.

**(3) `gc_allocator` to support STL containers.** `gc_allocator` wraps up a `gc_heap` an allocator, and in early testing appears to work with existing STL container implementations out-of-the-box. When using a `container<T, gc_allocator<T>>`, iterators can’t dangle (point to deallocated memory, or point to a destroyed object) because they keep objects alive, including erased `set` nodes (you can still navigate to other erased/current nodes they happen to be pointing to) and outgrown-and-discarded `vector` buffers.

- Note: `gc_allocator` relies on C++11 allocator extensions to support "fancy" user-defined pointer types. It does not work with pre-C++11 standard libraries, which required `allocator::pointer` to be a raw pointer type.
