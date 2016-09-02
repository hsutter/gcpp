#ifndef GCPP_GC
#define GCPP_GC

#include "gpage.h"

#include <vector>
#include <list>
#include <algorithm>
#include <type_traits>
#include <memory>

namespace gcpp {

	//----------------------------------------------------------------------------
	//
	//	The GC arena produces gc_ptr<T>s via make<T>.
	//
	//----------------------------------------------------------------------------

	class gc_heap {
		friend gc_heap& gc();	// TODO

		class  gc_ptr_void;
		friend class gc_ptr_void;

		template<class T> friend class gc_ptr;
		template<class T> friend class gc_allocator;

		//	TODO Can only be used via the global gc() accessor.
		gc_heap()  			  = default;
		~gc_heap();

		//	Disable copy and move
		gc_heap(gc_heap&)		  = delete;
		void operator=(gc_heap&) = delete;

		//	Add/remove a gc_ptr in the tracking list.
		//	Invoked when constructing and destroying a gc_ptr.
		void enregister(const gc_ptr_void& p);
		void deregister(const gc_ptr_void& p);

		//------------------------------------------------------------------------
		//
		//  gc_ptr_void is the generic GC pointer type we use and track
		//  internally. The user uses gc_ptr<T>, the type-casting wrapper.
		//
		class gc_ptr_void {
			void* p;

		protected:
			void  set(void* p_) noexcept { p = p_; }

			gc_ptr_void(void* p_ = nullptr)
				: p(p_)
			{
				gc().enregister(*this);
			}

			~gc_ptr_void() {
				gc().deregister(*this);
			}

			gc_ptr_void(const gc_ptr_void& that)
				: gc_ptr_void(that.p)
			{ }

			//	Note: =default makes this assignment operator trivial.
			gc_ptr_void& operator=(const gc_ptr_void& that) noexcept = default;
		public:
			void* get() const noexcept { return p; }
			void  reset() noexcept { p = nullptr; }
		};

		//  destructor contains a pointer and type-correct-but-erased dtor call.
		//  (Happily, a noncapturing lambda decays to a function pointer, which
		//	will make these both easy to construct and cheap to store without
		//	resorting to the usual type-erasure machinery.)
		//
		struct destructor {
			const byte* p;
			void (*dtor)(const void*);
		};

		struct array_destructor {
			const byte* p;
			std::size_t n;
			void(*dtor)(const void*);
		};

		//	For non-roots (gc_ptrs that are in the GC arena), we'll additionally
		//	store an int that we'll use for terminating marking within the GC heap.
		//  The level is the distance from some root -- not necessarily the smallest
		//	distance from a root, just along whatever path we took during marking.
		//
		struct nonroot {
			const gc_ptr_void* p;
			std::size_t level = 0;

			nonroot(const gc_ptr_void* p_) noexcept : p{ p_ } { }
		};

		struct gcpage {
			gpage						page;
			std::vector<bool> 			live_starts;	// for tracing
			std::vector<nonroot>		gc_ptrs;		// known gc_ptrs in this page

			//	Construct a page tuned to hold Hint objects,
			//	big enough for at least 2 Hint objects (but at least 1024B),
			//	and a tracking min_alloc chunk sizeof(Hint) (but at least 4 bytes).
			//	Note: Hint used only to deduce size/alignment.
			//
			template<class Hint>
			gcpage(const Hint* /*--*/)
				: page{ std::max<size_t>(sizeof(Hint) * 2, 1024), 
						std::max<size_t>(sizeof(Hint), 4) }
				, live_starts(page.locations(), false)
			{ }
		};


		//------------------------------------------------------------------------
		//	Data: Storage and tracking information
		//
		std::list<gcpage>				pages;
		std::vector<const gc_ptr_void*> gc_roots;		// outside GC arena
		std::vector<destructor>         destructors;
		std::vector<array_destructor>   array_destructors;

	public:
		//------------------------------------------------------------------------
		//
		//	make: Allocate one object of type T initialized with args
		//
		//	If allocation fails, the returned pointer will be null
		//
		template<class T, class ...Args>
		gc_ptr<T> make(Args&&... args) {
			auto p = allocate<T>();
			if (p != nullptr) {
				construct(p.get(), std::forward<Args>(args)...);
			}
			return p;
		}

