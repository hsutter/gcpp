
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


#ifndef GCPP_DEFERRED_HEAP
#define GCPP_DEFERRED_HEAP

#include "gpage.h"

#include <vector>
#include <list>
#include <utility>
#include <unordered_set>
#include <algorithm>
#include <type_traits>
#include <memory>

namespace gcpp {
	template<class T> class deferred_ptr;

	//  Copy and remove elements that satisfy pred from [first, last) into out.
	template<class BidirectionalIterator, class OutputIterator, class Predicate>
	std::pair<BidirectionalIterator, OutputIterator>
	unstable_remove_copy_if(BidirectionalIterator first, BidirectionalIterator last,
		OutputIterator out, Predicate pred)
	{
		for (;;) {
			first = std::find_if(first, last, pred);
			if (first == last) {
				break;
			}
			// *first satisfies pred. Move it out of the sequence...
			*out++ = std::move(*first);
			// ...and replace with the last element of the sequence.
			if (first == --last) {
				break;
			}
			*first = std::move(*last);
		}
		return {first, out};
	}

	//  destructor contains a pointer and type-correct-but-erased dtor call.
	//  (Happily, a noncapturing lambda decays to a function pointer, which
	//	will make these both easy to construct and cheap to store without
	//	resorting to the usual type-erasure machinery.)
	//
	class destructors {
		struct destructor {
			const void* p;
			void(*destroy)(const void*);
		};
		std::vector<destructor>	dtors;

	public:
		//	Store the destructor, if it's not trivial
		//
		template<class T>
		void store(gsl::span<T> p) {
			Expects(p.size() > 0
				&& "no object to register for destruction");
			if (!std::is_trivially_destructible<T>::value) {
				//	For now we'll just store individual dtors even for arrays.
				//	Future: To represent destructors for arrays more compactly,
				//	have an array_destructor type as well with a count and size,
				//  and when storing a new destructor check if it's immediately
				//	after the end of an existing array destructor and if so just
				//	++count, similarly when removing a destructor from the end,
				//	or break apart an array_destructor when removing a
				//	destructor from the middle
				for (auto& t : p) {
					dtors.push_back({
						std::addressof(t),		// address
						[](const void* x) { static_cast<const T*>(x)->~T(); }
					});							// dtor to invoke
				}
			}
		}

		//	Inquire whether there is a destructor registered for p
		//
		template<class T>
		bool is_stored(gsl::not_null<T*> p) const noexcept {
			return std::is_trivially_destructible<T>::value
				|| std::any_of(dtors.begin(), dtors.end(),
					[=](auto x) { return x.p == p.get(); });
		}

		//	Run all the destructors and clear the list
		//
		void run_all() {
			for (auto& d : dtors) {
				d.destroy(d.p);	// call object's destructor
			}
			dtors.clear();
		}

		//	Run all the destructors for objects in [begin,end)
		//
		bool run(gsl::span<byte> range) {
			if (range.size() == 0)
				return false;

			//	for reentrancy safety, we'll take a local copy of destructors to be run
			//
			//	move any destructors for objects in this range to a local list...
			//
			struct cleanup_t {
				std::vector<destructor> to_destroy;

				// ensure the locally saved destructors are run even if an exception is thrown
				~cleanup_t() {
					for (auto& d : to_destroy) {
						//	=====================================================================
						//  === BEGIN REENTRANCY-SAFE: ensure no in-progress use of private state
						d.destroy(d.p);	// call object's destructor
						//  === END REENTRANCY-SAFE: reload any stored copies of private state
						//	=====================================================================
					}
				}
			} cleanup;

			auto const lo = &*range.begin(), hi = lo + range.size();
			auto it = unstable_remove_copy_if(
				dtors.begin(), dtors.end(), std::back_inserter(cleanup.to_destroy),
				[=](destructor const& dtor) { return lo <= dtor.p && dtor.p < hi; }).first;
			dtors.erase(it, dtors.end());

			return !cleanup.to_destroy.empty();
		}

		void debug_print() const;
	};


	//----------------------------------------------------------------------------
	//
	//	The deferred heap produces deferred_ptr<T>s via make<T>.
	//
	//----------------------------------------------------------------------------

	class deferred_heap {
		class  deferred_ptr_void;
		friend class deferred_ptr_void;

		template<class T> friend class deferred_ptr;
		template<class T> friend class deferred_allocator;

		//	Disable copy and move
		deferred_heap(deferred_heap&)  = delete;
		void operator=(deferred_heap&) = delete;

