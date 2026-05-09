#pragma once

#include "engine/world/terrain/TerrainChunk.h"
#include "engine/world/terrain/TerrainLodChain.h"

#include <vulkan/vulkan_core.h>

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

	/// Description GPU d'un mesh terrain chunk : vertex + index buffers Vulkan
	/// + indexCount pour `vkCmdDrawIndexed` (M100.9 — Task 13).
	///
	/// Distinct du `engine::render::terrain::TerrainMeshGpu` legacy (vertex 8
	/// octets shared 17² + LOD index buffers, M03 single-zone démo). Ici un
	/// vertex buffer dédié par chunk de 257² (taille fixe ~258 Ko) avec
	/// vertex layout 32 octets `TerrainVertex`. Le owner des buffers est
	/// décidé par le caller (typiquement un cache GPU côté `WorldModel`).
	///
	/// Tous les handles peuvent être `VK_NULL_HANDLE` pour signaler "mesh non
	/// uploadé" — `TerrainChunkPipeline::RecordChunkDraw` skippe alors l'appel.
	struct TerrainMeshGpu
	{
		VkBuffer       vertexBuffer = VK_NULL_HANDLE;
		VkDeviceMemory vertexMemory = VK_NULL_HANDLE;
		VkBuffer       indexBuffer  = VK_NULL_HANDLE;
		VkDeviceMemory indexMemory  = VK_NULL_HANDLE;
		uint32_t       vertexCount  = 0;
		uint32_t       indexCount   = 0; ///< Nombre d'indices UINT32.
	};

	/// Construit un mesh CPU LOD0 depuis un `TerrainChunk`. Triangle list,
	/// UVs 0..1 sur le chunk, normales par gradient bilinéaire (différences
	/// finies sur les voisins immédiats).
	/// \return mesh prêt à upload GPU.
	TerrainMeshCpu BuildLod0Mesh(const TerrainChunk& chunk);

	/// Construit un mesh CPU pour un niveau de LOD réduit (M100.8). Identique
	/// à `BuildLod0Mesh` au niveau de la grille principale, plus une "jupe"
	/// géométrique optionnelle (vertices dupliqués 2 m sous le bord du chunk +
	/// triangles de couture) pour masquer les T-junctions entre chunks de LOD
	/// différent.
	/// \param withSkirt true pour ajouter la skirt (recommandé runtime).
	TerrainMeshCpu BuildLodMesh(const TerrainLod& lod, bool withSkirt);
}