		//------------------------------------------------------------------------
		//
		//	make_array: Allocate n default-constructed objects of type T
		//
		//	If allocation fails, the returned pointer will be null
		//
		template<class T>
		gc_ptr<T> make_array(std::size_t n) {
			auto p = allocate<T>(n);
			if (p != nullptr) {
				construct_array(p.get(), n);
			}
			return p;
		}

	private:
		//------------------------------------------------------------------------
		//
		//	Core allocator functions: allocate, construct, destroy
		//	(not deallocate, which is done at collection time)
		//
		//	There are private, for use via gc_allocator only

		//  Helper: Return the gcpage on which this object exists.
		//	If the object is not in our storage, returns null.
		//
		struct find_gcpage_of_ret {
			gcpage* page = nullptr;
			gpage::contains_ret info;
		};
		template<class T> 
		find_gcpage_of_ret find_gcpage_of(T& x) noexcept;

		template<class T> 
		gc_ptr<T> allocate(std::size_t n = 1);

		template<class T, class ...Args> 
		void construct(T* p, Args&& ...args);

		template<class T> 
		void construct_array(T* p, std::size_t n);

		template<class T, class Coll>
		auto find_destructor(T* p, const Coll& coll) noexcept {
			return std::find_if(std::begin(coll), std::end(coll),
				[=](auto x) { return x.p == (byte*)p; });
		}

		template<class T> 
		void destroy(gc_ptr<T>& p) noexcept;
		
		bool destroy_objects(byte* start, byte* end);

		//------------------------------------------------------------------------
		//
		//	collect, et al.: Sweep the GC arena
		//
		void mark(const void* p, std::size_t level) noexcept;

	public:
		void collect();

		void debug_print() const;

	};


	//------------------------------------------------------------------------
	//
	//  gc_ptr<T> is the typed GC pointer type for callers to use.
	//
	//------------------------------------------------------------------------
	//
	template<class T>
	class gc_ptr : public gc_heap::gc_ptr_void {
		gc_ptr(T* p)
			: gc_ptr_void(p)
		{ }

		friend gc_heap;

	public:
		//	Default and null construction. (Note we do not use a defaulted
		//	T* parameter, so that the T* overload can be private and the
		//	nullptr overload can be public.)
		//
		gc_ptr() : gc_ptr_void(nullptr) { }

		//	Construction and assignment from null. Note: The null constructor
		//	is not defined as a combination default constructor in the usual
		//	way (that is, as constructor from T* with a default null argument)
		//	because general construction from T* is private.
		//
		gc_ptr(std::nullptr_t) : gc_ptr{} { }

		gc_ptr& operator=(std::nullptr_t) noexcept {
			reset();
			return *this;
		}

		//	Copying.
		//
		gc_ptr(const gc_ptr& that)
			: gc_ptr_void(that)
		{ }

		gc_ptr& operator=(const gc_ptr& that) noexcept = default;	// trivial copy assignment

		//	Copying with conversions (base -> derived, non-const -> const).
		//
		template<class U>
		gc_ptr(const gc_ptr<U>& that)
			: gc_ptr_void(static_cast<T*>((U*)(that.p)))			// ensure U* converts to T*
		{ }

		template<class U>
		gc_ptr& operator=(const gc_ptr<U>& that) noexcept {
			gc_ptr_void::operator=(static_cast<T*>((U*)that.p));	// ensure U* converts to T*
			return *this;
		}

		//	Accessors.
		//
		T* get() const noexcept {
			return (T*)gc_ptr_void::get();
		}

		T& operator*() const noexcept {
			//	This assertion is currently disabled because MSVC's std::vector
			//	implementation relies on being able to innocuously dereference
			//	any pointer (even null) briefly just to take the pointee's
			//	address again, to un-fancy "fancy" pointers like this one
			//assert(get() && "attempt to dereference null");
			return *get();
		}

		T* operator->() const noexcept {
			//	This assertion is enabled, it doesn't break any STL I tried
			assert(get() && "attempt to dereference null");
			return get();
		}

		template<class T>
		static gc_ptr<T> pointer_to(T& t) {
			return gc_ptr<T>(&t);
		}

