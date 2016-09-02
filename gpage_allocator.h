#ifndef GCPP_GCPP
#define GCPP_GCPP

#include "gpage.h"

namespace gcpp {

	//----------------------------------------------------------------------------
	//
	//	gpage_allocator - wrap a gpage as a C++14 allocator, with thanks to
	//			 Howard Hinnant's allocator boilerplate exemplar code, online at
	//           https://howardhinnant.github.io/allocator_boilerplate.html
	//
	//----------------------------------------------------------------------------

	static gpage page;

	template <class T>
	class gpage_allocator {

	public:
		using value_type = T;

		gpage_allocator() noexcept
		{
		}

		template <class U> 
		gpage_allocator(gpage_allocator<U> const&) noexcept
		{
		}

		value_type* allocate(std::size_t n) noexcept 
		{
			return page.allocate<T>(n);
		}

		void deallocate(value_type* p, std::size_t) noexcept 
		{
			return page.deallocate(p);
		}
	};

	template <class T, class U>
	bool operator==(gpage_allocator<T> const&, gpage_allocator<U> const&) noexcept 
	{ 
		return true; 
	}

	template <class T, class U>
	bool operator!=(gpage_allocator<T> const& x, gpage_allocator<U> const& y) noexcept 
	{ 
		return !(x == y); 
	}

}

#endif
