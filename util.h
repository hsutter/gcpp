
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


#ifndef GCPP_UTIL
#define GCPP_UTIL

#ifdef _MSC_VER
//	This project is not currently compatible with MSVC's STL's iterator proxies.
#define _ITERATOR_DEBUG_LEVEL 0
#endif

//	This project requires GSL, see: https://github.com/microsoft/gsl
#include "gsl/gsl"
#include <limits>
#include <type_traits>

namespace gcpp {

	using gsl::byte;

}

//	This is the right way to do totally ordered comparisons
//	TODO propose again in ISO (in the language, not as a macro of course)
#define GCPP_TOTALLY_ORDERED_COMPARISON(Type) \
bool operator==(const Type& that) const { return compare3(that) == 0; } \
bool operator!=(const Type& that) const { return compare3(that) != 0; } \
bool operator< (const Type& that) const { return compare3(that) <  0; } \
bool operator<=(const Type& that) const { return compare3(that) <= 0; } \
bool operator> (const Type& that) const { return compare3(that) >  0; } \
bool operator>=(const Type& that) const { return compare3(that) >= 0; }

//  Returns true iff value is within the range of values representable by (arithmetic) type Target
template<class Target, class Value>
constexpr bool in_representable_range(Value const& value) {
	using C = std::common_type_t<Target, Value>;
	return static_cast<C>(value) <= static_cast<C>(std::numeric_limits<Target>::max()) &&
		(!std::is_signed<C>::value || static_cast<C>(std::numeric_limits<Target>::min()) <= static_cast<C>(value)) &&
		(C{} < static_cast<C>(value)) == (Value{} < value);
}

#endif
