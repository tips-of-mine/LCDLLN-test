#pragma once

#include "MeshMerge.h"
#include "Types.h"

#include <string>
#include <vector>

namespace tools::hlod_builder
{
	/// Chunk output: chunk coords + merged meshes (one per cluster).
	struct ChunkOutput
	{
		int32_t chunkX = 0;
		int32_t chunkZ = 0;
		std::vector<MergedMesh> clusters;
	};

	/// Writes hlod.pak (mesh + materials list). No texture atlas (M09.4: start without atlas).
	/// \param path Output file path (e.g. "hlod.pak").
	/// \param chunks Per-chunk merged clusters.
	/// \param materialNames Optional names per material id (id -> name); can be empty.
	/// \return true on success.
	bool WriteHlodPak(const std::string& path, const std::vector<ChunkOutput>& chunks,
	                  const std::vector<std::string>& materialNames = {});
}
