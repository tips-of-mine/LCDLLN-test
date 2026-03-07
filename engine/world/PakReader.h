#pragma once

#include "engine/world/PakFormat.h"

#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

namespace engine::world
{
	/// Streamable reader for .pak files (M10.5). Opens file, reads header and entry table, supports reading one entry at a time.
	class PakReader
	{
	public:
		PakReader() = default;

		/// Opens a .pak file. Returns true if header and entry table are valid.
		bool Open(const std::string& path);

		/// Closes the file. Safe to call multiple times.
		void Close();

		/// Returns true if a .pak file is open and valid.
		bool IsOpen() const;

		/// Returns number of entries in the pak.
		uint32_t GetNumEntries() const { return m_numEntries; }

		/// Returns offset in file for entry \p index (bytes from start of file).
		uint64_t GetEntryOffset(uint32_t index) const;

		/// Returns size in bytes for entry \p index.
		uint64_t GetEntrySize(uint32_t index) const;

		/// Reads entry \p index into \p outBuffer (must be at least GetEntrySize(index) bytes). Returns bytes read or 0 on failure.
		size_t ReadEntry(uint32_t index, void* outBuffer, size_t bufferSize) const;

		/// Reads entry \p index into a vector. Returns empty vector on failure.
		std::vector<uint8_t> ReadEntry(uint32_t index) const;

	private:
		mutable std::ifstream m_file;
		std::vector<PakEntry> m_entries;
		uint32_t m_numEntries = 0;
		std::string m_path;
	};
}