		//	Checked pointer arithmetic
		//
		gc_ptr& operator+=(int offset) noexcept {
#ifndef NDEBUG
			assert(get() != nullptr 
				&& "bad gc_ptr arithmetic: can't perform arithmetic on a null pointer");

			auto this_info = gc().find_gcpage_of(*get());

			assert(this_info.page != nullptr
				&& "corrupt non-null gc_ptr, not pointing into gc arena");

			assert(this_info.info.found > gpage::in_range_unallocated
				&& "corrupt non-null gc_ptr, pointing to unallocated memory");

			auto temp = get() + offset;
			auto temp_info = gc().find_gcpage_of(*temp);

			assert(this_info.page == temp_info.page 
				&& "bad gc_ptr arithmetic: attempt to leave gcpage");

			assert(
				//	if this points to the start of an allocation, it's always legal
				//	to form a pointer to the following element (just don't deref it)
				//	which covers one-past-the-end of single-element allocations
				(	(
					this_info.info.found == gpage::in_range_allocated_start
					&& (offset == 0 || offset == 1)
					)
				//	otherwise this and temp must point into the same allocation
				//	which is covered for arrays by the extra byte we allocated
				||	(
					this_info.info.start_location == temp_info.info.start_location 
					&& temp_info.info.found > gpage::in_range_unallocated)
					)
				&& "bad gc_ptr arithmetic: attempt to go outside the allocation");
#endif
			set(get() + offset);
			return *this;
		}

		gc_ptr& operator-=(int offset) noexcept {
			return operator+=(-offset);
		}

		gc_ptr& operator++() noexcept {
			return operator+=(1);
		}

		gc_ptr& operator++(int) noexcept {
			return operator+=(1);
		}

		gc_ptr& operator--() noexcept {
			return operator+=(-1);
		}

		gc_ptr operator+(int offset) const noexcept {
			auto ret = *this;
			ret += offset;
			return ret;
		}

		gc_ptr operator-(int offset) const noexcept {
			return *this + -offset;
		}

		T& operator[](size_t offset) noexcept {
#ifndef NDEBUG
			//	In debug mode, perform the arithmetic checks by creating a temporary gc_ptr
			auto tmp = *this;
			tmp += offset;	
			return *tmp;
#else
			//	In release mode, don't enregister/deregister a temnporary gc_ptr
			return *(get() + offset);
#endif
		}

		ptrdiff_t operator-(const gc_ptr& that) const noexcept {
#ifndef NDEBUG
			//	Note that this intentionally permits subtracting two null pointers
			if (get() == that.get()) {
				return 0;
			}

			assert(get() != nullptr && that.get() != nullptr
				&& "bad gc_ptr arithmetic: can't subtract pointers when one is null");

			auto this_info = gc().find_gcpage_of(*get());
			auto that_info = gc().find_gcpage_of(*that.get());

			assert(this_info.page != nullptr
				&& that_info.page != nullptr
				&& "corrupt non-null gc_ptr, not pointing into gc arena");

			assert(that_info.info.found > gpage::in_range_unallocated
				&& "corrupt non-null gc_ptr, pointing to unallocated space");

			assert(that_info.page == this_info.page
				&& "bad gc_ptr arithmetic: attempt to leave gcpage");

			assert(
				//	if that points to the start of an allocation, it's always legal
				//	to form a pointer to the following element (just don't deref it)
				//	which covers one-past-the-end of single-element allocations
				//
				//	TODO: we could eliminate this first test by adding an extra byte
				//	to every allocation, then we'd be type-safe too (this being the
				//	only way to form a gc_ptr<T> to something not allocated as a T)
				((
					that_info.info.found == gpage::in_range_allocated_start
					&& (get() == that.get()+1)
					)
					//	otherwise this and temp must point into the same allocation
					//	which is covered for arrays by the extra byte we allocated
					|| (
						that_info.info.start_location == this_info.info.start_location
						&& this_info.info.found > gpage::in_range_unallocated)
					)
				&& "bad gc_ptr arithmetic: attempt to go outside the allocation");
#endif

			return get() - that.get();
		}
	};

	//	Specialize void just to get rid of the void& return from op*

	//	TODO actually we should be able to just specialize that one function
	//	(if we do that, also disable arithmetic on void... perhaps that just falls out)

	template<>
	class gc_ptr<void> : public gc_heap::gc_ptr_void {
		gc_ptr(void* p)
			: gc_ptr_void(p)
		{ }

		friend gc_heap;