		//	Add/remove a deferred_ptr in the tracking list.
		//	Invoked when constructing and destroying a deferred_ptr.
		void enregister(const deferred_ptr_void& p);
		void deregister(const deferred_ptr_void& p);

		//------------------------------------------------------------------------
		//
		//  deferred_ptr_void is the generic pointer type we use and track
		//  internally. The user uses deferred_ptr<T>, the type-casting wrapper.
		//
		//	There are two main states:
		//
		//	- unattached, myheap == nullptr
		//		this pointer is not yet attached to a heap, and p must be null
		//
		//	- attached, myheap != nullptr
		//		this pointer is attached to myheap and must not be repointed to a
		//		different heap (assignment from a pointer into a different heap is
		//		not allowed)
		//
		//	The pointer becomes attached when it is first constructed or assigned with
		//	a non-null page pointer (incl. when copied from an attached pointer). The
		//	pointer becomes unattached when the heap it was attached to is destroyed.
		//
		class deferred_ptr_void {
			//	There are other ways to implement this, including to make deferred_ptr
			//	be the same size as an ordinary pointer and trivially copyable; one
			//	alternative implementation checked in earlier did that.
			//
			//	For right now, I think it's easier to present the key ideas initially
			//	with fewer distractions by just keeping a back pointer to the heap.
			//	The two reasons are to enable debug checks to prevent cross-assignment
			//	from multiple heaps, and to know where to deregister. Other ways to
			//	do the same include having statically tagged heaps, such as having
			//	deferred_heap<MyHeapTag> give deferred_ptr<T, MyHeapTag>s and
			//	just statically prevent cross-assignment and statically identify the
			//	heaps, which lets you have trivially copyable deferred_ptrs and no
			//	run-time overhead for deferred_ptr space and assignment. I felt that
			//	explaining static heap tagging would be a distraction in the initial
			//	presentation from the central concepts that are actually important.
			deferred_heap* myheap;
			void* p;

			friend deferred_heap;

		protected:
			void  set(void* p_) noexcept { p = p_; }

			deferred_ptr_void(deferred_heap* heap = nullptr, void* p_ = nullptr)
				: myheap{ heap }
				, p{ p_ }
			{
				//	Allow null pointers, we'll set the page on the first assignment
				Expects((p == nullptr || myheap != nullptr) && "heap cannot be null for a non-null pointer");
				if (myheap != nullptr) {
					myheap->enregister(*this);
				}
			}

			~deferred_ptr_void() {
				if (myheap != nullptr) {
					myheap->deregister(*this);
				}
			}

			deferred_ptr_void(const deferred_ptr_void& that)
				: deferred_ptr_void(that.myheap, that.p)
			{ }

			deferred_ptr_void& operator=(const deferred_ptr_void& that) noexcept {
				//	Allow assignment from an unattached null pointer
				if (that.myheap == nullptr) {
					Expects(that.p == nullptr && "unattached deferred_ptr must be null");
					reset();	// just to keep the nulling logic in one place
				}

				//	Otherwise, we must be unattached or pointing into the same heap
				else {
					Expects((myheap == nullptr || myheap == that.myheap)
						&& "cannot assign deferred_ptrs into different deferred_heaps");
					p = that.p;
					if (myheap == nullptr) {
						that.myheap->enregister(*this);	// perform lazy attach
						myheap = that.myheap;
					}
				}

				return *this;
			}

			//	detach is called from ~deferred_heap() when the heap is destroyed
			//	before this pointer is destroyed
			//
			void detach() noexcept {
				p = nullptr;
				myheap = nullptr;
			}

		public:
			deferred_heap* get_heap() const noexcept { return myheap; }

			void* get() const noexcept { return p; }

			void  reset() noexcept { p = nullptr; /* leave myheap alone so we can assign again */ }
		};

		//	For non-roots (deferred_ptrs that are in the deferred heap), we'll additionally
		//	store an int that we'll use for terminating marking within the deferred heap.
		//  The level is the distance from some root -- not necessarily the smallest
		//	distance from a root, just along whatever path we took during marking.
		//
		struct nonroot {
			const deferred_ptr_void* p;
			std::size_t level = 0;

			nonroot(const deferred_ptr_void* p_) noexcept : p{ p_ } { }
		};

		struct dhpage {
			gpage				 page;
			bitflags		 	 live_starts;	// for tracing
			std::vector<nonroot> deferred_ptrs;	// known deferred_ptrs in this page
			deferred_heap*		 myheap;

