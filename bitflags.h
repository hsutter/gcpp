
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


#ifndef GCPP_BITFLAGS
#define GCPP_BITFLAGS

#include "util.h"

#include <climits>
#include <memory>
#include <algorithm>
#include <type_traits>

namespace gcpp {

	//----------------------------------------------------------------------------
	//
	//	vector<bool> operations aren't always optimized, so here's a custom class.
	//
	//----------------------------------------------------------------------------

	class bitflags {
		using unit = unsigned int;
		static_assert(std::is_unsigned<unit>::value, "unit must be an unsigned integral type.");

		std::unique_ptr<unit[]> bits;
		const int size;

		static constexpr auto bits_per_unit = static_cast<int>(sizeof(unit) * CHAR_BIT);

		//  Return a unit with all bits set if "set" is true, or all bits cleared otherwise.
		//
		static constexpr unit all_bits(bool set) noexcept {
			return set ? ~unit(0) : unit(0);
		}

		//  Return a mask that will select the bit at position from its unit
		//
		static unit bit_mask(int at) noexcept {
			Expects(0 <= at && "position must be non-negative");
			return unit(1) << (at % bits_per_unit);
		}

		//  Return the number of units needed to represent a number of bits
		//
		static int unit_count(int bit_count) noexcept {
			Expects(0 <= bit_count && "bit_count must be non-negative");
			return (bit_count + bits_per_unit - 1) / bits_per_unit;
		}

		//  Get the unit that contains the bit at position
		//
		unit& bit_unit(int at) noexcept {
			Expects(0 <= at && "position must be non-negative");
			return bits[at / bits_per_unit];
		}
		const unit& bit_unit(int at) const noexcept {
			Expects(0 <= at && "position must be non-negative");
			return bits[at / bits_per_unit];
		}

	public:
		bitflags(int nbits, bool value)
			: size{ nbits }
		{
			Expects(nbits >= 0 && "#bits must be non-negative");
			bits = std::make_unique<unit[]>(unit_count(nbits));
			if (value) {
				set_all(true);
			}
		}

		//	Get flag value at position
		//
		bool get(int at) const noexcept {
			Expects(0 <= at && at < size && "bitflags get() out of range");
			return (bit_unit(at) & bit_mask(at)) != unit(0);
		}

		//	Test whether all bits are false
		//
		bool all_false() const noexcept {
			auto ret = std::none_of(bits.get(), bits.get() + unit_count(size), [](unit u) { return u > 0; });
			return ret;
		}

		//	Set flag value at position
		//
		void set(int at, bool value) noexcept {
			Expects(0 <= at && at < size && "bitflags set() out of range");
			if (value) {
				bit_unit(at) |= bit_mask(at);
			}
			else {
				bit_unit(at) &= ~bit_mask(at);
			}
		}

		//	Set all flags to value
		//
		void set_all(bool value) noexcept {
			std::fill_n(bits.get(), unit_count(size), all_bits(value));
		}

		//	Set all flags in positions [from,to) to value
		//
		void set(int from, int to, bool value) noexcept {
			if (from >= to) {
				return;
			}

			Expects(0 <= from && to <= size && "bitflags set() out of range");

			const auto from_unit = from / bits_per_unit;
			const auto from_mod  = from % bits_per_unit;
			const auto to_unit   = to   / bits_per_unit;
			const auto to_mod    = to   % bits_per_unit;

			const auto n_whole_units = to_unit - from_unit - 1;
			auto data = bits.get() + from_unit;

			// first set the remaining bits in the partial unit this range begins within
			if (from_mod != 0) {
				// set all bits less than from in a mask
				auto mask = (unit(1) << from_mod) - 1;
				if (n_whole_units < 0) {
					 // set all bits in mask that are >= to as well
					mask |= ~((unit(1) << to_mod) - 1);
				}

				if (value) {
					*data |= ~mask;
				}
				else {
					*data &= mask;
				}

				if (n_whole_units < 0) {
					return;
				}

				++data;
			}

			// then set whole units (makes a significant performance difference)
			data = std::fill_n(data, n_whole_units, all_bits(value));

			// then set the remaining bits in the partial unit this range ends within
			if (to_mod != 0) {
				// set all bits less than to in a mask
				auto mask = (unit(1) << to_mod) - 1;

				if (value) {
					*data |= mask;
				}
				else {
					*data &= ~mask;
				}
			}
		}
	};

	//	Future: Just set(from,to) is a performance improvement over vector<bool>,
	//	but also add find_next_false etc. functions to eliminate some loops

}

#endif
