#ifdef __PROSPERO__

#include "util/ps5_memory.h"

#include "util/asm.hpp"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <map>
#include <pthread.h>

extern "C"
{
	s32 sceKernelAllocateDirectMemory(s64 search_start, s64 search_end, usz length, usz alignment, int memory_type, s64* physical_start);
	s32 sceKernelReleaseDirectMemory(s64 start, usz length);
	s32 sceKernelMapDirectMemory(void** address, usz length, int protection, int flags, s64 direct_start, usz alignment);
	s64 sceKernelGetDirectMemorySize();
	s32 sceKernelMprotect(const void* address, usz length, int protection);
	s32 sceKernelReserveVirtualRange(void** address, usz length, int flags, usz alignment);
	s32 sceKernelMunmap(void* address, usz length);
}

namespace utils::ps5
{
	namespace
	{
		constexpr int map_fixed = 0x10;
		// The direct-memory type of a title's CPU-only memory.
		constexpr int direct_type_cpu = 12;

		constexpr u64 gpu_window_begin = 0x2'0000'0000;
		constexpr u64 gpu_window_end = 0x3'0000'0000;

		// Ranges asked for with no address go from here up: the kernel grants
		// a range at its hint from 0x10_0000_0000 to 0x80_0000_0000, and guest
		// memory takes the start of that.
		constexpr u64 anywhere_begin = 0x40'0000'0000;
		constexpr u64 anywhere_end = 0x80'0000'0000;

		// Statically initialized and never destroyed: RPCS3's static initializers
		// commit memory, and can run before this file's.
		pthread_mutex_t g_mutex = PTHREAD_MUTEX_INITIALIZER;

		// The direct memory behind each committed unit, by the unit's address.
		std::map<u64, s64>& backed()
		{
			static auto& s_backed = *new std::map<u64, s64>;
			return s_backed;
		}

		struct lock_guard
		{
			lock_guard() { pthread_mutex_lock(&g_mutex); }
			~lock_guard() { pthread_mutex_unlock(&g_mutex); }
		};

		u64 floor_page(u64 address)
		{
			return address & ~(page_size - 1);
		}

		u64 ceil_page(u64 address)
		{
			return utils::align<u64>(address, page_size);
		}

		bool in_gpu_window(u64 base, usz size)
		{
			return base < gpu_window_end && base + size > gpu_window_begin;
		}

		// Exactly at `address`: the kernel moves a hint whose space is taken,
		// so a range placed elsewhere is given back.
		void* reserve_at(void* address, usz size)
		{
			void* base = address;
			if (sceKernelReserveVirtualRange(&base, size, 0, unit_size) != 0)
			{
				return nullptr;
			}

			if (base != address)
			{
				sceKernelMunmap(base, size);
				return nullptr;
			}

			return base;
		}

		bool allocate_direct(usz size, s64& start)
		{
			return sceKernelAllocateDirectMemory(0, sceKernelGetDirectMemorySize(), size, unit_size, direct_type_cpu, &start) == 0;
		}

		bool map_direct(void* address, usz size, s64 start)
		{
			// Execute asked for at map time is refused, so a view is mapped
			// read-write and given its protection after.
			void* at = address;
			return sceKernelMapDirectMemory(&at, size, prot_read | prot_write, map_fixed, start, unit_size) == 0 && at == address;
		}
	}

