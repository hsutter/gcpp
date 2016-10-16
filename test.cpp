
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
using namespace gcpp;

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
	GCPP_TOTALLY_ORDERED_COMPARISON(widget);	// maybe someday this will be default
};

struct node {
	node() { cout << "+node\n"; }
	~node() { cout << "-node\n"; }

	deferred_ptr<node> xyzzy;
	deferred_ptr<node> plugh;
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
	(void)p1;
	g.debug_print();

	auto p2 = g.allocate<double>();
	(void)p2;
	g.debug_print();

	auto p3 = g.allocate<char>();
	g.debug_print();

	auto p4 = g.allocate<double>();
	(void)p4;
	g.debug_print();

	g.deallocate(p3);
	g.debug_print();

	auto p5 = g.allocate<char>();
	(void)p5;
	g.debug_print();
}


//----------------------------------------------------------------------------
//
//	Basic use of a deferred_heap.
//
//----------------------------------------------------------------------------

void test_deferred_heap() {
	deferred_heap heap;

	vector<deferred_ptr<int>> v;
	vector<deferred_ptr<array<char, 10>>> va;
	heap.debug_print();

	//v.emplace_back(heap.make<int>());
	//heap.debug_print();

	va.emplace_back(heap.make<array<char, 10>>());
	//heap.debug_print();

	//v.emplace_back(heap.make<int>());
	//heap.debug_print();

	//v.emplace_back(heap.make<int>());
	//heap.debug_print();

	//v.erase(v.begin() + 1);//heap.debug_print();

	auto x = heap.make<node>();
	x->plugh = heap.make<node>();
	x->plugh->xyzzy = x; // make a cycle
	x = nullptr;		// now the cycle is unreachable

	heap.debug_print();

	heap.collect();		// collects the cycle

	heap.debug_print();


	// test aliasing
	//
	struct Test {
		int i = 42;
		double d = 3.14159;
	};
	auto pt = heap.make<Test>();
	cout << "pt [" << (void*)pt.get() << "]\n";
	auto pi = pt.ptr_to(&Test::i);
	cout << "pi [" << (void*)pi.get() << "] is " << *pi << "\n";
	auto pd = pt.ptr_to(&Test::d);
	cout << "pd [" << (void*)pd.get() << "] is " << *pd << "\n";
}


//----------------------------------------------------------------------------
//
//	Some timing of deferred_heap.
//
//----------------------------------------------------------------------------

