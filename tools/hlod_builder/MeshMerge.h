#pragma once

#include "MeshLoader.h"
#include "Types.h"

#include <vector>

namespace tools::hlod_builder
{
	/// One merged mesh (HLOD cluster result): vertex/index data and bounds.
	struct MergedMesh
	{
		MeshData data;
		Bounds bounds;
		uint32_t materialId = 0;
	};

	/// Merges meshes for the given cluster: loads each instance mesh, applies position offset, concatenates, recalculates bounds.
	/// \param contentDir Content root for mesh paths.
	/// \param chunk Chunk containing instances.
	/// \param cluster Cluster (instance indices into chunk.instances).
	/// \return Merged mesh and AABB, or empty on failure.
	MergedMesh MergeClusterMeshes(const std::string& contentDir, const ChunkInput& chunk, const Cluster& cluster);
}
