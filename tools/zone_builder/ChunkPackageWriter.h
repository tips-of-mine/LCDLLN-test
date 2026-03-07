#pragma once

#include <cstdint>
#include <string>

namespace tools::zone_builder
{
	/// Writes chunk package (M10.5): chunk.meta + geo.pak, tex.pak, instances.bin, navmesh.bin, probes.bin.
	/// \param outputDir Directory for the chunk (e.g. "build/zone_0/chunks/chunk_0_0").
	/// \param chunkX Chunk X coordinate.
	/// \param chunkZ Chunk Z coordinate.
	/// \return true on success.
	bool WriteChunkPackage(const std::string& outputDir, int32_t chunkX, int32_t chunkZ);
}
