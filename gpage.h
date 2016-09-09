#ifndef GCPP_GPAGE
#define GCPP_GPAGE

#define _ITERATOR_DEBUG_LEVEL 0

#include <vector>
#include <memory>
#include <algorithm>
#include <cassert>
#include <climits>

//#ifndef NDEBUG
#include <iostream>
#include <string>
//#endif

namespace gcpp {

	using byte = std::uint8_t;

	//----------------------------------------------------------------------------
	//
	//	vector<bool> operations aren't always optimized, so here's a custom class.
	//
	//----------------------------------------------------------------------------

	class bitflags {
		const std::size_t size;
		std::vector<byte> bits;

	public:
		bitflags(std::size_t bits, bool value) 
			: size{ bits } 
			, bits( 1 + size / sizeof(byte), value ? 0xFF : 0x00 )
		{
			// set_all(value); 
		}

		bool get(int at) const {
			assert(0 <= at && at < size && "bitflags get() out of range");
			return (bits[at / sizeof(byte)] & (1 << (at % sizeof(byte)))) > 0;
		}

		void set(int at, bool value) {
			assert(0 <= at && at < size && "bitflags set() out of range");
			if (value) {
				bits[at / sizeof(byte)] |= (1 << (at % sizeof(byte)));
			}
			else {
				bits[at / sizeof(byte)] &= (0xff ^ (1 << (at % sizeof(byte))));
			}
		}

		void set_all(bool b) { 
			std::fill(begin(bits), end(bits), b ? 0xff : 0x00); 
		}

		void set(int from, int to, bool value) {
			// first set the remaining bits in the partial byte this range begins within
			while (from < to && from % sizeof(byte) != 0) {
				set(from++, value);
			}

			// then set whole bytes (makes a significant performance difference)
			while (from < to && to - from >= sizeof(byte)) {
				bits[from / sizeof(byte)] = value ? 0xFF : 0x00;
				from += sizeof(byte);
			}

			// then set the remaining bits in the partial byte this range ends within
			while (from < to) {
				set(from++, value);
			}
		}
	};


	//----------------------------------------------------------------------------
	//
	//	gpage - One contiguous allocation
	//
	//  total_size	Total arena size (arena does not grow)
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

		//	Disable copy and move
		//
		gpage(gpage&) = delete;
		void operator=(gpage&) = delete;

	public:
		std::size_t locations() const noexcept { return total_size / min_alloc; }

		//	Construct a page with a given size and chunk size
		//
		gpage(std::size_t total_size_ = 1024, std::size_t min_alloc_ = 4);

		//  Allocate space for num objects of type T
		//
		template<class T>
		T* 
		allocate(std::size_t num = 1) noexcept;

