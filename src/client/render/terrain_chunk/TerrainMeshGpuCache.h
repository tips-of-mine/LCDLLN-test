#pragma once

// src/client/render/terrain_chunk/TerrainMeshGpuCache.h
//
// Cache des `TerrainMeshGpu` (vertex + index VkBuffer) par chunk. Délègue
// l'upload Vulkan à un allocateur injectable (`IGpuBufferAllocator`) pour
// permettre des tests CPU avec un mock counter.

#include "src/client/render/terrain_chunk/ChunkRuntime.h"
#include "src/client/world/WorldModel.h"
#include "src/client/world/terrain/TerrainMeshBuilder.h" // TerrainMeshGpu, TerrainVertex, BuildLod0Mesh

#include <unordered_map>
#include <vulkan/vulkan_core.h>

namespace engine::render::terrain_chunk
{
	/// Interface allocateur GPU pour les buffers vertex + index. Mockable
	/// pour les tests. L'impl runtime concrète vit dans `TerrainChunkRenderer`
	/// et utilise `StagingAllocator` pour les uploads.
	class IGpuBufferAllocator
	{
	public:
		virtual ~IGpuBufferAllocator() = default;

		/// Crée un VkBuffer + alloue + upload `srcBytes`. Utilisé pour les
		/// vertex buffers (USAGE_VERTEX_BUFFER_BIT). Retourne le buffer prêt
		/// à bind. Sur la version runtime, le caller doit submit la barrier
		/// de transfert avant le draw.
		virtual VkBuffer CreateAndUploadVertexBuffer(const void* srcBytes, size_t sizeBytes) = 0;

		/// Idem pour les index buffers (USAGE_INDEX_BUFFER_BIT).
		virtual VkBuffer CreateAndUploadIndexBuffer(const void* srcBytes, size_t sizeBytes) = 0;

		/// Libère un VkBuffer alloué par `CreateAndUploadXxxBuffer`. Le caller
		/// doit garantir qu'il n'est plus en cours d'utilisation par le GPU
		/// (synchronisation au frame boundary recommandée).
		virtual void DestroyBuffer(VkBuffer buffer) = 0;
	};

	/// Cache LRU des meshes terrain GPU par chunk (M100). Délègue le runtime
	/// pour le tracking de budget global. Pas thread-safe.
	class TerrainMeshGpuCache
	{
	public:
		/// Initialise le cache. `alloc` et `runtime` sont possédés par le
		/// caller (pas de takeover). `runtime` peut être nullptr pour les
		/// tests qui ne valident pas le tracking budget.
		void Init(IGpuBufferAllocator* alloc, ChunkRuntime* runtime);

		/// Libère toutes les ressources GPU via l'allocator. Idempotent.
		void Shutdown();

		/// Lookup ou création. Si le mesh n'est pas en cache, builds via
		/// `engine::world::terrain::BuildLod0Mesh(chunk)` puis upload via
		/// l'allocator. Track `entry.bytes` dans le runtime si attaché.
		/// \return TerrainMeshGpu valide (vertex+index non null), ou {} si
		///         l'allocator a échoué (les deux buffers sont à VK_NULL_HANDLE).
		engine::world::terrain::TerrainMeshGpu GetOrUpload(
			engine::world::GlobalChunkCoord coord,
			const engine::world::terrain::TerrainChunk& chunk);

		/// Pure lookup (pas d'upload). Retourne {} si pas en cache.
		engine::world::terrain::TerrainMeshGpu Lookup(engine::world::GlobalChunkCoord coord) const;

		/// Évince le mesh pour `coord` (libère les VkBuffer via l'allocator).
		/// No-op si pas en cache.
		void Evict(engine::world::GlobalChunkCoord coord);

		/// Nombre de meshes résidents.
		size_t GetCachedCount() const { return m_cache.size(); }

	private:
		struct Entry
		{
			engine::world::terrain::TerrainMeshGpu mesh;
			size_t bytes = 0;
		};

		IGpuBufferAllocator* m_alloc = nullptr;
		ChunkRuntime* m_runtime = nullptr;
		std::unordered_map<uint64_t, Entry> m_cache; ///< packed coord -> entry

		static uint64_t PackCoord(engine::world::GlobalChunkCoord c);
	};
}
