#pragma once

#ifdef __PROSPERO__

#include "util/types.hpp"

// Memory for RPCS3 in a PS5 title, on libkernel alone. Anonymous mappings draw
// on a title's small flexible budget, so committed memory and shared objects
// are direct memory, mapped into ranges reserved outside RADV's GPU window
// (0x2_0000_0000-0x2_FFFF_FFFF), where a mapping with no address would land.
namespace utils::ps5
{
	// Kernel pages; direct memory is allocated and mapped in 64 KiB units.
	constexpr usz page_size = 0x4000;
	constexpr usz unit_size = 0x10000;

	// Protection bits: CPU read, write and execute.
	enum : int
	{
		prot_read = 0x1,
		prot_write = 0x2,
		prot_exec = 0x4,
	};

	// Address space nothing else is given. At `address` exactly when `fixed`;
	// otherwise at `address` when it is free, or anywhere outside the GPU window.
	void* reserve(usz size, void* address, bool fixed);

	// Gives back the range, with the memory committed in it.
	bool release(void* base, usz size);

	// Backs [address, address + size) with zeroed direct memory where it has
	// none, and sets its protection. Memory already backed keeps its contents.
	bool commit(void* address, usz size, int prot);

	// Gives back the units wholly inside the range, reserved again with no
	// access, and zeroes what it covers of the units at its ends.
	bool decommit(void* address, usz size);

	bool protect(void* address, usz size, int prot);

	// One zeroed direct-memory object, mapped at any number of addresses.
	struct object
	{
		s64 start = -1;
		usz size = 0;
	};

	bool object_create(usz size, object& result);
	void object_destroy(object& obj);

	// A view of the whole object: exactly at `address` when it is not null,
	// replacing what is there, otherwise in a range of its own.
	void* object_map(const object& obj, void* address, int prot);

	// Unmaps a view; `keep_reserved` leaves its range reserved.
	bool object_unmap(void* view, usz size, bool keep_reserved);
}

#endif
