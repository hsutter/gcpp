
#include "deferred_allocator.h"
using namespace gcpp;

#include <iostream>
#include <iomanip>
#include <memory>
#include <vector>
#include <algorithm>
using namespace std;


class Counter {
public:
	Counter()  { ++count_; }
	~Counter() { --count_; }
	static int count() { return count_; }
private:
	static int count_;
};

int Counter::count_ = 0;

class MyGraph1 {
public:
	class Node : public Counter {
		vector<shared_ptr<Node>> children;
	public:
		void AddChild(const shared_ptr<Node>& node) {
			children.push_back(node);
		}
		void RemoveChild(const shared_ptr<Node>& node) {
			auto it = find(children.begin(), children.end(), node);
			assert(it != children.end() && "trying to remove a child that was never added");
			children.erase(it);
		}
	};

	void SetRoot(const shared_ptr<Node>& node) {
		root = node;
	}

private:
	shared_ptr<Node> root;
};


static deferred_heap heap;

class MyGraph2 {
public:

	class Node : public Counter {
		deferred_vector<deferred_ptr<Node>> children{ heap };
	public:
		void AddChild(const deferred_ptr<Node>& node) {
			children.push_back(node);
		}
		void RemoveChild(const deferred_ptr<Node>& node) {
			auto it = find(children.begin(), children.end(), node);
			assert(it != children.end() && "trying to remove a child that was never added");
			//children.erase(it);
			*it = nullptr;
		}
	};

	void SetRoot(const deferred_ptr<Node>& node) {
		root = node;
	}

private:
	deferred_ptr<Node> root;
};


/*
using MyGraph = MyGraph1;
auto make() { return make_shared<MyGraph::Node>(); }
auto clean() { }
/*/
using MyGraph = MyGraph2;
auto make() { return heap.make<MyGraph::Node>(); }
auto clean() { 
	//heap.debug_print(); 
	heap.collect(); 
	//heap.debug_print(); 
}
//*/

bool TestCase1() {
	clean();
	using Node = MyGraph::Node;
	MyGraph g;
	{
		auto a = make();
		g.SetRoot(a);
		auto b = make();
		a->AddChild(b);
		auto c = make();
		b->AddChild(c);
		a->RemoveChild(b);
	}
	clean();
	return Counter::count() == 1;
}

bool TestCase2() {
	clean();
	using Node = MyGraph::Node;
	MyGraph g;
	{
		auto a = make();
		g.SetRoot(a);
		auto b = make();
		a->AddChild(b);
		auto c = make();
		b->AddChild(c);
		auto d = make();
		b->AddChild(d);
		d->AddChild(b);
		a->RemoveChild(b);
	}
	clean();
	return Counter::count() == 1;
}

bool TestCase3() {
	clean();
	using Node = MyGraph::Node;
	MyGraph g;
	{
		auto a = make();
		g.SetRoot(a);
		auto b = make();
		a->AddChild(b);
		auto c = make();
		b->AddChild(c);
		auto d = make();
		b->AddChild(d);
		d->AddChild(b);
	}
	clean();
	return Counter::count() == 4;
}

int main() {
	cout.setf(ios::boolalpha);

	bool passed1 = TestCase1();
	cout << passed1 << endl;

	bool passed2 = TestCase2();
	cout << passed2 << endl;

	bool passed3 = TestCase3();
	cout << passed3 << endl;

	return 0;
}
