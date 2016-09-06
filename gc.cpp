
#include "gc.h"

namespace gcpp {

	//	For now, there's just one global gc_heap (will allow multiple local gc_heaps soon)
	//
	gc_heap& gc() {
		static gc_heap the_arena;
		return the_arena;
	}

}


//----------------------------------------------------------------------------
//
//	Various tests
//
//----------------------------------------------------------------------------

#include "gc_allocator.h"
using namespace gcpp;

#include <iostream>
#include <vector>
#include <set>
#include <array>
#include <chrono>
using namespace std;


#define TOTALLY_ORDERED_COMPARISON(Type) \
bool operator==(const Type& that) const { return compare(that) == 0; } \
bool operator!=(const Type& that) const { return compare(that) != 0; } \
bool operator< (const Type& that) const { return compare(that) <  0; } \
bool operator<=(const Type& that) const { return compare(that) <= 0; } \
bool operator> (const Type& that) const { return compare(that) >  0; } \
bool operator>=(const Type& that) const { return compare(that) >= 0; }


struct widget {
	long v;

	widget(long value = 0) 
		: v{ value }
	{
#ifndef NDEBUG
		cout << "+widget " << v << "\n";
#endif
	}

	widget(const widget& that) 
		: v{ that.v }
	{
#ifndef NDEBUG
		cout << "+widget (copy " << v<< ")\n";
#endif
	}

	~widget() {
#ifndef NDEBUG
		cout << "-widget " << v<< "\n";
#endif
	}

	operator long() const { return v; }

	// (grump) this is the right way to do totally ordered comparisons, it ought to be standard and default
	int compare(const widget& that) const { return v < that.v ? -1 : v == that.v ? 0 : 1; };
	TOTALLY_ORDERED_COMPARISON(widget);
};

struct node {
	node() { cout << "+node\n"; }
	~node() { cout << "-node\n"; }

	gc_ptr<node> xyzzy;
	gc_ptr<node> plugh;
};


//----------------------------------------------------------------------------
//
//	Basic use of a single page.
//
//----------------------------------------------------------------------------

void test_page() {
	gpage g;
	g.debug_print();

	auto p1 = g.allocate<char>();
	g.debug_print();

	auto p2 = g.allocate<double>();
	g.debug_print();

	auto p3 = g.allocate<char>();
	g.debug_print();

	auto p4 = g.allocate<double>();
	g.debug_print();

	g.deallocate(p3);
	g.debug_print();

	auto p5 = g.allocate<char>();
	g.debug_print();
}


//----------------------------------------------------------------------------
//
//	Basic use of a gc_heap.
//
//----------------------------------------------------------------------------

void test_gc() {
	vector<gc_ptr<int>> v;
	vector<gc_ptr<array<char, 10>>> va;
	gc().debug_print();

	//v.emplace_back(make_gc<int>());
	//gc().debug_print();

	va.emplace_back(make_gc<array<char, 10>>());
	//gc().debug_print();

	//v.emplace_back(make_gc<int>());
	//gc().debug_print();

	//v.emplace_back(make_gc<int>());
	//gc().debug_print();

	//v.erase(v.begin() + 1);
	//gc().debug_print();

	auto x = make_gc<node>();
	x->plugh = make_gc<node>();
	x->plugh->xyzzy = x; // make a cycle
	x = nullptr;		// now the cycle is unreachable

	gc().debug_print();

	gc().collect();		// collects the cycle

	gc().debug_print();
}


//----------------------------------------------------------------------------
//
//	Some timing of gc_heap.
//
//----------------------------------------------------------------------------

template<class T>
void time_gc_shared(int N) {
	vector<shared_ptr<T>> v;
	auto start = std::chrono::high_resolution_clock::now();
	for (int i = 0; i < N; ++i)
		v.push_back(make_shared<T>());
	auto end = std::chrono::high_resolution_clock::now();
	cout << "shared_ptr (" << N << ") time: "
		<< std::chrono::duration<double, std::milli>(end - start).count()
		<< "ms  ";
}

template<class T>
void time_gc_gc(int N) {
	vector<gc_ptr<T>> v;
	auto start = std::chrono::high_resolution_clock::now();
	for (int i = 0; i < N; ++i) {
		v.push_back(make_gc<T>());
	}
	auto end = std::chrono::high_resolution_clock::now();
	cout << "\tgc_ptr (" << N << ") time: "
		<< std::chrono::duration<double, std::milli>(end - start).count()
		<< "ms\n";
}

void time_gc() {
	for (int i = 10; i < 10000; i *= 2) {
		time_gc_shared<int>(i);
		time_gc_gc<int>(i);
		//gc().debug_print();
		//gc().collect();
		//gc().debug_print();
	}
}


//----------------------------------------------------------------------------
//
//	Basic use of a gc_allocator on its own, just to make sure it's wired up
//	correctly for allocator_traits to call the right things.
//
//----------------------------------------------------------------------------

