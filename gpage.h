
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


#ifndef GCPP_GPAGE
#define GCPP_GPAGE

#include "bitflags.h"
#include "util.h"

#include <vector>
#include <algorithm>
#include <memory>

//#ifndef NDEBUG
#include <iostream>
#include <string>
//#endif

namespace gcpp {

	//----------------------------------------------------------------------------
	//
	//	gpage - One contiguous allocation
	//
	//  total_size	Total page size (page does not grow)
	//  min_alloc	Minimum allocation size in bytes
	//
	//	storage		Underlying storage bytes
	//  inuse		Tracks whether location is in use: false = unused, true = used
	//  starts		Tracks whether location starts an allocation: false = no, true = yes
	//
	//	current_known_request_bound		Cached hint about largest current hole
	//
	//----------------------------------------------------------------------------

	class gpage {
	private:
		const std::size_t				total_size;
		const std::size_t				min_alloc;
		const std::unique_ptr<byte[]>	storage;
		bitflags						inuse;
		bitflags						starts;
		std::size_t						current_known_request_bound = total_size;

		//	Copy and move are disabled by const unique_ptr member, but let's be explicit
		//
		gpage(gpage&) = delete;
		void operator=(gpage&) = delete;

	public:
		int locations() const noexcept { return gsl::narrow_cast<int>(total_size / min_alloc); }

		gsl::span<const byte> extent() const noexcept {
			return { storage.get(), gsl::narrow_cast<std::ptrdiff_t>(total_size) };
		}
		gsl::span<byte> extent() noexcept {
			return { storage.get(), gsl::narrow_cast<std::ptrdiff_t>(total_size) };
		}

		bool is_empty() const noexcept {
			auto ret = inuse.all_false();
			Ensures((!ret || starts.all_false()) && "gpage with no inuse still has starts");
			return ret;
		}

		//	Construct a page with a given size and chunk size
		//
		gpage(std::size_t total_size_ = 1024, std::size_t min_alloc_ = 4);

		//  Allocate space for n objects of type T
		//
		template<class T>
		byte* allocate(int n = 1) noexcept;

		//  Return whether p points into this page's storage and is allocated.
		//
		bool contains(gsl::not_null<const byte*> p) const noexcept;

		enum gpage_find_result {
			not_in_range = 0,
			in_range_unallocated,
			in_range_allocated_middle,
			in_range_allocated_start
		};
		struct contains_info_ret {
			gpage_find_result found;
			std::size_t		  location;
			std::size_t		  start_location;
		};
		contains_info_ret
		contains_info(gsl::not_null<const byte*> p) const noexcept;

		//  Return whether there is an allocation starting at this location.
		//
		struct location_info_ret {
			bool  is_start;
			byte* pointer;
		};
		location_info_ret
		location_info(int where) const noexcept;

		//  Deallocate the allocation that starts at *p.
		//	Note: p must be a pointer previously returned by allocate().
		//
		void deallocate(gsl::not_null<byte*> p) noexcept;

		//	Debugging support
		//
		void debug_print() const;
	};


	//----------------------------------------------------------------------------
	//
	//	gpage function implementations
	//
	//----------------------------------------------------------------------------
	//

	//	Construct a page with a given size and chunk size
	//
	inline
	gpage::gpage(std::size_t total_size_, std::size_t min_alloc_)
		//	total_size must be a multiple of min_alloc, so round up if necessary
		: total_size(total_size_ +
			(total_size_ % min_alloc_ > 0
			? min_alloc_ - (total_size_ % min_alloc_)
			: 0))
		, min_alloc(min_alloc_)
		, storage(std::make_unique<byte[]>(total_size))
		, inuse(locations(), false)
		, starts(locations(), false)
	{
		Expects(total_size % min_alloc == 0 &&
			"total_size must be a multiple of min_alloc");
		Expects(in_representable_range<int>(total_size / min_alloc) &&
			"total_size / min_alloc must be representable by int");
		Expects(in_representable_range<std::ptrdiff_t>(total_size) &&
			"total_size must be representable by ptrdiff_t");
	}


	//  Allocate space for n objects of type T
	//
	template<class T>
	byte* gpage::allocate(int n) noexcept {
		Expects(n > 0 && "cannot request an empty allocation");
		Expects(static_cast<std::size_t>(n) <=
			std::numeric_limits<std::size_t>::max() / sizeof(T) &&
			"sizeof(T)*n must be representable by std::size_t");

		const auto bytes_needed = sizeof(T)*n;

		//	optimization: if we know we don't have room, don't even scan
		if (bytes_needed > current_known_request_bound) {
			return nullptr;
		}

		//	check if we need to start at an offset from the beginning of the page
		//	because of alignment requirements, and also whether the request can fit
		void* aligned_start = storage.get();
		auto  aligned_space = total_size;
		if (std::align(alignof(T), bytes_needed, aligned_start, aligned_space) == nullptr) {
			return nullptr;	// page can't have enough space for this #bytes, after alignment
		}

		//	alignment of location needed by a T
		const auto locations_step = 1 + (alignof(T)-1) / min_alloc;

		//	# contiguous locations needed total
		//	note: as a simplification, for now we just add an extra location to every
		//	      allocation as a simple way to support one-past-the-end arithmetic
		const auto locations_needed = (1 + (bytes_needed - 1) / min_alloc) + 1;

		const auto end = locations() - locations_needed;
		//	intentionally omitting "+1" here in order to keep the
		//	last location valid for one-past-the-end pointing

		//	for each correctly aligned location candidate
		std::size_t i = ((byte*)aligned_start - storage.get()) / min_alloc;
		Expects(i == 0 && "temporary debug check: the current test harness shouldn't have generated something that required a starting offset for alignment reasons");
		for (; i < end; i += locations_step) {
			//	check to see whether we have enough free locations starting here
			std::size_t j = 0;
			//	Future: replace this loop with a function call
			for (; j < locations_needed; ++j) {
				// if any location is in use, keep going
				if (inuse.get(i + j)) {
					// optimization: bump i to avoid probing the same location twice
					i += j - j % locations_step;
					break;
				}
			}

			// if we have enough free locations, break the outer loop
			if (j == locations_needed)
				break;
		}

		//	if we didn't find anything, return null
		if (i >= end) {
			//	optimization: remember that we couldn't satisfy this request size
			current_known_request_bound = std::min(current_known_request_bound, bytes_needed - 1);
			return nullptr;
		}

		//	otherwise, allocate it: mark the start and now-used locations...
		starts.set(i, true);							// mark that 'i' begins an allocation
		inuse.set(i, i + locations_needed, true);

		//	optimization: remember that we have this much less memory free
		current_known_request_bound -= min_alloc * locations_needed;

		//	... and return the storage
		return &storage[i*min_alloc];
	}


