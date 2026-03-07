#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace tools::hlod_builder
{
	/// One mesh instance in a chunk (input from manifest or M11.1).
	struct Instance
	{
		std::string meshPath;
		uint32_t materialId = 0;
		float x = 0.0f, y = 0.0f, z = 0.0f;
	};

	/// Chunk identifier and its instances (input).
	struct ChunkInput
	{
		int32_t chunkX = 0;
		int32_t chunkZ = 0;
		std::vector<Instance> instances;
	};

	/// AABB bounds (min/max).
	struct Bounds
	{
		float minX = 0.0f, minY = 0.0f, minZ = 0.0f;
		float maxX = 0.0f, maxY = 0.0f, maxZ = 0.0f;
	};

	/// One cluster = one merged mesh (output). Material and instance indices into the chunk's instance list.
	struct Cluster
	{
		uint32_t materialId = 0;
		std::vector<size_t> instanceIndices; ///< Indices into ChunkInput::instances
	};
}