	public:
		//	Default and null construction. (Note we do not use a defaulted
		//	T* parameter, so that the T* overload can be private and the
		//	nullptr overload can be public.)
		//
		gc_ptr() : gc_ptr_void(nullptr) { }

		//	Construction and assignment from null. Note: The null constructor
		//	is not defined as a combination default constructor in the usual
		//	way (that is, as constructor from T* with a default null argument)
		//	because general construction from T* is private.
		//
		gc_ptr(std::nullptr_t) : gc_ptr{} { }

		gc_ptr& operator=(std::nullptr_t) noexcept {
			reset();
			return *this;
		}

		//	Copying.
		//
		gc_ptr(const gc_ptr& that)
			: gc_ptr_void(that)
		{ }

		gc_ptr& operator=(const gc_ptr& that) noexcept
		{
			gc_ptr_void::operator=(that);
			return *this;
		}

		//	Copying with conversions (base -> derived, non-const -> const).
		//
		template<class U>
		gc_ptr(const gc_ptr<U>& that)
			: gc_ptr_void(that)
		{ }

		template<class U>
		gc_ptr& operator=(const gc_ptr<U>& that) noexcept {
			gc_ptr_void::operator=(that);
			return *this;
		}

		//	Accessors.
		//
		void* get() const noexcept {
			return gc_ptr_void::get(); 
		}

		void* operator->() const noexcept {
			assert(get() && "attempt to dereference null"); 
			return get(); 
		}
	};

	//	Provide comparisons

	template<class T>
	bool operator==(const gc_ptr<T>& a, const gc_ptr<T>& b) noexcept {
		return a.get() == b.get();
	}

	template<class T>
	bool operator!=(const gc_ptr<T>& a, const gc_ptr<T>& b) noexcept {
		return !(a == b);
	}

	template<class T>
	bool operator==(const gc_ptr<T>& a, std::nullptr_t) noexcept {
		return a.get() == nullptr;
	}

	template<class T>
	bool operator!=(const gc_ptr<T>& a, std::nullptr_t) noexcept {
		return a.get() != nullptr;
	}


	//	Allocate one object of type T initialized with args
	//
	template<class T, class ...Args>
	gc_ptr<T> make_gc(Args&&... args) {
		return gc().make<T>( std::forward<Args>(args)... );
	}

	//	Allocate an array of n objects of type T
	//
	template<class T>
	gc_ptr<T> make_gc_array(std::size_t n) {
		return gc().make_array<T>(n);
	}


	//----------------------------------------------------------------------------
	//
	//	gc_heap function implementations
	//
	//----------------------------------------------------------------------------
	//
	gc_heap::~gc_heap() 
	{
		//	when destroying the arena, reset all pointers and run all destructors 
		for (auto& p : gc_roots) 
		{
			const_cast<gc_ptr_void*>(p)->reset();
		}
		for (auto& pg : pages) 
		{
			for (auto& p : pg.gc_ptrs) 
			{
				const_cast<gc_ptr_void*>(p.p)->reset();
			}
		}
		while (destructors.size() > 0) 
		{
			auto d = destructors.back();
			//	=====================================================================
			//  === BEGIN REENTRANCY-SAFE: ensure no in-progress use of private state
			d.dtor(d.p);	// call object's destructor
			//  === END REENTRANCY-SAFE: reload any stored copies of private state
			//	=====================================================================
			destructors.pop_back();
		}
		while (array_destructors.size() > 0) 
		{
			auto d = array_destructors.back();
			for (std::size_t i = 0; i < d.n; ++i) 
			{
				//	=====================================================================
				//  === BEGIN REENTRANCY-SAFE: ensure no in-progress use of private state
				d.dtor(d.p + i);	// call object's destructor
				//  === END REENTRANCY-SAFE: reload any stored copies of private state
				//	=====================================================================
			}
			array_destructors.pop_back();
		}

		assert(destructors.size() == 0
			&& "while destroying the gc_heap, destruction of remaining objects "
			" caused a new object to have its nontrivial destructor registered");
		//	Alternative to this assert:
		//	Defensively, we could run more than one pass over the dtors just in case
		//	any of the dtors did something to reallocate+register some object for
		//	destruction (well-written code shouldn't use the arena while it's being
		//	destroyed, but it doesn't hurt to be defensive)

	}