			//	Construct a page tuned to hold Hint objects, big enough for
			//	at least 1 + phi ~= 2.62 of these requests (but at least 8K),
			//	and a tracking min_alloc chunk sizeof(request) (but at least 4 bytes).
			//	Note: Hint used only to deduce total size and tracking granularity.
			//	Future: Don't allocate objects on pages with chunk sizes > 2 * object size
			//
			template<class Hint>
			dhpage(const Hint* /*--*/, size_t n, deferred_heap* heap)
				: page{ std::max<size_t>(sizeof(Hint) * n * 3, 8192 /*good general default*/),
						std::max<size_t>(sizeof(Hint), 4) }
				, live_starts{ page.locations(), false }
				, myheap{ heap }
			{ }
		};


		//------------------------------------------------------------------------
		//	Data: Storage and tracking information
		//
		std::list<dhpage>							 pages;
		std::unordered_set<const deferred_ptr_void*> roots;	// outside deferred heap
		destructors									 dtors;

		bool is_destroying = false;
		bool collect_before_expand = false;	// Future: pull this into an options struct


	public:
		//------------------------------------------------------------------------
		//
		//	Construct and destroy
		//
		deferred_heap() = default;

		~deferred_heap();

		//------------------------------------------------------------------------
		//
		//	make: Allocate one object of type T initialized with args
		//
		//	If allocation fails, the returned pointer will be null
		//
		template<class T, class ...Args>
		deferred_ptr<T> make(Args&&... args) {
			auto p = allocate<T>();
			if (p != nullptr) {
				construct<T>(p.get(), std::forward<Args>(args)...);
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
		deferred_ptr<T> make_array(std::size_t n) {
			auto p = allocate<T>(n);
			if (p != nullptr) {
				construct_array<T>(p.get(), n);
			}
			return p;
		}

	private:
		//------------------------------------------------------------------------
		//
		//	Core allocator functions: allocate, construct, destroy
		//	(not deallocate, which is done at collection time)
		//
		//	There are private, for use via deferred_allocator only

		//  Helper: Return the dhpage on which this object exists.
		//	If the object is not in our storage, returns null.
		//
		template<class T>
		dhpage* find_dhpage_of(T* p) noexcept;

		struct find_dhpage_info_ret {
			dhpage* page = nullptr;
			gpage::contains_info_ret info;
		};
		template<class T>
		find_dhpage_info_ret find_dhpage_info(T* p) noexcept;

		template<class T>
		std::pair<dhpage*, byte*> allocate_from_existing_pages(int n);

		template<class T>
		deferred_ptr<T> allocate(int n = 1);

		template<class T, class ...Args>
		void construct(gsl::not_null<T*> p, Args&& ...args);

		template<class T>
		void construct_array(gsl::not_null<T*> p, int n);

		template<class T>
		void destroy(gsl::not_null<T*> p) noexcept;

		bool destroy_objects(gsl::span<byte> range);

		//------------------------------------------------------------------------
		//
		//	collect, et al.: Sweep the deferred heap
		//
		void mark(const deferred_ptr_void& p, std::size_t level) noexcept;

	public:
		void collect();

		auto get_collect_before_expand() {
			return collect_before_expand;
		}

		void set_collect_before_expand(bool enable = false) {
			collect_before_expand = enable;
		}

		void debug_print() const;
	};


	//------------------------------------------------------------------------
	//
	//  deferred_ptr<T> is the typed pointer type for callers to use.
	//
	//------------------------------------------------------------------------
	//
	template<class T>
	class deferred_ptr : public deferred_heap::deferred_ptr_void {
		deferred_ptr(deferred_heap* heap, T* p)
			: deferred_ptr_void{ heap, p }
		{ }

		friend deferred_heap;

		template<class U>
		friend class deferred_ptr;

	public:
		// iterator traits
		using value_type         = T;
		using pointer            = deferred_ptr<value_type>;
		using reference          = std::add_lvalue_reference_t<T>;
		using difference_type    = ptrdiff_t;
		using iterator_category  = std::random_access_iterator_tag;

		//	Default and null construction. (Note we do not use a defaulted
		//	T* parameter, so that the T* overload can be private and the
		//	nullptr overload can be public.)
		//
		deferred_ptr() = default;

		//	Construction and assignment from null. Note: The null constructor
		//	is not defined as a combination default constructor in the usual
		//	way (that is, as constructor from T* with a default null argument)
		//	because general construction from T* is private.
		//
		deferred_ptr(std::nullptr_t) : deferred_ptr{} { }

		deferred_ptr& operator=(std::nullptr_t) noexcept {
			reset();
			return *this;
		}

		//	Copying.
		//
		deferred_ptr(const deferred_ptr& that)
			: deferred_ptr_void(that)
		{ }

		deferred_ptr& operator=(const deferred_ptr& that) noexcept = default;	// trivial copy assignment

		//	Copying with conversions (base -> derived, non-const -> const).
		//
		template<class U, class = typename std::enable_if<std::is_convertible<U*, T*>::value, void>::type>
		deferred_ptr(const deferred_ptr<U>& that)
			: deferred_ptr_void(that)
		{ }

		template<class U, class = typename std::enable_if<std::is_convertible<U*, T*>::value, void>::type>
		deferred_ptr& operator=(const deferred_ptr<U>& that) noexcept {
			deferred_ptr_void::operator=(that);
			return *this;
		}

		//	Aliasing conversion: Type-safely forming a pointer to data member of T of type U.
		//	Thanks to Casey Carter and Jon Caves for helping get this incantation right.
		//
		template<class U> struct id { using type = U; };		// this is just to turn off
		template<class U> using id_t = typename id<U>::type;	// type deduction for TT and ...

		template<class U, class TT = T>	// .. TT itself is a workaround for that we can't just
		deferred_ptr<U> ptr_to(U id_t<TT>::*pU) {	// write T:: here because <<C++ arcana>>
			Expects(get_heap() && get() && "can't ptr_to on an unattached or null pointer");
			return{ get_heap(), &(get()->*pU) };
		}

		//	Accessors.
		//
		T* get() const noexcept {
			return (T*)deferred_ptr_void::get();
		}

		explicit operator bool() const { return deferred_ptr_void::get() != nullptr; }

		std::add_lvalue_reference_t<T> operator*() const noexcept {
			//	This contract is currently disabled because MSVC's std::vector
			//	implementation relies on being able to innocuously dereference
			//	any pointer (even null) briefly just to take the pointee's
			//	address again, to un-fancy "fancy" pointers like this one
			//	(The next VS "15" Preview has a fix & we can re-enable this.)
			//Expects(get() && "attempt to dereference null");
			return *get();
		}

		T* operator->() const noexcept {
			Expects(get() && "attempt to dereference null");
			return get();
		}

		template<class U>
		static deferred_ptr<U> pointer_to(U& u) {
			return deferred_ptr<U>(&u);
		}

		int compare3(const deferred_ptr& that) const { return get() < that.get() ? -1 : get() == that.get() ? 0 : 1; };
		GCPP_TOTALLY_ORDERED_COMPARISON(deferred_ptr);	// maybe someday this will be default

		//	Checked pointer arithmetic
		//
		//	Future: This is checked in debug mode, but it might be better to split off
		//	arithmetic into a separate array_deferred_ptr or deferred_span or suchlike
		//	type. For now it's on deferred_ptr itself because when you instantiate
		//	vector<T, deferred_allocator<T>> you need a pointer type that works as a
		//	random-access iterator, and if we split this into an array_deferred_ptr type
		//	we'll also need to make deferred_allocator use that instead... that can wait.
		//
		deferred_ptr& operator+=(int offset) noexcept {
#ifndef NDEBUG
			Expects(get() != nullptr
				&& "bad deferred_ptr arithmetic: can't perform arithmetic on a null pointer");

			auto this_info = get_heap()->find_dhpage_info(get());

			Expects(this_info.page != nullptr
				&& "corrupt non-null deferred_ptr, not pointing into deferred heap");

			Expects(this_info.info.found > gpage::in_range_unallocated
				&& "corrupt non-null deferred_ptr, pointing to unallocated memory");

			auto temp = get() + offset;
			auto temp_info = get_heap()->find_dhpage_info(temp);

			Expects(this_info.page == temp_info.page
				&& "bad deferred_ptr arithmetic: attempt to leave dhpage");

			Expects(
				//	if this points to the start of an allocation, it's always legal
				//	to form a pointer to the following element (just don't deref it)
				//	which covers one-past-the-end of single-element allocations
				(	(
					this_info.info.found == gpage::in_range_allocated_start
					&& (offset == -1 || offset == 0 || offset == 1)
					)
				//	otherwise this and temp must point into the same allocation
				//	which is covered for arrays by the extra byte we allocated
				||	(
					this_info.info.start_location == temp_info.info.start_location
					&& temp_info.info.found > gpage::in_range_unallocated)
					)
				&& "bad deferred_ptr arithmetic: attempt to go outside the allocation");
#endif
			set(get() + offset);
			return *this;
		}

		deferred_ptr& operator-=(int offset) noexcept {
			return operator+=(-offset);
		}

		deferred_ptr& operator++() noexcept {
			return operator+=(1);
		}

		deferred_ptr& operator++(int) noexcept {
			return operator+=(1);
		}

		deferred_ptr& operator--() noexcept {
			return operator+=(-1);
		}

		deferred_ptr operator+(int offset) const noexcept {
			auto ret = *this;
			ret += offset;
			return ret;
		}

		deferred_ptr operator-(int offset) const noexcept {
			return *this + -offset;
		}

		std::add_lvalue_reference_t<T> operator[](size_t offset) noexcept {
#ifndef NDEBUG
			//	In debug mode, perform the arithmetic checks by creating a temporary deferred_ptr
			auto tmp = *this;
			tmp += offset;
			return *tmp;
#else
			//	In release mode, don't enregister/deregister a temnporary deferred_ptr
			return *(get() + offset);
#endif
		}

		ptrdiff_t operator-(const deferred_ptr& that) const noexcept {
#ifndef NDEBUG
			//	Note that this intentionally permits subtracting two null pointers
			if (get() == that.get()) {
				return 0;
			}

			Expects(get() != nullptr && that.get() != nullptr
				&& "bad deferred_ptr arithmetic: can't subtract pointers when one is null");

			auto this_info = get_heap()->find_dhpage_info(get());
			auto that_info = get_heap()->find_dhpage_info(that.get());

			Expects(this_info.page != nullptr
				&& that_info.page != nullptr
				&& "corrupt non-null deferred_ptr, not pointing into deferred heap");

			Expects(that_info.info.found > gpage::in_range_unallocated
				&& "corrupt non-null deferred_ptr, pointing to unallocated space");

			Expects(that_info.page == this_info.page
				&& "bad deferred_ptr arithmetic: attempt to leave dhpage");

			Expects(
				//	If that points to the start of an allocation, it's always legal
				//	to form a pointer to the following element (just don't deref it)
				//	which covers one-past-the-end of single-element allocations
				//
				//	Future: We could eliminate this first test by adding an extra byte
				//	to every allocation, then we'd be type-safe too (this being the
				//	only way to form a deferred_ptr<T> to something not allocated as a T)
				((
					that_info.info.found == gpage::in_range_allocated_start
					&& (get() == that.get()+1)
					)
					//	Otherwise this and temp must point into the same allocation
					//	which is covered for arrays by the extra byte we allocated
					|| (
						that_info.info.start_location == this_info.info.start_location
						&& this_info.info.found > gpage::in_range_unallocated)
					)
				&& "bad deferred_ptr arithmetic: attempt to go outside the allocation");
#endif

			return get() - that.get();
		}
	};

	//	Specialize void just to get rid of the void& return from operator*

	template<>
	class deferred_ptr<void> : public deferred_heap::deferred_ptr_void {
		deferred_ptr(deferred_heap* page, void* p)
			: deferred_ptr_void(page, p)
		{ }

		friend deferred_heap;

	public:
		//	Default and null construction. (Note we do not use a defaulted
		//	T* parameter, so that the T* overload can be private and the
		//	nullptr overload can be public.)
		//
		deferred_ptr() : deferred_ptr_void(nullptr) { }

		//	Construction and assignment from null. Note: The null constructor
		//	is not defined as a combination default constructor in the usual
		//	way (that is, as constructor from T* with a default null argument)
		//	because general construction from T* is private.
		//
		deferred_ptr(std::nullptr_t) : deferred_ptr{} { }

		deferred_ptr& operator=(std::nullptr_t) noexcept {
			reset();
			return *this;
		}

		//	Copying.
		//
		deferred_ptr(const deferred_ptr& that)
			: deferred_ptr_void(that)
		{ }

		deferred_ptr& operator=(const deferred_ptr& that) noexcept
		{
			deferred_ptr_void::operator=(that);
			return *this;
		}

		//	Copying with conversions (base -> derived, non-const -> const).
		//
		template<class U>
		deferred_ptr(const deferred_ptr<U>& that)
			: deferred_ptr_void(that)
		{ }

		template<class U>
		deferred_ptr& operator=(const deferred_ptr<U>& that) noexcept {
			deferred_ptr_void::operator=(that);
			return *this;
		}

		//	Accessors.
		//
		void* get() const noexcept {
			return deferred_ptr_void::get();
		}

		void* operator->() const noexcept {
			Expects(get() && "attempt to dereference null");
			return get();
		}
	};


	//----------------------------------------------------------------------------
	//
	//	deferred_heap function implementations
	//
	//----------------------------------------------------------------------------
	//
	inline
	deferred_heap::~deferred_heap()
	{
		//	Note: setting this flag lets us skip worrying about reentrancy;
		//	a destructor may not allocate a new object (which would try to
		//	enregister and therefore change our data structures)
		is_destroying = true;

		//	when destroying the arena, detach all pointers and run all destructors
		//
		for (auto& p : roots) {
			const_cast<deferred_ptr_void*>(p)->detach();
		}

		for (auto& pg : pages) {
			for (auto& p : pg.deferred_ptrs) {
				const_cast<deferred_ptr_void*>(p.p)->detach();
			}
		}

		//	this calls user code (the dtors), but no reentrancy care is
		//	necessary per note above
		dtors.run_all();
	}

	//	Add this deferred_ptr to the tracking list. Invoked when constructing a deferred_ptr.
	//
	inline
	void deferred_heap::enregister(const deferred_ptr_void& p) {
		//	append it to the back of the appropriate list
		Expects(!is_destroying
			&& "cannot allocate new objects on a deferred_heap that is being destroyed");
		auto pg = find_dhpage_of(&p);
		if (pg != nullptr)
		{
			pg->deferred_ptrs.push_back(&p);
		}
		else
		{
			roots.insert(&p);
		}
	}

	//	Remove this deferred_ptr from tracking. Invoked when destroying a deferred_ptr.
	//
	inline
	void deferred_heap::deregister(const deferred_ptr_void& p) {
		//	no need to actually deregister if we're tearing down this deferred_heap
		if (is_destroying)
			return;

		//	find its entry, starting from the back because it's more
		//	likely to be there (newer objects tend to have shorter
		//	lifetimes... all local deferred_ptrs fall into this category,
		//	and especially temporary deferred_ptrs)
		//
		auto erased_count = roots.erase(&p);
		Expects(erased_count < 2 && "duplicate registration");
		if (erased_count > 0)
			return;

		for (auto& pg : pages) {
			auto j = find_if(pg.deferred_ptrs.rbegin(), pg.deferred_ptrs.rend(),
				[&p](auto x) { return x.p == &p; });
			if (j != pg.deferred_ptrs.rend()) {
				*j = pg.deferred_ptrs.back();
				pg.deferred_ptrs.pop_back();
				return;
			}
		}

		Expects(!"attempt to deregister an unregistered deferred_ptr");
	}

	//  Return the dhpage on which this object exists.
	//	If the object is not in our storage, returns null.
	//
	template<class T>
	deferred_heap::dhpage* deferred_heap::find_dhpage_of(T* p) noexcept {
		if (p != nullptr) {
			for (auto& pg : pages) {
				if (pg.page.contains((byte*)p))
					return &pg;
			}
		}
		return nullptr;
	}

	template<class T>
	deferred_heap::find_dhpage_info_ret deferred_heap::find_dhpage_info(T* p)  noexcept {
		find_dhpage_info_ret ret;
		for (auto& pg : pages) {
			auto info = pg.page.contains_info((byte*)p);
			if (info.found != gpage::not_in_range) {
				ret.page = &pg;
				ret.info = info;
			}
		}
		return ret;
	}

	template<class T>
	std::pair<deferred_heap::dhpage*, byte*>
	deferred_heap::allocate_from_existing_pages(int n) {
		for (auto& pg : pages) {
			auto p = pg.page.allocate<T>(n);
			if (p != nullptr)
				return{ &pg, p };
		}
		return{ nullptr, nullptr };
	}

	template<class T>
	deferred_ptr<T> deferred_heap::allocate(int n)
	{
		Expects(n > 0 && "cannot request an empty allocation");

		//	get raw memory from the backing storage...
		auto p = allocate_from_existing_pages<T>(n);

		//	... performing a collection if necessary ...
		if (p.second == nullptr && collect_before_expand) {
			collect();
			p = allocate_from_existing_pages<T>(n);
		}

		//	... allocating another page if necessary
		if (p.second == nullptr) {
			//	pass along the type hint for size/alignment
			pages.emplace_back((T*)nullptr, n, this);
			p.first = &pages.back();	// Future: just use emplace_back's return value, in a C++17 STL
			p = { p.first, p.first->page.template allocate<T>(n) };
		}

		Expects(p.second != nullptr && "failed to allocate but didn't throw an exception");
		return{ this, reinterpret_cast<T*>(p.second) };
	}

	template<class T, class ...Args>
	void deferred_heap::construct(gsl::not_null<T*> p, Args&& ...args)
	{
		//	if there are objects with deferred destructors in this
		//	region, run those first and remove them
		destroy_objects({ (byte*)p.get(), sizeof(T) });

		//	construct the object...

		//	=====================================================================
		//  === BEGIN REENTRANCY-SAFE: ensure no in-progress use of private state
		::new (static_cast<void*>(p.get())) T{ std::forward<Args>(args)... };
		//  === END REENTRANCY-SAFE: reload any stored copies of private state
		//	=====================================================================

		//	... and store the destructor
		dtors.store(gsl::span<T>(p, 1));
	}

	template<class T>
	void deferred_heap::construct_array(gsl::not_null<T*> p, int n)
	{
		Expects(n > 0 && "cannot request an empty array");

		//	if there are objects with deferred destructors in this
		//	region, run those first and remove them
		destroy_objects({ (byte*)p.get(), gsl::narrow_cast<int>(sizeof(T)) * n });

		//	construct all the objects...

		for (auto i = 0; i < n; ++i) {
			//	=====================================================================
			//  === BEGIN REENTRANCY-SAFE: ensure no in-progress use of private state
			try {
				::new (static_cast<void*>(p.get() + i)) T{};
			} catch(...) {
				while (i-- > 0) {
					(p.get() + i)->~T();
				}
				throw;
			}
			//  === END REENTRANCY-SAFE: reload any stored copies of private state
			//	=====================================================================
		}

		//	... and store the destructor
		dtors.store(gsl::span<T>(p, n));
	}

	template<class T>
	void deferred_heap::destroy(gsl::not_null<T*> p) noexcept
	{
		Expects(dtors.is_stored(p)
			&& "attempt to destroy an object whose destructor is not registered");
	}

	inline
	bool deferred_heap::destroy_objects(gsl::span<byte> range) {
		return dtors.run(range);
	}

	//------------------------------------------------------------------------
	//
	//	collect, et al.: Sweep the deferred heap
	//
	inline
	void deferred_heap::mark(const deferred_ptr_void& p, std::size_t level) noexcept
	{
		//	if it isn't null ...
		if (p.get() == nullptr)
			return;

		// ... find which page it points into ...
		for (auto& pg : pages) {
			auto where = pg.page.contains_info((byte*)p.get());
			Expects(where.found != gpage::in_range_unallocated
				&& "must not point to unallocated memory");
			if (where.found != gpage::not_in_range) {
				// ... and mark the chunk as live ...
				pg.live_starts.set(where.start_location, true);

				// ... and mark any deferred_ptrs in the allocation as reachable
				for (auto& dp : pg.deferred_ptrs) {
					auto dp_where = pg.page.contains_info((byte*)dp.p);
					Expects((dp_where.found == gpage::in_range_allocated_middle
						|| dp_where.found == gpage::in_range_allocated_start)
						&& "points to unallocated memory");
					if (dp_where.start_location == where.start_location
						&& dp.level == 0) {
						dp.level = level;	// 'level' steps from a root
					}
				}
				break;
			}
		}
	}

	inline
	void deferred_heap::collect()
	{
		//	1. reset all the mark bits and in-arena deferred_ptr levels
		//
		for (auto& pg : pages) {
			pg.live_starts.set_all(false);
			for (auto& dp : pg.deferred_ptrs) {
				dp.level = 0;
			}
		}

		//	2. mark all roots + the in-arena deferred_ptrs reachable from them
		//
		std::size_t level = 1;
		for (auto& p : roots) {
			mark(*p, level);	// mark this deferred_ptr root
		}

		bool done = false;
		while (!done) {
			done = true;	// we're done unless we find another to mark
			++level;
			for (auto& pg : pages) {
				for (auto& dp : pg.deferred_ptrs) {
					if (dp.level == level - 1) {
						done = false;
						mark(*(dp.p), level);	// mark this reachable in-arena deferred_ptr
					}
				}
			}
		}

		//	We have now marked every allocation to save, so now
		//	go through and clean up all the unreachable objects

		//	3. reset all unreached deferred_ptrs to null
		//
		//	Note: 'const deferred_ptr' is supported and behaves as const w.r.t. the
		//	the program code; however, a deferred_ptr data member can become
		//	spontaneously null *during object destruction* even if declared
		//	const to the rest of the program. So the collector is an exception
		//	to constness, and the const_cast below is because any deferred_ptr must
		//	be able to be set to null during collection, as part of safely
		//	breaking cycles. (We could declare the data member mutable, but
		//	then we might accidentally modify it in another const function.
		//	Since a const deferred_ptr should only be reset in this one case, it's
		//	more appropriate to avoid mutable and put the const_cast here.)
		//
		//	This is the same "don't touch other objects during finalization
		//	because they may already have been finalized" rule as has evolved
		//	in all cycle-breaking approaches. But, unlike the managed languages, here
		//	the rule is actually directly supported and enforced (one object
		//	being destroyed cannot touch another deferred-cleanup object by
		//	accident because the deferred_ptr to that other object is null), it
		//	removes the need for separate "finalizer" functions (we always run
		//	real destructors, and only have to teach that deferred_ptrs might be null
		//	in a destructor), and it eliminates the possibility of resurrection
		//	(it is not possible for a destructor to make a collectable object
		//	reachable again because we eliminate all pointers to it before any
		//	user-defined destructor gets a chance to run). This is fully
		//	compatible with learnings from existing approaches, but strictly
		//	better in all these respects by directly enforcing those learnings
		//	in the design, thus eliminating large classes of errors while also
		//	minimizing complexity by inventing no new concepts other than
		//	the rule "deferred_ptrs can be null in dtors."
		//
		for (auto& pg : pages) {
			for (auto& dp : pg.deferred_ptrs) {
				if (dp.level == 0) {
					const_cast<deferred_ptr_void*>(dp.p)->reset();
				}
			}
		}

		//	4. deallocate all unreachable allocations, running
		//	destructors if registered
		//
		for (auto& pg : pages) {
			for (auto i = 0; i < pg.page.locations(); ++i) {
				auto start = pg.page.location_info(i);
				if (start.is_start && !pg.live_starts.get(i)) {
					//	this is an allocation to destroy and deallocate

					//	find the end of the allocation
					auto end_i = i + 1;
					auto end = [](auto s) { return s.data() + s.size(); }(pg.page.extent());
					for (; end_i < pg.page.locations(); ++end_i) {
						auto info = pg.page.location_info(end_i);
						if (info.is_start) {
							end = info.pointer;
							break;
						}
					}

					// call the destructors for objects in this range
					destroy_objects({ start.pointer, end });

					// and then deallocate the raw storage
					pg.page.deallocate(start.pointer);
				}
			}
		}

		//	5. finally, drop all now-unused pages
		//
		auto empty = pages.begin();
		while ((empty = std::find_if(pages.begin(), pages.end(),
							[](const auto& pg) { return pg.page.is_empty(); }))
				!= pages.end()) {
			Ensures(empty->deferred_ptrs.empty() && "page with no allocations still has deferred_ptrs");
			pages.erase(empty);
		}
	}

	inline
	void destructors::debug_print() const {
		std::cout << "\n  destructors size() is " << dtors.size() << "\n";
		for (auto& d : dtors) {
			std::cout << "    " << (void*)(d.p) << ", " << (void*)(d.destroy) << "\n";
		}
		std::cout << "\n";
	}

	inline
	void deferred_heap::debug_print() const
	{
		std::cout << "\n*** heap snapshot [" << (void*)this << "] *** "
			<< pages.size() << " page" << (pages.size() != 1 ? "s *" : " **")
			<< "***********************************\n\n";
		for (auto& pg : pages) {
			pg.page.debug_print();
			std::cout << "\n  this page's deferred_ptrs.size() is " << pg.deferred_ptrs.size() << "\n";
			for (auto& dp : pg.deferred_ptrs) {
				std::cout << "    " << (void*)dp.p << " -> " << dp.p->get()
					<< ", level " << dp.level << "\n";
			}
			std::cout << "\n";
		}
		std::cout << "  roots.size() is " << roots.size()
				  << ", load_factor is " << roots.load_factor() << "\n";
		for (auto& p : roots) {
			std::cout << "    " << (void*)p << " -> " << p->get() << "\n";
		}
		dtors.debug_print();
	}

}

#endif

