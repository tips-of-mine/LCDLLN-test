#pragma once

#include <cstdint>

namespace engine::world
{
	/// Simple .pak format for chunk packages (M10.5): header + entries (offset + size), streamable.
	/// Layout: magic(4) version(4) numEntries(4) [entry0.offset(8) entry0.size(8) ...] then payload data.
	constexpr uint32_t kPakMagic = 0x4348504Bu; // "CHPK"
	constexpr uint32_t kPakVersion = 1u;

	/// One entry in a .pak file: offset and size in bytes (offset from start of file).
	struct PakEntry
	{
		uint64_t offset = 0;
		uint64_t size = 0;
	};

	/// Header size in bytes: magic + version + numEntries (no entry array in header; reader uses fixed entry size).
	constexpr uint32_t kPakHeaderSize = 4u + 4u + 4u;
	/// Size of one entry in the table (offset + size).
	constexpr uint32_t kPakEntrySize = 8u + 8u;
}