	void* reserve(usz size, void* address, bool fixed)
	{
		size = utils::align<usz>(size, unit_size);

		if (fixed)
		{
			if (!address || reinterpret_cast<u64>(address) % unit_size)
			{
				return nullptr;
			}

			return reserve_at(address, size);
		}

		if (address)
		{
			void* base = address;
			if (sceKernelReserveVirtualRange(&base, size, 0, unit_size) == 0)
			{
				if (!in_gpu_window(reinterpret_cast<u64>(base), size))
				{
					return base;
				}

				sceKernelMunmap(base, size);
			}
		}

		// A hint bumped through the range, wrapping: compilers' ranges are
		// given back when they end.
		static std::atomic<u64> s_next_hint{anywhere_begin};
		const u64 step = utils::align<u64>(size, 0x1000'0000);

		for (u32 attempt = 0; attempt < 64; attempt++)
		{
			const u64 hint = s_next_hint.fetch_add(step);

			if (hint + size > anywhere_end)
			{
				u64 expected = hint + step;
				s_next_hint.compare_exchange_strong(expected, anywhere_begin);
				continue;
			}

			void* base = reinterpret_cast<void*>(hint);
			if (sceKernelReserveVirtualRange(&base, size, 0, unit_size) == 0)
			{
				if (!in_gpu_window(reinterpret_cast<u64>(base), size))
				{
					return base;
				}

				sceKernelMunmap(base, size);
			}
		}

		return nullptr;
	}

	bool release(void* base, usz size)
	{
		size = utils::align<usz>(size, unit_size);
		const u64 begin = reinterpret_cast<u64>(base);

		lock_guard lock;

		if (sceKernelMunmap(base, size) != 0)
		{
			return false;
		}

		for (auto it = backed().lower_bound(begin); it != backed().end() && it->first < begin + size;)
		{
			sceKernelReleaseDirectMemory(it->second, unit_size);
			it = backed().erase(it);
		}

		return true;
	}

	bool commit(void* address, usz size, int prot)
	{
		if (!size)
		{
			return true;
		}

		const u64 begin = reinterpret_cast<u64>(address);
		const u64 end = begin + size;

		lock_guard lock;

		for (u64 unit = begin & ~(unit_size - 1); unit < end; unit += unit_size)
		{
			if (backed().contains(unit))
			{
				continue;
			}

			// The kernel gives released direct memory out again as it was.
			s64 start = -1;
			if (!allocate_direct(unit_size, start))
			{
				return false;
			}

			if (!map_direct(reinterpret_cast<void*>(unit), unit_size, start))
			{
				sceKernelReleaseDirectMemory(start, unit_size);
				return false;
			}

			std::memset(reinterpret_cast<void*>(unit), 0, unit_size);
			backed().emplace(unit, start);

			// What the unit backs beyond the range stays inaccessible, as reserved space.
			sceKernelMprotect(reinterpret_cast<void*>(unit), unit_size, 0);
		}

		return sceKernelMprotect(reinterpret_cast<void*>(floor_page(begin)), ceil_page(end) - floor_page(begin), prot) == 0;
	}

	bool decommit(void* address, usz size)
	{
		if (!size)
		{
			return true;
		}

		const u64 begin = reinterpret_cast<u64>(address);
		const u64 end = begin + size;

		lock_guard lock;

		for (u64 unit = begin & ~(unit_size - 1); unit < end; unit += unit_size)
		{
			const auto found = backed().find(unit);

			if (found == backed().end())
			{
				continue;
			}

			if (unit >= begin && unit + unit_size <= end)
			{
				sceKernelMunmap(reinterpret_cast<void*>(unit), unit_size);
				sceKernelReleaseDirectMemory(found->second, unit_size);
				backed().erase(found);

				if (!reserve_at(reinterpret_cast<void*>(unit), unit_size))
				{
					return false;
				}

				continue;
			}

			// Part of a unit: zeroed, and its whole pages inside the range made inaccessible.
			const u64 from = std::max(begin, unit);
			const u64 to = std::min(end, unit + unit_size);
			sceKernelMprotect(reinterpret_cast<void*>(floor_page(from)), ceil_page(to) - floor_page(from), prot_read | prot_write);
			std::memset(reinterpret_cast<void*>(from), 0, to - from);

			if (ceil_page(from) < floor_page(to))
			{
				sceKernelMprotect(reinterpret_cast<void*>(ceil_page(from)), floor_page(to) - ceil_page(from), 0);
			}
		}

		return true;
	}

	bool protect(void* address, usz size, int prot)
	{
		const u64 begin = reinterpret_cast<u64>(address);
		return sceKernelMprotect(reinterpret_cast<void*>(floor_page(begin)), ceil_page(begin + size) - floor_page(begin), prot) == 0;
	}

	bool object_create(usz size, object& result)
	{
		size = utils::align<usz>(size, unit_size);
		s64 start = -1;

		if (!allocate_direct(size, start))
		{
			return false;
		}

		// Zeroed through a view of its own, placed outside the GPU window.
		void* view = reserve(size, nullptr, false);

		if (!view || !map_direct(view, size, start))
		{
			if (view)
			{
				sceKernelMunmap(view, size);
			}

			sceKernelReleaseDirectMemory(start, size);
			return false;
		}

		std::memset(view, 0, size);
		sceKernelMunmap(view, size);
		result = {start, size};
		return true;
	}

	void object_destroy(object& obj)
	{
		if (obj.start >= 0)
		{
			sceKernelReleaseDirectMemory(obj.start, obj.size);
		}

		obj = {};
	}

	void* object_map(const object& obj, void* address, int prot)
	{
		void* view = address ? address : reserve(obj.size, nullptr, false);

		if (!view)
		{
			return nullptr;
		}

		if (!map_direct(view, obj.size, obj.start))
		{
			if (!address)
			{
				sceKernelMunmap(view, obj.size);
			}

			return nullptr;
		}

		if (prot != (prot_read | prot_write) && sceKernelMprotect(view, obj.size, prot) != 0)
		{
			sceKernelMunmap(view, obj.size);
			return nullptr;
		}

		return view;
	}

	bool object_unmap(void* view, usz size, bool keep_reserved)
	{
		size = utils::align<usz>(size, unit_size);

		if (sceKernelMunmap(view, size) != 0)
		{
			return false;
		}

		return !keep_reserved || reserve_at(view, size);
	}
}

#endif
