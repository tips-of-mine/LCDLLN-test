#pragma once

#include "engine/world/terrain/TerrainChunk.h"

#include <cstdint>
#include <vector>

namespace engine::world::terrain
{
	/// Vertex packé du mesh terrain : 32 octets, position en mètres chunk-local.
	/// Layout binaire stable :
	///   0..11  : float3 position (x, y=hauteur, z)
	///   12..23 : float3 normale (calculée par gradient)
	///   24..31 : float2 UV (0..1 sur le chunk)
	struct TerrainVertex
	{
		float position[3];
		float normal[3];
		float uv[2];
	};
	static_assert(sizeof(TerrainVertex) == 32, "TerrainVertex must stay 32 bytes");

	/// Description CPU d'un mesh terrain : vertex + index buffers prêts à upload
	/// vers le GPU. L'index buffer est UINT32 (257² > 65k).
	struct TerrainMeshCpu
	{
		std::vector<TerrainVertex> vertices;
		std::vector<uint32_t> indices;
	};

	/// Construit un mesh CPU LOD0 depuis un `TerrainChunk`. Triangle list,
	/// UVs 0..1 sur le chunk, normales par gradient bilinéaire (différences
	/// finies sur les voisins immédiats).
	/// \return mesh prêt à upload GPU.
	TerrainMeshCpu BuildLod0Mesh(const TerrainChunk& chunk);
}
