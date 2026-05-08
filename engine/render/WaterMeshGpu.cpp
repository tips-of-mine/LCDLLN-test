// engine/render/WaterMeshGpu.cpp
#include "engine/render/WaterMeshGpu.h"
#include "engine/world/water/WaterMeshBuilder.h"
#include "engine/core/Log.h"

#include <cassert>
#include <cmath>

namespace engine::render
{
	namespace
	{
		static constexpr size_t kFloatsPerVertex = 7;  // pos3 + uv2 + flowDir2

		static_assert(sizeof(engine::render::WaterVertex) == kFloatsPerVertex * sizeof(float),
			"WaterVertex layout must match kFloatsPerVertex");

		// UV pour un lac : projection top-down (XZ) normalisee par la BBox du polygone.
		// Cela garantit des UV [0..1] coherents quelle que soit la taille du lac.
		void EmitLakeVertices(const engine::world::water::LakeInstance& lake,
			const engine::world::water::WaterMeshCpu& cpuMesh,
			std::vector<float>& outVerts)
		{
			// Calcule la BBox XZ du polygone.
			float minX = lake.polygon[0].x, maxX = lake.polygon[0].x;
			float minZ = lake.polygon[0].z, maxZ = lake.polygon[0].z;
			for (const auto& p : lake.polygon)
			{
				minX = std::fmin(minX, p.x); maxX = std::fmax(maxX, p.x);
				minZ = std::fmin(minZ, p.z); maxZ = std::fmax(maxZ, p.z);
			}
			const float dx = std::fmax(1e-3f, maxX - minX);
			const float dz = std::fmax(1e-3f, maxZ - minZ);

			for (const auto& v : cpuMesh.vertices)
			{
				outVerts.push_back(v.position.x);
				outVerts.push_back(v.position.y);
				outVerts.push_back(v.position.z);
				outVerts.push_back((v.position.x - minX) / dx);  // u
				outVerts.push_back((v.position.z - minZ) / dz);  // v
				outVerts.push_back(0.0f);                         // flowDir.x = 0 (lac)
				outVerts.push_back(0.0f);                         // flowDir.y = 0 (lac)
			}
		}

		// UV pour une riviere : u le long du flow, v perpendiculaire.
		// Le ribbon mesh produit par BuildRiverMesh emet 2 vertices par node
		// (gauche/droite). On peut donc deriver u depuis l'index pair/impair.
		void EmitRiverVertices(const engine::world::water::RiverInstance& river,
			const engine::world::water::WaterMeshCpu& cpuMesh,
			std::vector<float>& outVerts)
		{
			// Flow direction par segment, depuis ComputeFlowDirections (M100.13).
			const auto flows = engine::world::water::ComputeFlowDirections(river);

			// Le ribbon BuildRiverMesh emet vertices dans l'ordre :
			//   node 0 left, node 0 right, node 1 left, node 1 right, ..., node N-1 left, node N-1 right.
			// Soit 2 * N_nodes vertices, ou N_nodes = river.nodes.size().
			// Le vertex 2*i est "left" du node i, le vertex 2*i+1 est "right".
			const size_t nNodes = river.nodes.size();
			for (size_t vi = 0; vi < cpuMesh.vertices.size(); ++vi)
			{
				const auto& v = cpuMesh.vertices[vi];
				const size_t nodeIdx = vi / 2;
				const bool isLeft = (vi % 2) == 0;

				// u = nodeIdx / (nNodes - 1) (longueur le long du flow)
				const float u = (nNodes > 1) ? static_cast<float>(nodeIdx) / static_cast<float>(nNodes - 1) : 0.0f;
				const float vCoord = isLeft ? 0.0f : 1.0f;

				// flowDir : utilise le flow du segment courant (clamp au dernier pour le dernier node).
				assert(!flows.empty());  // Précondition BuildRiverMesh : nodes.size() >= 2
				const size_t segIdx = (nodeIdx < flows.size()) ? nodeIdx : flows.size() - 1;
				const float fx = flows[segIdx].x;
				const float fz = flows[segIdx].z;

				outVerts.push_back(v.position.x);
				outVerts.push_back(v.position.y);
				outVerts.push_back(v.position.z);
				outVerts.push_back(u);
				outVerts.push_back(vCoord);
				outVerts.push_back(fx);
				outVerts.push_back(fz);
			}
		}
	} // namespace

	void BuildDrawInfos(const engine::world::water::WaterScene& scene,
		std::vector<float>& outVertices,
		std::vector<uint32_t>& outIndices,
		std::vector<WaterInstanceDrawInfo>& outDrawInfos)
	{
		outVertices.clear();
		outIndices.clear();
		outDrawInfos.clear();

		uint32_t globalParamIdx = 0;

		// 1) Lacs en tete.
		for (const auto& lake : scene.lakes)
		{
			engine::world::water::WaterMeshCpu cpuMesh;
			std::string err;
			if (!engine::world::water::BuildLakeMesh(lake, cpuMesh, err))
			{
				LOG_WARN(Render, "[WaterMeshGpu] BuildLakeMesh failed for '{}': {}", lake.name, err);
				++globalParamIdx;
				continue;
			}
			if (cpuMesh.indices.empty()) { ++globalParamIdx; continue; }

			const int32_t baseVertex = static_cast<int32_t>(outVertices.size() / kFloatsPerVertex);
			const uint32_t firstIndex = static_cast<uint32_t>(outIndices.size());

			EmitLakeVertices(lake, cpuMesh, outVertices);
			for (uint32_t idx : cpuMesh.indices) outIndices.push_back(idx);

			WaterInstanceDrawInfo info{};
			info.firstIndex = firstIndex;
			info.indexCount = static_cast<uint32_t>(cpuMesh.indices.size());
			info.vertexOffset = baseVertex;
			info.paramsIndex = globalParamIdx;
			outDrawInfos.push_back(info);

			++globalParamIdx;
		}

		// 2) Rivieres ensuite.
		for (const auto& river : scene.rivers)
		{
			engine::world::water::WaterMeshCpu cpuMesh;
			std::string err;
			if (!engine::world::water::BuildRiverMesh(river, cpuMesh, err))
			{
				LOG_WARN(Render, "[WaterMeshGpu] BuildRiverMesh failed for '{}': {}", river.name, err);
				++globalParamIdx;
				continue;
			}
			if (cpuMesh.indices.empty()) { ++globalParamIdx; continue; }

			const int32_t baseVertex = static_cast<int32_t>(outVertices.size() / kFloatsPerVertex);
			const uint32_t firstIndex = static_cast<uint32_t>(outIndices.size());

			EmitRiverVertices(river, cpuMesh, outVertices);
			for (uint32_t idx : cpuMesh.indices) outIndices.push_back(idx);

			WaterInstanceDrawInfo info{};
			info.firstIndex = firstIndex;
			info.indexCount = static_cast<uint32_t>(cpuMesh.indices.size());
			info.vertexOffset = baseVertex;
			info.paramsIndex = globalParamIdx;
			outDrawInfos.push_back(info);

			++globalParamIdx;
		}
	}
}