	//	Add this gc_ptr to the tracking list. Invoked when constructing a gc_ptr.
	//
	void gc_heap::enregister(const gc_ptr_void& p) 
	{
		// append it to the back of the appropriate list
		auto pg = find_gcpage_of(p).page;
		if (pg != nullptr) 
		{
			pg->gc_ptrs.push_back(&p);
		}
		else 
		{
			gc_roots.push_back(&p);
		}
	}

	//	Remove this gc_ptr from tracking. Invoked when destroying a gc_ptr.
	//
	void gc_heap::deregister(const gc_ptr_void& p) 
	{
		//	find its entry, starting from the back because it's more 
		//	likely to be there (newer objects tend to have shorter
		//	lifetimes... all local gc_ptrs fall into this category,
		//	and especially temporary gc_ptrs)
		//
		//	then remove it by shuffling up the ones after it
		//
		auto i = find(gc_roots.rbegin(), gc_roots.rend(), &p);
		if (i != gc_roots.rend()) 
		{
			gc_roots.erase(--i.base());	// because reverse_iterators are... nonobvious
			return;
		}

		for (auto& pg : pages) 
		{
			auto j = find_if(pg.gc_ptrs.rbegin(), pg.gc_ptrs.rend(),
				[&p](auto x) { return x.p == &p; });
			if (j != pg.gc_ptrs.rend()) 
			{
				pg.gc_ptrs.erase(--j.base());	// because reverse_iterators are... nonobvious
				return;
			}
		}

		assert(!"attempt to deregister an unregistered gc_ptr");
	}

	//  Return the gcpage on which this object exists.
	//	If the object is not in our storage, returns null.
	//
	template<class T>
	gc_heap::find_gcpage_of_ret gc_heap::find_gcpage_of(T& x) noexcept {
		find_gcpage_of_ret ret;
		auto addr = (byte*)&x;
		for (auto& pg : pages) {
			auto info = pg.page.contains(addr);
			if (info.found != gpage::not_in_range) {
				ret.page = &pg;
				ret.info = info;
			}
		}
		return ret;
	}

	template<class T>
	gc_ptr<T> gc_heap::allocate(std::size_t n) 
	{
		//	get raw memory from the backing storage...
		T* p = nullptr;
		for (auto& pg : pages) {
			p = pg.page.allocate<T>(n);
			if (p != nullptr)
				break;
		}

		//	... allocating another page if necessary
		if (p == nullptr) {
			//	pass along the type hint for size/alignment
			pages.emplace_back((T*)nullptr);
			p = pages.back().page.allocate<T>(n);
		}

		if (p == nullptr) {
			throw std::bad_alloc();
		}
		return p;
	}

	template<class T, class ...Args>
	void gc_heap::construct(T* p, Args&& ...args) 
	{
		assert(p != nullptr && "construction at null location");

		//	if there are objects with deferred destructors in this
		//	region, run those first and remove them
		destroy_objects((byte*)p, (byte*)(p + 1));

		//	construct the object...

		//	=====================================================================
		//  === BEGIN REENTRANCY-SAFE: ensure no in-progress use of private state
		::new (p) T{ std::forward<Args>(args)... };
		//  === END REENTRANCY-SAFE: reload any stored copies of private state
		//	=====================================================================

		//	... and store the destructor, if it's not trivial
		if (!std::is_trivially_destructible<T>::value) {
			destructors.push_back({
				(byte*)p,								// address p
				[](const void* x) { ((T*)x)->~T(); }	// dtor to invoke with p
			});
		}
	}

	template<class T>
	void gc_heap::construct_array(T* p, std::size_t n) 
	{
		assert(p != nullptr && "construction at null location");

		//	if there are objects with deferred destructors in this
		//	region, run those first and remove them
		destroy_objects((byte*)p, (byte*)(p + n));

		//	construct all the objects...

		for (std::size_t i = 0; i < n; ++i) {
			//	=====================================================================
			//  === BEGIN REENTRANCY-SAFE: ensure no in-progress use of private state
			::new (p) T{};
			//  === END REENTRANCY-SAFE: reload any stored copies of private state
			//	=====================================================================
		}

		//	... and store the destructor, if it's not trivial
		if (!std::is_trivially_destructible<T>::value) {
			array_destructors.push_back({
				(byte*)p,								// address p
				n,										// count n
				[](const void* x) { ((T*)x)->~T(); }	// dtor to invoke with p
			});
		}
	}

