
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


#ifndef GALLOC_DEFERRED_ALLOCATOR
#define GALLOC_DEFERRED_ALLOCATOR

#include "deferred_heap.h"

namespace galloc {

	//----------------------------------------------------------------------------
	//
	//	deferred_allocator - wrap up global_deferred_heap() as a C++14 allocator, with thanks to
	//			 Howard Hinnant's allocator boilerplate exemplar code, online at
	//           https://howardhinnant.github.io/allocator_boilerplate.html
	//
	//----------------------------------------------------------------------------

	template <class T>
	class deferred_allocator
	{
	public:
		using value_type         = T;
		using pointer            = deferred_ptr<value_type>;
		using const_pointer      = deferred_ptr<const value_type>;
		using void_pointer       = deferred_ptr<void>;
		using const_void_pointer = deferred_ptr<const void>;
		using difference_type    = ptrdiff_t;
		using size_type          = std::size_t;

		template <class U> 
		struct rebind 
		{
			using other = deferred_allocator<U>;
		};

		deferred_allocator() noexcept 
		{
		}

		template <class U> 
		deferred_allocator(deferred_allocator<U> const&) noexcept 
		{
		}

		pointer allocate(size_type n) 
		{
			return global_deferred_heap().allocate<value_type>(n);
		}

		void deallocate(pointer, size_type) noexcept
		{ 
		}

		pointer allocate(size_type n, const_void_pointer) 
		{
			return allocate(n);
		}

		template <class U, class ...Args>
		void construct(U* p, Args&& ...args) 
		{
			global_deferred_heap().construct(p, std::forward<Args>(args)...);
		}

		template <class U>
		void destroy(U* p) noexcept
		{
			global_deferred_heap().destroy(p);
		}

		size_type max_size() const noexcept 
		{
			return std::numeric_limits<size_type>::max();
		}

		deferred_allocator select_on_container_copy_construction() const 
		{
			return *this;	// TODO deferred_heap is currently not copyable
		}

		using propagate_on_container_copy_assignment = std::false_type;
		using propagate_on_container_move_assignment = std::true_type;
		using propagate_on_container_swap            = std::true_type;
		using is_always_equal                        = std::true_type;
	};

	template <class T, class U>
	inline bool operator==(deferred_allocator<T> const&, deferred_allocator<U> const&) noexcept 
	{
		return true;
	}

	template <class T, class U>
	inline bool operator!=(deferred_allocator<T> const& x, deferred_allocator<U> const& y) noexcept
	{
		return !(x == y);
	}

}

#endif
