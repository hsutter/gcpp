
/////////////////////////////////////////////////////////////////////////////// 
// 
// Copyright (c) 2016 Herb Sutter. All rights reserved. 
// 
// This code is licensed under the MIT License (MIT). 
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR 
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, 
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE 
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER 
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, 
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN 
// THE SOFTWARE. 
// 
///////////////////////////////////////////////////////////////////////////////


//----------------------------------------------------------------------------
//
//	Various tests
//
//----------------------------------------------------------------------------

#include "deferred_allocator.h"
using namespace galloc;

#include <iostream>
#include <vector>
#include <set>
#include <array>
#include <chrono>
using namespace std;


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
		cout << "+widget (copy " << v << ")\n";
#endif
	}

	~widget() {
#ifndef NDEBUG
		cout << "-widget " << v << "\n";
#endif
	}

	operator long() const { return v; }

	int compare3(const widget& that) const { return v < that.v ? -1 : v == that.v ? 0 : 1; };
	GALLOC_TOTALLY_ORDERED_COMPARISON(widget);	// maybe someday this will be default
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

void test_global_deferred_heap() {
	vector<gc_ptr<int>> v;
	vector<gc_ptr<array<char, 10>>> va;
	global_deferred_heap().debug_print();

	//v.emplace_back(make_gc<int>());
	//global_deferred_heap().debug_print();

	va.emplace_back(make_gc<array<char, 10>>());
	//global_deferred_heap().debug_print();

	//v.emplace_back(make_gc<int>());
	//global_deferred_heap().debug_print();

	//v.emplace_back(make_gc<int>());
	//global_deferred_heap().debug_print();

	//v.erase(v.begin() + 1);//global_deferred_heap().debug_print();

	auto x = make_gc<node>();
	x->plugh = make_gc<node>();
	x->plugh->xyzzy = x; // make a cycle
	x = nullptr;		// now the cycle is unreachable

	global_deferred_heap().debug_print();

	global_deferred_heap().collect();		// collects the cycle

	global_deferred_heap().debug_print();
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

void time_global_deferred_heap() {
	for (int i = 10; i < 11000; i *= 2) {
		time_gc_shared<int>(i);
		time_gc_gc<int>(i);
		//global_deferred_heap().debug_print();
		//global_deferred_heap().collect();
		//global_deferred_heap().debug_print();
	}
}


//----------------------------------------------------------------------------
//
//	Basic use of a deferred_allocator on its own, just to make sure it's wired up
//	correctly for allocator_traits to call the right things.
//
//----------------------------------------------------------------------------

void test_deferred_allocator() {

	using X = std::allocator_traits<deferred_allocator<int>>;
	deferred_allocator<int> x;

	auto p = X::allocate(x, 1);
	X::construct(x, p.get(), 1);
	X::destroy(x, p.get());
	X::deallocate(x, p, 1);
}


//----------------------------------------------------------------------------
//
//	Try a deferred_allocator with a set.
//
//----------------------------------------------------------------------------

void test_deferred_allocator_set() {
	set<widget, less<widget>, deferred_allocator<widget>> s;
	s.insert(2);
	s.insert(1);
	s.insert(3);

	// make an iterator that points to an erased node
	auto i = s.begin();
	s.erase(i);

	global_deferred_heap().debug_print();	// at this point the second node allocated (which was 1 and therefore pointed to by i)
						// is unreachable from within the tree but reachable from i

	global_deferred_heap().collect();
	global_deferred_heap().debug_print();	// the erased node is still there, because i kept it alive

	cout << "i -> (" << *i << ")\n";	// i points to 1
	++i;								// navigate -- to node that used to be the right child
	cout << "i -> (" << *i << ")\n";	// which is 2, we've navigated back into the tree

	i = s.begin();	// now make the iterator point back into the container, making the erased node unreachable

	global_deferred_heap().collect();
	global_deferred_heap().debug_print();	// now the erased node is deleted (including correctly destroyed)
}


//----------------------------------------------------------------------------
//
//	Try a deferred_allocator with a vector.
//
//----------------------------------------------------------------------------

void test_deferred_allocator_vector() {
	{
		global_deferred_heap().set_collect_before_expand(true);

		vector<widget, deferred_allocator<widget>> v;
		auto iter = v.begin();

		auto old_capacity = v.capacity();
		for (int i = 1; i <= 10; ++i) {
			v.push_back(i);
			if (old_capacity != v.capacity()) {
				cout << "RESIZED! new size is " << v.size()
					<< " and capacity is " << v.capacity() << '\n';
				old_capacity = v.capacity();
				global_deferred_heap().debug_print();
			}
			if (i == 1) {
				iter = begin(v) + 1;	// keeps alive one of the vector buffers; on MSVC, points to an interior element
			}
		}

		global_deferred_heap().collect();
		global_deferred_heap().debug_print();	// now we have the current (largest) vector buffer alive, as well as
							// one of the earlier smaller ones kept alive by i

		iter = v.begin();	// now remove the last iterator referring to that earlier buffer

		global_deferred_heap().collect();
		global_deferred_heap().debug_print();	// now we have only the current buffer alive

		v.pop_back();	// this logically removes the last element as usual, but does NOT destroy it
		v.push_back(999);	// this destroys the element previously in that location before
							// constructing the new one to avoid overlapping object lifetimes
							// (this happens automatically inside construct())
	}
	global_deferred_heap().collect();
	global_deferred_heap().debug_print();
}


//----------------------------------------------------------------------------
//
//	Some timing of deferred_allocator with set and vector.
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

void time_deferred_allocator_set() {
	for (int i = 10; i < 11000; i *= 2) {
		time_set<set<int>>("set<int>", i);
		time_set<set<int, less<int>, deferred_allocator<int>>>("set<int,gc>", i);
		//global_deferred_heap().debug_print();
		//global_deferred_heap().collect();
		//global_deferred_heap().debug_print();
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

void time_deferred_allocator_vector() {
	for (int i = 10; i < 11000; i *= 2) {
		//time_vec<vector<int>>("vector<int>", i);
		time_vec<vector<int, deferred_allocator<int>>>("vector<int,gc>", i);
		//global_deferred_heap().debug_print();
		//global_deferred_heap().collect();
		//global_deferred_heap().debug_print();
	}
}

void test_gc_array() {
	vector<gc_ptr<widget>> v;

	v.push_back(make_gc_array<widget>(3));
	global_deferred_heap().debug_print();

	v.push_back(make_gc_array<widget>(2));
	global_deferred_heap().debug_print();

	v.push_back(make_gc_array<widget>(4));

	global_deferred_heap().debug_print();

	v.push_back(make_gc_array<widget>(3));

	global_deferred_heap().debug_print();

	v.erase(v.begin() + 2);

	global_deferred_heap().collect();
	global_deferred_heap().debug_print();

}


int main() {
	//test_page();

	//test_global_deferred_heap();
	time_global_deferred_heap();

	//test_deferred_allocator();

	//test_deferred_allocator_set();
	//time_deferred_allocator_set();

	//test_deferred_allocator_vector();
	//time_deferred_allocator_vector();

	//test_gc_array();

	//global_deferred_heap().collect();
	//global_deferred_heap().debug_print();
}

