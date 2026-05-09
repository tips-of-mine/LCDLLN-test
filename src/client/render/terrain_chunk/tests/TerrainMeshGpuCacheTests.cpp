/// Tests unitaires pour TerrainMeshGpuCache (mock allocator, pas de Vulkan).
///
/// Vérifient :
///   - GetOrUpload caches le mesh + ne re-upload pas au 2e appel.
///   - Lookup miss retourne TerrainMeshGpu nul.
///   - Evict détruit les VkBuffer via l'allocator + retire du cache.
///   - GetOrUpload track la résidence dans ChunkRuntime.

#include "engine/render/terrain_chunk/TerrainMeshGpuCache.h"

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
	using engine::render::terrain_chunk::IGpuBufferAllocator;
	using engine::render::terrain_chunk::TerrainMeshGpuCache;
	using engine::world::terrain::TerrainChunk;

	/// Allocator factice : retourne des handles uint64 incrémentaux castés en
	/// VkBuffer. Les tests vérifient les counters create/destroy.
	struct MockBufferAllocator final : IGpuBufferAllocator
	{
		uint64_t nextHandle = 1;
		int creates = 0;
		int destroys = 0;

		VkBuffer CreateAndUploadVertexBuffer(const void*, size_t) override
		{
			++creates;
			return reinterpret_cast<VkBuffer>(nextHandle++);
		}

		VkBuffer CreateAndUploadIndexBuffer(const void*, size_t) override
		{
			++creates;
			return reinterpret_cast<VkBuffer>(nextHandle++);
		}

		void DestroyBuffer(VkBuffer) override { ++destroys; }
	};

	/// 1er GetOrUpload crée 2 buffers (vertex + index), 2e ne crée rien.
	void Test_GetOrUpload_CachesAndReuses()
	{
		MockBufferAllocator alloc;
		ChunkRuntime rt;
		rt.Init({});
		TerrainMeshGpuCache cache;
		cache.Init(&alloc, &rt);
		auto chunk = TerrainChunk::MakeFlat(0.0f);

		auto m1 = cache.GetOrUpload({0, 0}, chunk);
		REQUIRE(alloc.creates == 2); // 1 vertex + 1 index
		REQUIRE(m1.vertexBuffer != VK_NULL_HANDLE);
		REQUIRE(m1.indexBuffer != VK_NULL_HANDLE);

		auto m2 = cache.GetOrUpload({0, 0}, chunk);
		REQUIRE(alloc.creates == 2); // pas de nouveau create (cache hit)
		REQUIRE(m1.vertexBuffer == m2.vertexBuffer);
		REQUIRE(m1.indexBuffer == m2.indexBuffer);
	}

	/// Lookup d'un coord absent retourne TerrainMeshGpu nul.
	void Test_Lookup_MissReturnsZero()
	{
		MockBufferAllocator alloc;
		ChunkRuntime rt; rt.Init({});
		TerrainMeshGpuCache cache; cache.Init(&alloc, &rt);
		auto m = cache.Lookup({0, 0});
		REQUIRE(m.vertexBuffer == VK_NULL_HANDLE);
		REQUIRE(m.indexBuffer == VK_NULL_HANDLE);
	}

	/// Evict détruit les 2 VkBuffer + retire du cache.
	void Test_Evict_DestroysBuffersAndRemovesFromCache()
	{
		MockBufferAllocator alloc;
		ChunkRuntime rt; rt.Init({});
		TerrainMeshGpuCache cache; cache.Init(&alloc, &rt);
		auto chunk = TerrainChunk::MakeFlat(0.0f);
		cache.GetOrUpload({0, 0}, chunk);
		REQUIRE(cache.GetCachedCount() == 1u);
		cache.Evict({0, 0});
		REQUIRE(cache.GetCachedCount() == 0u);
		REQUIRE(alloc.destroys == 2);
	}

	/// GetOrUpload track la résidence dans ChunkRuntime.
	void Test_GetOrUpload_TracksBudgetInRuntime()
	{
		MockBufferAllocator alloc;
		ChunkRuntime rt; rt.Init({});
		TerrainMeshGpuCache cache; cache.Init(&alloc, &rt);
		auto chunk = TerrainChunk::MakeFlat(0.0f);
		const size_t bytesBefore = rt.GetResidentBytes();
		cache.GetOrUpload({0, 0}, chunk);
		const size_t bytesAfter = rt.GetResidentBytes();
		REQUIRE(bytesAfter > bytesBefore);
	}
}

int main()
{
	Test_GetOrUpload_CachesAndReuses();
	Test_Lookup_MissReturnsZero();
	Test_Evict_DestroysBuffersAndRemovesFromCache();
	Test_GetOrUpload_TracksBudgetInRuntime();

	if (g_failed == 0)
	{
		std::printf("[PASS] TerrainMeshGpuCacheTests (4/4)\n");
		return 0;
	}
	std::printf("[FAIL] TerrainMeshGpuCacheTests: %d failure(s)\n", g_failed);
	return 1;
}