		//  Return whether p points into this page's storage and is allocated.
		//
		template<class T>
		bool contains(T* p) const noexcept;

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
			//std::size_t		  end_location;	// one past the end
		};
		template<class T>
		contains_info_ret 
		contains_info(T* p) const noexcept;

		//  Return whether there is an allocation starting at this location.
		//
		struct location_info_ret {
			bool  is_start;
			byte* pointer;
		};
		location_info_ret
		location_info(std::size_t where) const noexcept;

		//  Deallocate space for object(s) of type T
		//
		template<class T>
		void deallocate(T* p) noexcept;

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
	inline gpage::gpage(std::size_t total_size_, std::size_t min_alloc_)
		//	total_size must be a multiple of min_alloc, so round up if necessary
		: total_size(total_size_ +
			(total_size_ % min_alloc_ > 0
			? min_alloc_ - (total_size_ % min_alloc_)
			: 0))
		, min_alloc(min_alloc_)
		, storage(new byte[total_size])
		, inuse(total_size, false)
		, starts(total_size, false)
	{
		assert(total_size % min_alloc == 0 &&
			"total_size must be a multiple of min_alloc");
	}


	//  Allocate space for num objects of type T
	//
	template<class T>
	T* gpage::allocate(std::size_t num) noexcept {
		const auto bytes_needed = sizeof(T)*num;

		//	optimization: if we know we don't have room, don't even scan
		if (bytes_needed > current_known_request_bound) {
			return nullptr;
		}

		//	check if we need to start at an offset from the beginning of the page
		//	because of alignment requirements, and also whether the request can fit
		void* aligned_start = &storage[0];
		auto  aligned_space = total_size;
		if (std::align(alignof(T), bytes_needed, aligned_start, aligned_space) == nullptr) {
			return nullptr;	// page can't have enough space for this #bytes, after alignment
		}

		//	alignment of location needed by a T
		const auto locations_step = 1 + (alignof(T)-1) / min_alloc;

		//	# contiguous locations needed total (note: array allocations get one 
		//	extra location as a simple way to support one-past-the-end arithmetic)
		const auto locations_needed = (1 + (bytes_needed - 1) / min_alloc) + (num > 1 ? 1 : 0);

		const auto end = locations() - locations_needed;
		//	intentionally omitting "+1" here in order to keep the 
		//	last location valid for one-past-the-end pointing

		//	for each correctly aligned location candidate
		std::size_t i = ((byte*)aligned_start - &storage[0]) / min_alloc;
		assert(i == 0 && "temporary debug check: the current test harness shouldn't have generated something that required a starting offset for alignment reasons");
		for (; i < end; i += locations_step) {
			//	check to see whether we have enough free locations starting here
			std::size_t j = 0;
			//	TODO replace with std::find_if
			for (; j < locations_needed; ++j) {
				// if any location is in use, keep going
				if (inuse.get(i + j)) {
					// optimization: bump i to avoid probing the same location twice
					i += j;
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
			current_known_request_bound = min(current_known_request_bound, bytes_needed - 1);
			return nullptr;
		}

		//	otherwise, allocate it: mark the start and now-used locations...
		starts.set(i, true);							// mark that 'i' begins an allocation
		inuse.set(i, i + locations_needed, true);

		//	optimization: remember that we have this much less memory free
		current_known_request_bound -= min_alloc * locations_needed;

		//	... and return the storage
		return reinterpret_cast<T*>(&storage[i*min_alloc]);
	}


	//  Return whether p points into this page's storage and is allocated.
	//
	template<class T>
	inline
	bool gpage::contains(T* p) const noexcept {
		auto pp = reinterpret_cast<const byte*>(p);
		return (&storage[0] <= pp && pp < &storage[total_size - 1]);
	}

	template<class T>
	gpage::contains_info_ret gpage::contains_info(T* p) const noexcept {
		auto pp = reinterpret_cast<const byte*>(p);
		if (!(&storage[0] <= pp && pp < &storage[total_size - 1])) {
			return{ not_in_range, 0, 0 };
		}

		auto where = (pp - &storage[0]) / min_alloc;
		if (!inuse.get(where)) {
			return{ in_range_unallocated, where, 0 };
		}

		//	find the end of this allocation
		//	TODO replace with find_if, possibly
		//auto end = where + 1;
		//while (end < MAX && !starts.get(end) && inuse.get(end)) {
		//	++end;
		//}

		if (!starts.get(where))	{
			auto start = where;
			//	TODO replace with find_if, possibly
			while (start > 0 && !starts.get(start - 1)) {
				--start;
			}
			assert(start > 0 && "there was no start to this allocation");
			return{ in_range_allocated_middle, where, start - 1 };
		}

		return{ in_range_allocated_start, where, where };
	}


	//  Return whether there is an allocation starting at this location.
	//
	inline gpage::location_info_ret
	gpage::location_info(std::size_t where) const noexcept {
		return{ starts.get(where), &storage[where*min_alloc] };
	}


	//  Deallocate space for object(s) of type T
	//
	template<class T>
	void gpage::deallocate(T* p) noexcept {
		if (p == nullptr) return;

		auto here = (reinterpret_cast<byte*>(p) - &storage[0]) / min_alloc;

		// p had better point to our storage and to the start of an allocation
		// (note: we could also check alignment here but that seems superfluous)
		assert(0 <= here && here < locations() && "attempt to deallocate - out of range");
		assert(starts.get(here) && "attempt to deallocate - not at start of a valid allocation");
		assert(inuse.get(here) && "attempt to deallocate - location is not in use");

		// reset 'starts' to erase the record of the start of this allocation
		starts.set(here, false);

		// scan 'starts' to find the start of the following allocation, if any
		//	TODO replace with find_if
		auto next_start = here + 1;
		for (; next_start < locations(); ++next_start) {
			if (starts.get(next_start))
				break;
		}

		//	optimization: we now have an unallocated gap (the deallocated bytes +
		//	whatever unallocated space followed it before the start of the next
		//	allocation), so remember that
		auto bytes_unallocated_here = (next_start - here) * min_alloc;
		current_known_request_bound =
			std::max(current_known_request_bound, bytes_unallocated_here);

		//	scan 'inuse' to find the end of this allocation
		//		== one past the last location in-use before the next_start
		//	and flip the allocated bits as we go to erase the allocation record
		//	TODO is there a std::algorithm we could use to replace this loop?
		while (here < next_start && inuse.get(here)) {
			inuse.set(here, false);
			++here;
		}
	}


	//	Debugging support
	//
	std::string lowest_hex_digits_of_address(byte* p, int num = 1) {
		assert(0 < num && num < 9 && "number of digits must be 0..8");
		static const char digits[] = "0123456789ABCDEF";

		std::string ret(num, ' ');
		std::size_t val = (std::size_t)p;
		while (num-- > 0) {
			ret[num] = digits[val % 16];
			val >>= 4;
		}
		return ret;
	}

	void gpage::debug_print() const {
		auto base = &storage[0];
		std::cout << "--- total_size " << total_size << " --- min_alloc " << min_alloc
			<< " --- " << (void*)base << " ---------------------------\n     ";

		for (std::size_t i = 0; i < 64; i += 2) {
			std::cout << lowest_hex_digits_of_address(base + i*min_alloc) << ' ';
			if (i % 8 == 6) { std::cout << ' '; }
		}
		std::cout << '\n';

		for (std::size_t i = 0; i < locations(); ++i) {
			if (i % 64 == 0) { std::cout << lowest_hex_digits_of_address(base + i*min_alloc, 4) << ' '; }
			std::cout << (starts.get(i) ? 'A' : inuse.get(i) ? 'a' : '.');
			if (i % 8 == 7) { std::cout << ' '; }
			if (i % 64 == 63) { std::cout << '\n'; }
		}

		std::cout << '\n';
	}

}

#endif