	//  Return whether p points into this page's storage and is allocated.
	//
	inline
	bool gpage::contains(gsl::not_null<const byte*> p) const noexcept {
		//  Use std::less<> to compare (possibly unrelated) pointers portably
		auto const cmp = std::less<>{};
		auto const ext = extent();
		return !cmp(p, ext.data()) && cmp(p, ext.data() + ext.size());
	}

	inline
	gpage::contains_info_ret gpage::contains_info(gsl::not_null<const byte*> p) const noexcept {
		if (!contains(p)) {
			return{ not_in_range, 0, 0 };
		}

		auto where = (p - storage.get()) / min_alloc;
		if (!inuse.get(where)) {
			return{ in_range_unallocated, where, 0 };
		}

		if (!starts.get(where))	{
			auto start = where;
			//	Future: replace this loop with a function call
			while (start > 0 && !starts.get(start - 1)) {
				--start;
			}
			Expects(start > 0 && "there was no start to this allocation");
			return{ in_range_allocated_middle, where, start - 1 };
		}

		return{ in_range_allocated_start, where, where };
	}


	//  Return whether there is an allocation starting at this location.
	//
	inline
	gpage::location_info_ret
	gpage::location_info(int where) const noexcept {
		return{ starts.get(where), &storage[where*min_alloc] };
	}


	//  Deallocate space for object(s) of type T
	//
	inline
	void gpage::deallocate(gsl::not_null<byte*> p) noexcept {
		// p had better point to our storage ...
		Expects(contains(p) && "attempt to deallocate - out of range");

		auto here = gsl::narrow_cast<int>((p - storage.get()) / min_alloc);

		// ... and to the start of an allocation
		// (note: we could also check alignment here but that seems superfluous)
		Expects(starts.get(here) && "attempt to deallocate - not at start of a valid allocation");
		Expects(inuse.get(here) && "attempt to deallocate - location is not in use");

		// reset 'starts' to erase the record of the start of this allocation
		starts.set(here, false);

		// scan 'starts' to find the start of the following allocation, if any
		//	TODO replace this loop with a function call
		auto next_start = here + 1;
		for (; next_start < locations(); ++next_start) {
			if (starts.get(next_start))
				break;
		}

		//	optimization: spill the cached bound (we could also scan backwards to
		//	find the end of the previous allocation to see exactly how big this
		//	new hole is, and update the cached bound if the hole is bigger, but
		//	that would be incurring extra work)
		current_known_request_bound = total_size;

		//	scan 'inuse' to find the end of this allocation
		//		== one past the last location in-use before the next_start
		//	and flip the allocated bits as we go to erase the allocation record
		//	TODO replace this loop with a function call
		while (here < next_start && inuse.get(here)) {
			inuse.set(here, false);
			++here;
		}
	}


	//	Debugging support
	//
	inline
	std::string lowest_hex_digits_of_address(byte* p, int num = 1) {
		Expects(0 < num && num < 9 && "number of digits must be 0..8");
		static const char digits[] = "0123456789ABCDEF";

		std::string ret(num, ' ');
		auto val = reinterpret_cast<std::uintptr_t>(p);
		while (num-- > 0) {
			ret[num] = digits[val % 16];
			val >>= 4;
		}
		return ret;
	}

	inline
	void gpage::debug_print() const {
		auto base = storage.get();
		std::cout << "--- total_size " << total_size << " --- min_alloc " << min_alloc
			<< " --- " << (void*)base << " ---------------------------\n     ";

		for (auto i = 0; i < 64; i += 2) {
			std::cout << lowest_hex_digits_of_address(base + i*min_alloc,2)[0] << ' ';
			if (i % 8 == 6) { std::cout << ' '; }
		}
		std::cout << "\n     ";
		for (auto i = 0; i < 64; i += 2) {
			std::cout << lowest_hex_digits_of_address(base + i*min_alloc) << ' ';
			if (i % 8 == 6) { std::cout << ' '; }
		}
		std::cout << '\n';

		for (auto i = 0; i < locations(); ++i) {
			if (i % 64 == 0) { std::cout << lowest_hex_digits_of_address(base + i*min_alloc, 4) << ' '; }
			std::cout << (starts.get(i) ? 'A' : inuse.get(i) ? 'a' : '.');
			if (i % 8 == 7) {
				if (i % 64 == 63) { std::cout << '\n'; }
				else { std::cout << ' '; }
			}
		}

		std::cout << '\n';
	}

}

#endif
