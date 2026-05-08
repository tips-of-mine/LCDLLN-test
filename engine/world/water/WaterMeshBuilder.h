// engine/world/water/WaterMeshBuilder.h
#pragma once

#include "engine/math/Math.h"
#include "engine/world/water/WaterSurfaces.h"

#include <cstdint>
#include <string>
#include <vector>

namespace engine::world::water
{
	/// Vertex CPU minimal pour le mesh d'eau (M100.13). Position seulement ;
	/// les UV sont dérivés au runtime (M100.14) à partir des positions XZ.
	struct WaterVertex
	{
		engine::math::Vec3 position;
	};

	/// Mesh CPU produit par WaterMeshBuilder. Triangles indexés (3 indices
	/// par triangle). Le caller est responsable de l'upload GPU.
	struct WaterMeshCpu
	{
		std::vector<WaterVertex> vertices;
		std::vector<uint32_t>    indices;          // 3 par triangle
	};

	/// Triangulation du polygone d'un lac via ear clipping (M100.13).
	/// Précondition : polygon a >= 3 vertices, simple (non auto-intersectant).
	/// Si CW, inverse l'ordre interne avant traitement (CCW imposé).
	/// Tous les vertices output ont Y = lake.waterLevelY (mesh plat).
	/// Winding : normale +Y monde (CCW vu du dessus, +Y haut). Le caller
	/// (M100.14 GPU upload) doit configurer VkFrontFace en conséquence ou
	/// désactiver le backface culling.
	bool BuildLakeMesh(const LakeInstance& lake,
		WaterMeshCpu& outMesh, std::string& outError);

	/// Ribbon mesh d'une rivière. N nodes → N-1 segments → 2*(N-1) triangles.
	/// 2*N vertices total (2 par node, perpendiculaires au tangent local).
	/// Y de chaque vertex = node.position.y. Précondition : nodes.size() >= 2.
	/// Winding : normale +Y monde (visible vu du dessus). Le caller
	/// (M100.14 GPU upload) doit configurer VkFrontFace en conséquence.
	bool BuildRiverMesh(const RiverInstance& river,
		WaterMeshCpu& outMesh, std::string& outError);

	/// Calcule les directions de flot par segment de rivière.
	/// flow[i] = normalize((node[i+1] - node[i]).xz), Y forcé à 0.
	/// Précondition : river.nodes.size() >= 2. Output size = nodes.size() - 1.
	std::vector<engine::math::Vec3> ComputeFlowDirections(const RiverInstance& river);
}
