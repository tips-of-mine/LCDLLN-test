#pragma once

// engine/render/terrain_chunk/SplatMapGpuCache.h
//
// Cache des splat-maps GPU par chunk. Chaque chunk produit deux VkImage
// RGBA8 257² (image0 = layers 0..3, image1 = layers 4..7) à partir du blob
// 8-channel interleaved de `engine::world::terrain::SplatMap`. Délègue
// l'upload Vulkan à un allocateur injectable (`IGpuImageAllocator`) pour
// permettre des tests CPU avec un mock counter.

#include "engine/render/terrain_chunk/ChunkRuntime.h"
#include "engine/world/WorldModel.h"
#include "engine/world/terrain/SplatMap.h"

#include <unordered_map>
#include <vulkan/vulkan_core.h>

namespace engine::render::terrain_chunk
{
	/// Paire d'images splat pour un chunk (layers 0..3 + 4..7).
	struct SplatMapGpu
	{
		VkImage image0 = VK_NULL_HANDLE;     ///< Layers 0..3 (RGBA = 4 layers)
		VkImage image1 = VK_NULL_HANDLE;     ///< Layers 4..7
		VkImageView view0 = VK_NULL_HANDLE;
		VkImageView view1 = VK_NULL_HANDLE;
	};

	/// Interface allocateur GPU pour les VkImage RGBA8 + VkImageView. Mockable
	/// pour tests. L'impl runtime concrète vit dans `TerrainChunkRenderer`.
	class IGpuImageAllocator
	{
	public:
		virtual ~IGpuImageAllocator() = default;

		/// Crée un VkImage R8G8B8A8_UNORM `width × height` + alloue + upload
		/// `srcBytes` (taille `width * height * 4`). Crée aussi le VkImageView
		/// associé (VK_IMAGE_VIEW_TYPE_2D, format VK_FORMAT_R8G8B8A8_UNORM).
		/// Retourne `{outImage, outView}` couplés.
		virtual void CreateAndUploadRGBA8Image(uint32_t width, uint32_t height,
			const void* srcBytes, VkImage& outImage, VkImageView& outView) = 0;

		/// Libère un couple (image, view) alloué par `CreateAndUploadRGBA8Image`.
		virtual void DestroyImage(VkImage image, VkImageView view) = 0;
	};

	/// Cache LRU des splat-maps GPU par chunk (M100). 2× VkImage par chunk.
	/// Pas thread-safe.
	class SplatMapGpuCache
	{
	public:
		void Init(IGpuImageAllocator* alloc, ChunkRuntime* runtime);
		void Shutdown();

		/// Convertit la `SplatMap` (8 octets par cellule, planar par layer)
		/// en deux blobs 257×257×4 RGBA8 (layers 0..3 dans image0, 4..7 dans
		/// image1) puis upload + cache.
		/// \return SplatMapGpu valide (4 handles non null) ou {} si l'allocator
		///         a échoué.
		SplatMapGpu GetOrUpload(engine::world::GlobalChunkCoord coord,
			const engine::world::terrain::SplatMap& splat);

		/// Pure lookup. Retourne {} si pas en cache.
		SplatMapGpu Lookup(engine::world::GlobalChunkCoord coord) const;

		/// Évince le couple d'images pour `coord`. No-op si pas en cache.
		void Evict(engine::world::GlobalChunkCoord coord);

		size_t GetCachedCount() const { return m_cache.size(); }

	private:
		struct Entry
		{
			SplatMapGpu maps;
			size_t bytes = 0;
		};

		IGpuImageAllocator* m_alloc = nullptr;
		ChunkRuntime* m_runtime = nullptr;
		std::unordered_map<uint64_t, Entry> m_cache;

		static uint64_t PackCoord(engine::world::GlobalChunkCoord c);
	};
}
