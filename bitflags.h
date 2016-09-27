
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
#include <iostream>

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
			Expects(nbits > 0 && "#bits must be positive");
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
			auto all_false = [](unit u) { return u == unit(0); };
			return std::all_of(bits.get(), bits.get() + unit_count(size) - 1, all_false)
				&& all_false(*(bits.get() + unit_count(size) - 1) & (bit_mask(size) - 1));
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
			Expects(0 <= from && from <= to && to <= size && "bitflags set() out of range");

			if (from == to) {
				return;
			}

			const auto from_unit = from / bits_per_unit;
			const auto from_mod  = from % bits_per_unit;
			const auto to_unit   = to   / bits_per_unit;
			const auto to_mod    = to   % bits_per_unit;

			auto data = bits.get() + from_unit;

			// first set the remaining bits in the partial unit this range begins within
			if (from_mod != 0) {
				// set all bits less than from in a mask
				auto mask = (unit(1) << from_mod) - 1;
				if (from_unit == to_unit) {
					 // set all bits in mask that are >= to as well
					mask |= ~((unit(1) << to_mod) - 1);
				}

				if (value) {
					*data |= ~mask;
				}
				else {
					*data &= mask;
				}

				if (from_unit == to_unit) {
					return;
				}

				++data;
			}

			// then set whole units (makes a significant performance difference)
			data = std::fill_n(data, bits.get() + to_unit - data, all_bits(value));

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

		void debug_print() {
			for (auto i = 0; i < this->size; ++i) {
				std::cout << (get(i) ? "T" : "f");
				if (i % 8 == 7) { std::cout << ' '; }
				if (i % 64 == 63) { std::cout << '\n'; }
			}
			std::cout << "\n";
		}

		//	Find next flag in positions [from,to) that is set to value
		//	Returns index of next flag that is set to value, or "to" if none was found
		//
		int find_next(int from, int to, bool value) noexcept {
			Expects(0 <= from && from <= to && to <= size && "bitflags find_next() out of range");

			if (from == to) {
				return to;
			}

			const auto from_unit = from / bits_per_unit;
			const auto from_mod = from % bits_per_unit;
			const auto to_unit = to / bits_per_unit;
			const auto to_mod = to   % bits_per_unit;

			auto data = bits.get() + from_unit;

			// first test the remaining bits in the partial unit this range begins within
			if (from_mod != 0) {
				// mask all bits less than from
				auto mask = (unit(1) << from_mod) - 1;
				if (from_unit == to_unit) {
					// mask all bits that are >= to as well
					mask |= ~((unit(1) << to_mod) - 1);
				}
				mask = ~mask;	// now invert the mask to the bits we care about

				if ((value && (*data & mask) != unit(0))		// looking for true and there's one in here
					|| (!value && (~*data & mask) != unit(0))) {	// looking for false and there's one in here
					while (get(from) != value) {
						++from;
					}
					Ensures(from < to && "wait, we should have found the value in the first unit");
					return from;
				}

				if (from_unit == to_unit) {
					return to;
				}

				++data;
			}

			// then test whole units (makes a significant performance difference)
			data = std::find_if(data, bits.get() + to_unit,
				[=](unit u) { return value ? u != unit(0) : u != ~unit(0); });
			if (data != bits.get() + to_unit) {
				from = (data - bits.get()) * bits_per_unit;
				while (get(from) != value) {
					++from;
				}
				Ensures(from < to && "wait, we should have found the value in this unit");
				return from;
			}

			// then test the remaining bits in the partial unit this range ends within
			if (to_mod != 0) {
				// mask all bits less than to
				auto mask = (unit(1) << to_mod) - 1;

				if ((value && (*data & mask) != unit(0))			// looking for true and there's one in here
					|| (!value && (~*data & mask) != unit(0))) {	// looking for false and there's one in here
					from = (data - bits.get()) * bits_per_unit;
					while (get(from) != value) {
						++from;
					}
					Ensures(from < to && "wait, we should have found the value in the last unit");
					return from;
				}
			}

			return to;
		}

	};

	//	Future: Just set(from,to) is a performance improvement over vector<bool>,
	//	but also add find_next_false etc. functions to eliminate some loops

}

#endif
