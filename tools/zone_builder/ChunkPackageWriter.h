#pragma once

#include "LayoutImporter.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace tools::zone_builder
{
	/// Writes chunk package (M10.5): chunk.meta + geo.pak, tex.pak, instances.bin, navmesh.bin, probes.bin.
	/// \param outputDir Directory for the chunk (e.g. "build/zone_0/chunks/chunk_0_0").
	/// \param chunkX Chunk X coordinate.
	/// \param chunkZ Chunk Z coordinate.
	/// \return true on success.
	bool WriteChunkPackage(const std::string& outputDir, int32_t chunkX, int32_t chunkZ);

	/// Builds one zone output tree from a layout by chunking instances with floor(x/256), floor(z/256).
	/// Writes `zone.meta`, `probes.bin`, `atmosphere.json`, then `chunks/chunk_i_j/chunk.meta` and `instances.bin` under `outputRootDir`.
	/// `assetId` values written to `instances.bin` are deterministic hashes of the relative glTF path.
	/// \param outputRootDir Root directory for the zone output (e.g. "build/zone_0").
	/// \param layout Loaded layout document to split into chunks.
	/// \param outError Receives a human-readable error on failure.
	/// \return true on success.
	bool WriteChunkedZoneOutputs(std::string_view outputRootDir, const LayoutDocument& layout, std::string& outError);
}
