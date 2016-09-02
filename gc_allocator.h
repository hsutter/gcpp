#ifndef GCALLOC_GCPP
#define GCALLOC_GCPP

#include "gc.h"

namespace gcpp {

	//----------------------------------------------------------------------------
	//
	//	gc_allocator - wrap up gc() as a C++14 allocator, with thanks to
	//			 Howard Hinnant's allocator boilerplate exemplar code, online at
	//           https://howardhinnant.github.io/allocator_boilerplate.html
	//
	//----------------------------------------------------------------------------

	template <class T>
	class gc_allocator
	{
	public:
		using value_type         = T;
		using pointer            = gc_ptr<value_type>;
		using const_pointer      = gc_ptr<const value_type>;
		using void_pointer       = gc_ptr<void>;
		using const_void_pointer = gc_ptr<const void>;
		using difference_type    = ptrdiff_t;
		using size_type          = std::size_t;

		template <class U> 
		struct rebind 
		{
			using other = gc_allocator<U>;
		};

		gc_allocator() noexcept 
		{
		}

		template <class U> 
		gc_allocator(gc_allocator<U> const&) noexcept 
		{
		}

		pointer allocate(size_type n) 
		{
			return gc().allocate<value_type>(n);
		}

		void deallocate(pointer, size_type) noexcept
		{ 
		}

		pointer allocate(size_type n, const_void_pointer) 
		{
			return allocate(n);
		}

		template <class U, class ...Args>
		void construct(const gc_ptr<U>& p, Args&& ...args) 
		{
			gc().construct(p, std::forward<Args>(args)...);
		}

		template <class U>
		void destroy(const gc_ptr<U>& p) noexcept 
		{
			gc().destroy(p);
		}

		size_type max_size() const noexcept 
		{
			return std::numeric_limits<size_type>::max();
		}

		gc_allocator select_on_container_copy_construction() const 
		{
			return *this;	// TODO gc_heap is currently not copyable
		}

		using propagate_on_container_copy_assignment = std::false_type;
		using propagate_on_container_move_assignment = std::true_type;
		using propagate_on_container_swap            = std::true_type;
		using is_always_equal                        = std::true_type;
	};

	template <class T, class U>
	inline bool operator==(gc_allocator<T> const&, gc_allocator<U> const&) noexcept 
	{
		return true;
	}

	template <class T, class U>
	inline bool operator!=(gc_allocator<T> const& x, gc_allocator<U> const& y) noexcept
	{
		return !(x == y);
	}

}

#endif