	template<class T>
	void gc_heap::destroy(gc_ptr<T>& p) noexcept 
	{
		assert(
			(p == nullptr
				|| std::is_trivially_destructible<T>::value
				|| find_destructor(p, destructors) != end(destructors)
				)
			&& "attempt to destroy an object whose destructor is not registered"
		);
	}

	bool gc_heap::destroy_objects(byte* start, byte* end) 
	{
		assert(start < end && "start must precede end");
		bool ret = false;

		//	for reentrancy safety, we'll take a local copy of destructors to be run

		//	first look through the single-objects list
		{
			std::vector<destructor> to_destroy;
			for (auto it = destructors.begin(); it != destructors.end(); /*--*/) {
				if (start <= it->p && it->p < end) {
					to_destroy.push_back(*it);
					it = destructors.erase(it);
					ret = true;
				}
				else {
					++it;
				}
			}

			for (auto& d : to_destroy) {
				//	=====================================================================
				//  === BEGIN REENTRANCY-SAFE: ensure no in-progress use of private state
				d.dtor(d.p);
				//  === END REENTRANCY-SAFE: reload any stored copies of private state
				//	=====================================================================
			}
		}

		//	then look through in the arrays list
		{
			std::vector<array_destructor> to_destroy;
			for (auto it = array_destructors.begin(); it != array_destructors.end(); /*--*/) {
				if (start <= it->p && it->p < end) {
					to_destroy.push_back(*it);
					it = array_destructors.erase(it);
					ret = true;
				}
				else {
					++it;
				}
			}

			for (auto& d : to_destroy) {
				for (std::size_t i = 0; i < d.n; ++i) {
					//	=====================================================================
					//  === BEGIN REENTRANCY-SAFE: ensure no in-progress use of private state
					d.dtor(d.p + i);	// call each object's destructor
										//  === END REENTRANCY-SAFE: reload any stored copies of private state
										// TODO REENTRANCY-UNSAFE
										//	=====================================================================
				}
			}
		}

		//	else there wasn't a nontrivial destructor
		return ret;
	}

	//------------------------------------------------------------------------
	//
	//	collect, et al.: Sweep the GC arena
	//
	void gc_heap::mark(const void* p, std::size_t level) noexcept 
	{
		// if it isn't null ...
		if (p == nullptr)
			return;

		// ... find which page it points into ...
		for (auto& pg : pages) {
			auto where = pg.page.contains((byte*)p);
			assert(where.found != gpage::in_range_unallocated
				&& "must not point to unallocated memory");
			if (where.found != gpage::not_in_range) {
				// ... and mark the chunk as live ...
				pg.live_starts[where.start_location] = true;

				// ... and mark any gc_ptrs in the allocation as reachable
				for (auto& gcp : pg.gc_ptrs) {
					auto gcp_where = pg.page.contains((byte*)gcp.p);
					assert((gcp_where.found == gpage::in_range_allocated_middle
						|| gcp_where.found == gpage::in_range_allocated_start)
						&& "points to unallocated memory");
					if (gcp_where.start_location == where.start_location
						&& gcp.level == 0) {
						gcp.level = level;	// 'level' steps from a root
					}
				}
				break;
			}
		}
	}

