#include "Clustering.h"
#include "MeshLoader.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <vector>

namespace tools::hlod_builder
{
	namespace
	{
		/// Spatial cell key for grid hashing (fixed resolution).
		struct CellKey
		{
			int32_t gx = 0, gy = 0, gz = 0;
			bool operator==(const CellKey& o) const { return gx == o.gx && gy == o.gy && gz == o.gz; }
		};
		struct CellKeyHash
		{
			size_t operator()(const CellKey& k) const
			{
				return static_cast<size_t>(k.gx) * 73856093u
					^ static_cast<size_t>(k.gy) * 19349663u
					^ static_cast<size_t>(k.gz) * 83492791u;
			}
		};

		constexpr float kCellSize = 32.0f; ///< Meters per grid cell for spatial clustering.
	}

	std::vector<Cluster> ClusterInstances(const ChunkInput& chunk, uint32_t minClusters, uint32_t maxClusters)
	{
		std::vector<Cluster> out;
		if (chunk.instances.empty())
			return out;

		// Group by (materialId, spatial cell). Cell = floor(pos / kCellSize).
		std::unordered_map<uint32_t, std::unordered_map<CellKey, std::vector<size_t>, CellKeyHash>> materialCellToIndices;
		for (size_t i = 0; i < chunk.instances.size(); ++i)
		{
			const Instance& inst = chunk.instances[i];
			CellKey c;
			c.gx = static_cast<int32_t>(std::floor(inst.x / kCellSize));
			c.gy = static_cast<int32_t>(std::floor(inst.y / kCellSize));
			c.gz = static_cast<int32_t>(std::floor(inst.z / kCellSize));
			materialCellToIndices[inst.materialId][c].push_back(i);
		}

		// Flatten to one cluster per (material, cell).
		for (auto& [matId, cellMap] : materialCellToIndices)
			for (auto& [cell, indices] : cellMap)
			{
				Cluster cl;
				cl.materialId = matId;
				cl.instanceIndices = std::move(indices);
				out.push_back(std::move(cl));
			}

		// Enforce 20–80 clusters: merge or split conceptually. Here we only merge if > maxClusters.
		while (out.size() > maxClusters)
		{
			// Merge two smallest clusters (by instance count) that share same material.
			std::sort(out.begin(), out.end(), [](const Cluster& a, const Cluster& b) {
				return a.instanceIndices.size() < b.instanceIndices.size();
			});
			bool merged = false;
			for (size_t i = 0; i < out.size() && !merged; ++i)
				for (size_t j = i + 1; j < out.size() && !merged; ++j)
					if (out[i].materialId == out[j].materialId)
					{
						for (size_t idx : out[j].instanceIndices)
							out[i].instanceIndices.push_back(idx);
						out.erase(out.begin() + static_cast<std::ptrdiff_t>(j));
						merged = true;
					}
			if (!merged)
				break;
		}

		// If we have too few clusters, split largest ones (by instance count) until we reach minClusters or can't split.
		while (out.size() < minClusters)
		{
			auto it = std::max_element(out.begin(), out.end(), [](const Cluster& a, const Cluster& b) {
				return a.instanceIndices.size() < b.instanceIndices.size();
			});
			if (it == out.end() || it->instanceIndices.size() < 2)
				break;
			Cluster half;
			half.materialId = it->materialId;
			const size_t n = it->instanceIndices.size();
			half.instanceIndices.assign(it->instanceIndices.begin() + n / 2, it->instanceIndices.end());
			it->instanceIndices.resize(n / 2);
			out.push_back(std::move(half));
		}

		return out;
	}
}