void test_gc_allocator() {

	using X = std::allocator_traits<gc_allocator<int>>;
	gc_allocator<int> x;

	auto p = X::allocate(x, 1);
	X::construct(x, p.get(), 1);
	X::destroy(x, p.get());
	X::deallocate(x, p, 1);
}


//----------------------------------------------------------------------------
//
//	Try a gc_allocator with a set.
//
//----------------------------------------------------------------------------

void test_gc_allocator_set() {
	set<widget, less<widget>, gc_allocator<widget>> s;
	s.insert(2);
	s.insert(1);
	s.insert(3);

	// make an iterator that points to an erased node
	auto i = s.begin();
	s.erase(i);

	gc().debug_print();	// at this point the second node allocated (which was 1 and therefore pointed to by i)
						// is unreachable from within the tree but reachable from i

	gc().collect();
	gc().debug_print();	// the erased node is still there, because i kept it alive

	cout << "i -> (" << *i << ")\n";	// i points to 1
	++i;								// navigate -- to node that used to be the right child
	cout << "i -> (" << *i << ")\n";	// which is 2, we've navigated back into the tree

	i = s.begin();	// now make the iterator point back into the container, making the erased node unreachable

	gc().collect();
	gc().debug_print();	// now the erased node is deleted (including correctly destroyed)
}


//----------------------------------------------------------------------------
//
//	Try a gc_allocator with a vector.
//
//----------------------------------------------------------------------------

void test_gc_allocator_vector() {
	vector<widget, gc_allocator<widget>> v;
	auto iter = v.begin();

	auto old_capacity = v.capacity();
	for (int i = 1; i <= 10; ++i) {
		v.push_back({});
		if (old_capacity != v.capacity()) {
			cout << "RESIZED! new size is " << v.size()
				<< " and capacity is " << v.capacity() << '\n';
			old_capacity = v.capacity();
			gc().debug_print();
		}
		if (i == 3) {
			iter = begin(v) + 1;	// keeps alive one of the vector buffers; on MSVC, points to an interior element
		}
	}

	gc().collect();
	gc().debug_print();	// now we have the current (largest) vector buffer alive, as well as
						// one of the earlier smaller ones kept alive by i

	iter = v.begin();	// now remove the last iterator referring to that earlier buffer

	gc().collect();
	gc().debug_print();	// now we have only the current buffer alive

	v.pop_back();	// this logically removes the last element as usual, but does NOT destroy it
	v.push_back({});	// this destroys the element previously in that location before
						// constructing the new one to avoid overlapping object lifetimes
						// (this happens automatically inside construct())
}


//----------------------------------------------------------------------------
//
//	Some timing of gc_allocator with set and vector.
//
//----------------------------------------------------------------------------

template<class Set>
void time_set(const char* sz, int N) {
	Set s;
	auto start = std::chrono::high_resolution_clock::now();
	for (int i = 0; i < N; ++i)
		s.insert(i);
	auto end = std::chrono::high_resolution_clock::now();
	cout << sz << "(" << N << ") time: "
		<< std::chrono::duration<double, std::milli>(end - start).count()
		<< "ms\n";
}

void time_gc_allocator_set() {
	for (int i = 10; i < 11000; i *= 2) {
		time_set<set<int>>("set<int>", i);
		time_set<set<int, less<int>, gc_allocator<int>>>("set<int,gc>", i);
		//gc().debug_print();
		//gc().collect();
		//gc().debug_print();
	}
}

template<class Vec>
void time_vec(const char* sz, int N) {
	Vec v;
	auto start = std::chrono::high_resolution_clock::now();
	for (int i = 0; i < N; ++i)
		v.push_back(i);
	auto end = std::chrono::high_resolution_clock::now();
	cout << sz << "(" << N << ") time: "
		<< std::chrono::duration<double, std::milli>(end - start).count()
		<< "ms\n";
}

void time_gc_allocator_vector() {
	for (int i = 10; i < 11000; i *= 2) {
		//time_vec<vector<int>>("vector<int>", i);
		time_vec<vector<int, gc_allocator<int>>>("vector<int,gc>", i);
		//gc().debug_print();
		//gc().collect();
		//gc().debug_print();
	}
}

void test_gc_array() {
	vector<gc_ptr<widget>> v;

	v.push_back(make_gc_array<widget>(3));
	gc().debug_print();

	v.push_back(make_gc_array<widget>(2));
	gc().debug_print();

	v.push_back(make_gc_array<widget>(4));

	gc().debug_print();

	v.push_back(make_gc_array<widget>(3));

	gc().debug_print();

	v.erase(v.begin()+2);

	gc().collect();
	gc().debug_print();

}


int main() {
	//test_page();

	//test_gc();
	//time_gc();

	//test_gc_allocator();

	test_gc_allocator_set();
	//time_gc_allocator_set();

	//test_gc_allocator_vector();
	//time_gc_allocator_vector();

	//test_gc_array();

	gc().collect();
	gc().debug_print();
}