template<class T>
void time_shared(int N) {
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
void time_deferred(deferred_heap& heap, int N) {
	vector<deferred_ptr<T>> v;
	auto start = std::chrono::high_resolution_clock::now();
	for (int i = 0; i < N; ++i) {
		v.push_back(heap.make<T>());
	}
	auto end = std::chrono::high_resolution_clock::now();
	cout << "\tdeferred_ptr (" << N << ") time: "
		<< std::chrono::duration<double, std::milli>(end - start).count()
		<< "ms\n";
}

void time_deferred_heap() {
	deferred_heap heap;
	for (int i = 10; i < 11000; i *= 2) {
		time_shared<int>(i);
		time_deferred<int>(heap, i);
	}
	//heap.debug_print();
	//heap.collect();
	//heap.debug_print();
}


//----------------------------------------------------------------------------
//
//	Basic use of a deferred_allocator on its own, just to make sure it's wired up
//	correctly for allocator_traits to call the right things.
//
//----------------------------------------------------------------------------

void test_deferred_allocator() {
	deferred_heap heap;

	using X = std::allocator_traits<deferred_allocator<int>>;
	deferred_allocator<int> x(heap);

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
#if !defined(__GLIBCXX__) && !defined(_LIBCPP_VERSION)
	deferred_heap heap;

	auto s = deferred_set<widget>(heap);
	s.insert(2);
	s.insert(1);
	s.insert(3);

	// make an iterator that points to an erased node
	auto i = s.begin();
	s.erase(i);

	heap.debug_print();	// at this point the second node allocated (which was 1 and therefore pointed to by i)
						// is unreachable from within the tree but reachable from i

	heap.collect();
	heap.debug_print();	// the erased node is still there, because i kept it alive

	cout << "i -> (" << *i << ")\n";	// i points to 1
	++i;								// navigate -- to node that used to be the right child
	cout << "i -> (" << *i << ")\n";	// which is 2, we've navigated back into the tree

	i = s.begin();	// now make the iterator point back into the container, making the erased node unreachable

	heap.collect();
	heap.debug_print();	// now the erased node is deleted (including correctly destroyed)
#endif
}


//----------------------------------------------------------------------------
//
//	Try a deferred_allocator with a vector.
//
//----------------------------------------------------------------------------

void test_deferred_allocator_vector() {
	deferred_heap heap;

	//	Note: For the following line to make any difference you need to exhaust
	//	at least the first page the heap owns. To force that, either artificially
	//	decrease the page size to about 80 bytes in dhpage::dhpage (I usually
	//	change 8192 to 81), or increase the amount of work below.
	heap.set_collect_before_expand(true);

	{
		auto v = deferred_vector<widget>(heap);
		auto iter = v.begin();

		auto old_capacity = v.capacity();
		for (int i = 1; i <= 10; ++i) {
			v.push_back(i);
			if (old_capacity != v.capacity()) {
				cout << "RESIZED! new size is " << v.size()
					<< " and capacity is " << v.capacity() << '\n';
				old_capacity = v.capacity();
				heap.debug_print();
			}
			if (i == 1) {
				iter = begin(v) + 1;	// keeps alive one of the vector buffers; on MSVC, points to an interior element
			}
		}

		heap.collect();
		heap.debug_print();	// now we have the current (largest) vector buffer alive, as well as
							// one of the earlier smaller ones kept alive by i

		iter = v.begin();	// now remove the last iterator referring to that earlier buffer

		heap.collect();
		heap.debug_print();	// now we have only the current buffer alive

		v.pop_back();	// this logically removes the last element as usual, but does NOT destroy it
		v.push_back(999);	// this destroys the element previously in that location before
							// constructing the new one to avoid overlapping object lifetimes
							// (this happens automatically inside construct())
	}
	heap.collect();
	heap.debug_print();
}


//----------------------------------------------------------------------------
//
//	Some timing of deferred_allocator with set and vector.
//
//----------------------------------------------------------------------------

template<class Set>
void time_set(Set s, const char* sz, int N) {
	auto start = std::chrono::high_resolution_clock::now();
	for (int i = 0; i < N; ++i)
		s.insert(i);
	auto end = std::chrono::high_resolution_clock::now();
	cout << sz << "(" << N << ") time: "
		<< std::chrono::duration<double, std::milli>(end - start).count()
		<< "ms\n";
}

void time_deferred_allocator_set() {
#ifndef __GNUC__
	deferred_heap heap;
	auto s = set<int>();
	auto s2 = deferred_set<int>(heap);

	for (int i = 10; i < 11000; i *= 2) {
		time_set(s, "set<int>", i);
		time_set(s2, "deferred_set<int>", i);
		//heap.debug_print();
		//heap.collect();
		//heap.debug_print();
	}
#endif
}

template<class Vec>
void time_vec(Vec v, const char* sz, int N) {
	auto start = std::chrono::high_resolution_clock::now();
	for (int i = 0; i < N; ++i)
		v.push_back(i);
	auto end = std::chrono::high_resolution_clock::now();
	cout << sz << "(" << N << ") time: "
		<< std::chrono::duration<double, std::milli>(end - start).count()
		<< "ms\n";
}

void time_deferred_allocator_vector() {
	deferred_heap heap;
	auto v = vector<widget>();
	auto v2 = deferred_vector<widget>(heap);

	for (int i = 10; i < 11000; i *= 2) {
		//time_vec(v, "vector<int>", i);
		time_vec(v, "deferred_vector<int>", i);
		//heap.debug_print();
		//heap.collect();
		//heap.debug_print();
	}
}

void test_deferred_array() {
	deferred_heap heap;
	vector<deferred_ptr<widget>> v;

	v.push_back(heap.make_array<widget>(3));
	heap.debug_print();

	v.push_back(heap.make_array<widget>(2));
	heap.debug_print();

	v.push_back(heap.make_array<widget>(4));

	heap.debug_print();

	v.push_back(heap.make_array<widget>(3));

	heap.debug_print();

	v.erase(v.begin() + 2);

	heap.collect();
	heap.debug_print();

}


void test_bitflags() {
	const int N = 100;	// picked so that we have 3 x 32-bit units + 1 partial unit,
						// so we can exercise the boundary and internal unit cases

	//	Test that we can correctly set any bit range [i,j)
	for (auto i = 0; i < N; ++i) {
		for (auto j = i; j < N; ++j) {
			bitflags flags(100, false);
			flags.set(i, j, true);
			for (auto test = 0; test < N; ++test) {
				assert(flags.get(test) == (i <= test && test < j));
			}
		}
	}

	//	Test that we can find a true bit set anywhere with any range
	for (auto set = 0; set < N; ++set) {
		bitflags flags(100, false);
		flags.set(set, true);
		for (auto i = 0; i <= set; ++i) {
			for (auto j = i; j < N; ++j) {
				assert(flags.find_next(i, j, true) == min(j,set));
			}
		}
	}

	//	Test that we can find a false bit set anywhere with any range
	for (auto set = 0; set < N; ++set) {
		bitflags flags(100, true);
		flags.set(set, false);
		for (auto i = 0; i <= set; ++i) {
			for (auto j = i; j < N; ++j) {
				assert(flags.find_next(i, j, false) == min(j, set));
			}
		}
	}

	//flags.debug_print();
}


int main() {
	//test_page();
	//test_bitflags();

	//test_deferred_heap();
	//time_deferred_heap();

	//test_deferred_allocator();

	//test_deferred_allocator_set();
	//time_deferred_allocator_set();

	test_deferred_allocator_vector();
	//time_deferred_allocator_vector();

	//test_deferred_array();

	//heap.collect();
	//heap.debug_print();

	return 0;
}

