
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


#ifndef GCPP_DEFERRED_ALLOCATOR
#define GCPP_DEFERRED_ALLOCATOR

#include "deferred_heap.h"

#include <vector>
#include <list>
#include <set>
#include <map>
#include <unordered_set>
#include <unordered_map>

namespace gcpp {

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
		deferred_heap& h;
	public:

		deferred_heap& heap() const {
			return h;
		}

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

		deferred_allocator(deferred_heap& h_) noexcept 
			: h{ h_ }
		{
		}

		template <class U> 
		deferred_allocator(deferred_allocator<U> const& that) noexcept
			: h{ that.heap() }
		{
		}

		pointer allocate(size_type n) 
		{
			return h.allocate<value_type>(n);
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
			h.construct<U>(p, std::forward<Args>(args)...);
		}

		template <class U>
		void destroy(U* p) noexcept
		{
			h.destroy<U>(p);
		}

		size_type max_size() const noexcept 
		{
			return std::numeric_limits<size_type>::max();
		}

		deferred_allocator select_on_container_copy_construction() const 
		{
			return *this;	// deferred_heap is not copyable
		}

		using propagate_on_container_copy_assignment = std::false_type;
		using propagate_on_container_move_assignment = std::true_type;
		using propagate_on_container_swap            = std::true_type;
		using is_always_equal                        = std::true_type;
	};

	template <class T, class U>
	inline bool operator==(deferred_allocator<T> const& a, deferred_allocator<U> const& b) noexcept 
	{
		return &a.h == &b.h;
	}

	template <class T, class U>
	inline bool operator!=(deferred_allocator<T> const& a, deferred_allocator<U> const& b) noexcept
	{
		return !(a == b);
	}


	//----------------------------------------------------------------------------
	//
	//	Convenience aliases for containers
	//
	//----------------------------------------------------------------------------

	template<class T>
	using deferred_vector = std::vector<T, deferred_allocator<T>>;

	template<class T>
	using deferred_list = std::list<T, deferred_allocator<T>>;

	template<class K, class C = std::less<K>>
	using deferred_set = std::set<K, C, deferred_allocator<K>>;

	template<class K, class T, class C = std::less<K>>
	using deferred_multiset = std::multiset<K, C, deferred_allocator<K>>;

	template<class K, class T, class C = std::less<K>>
	using deferred_map = std::map<K, T, C, deferred_allocator<std::pair<const K, T>>>;

	template<class K, class T, class C = std::less<K>>
	using deferred_multimap = std::multimap<K, T, C, deferred_allocator<std::pair<const K, T>>>;

	template<class K, class H = std::hash<K>, class E = std::equal_to<K>>
	using deferred_unordered_set = std::unordered_set<K, H, E, deferred_allocator<K>>;

	template<class K, class H = std::hash<K>, class E = std::equal_to<K>>
	using deferred_unordered_multiset = std::unordered_multiset<K, H, E, deferred_allocator<K>>;

	template<class K, class T, class H = std::hash<K>, class E = std::equal_to<K>>
	using deferred_unordered_map = std::unordered_map<K, T, H, E, deferred_allocator<std::pair<const K, T>>>;

	template<class K, class T, class H = std::hash<K>, class E = std::equal_to<K>>
	using deferred_unordered_multimap = std::unordered_multimap<K, H, E, deferred_allocator<std::pair<const K, T>>>;

}

#endif
