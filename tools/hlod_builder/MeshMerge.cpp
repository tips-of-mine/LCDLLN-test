#include "MeshMerge.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace tools::hlod_builder
{
	namespace
	{
		void ExpandBounds(Bounds& b, float px, float py, float pz)
		{
			b.minX = std::min(b.minX, px);
			b.minY = std::min(b.minY, py);
			b.minZ = std::min(b.minZ, pz);
			b.maxX = std::max(b.maxX, px);
			b.maxY = std::max(b.maxY, py);
			b.maxZ = std::max(b.maxZ, pz);
		}
	}

	MergedMesh MergeClusterMeshes(const std::string& contentDir, const ChunkInput& chunk, const Cluster& cluster)
	{
		MergedMesh out;
		out.materialId = cluster.materialId;
		out.bounds.minX = out.bounds.minY = out.bounds.minZ = std::numeric_limits<float>::max();
		out.bounds.maxX = out.bounds.maxY = out.bounds.maxZ = -std::numeric_limits<float>::max();

		for (size_t instIdx : cluster.instanceIndices)
		{
			if (instIdx >= chunk.instances.size())
				continue;
			const Instance& inst = chunk.instances[instIdx];
			MeshData mesh = LoadMesh(contentDir, inst.meshPath);
			if (mesh.vertices.empty() || mesh.indices.empty())
				continue;

			const uint32_t indexOffset = static_cast<uint32_t>(out.data.vertices.size());
			for (const Vertex& v : mesh.vertices)
			{
				Vertex w;
				w.px = v.px + inst.x;
				w.py = v.py + inst.y;
				w.pz = v.pz + inst.z;
				w.nx = v.nx;
				w.ny = v.ny;
				w.nz = v.nz;
				w.u = v.u;
				w.v = v.v;
				out.data.vertices.push_back(w);
				ExpandBounds(out.bounds, w.px, w.py, w.pz);
			}
			for (uint32_t i : mesh.indices)
				out.data.indices.push_back(i + indexOffset);
		}

		if (out.data.vertices.empty())
			out.bounds = Bounds{};
		return out;
	}
}
