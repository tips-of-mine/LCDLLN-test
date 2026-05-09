/// Tests unitaires pour SplatMapGpuCache (mock allocator, pas de Vulkan).
///
/// Vérifient :
///   - GetOrUpload crée 2 VkImage par chunk (layers 0..3 + layers 4..7).
///   - GetOrUpload caches + ne re-upload pas au 2e appel.
///   - Evict détruit les 2 paires (image, view) via l'allocator.

#include "engine/render/terrain_chunk/SplatMapGpuCache.h"

#include <cstdio>
#include <cstdint>

namespace
{
	int g_failed = 0;

	#define REQUIRE(cond) do { \
		if (!(cond)) { \
			std::fprintf(stderr, "[FAIL] %s:%d  %s\n", __FILE__, __LINE__, #cond); \
			++g_failed; \
		} \
	} while (0)

	using engine::render::terrain_chunk::ChunkRuntime;
	using engine::render::terrain_chunk::IGpuImageAllocator;
	using engine::render::terrain_chunk::SplatMapGpuCache;
	using engine::world::terrain::SplatMap;

	struct MockImageAllocator final : IGpuImageAllocator
	{
		uint64_t nextHandle = 1;
		int creates = 0;
		int destroys = 0;

		void CreateAndUploadRGBA8Image(uint32_t, uint32_t, const void*,
			VkImage& outImage, VkImageView& outView) override
		{
			++creates;
			outImage = reinterpret_cast<VkImage>(nextHandle++);
			outView  = reinterpret_cast<VkImageView>(nextHandle++);
		}

		void DestroyImage(VkImage, VkImageView) override { ++destroys; }
	};

	/// 1er GetOrUpload crée 2 images (layers 0..3 et 4..7).
	void Test_GetOrUpload_TwoImagesPerChunk()
	{
		MockImageAllocator alloc;
		ChunkRuntime rt; rt.Init({});
		SplatMapGpuCache cache; cache.Init(&alloc, &rt);
		auto splat = SplatMap::MakeUniform(0u);
		auto gpu = cache.GetOrUpload({0, 0}, splat);
		REQUIRE(alloc.creates == 2);
		REQUIRE(gpu.image0 != VK_NULL_HANDLE);
		REQUIRE(gpu.image1 != VK_NULL_HANDLE);
		REQUIRE(gpu.view0 != VK_NULL_HANDLE);
		REQUIRE(gpu.view1 != VK_NULL_HANDLE);
	}

	/// 2e GetOrUpload sur même coord = cache hit, pas de nouveau create.
	void Test_GetOrUpload_CachesAndReuses()
	{
		MockImageAllocator alloc;
		ChunkRuntime rt; rt.Init({});
		SplatMapGpuCache cache; cache.Init(&alloc, &rt);
		auto splat = SplatMap::MakeUniform(0u);
		cache.GetOrUpload({0, 0}, splat);
		cache.GetOrUpload({0, 0}, splat);
		REQUIRE(alloc.creates == 2); // pas de re-upload
		REQUIRE(cache.GetCachedCount() == 1u);
	}

	/// Evict détruit les 2 paires (image, view) + retire du cache.
	void Test_Evict_DestroysImages()
	{
		MockImageAllocator alloc;
		ChunkRuntime rt; rt.Init({});
		SplatMapGpuCache cache; cache.Init(&alloc, &rt);
		auto splat = SplatMap::MakeUniform(0u);
		cache.GetOrUpload({0, 0}, splat);
		cache.Evict({0, 0});
		REQUIRE(alloc.destroys == 2);
		REQUIRE(cache.GetCachedCount() == 0u);
	}
}

int main()
{
	Test_GetOrUpload_TwoImagesPerChunk();
	Test_GetOrUpload_CachesAndReuses();
	Test_Evict_DestroysImages();

	if (g_failed == 0)
	{
		std::printf("[PASS] SplatMapGpuCacheTests (3/3)\n");
		return 0;
	}
	std::printf("[FAIL] SplatMapGpuCacheTests: %d failure(s)\n", g_failed);
	return 1;
}
