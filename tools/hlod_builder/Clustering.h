#pragma once

#include "Types.h"

#include <vector>

namespace tools::hlod_builder
{
	/// Partitions chunk instances into 20–80 clusters (spatial + by material). M09.4 spec.
	/// \param chunk Chunk with instances.
	/// \param minClusters Minimum clusters per chunk (20).
	/// \param maxClusters Maximum clusters per chunk (80).
	/// \return Vector of clusters; each cluster groups instance indices by material and spatial proximity.
	std::vector<Cluster> ClusterInstances(const ChunkInput& chunk, uint32_t minClusters = 20, uint32_t maxClusters = 80);
}
