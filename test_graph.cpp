
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


/*--- Solution 1 (default) ----------------------------------------------------

class MyGraph {
public:
	class Node : public Counter {
		vector<shared_ptr<Node>> children;
	public:
		void AddChild(const shared_ptr<Node>& node) {
			children.push_back(node);
		}
		void RemoveChild(const shared_ptr<Node>& node) {
			auto it = find(children.begin(), children.end(), node);
			Expects(it != children.end() && "trying to remove a child that was never added");
			children.erase(it);
		}
	};

	void SetRoot(const shared_ptr<Node>& node) {
		root = node;
	}

	void ShrinkToFit() {
	}

	static auto MakeNode() { return make_shared<MyGraph::Node>(); }

private:
	shared_ptr<Node> root;
};

//--- Solution 2 (deferred_ptr) -----------------------------------------------
/*/

static deferred_heap heap;

class MyGraph {
public:
	class Node : public Counter {
		deferred_vector<deferred_ptr<Node>> children{ heap };
	public:
		void AddChild(const deferred_ptr<Node>& node) {
			children.push_back(node);
		}
		void RemoveChild(const deferred_ptr<Node>& node) {
			auto it = find(children.begin(), children.end(), node);
			Expects(it != children.end() && "trying to remove a child that was never added");
			//children.erase(it);
			*it = nullptr;
		}
	};

	void SetRoot(const deferred_ptr<Node>& node) {
		root = node;
	}

	void ShrinkToFit() {
		heap.collect();
	}

	static auto MakeNode() { return heap.make<MyGraph::Node>(); }

private:
	deferred_ptr<Node> root;
};

// ----------------------------------------------------------------------------
//*/


bool TestCase1() {
	MyGraph g;
	{
		auto a = MyGraph::MakeNode();
		g.SetRoot(a);
		auto b = MyGraph::MakeNode();
		a->AddChild(b);
		auto c = MyGraph::MakeNode();
		b->AddChild(c);
		a->RemoveChild(b);
	}
	g.ShrinkToFit();
	return Counter::count() == 1;
}

bool TestCase2() {
	MyGraph g;
	{
		auto a = MyGraph::MakeNode();
		g.SetRoot(a);
		auto b = MyGraph::MakeNode();
		a->AddChild(b);
		auto c = MyGraph::MakeNode();
		b->AddChild(c);
		auto d = MyGraph::MakeNode();
		b->AddChild(d);
		d->AddChild(b);
		a->RemoveChild(b);
	}
	g.ShrinkToFit();
	return Counter::count() == 1;
}

bool TestCase3() {
	MyGraph g;
	{
		auto a = MyGraph::MakeNode();
		g.SetRoot(a);
		auto b = MyGraph::MakeNode();
		a->AddChild(b);
		auto c = MyGraph::MakeNode();
		b->AddChild(c);
		auto d = MyGraph::MakeNode();
		b->AddChild(d);
		d->AddChild(b);
	}
	g.ShrinkToFit();
	return Counter::count() == 4;
}

bool TestCase4() {
    MyGraph g;
    {
        auto a = MyGraph::MakeNode();
        g.SetRoot(a);
        auto b = MyGraph::MakeNode();
        a->AddChild(b);
        auto c = MyGraph::MakeNode();
        b->AddChild(c);
        auto d = MyGraph::MakeNode();
        b->AddChild(d);
        d->AddChild(b);
        d->RemoveChild(b);
    }
    g.ShrinkToFit();
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

	bool passed4 = TestCase4();
	cout << passed4 << endl;

	return 0;
}
