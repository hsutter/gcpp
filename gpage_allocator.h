
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


#ifndef GALLOC_GPAGE_ALLOCATOR
#define GALLOC_GPAGE_ALLOCATOR

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
