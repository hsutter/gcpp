
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
using namespace std;

struct widget {
	char filler;

	widget() {
		cout << "+widget " << sizeof(widget) << "\n";
	}

	widget(const widget&) {
		cout << "+widget (copy)\n";
	}

	~widget() {
		filler = 0; // just to make this a nontrivial dtor
		cout << "-widget\n";
	}
};

struct node {
	node() { cout << "+node\n"; }
	~node() { cout << "-node\n"; }

	gc_ptr<node> xyzzy;
	gc_ptr<node> plugh;
};


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

void test_vector() {
	vector<int> v;
	cout << v.capacity() << " ";

	int i = 0;
	size_t last_capacity = 0;
	while (++i < 256) {
		v.push_back(i);
		if (last_capacity != v.capacity())
			cout << v.capacity() << " ";
		last_capacity = v.capacity();
	}
	cout << '\n';
}

void test_std_allocator() {
	const size_t MaxSize = 32;
	array<int, MaxSize> sizes = { 0 };
	array<void*, 50> addr;
	allocator<int> a;

	for (int size = 0; size < MaxSize; ++size) {

		for (int i = 0; i < 50; ++i) {
			addr[i] = (void*)a.allocate(size + 1);
		}

		//for (auto x : addr)
		//	cout << x << '\n';
		//cout << '\n';

		std::sort(std::begin(addr), std::end(addr));

		void* prev = nullptr;
		int least = 99999;
		for (auto x : addr) {
			auto delta = (int)((char*)x - (char*)prev);
			least = std::min(least, delta);
			//cout << x << " - " << delta << " " << least << '\n';
			prev = x;
		}
		//cout << '\n';

		sizes[size] = least;
	}

	for (int size = 0; size < MaxSize; ++size) {
		cout << "allocate(" << (size + 1) << ") allocated " << sizes[size] << " bytes\n";
	}
}

//void test_gpage_allocator() {
//	vector<int, gpage_allocator<int>> v;
//	page.debug_print();
//
//	for (int i = 0; i < 10; ++i) {
//		v.push_back(1);
//		page.debug_print();
//	}
//}

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
	x->plugh->xyzzy = x; // cycle time!
	x = nullptr;

	gc().debug_print();

	gc().collect();

	gc().debug_print();

	//gc().collect();

	//gc().debug_print();
}

void test_gc_allocator_set() {
	set<int, less<int>, gc_allocator<int>> s;
	s.insert(2);
	s.insert(1);
	s.insert(3);

	// make an iterator that points to an erased node
	auto i = s.begin();
	s.erase(i);

	gc().debug_print();	// at this point the second node allocated (which was 1 and therefore pointed to by i)
						// is unreachable from within the tree but reachable from i

	gc().collect();
	gc().debug_print();	// the erased node is still there, because iter kept it alive

	cout << "i -> (" << *i << ")\n";
	++i;
	cout << "i -> (" << *i << ")\n";
	i = s.begin();	// now make the iterator point back into the container, makin the erased node unreachable
	cout << "i -> (" << *i << ")\n";

	gc().collect();
	gc().debug_print();	// now the erased node is deleted (including correctly destroyed)
}

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

	//p = nullptr;
	//gc().collect();

	//gc().debug_print();

}


int main() {
	//test_page();
	//test_vector();
	//test_std_allocator();
	//test_gpage_allocator();
	//test_gc();
	test_gc_allocator_set();
	//test_gc_allocator_vector();
	//test_gc_array();
}