	void gc_heap::collect() 
	{
		//	1. reset all the mark bits and in-arena gc_ptr levels
		//
		for (auto& pg : pages) {
			pg.live_starts.assign(pg.live_starts.size(), false);
			for (auto& gcp : pg.gc_ptrs) {
				gcp.level = 0;
			}
		}

		//	2. mark all roots + the in-arena gc_ptrs reachable from them
		//
		std::size_t level = 1;
		for (auto& p : gc_roots) {
			mark(p->get(), level);	// mark this gc_ptr root
		}

		bool done = false;
		while (!done) {
			done = true;	// we're done unless we find another to mark
			++level;
			for (auto& pg : pages) {
				for (auto& gcp : pg.gc_ptrs) {
					if (gcp.level == level - 1) {
						done = false;
						mark(gcp.p->get(), level);	// mark this reachable in-arena gc_ptr
					}
				}
			}
		}

//#ifndef NDEBUG
		std::cout << "=== COLLECT live_starts locations:\n     ";
		for (auto& pg : pages) {
			for (std::size_t i = 0; i < pg.page.locations(); ++i) {
				std::cout << (pg.live_starts[i] ? 'A' : '.');
				if (i % 8 == 7) { std::cout << ' '; }
				if (i % 64 == 63) { std::cout << "\n     "; }
			}
		}
		std::cout << "\n\n";
//#endif

		//	We have now marked every allocation to save, so now
		//	go through and clean up all the unreachable objects

		//	3. reset all unreached gc_ptrs to null
		//	
		//	Note: 'const gc_ptr' is supported and behaves as const w.r.t. the
		//	the program code; however, a gc_ptr data member can become
		//	spontaneously null *during object destruction* even if declared
		//	const to the rest of the program. So the collector is an exception
		//	to constness, and the const_cast below is because any gc_ptr must
		//	be able to be set to null during collection, as part of safely
		//	breaking cycles. (We could declare the data member mutable, but
		//	then we might accidentally modify it in another const function.
		//	Since a const gc_ptr should only be reset in this one case, it's
		//	more appropriate to avoid mutable and put the const_cast here.)
		//
		//	This is the same "don't touch other objects during finalization
		//	because they may already have been finalized" rule as has evolved
		//	in all cycle-breaking GCs. But, unlike the managed languages, here
		//	the rule is actually directly supported and enforced (one object
		//	being collected cannot touch another collectable object by
		//	accident because the gc_ptr to that other object is null), it
		//	removes the need for separate "finalizer" functions (we always run
		//	real destructors, and only have to teach that gc_ptrs might be null
		//	in a destructor), and it eliminates the possibility of resurrection
		//	(it is not possible for a destructor to make a collectable object
		//	reachable again because we eliminate all pointers to it before any
		//	user-defined destructor gets a chance to run). This is fully
		//	compatible with learnings from existing GC approaches, but strictly
		//	better in all these respects by directly enforcing those learnings
		//	in the design, thus eliminating large classes of errors while also
		//	minimizing complexity by inventing no new concepts other than
		//	the rule "gc_ptrs can be null in destructors."
		//
		for (auto& pg : pages) {
			for (auto& gcp : pg.gc_ptrs) {
				if (gcp.level == 0) {
					const_cast<gc_ptr_void*>(gcp.p)->reset();
				}
			}
		}

		//	4. deallocate all unreachable allocations, running
		//	destructors if registered
		//
		for (auto& pg : pages) {
			for (std::size_t i = 0; i < pg.page.locations(); ++i) {
				auto start = pg.page.location_info(i);
				if (start.is_start && !pg.live_starts[i]) {
					//	this is an allocation to destroy and deallocate

					//	find the end of the allocation
					auto end_i = i + 1;
					auto end = pg.page.location_info(pg.page.locations()).pointer;
					for (; end_i < pg.page.locations(); ++end_i) {
						auto info = pg.page.location_info(end_i);
						if (info.is_start) {
							end = info.pointer;
							break;
						}
					}

					// call the destructors for objects in this range
					destroy_objects(start.pointer, end);

					// and then deallocate the raw storage
					pg.page.deallocate(start.pointer);
				}
			}
		}

	}


	void gc_heap::debug_print() const 
	{
		for (auto& pg : pages) {
			pg.page.debug_print();
			std::cout << "  this page's gc_ptrs.size() is " << pg.gc_ptrs.size() << "\n";
			for (auto& gcp : pg.gc_ptrs) {
				std::cout << "    " << (void*)gcp.p << " -> " << gcp.p->get()
					<< ", level " << gcp.level << "\n";
			}
			std::cout << "\n";
		}
		std::cout << "  gc_roots.size() is " << gc_roots.size() << "\n";
		for (auto& p : gc_roots) {
			std::cout << "    " << (void*)p << " -> " << p->get() << "\n";
		}
		std::cout << "\n  destructors.size() is " << destructors.size() << "\n";
		for (auto& d : destructors) {
			std::cout << "    " << (void*)(d.p) << ", " << (void*)(d.dtor) << "\n";
		}
		std::cout << "\n  array_destructors.size() is " << array_destructors.size() << "\n";
		for (auto& d : array_destructors) {
			std::cout << "    " << (void*)(d.p) << ", " << d.n << ", " << (void*)(d.dtor) << "\n";
		}
		std::cout << "\n";
	}


}

#endif

