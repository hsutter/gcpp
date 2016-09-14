
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

#define _ITERATOR_DEBUG_LEVEL 0

#define GCPP_TOTALLY_ORDERED_COMPARISON(Type) \
bool operator==(const Type& that) const { return compare3(that) == 0; } \
bool operator!=(const Type& that) const { return compare3(that) != 0; } \
bool operator< (const Type& that) const { return compare3(that) <  0; } \
bool operator<=(const Type& that) const { return compare3(that) <= 0; } \
bool operator> (const Type& that) const { return compare3(that) >  0; } \
bool operator>=(const Type& that) const { return compare3(that) >= 0; }

#include <cstdint>

namespace gcpp {

	using byte = std::uint8_t;

}

#endif
