#include "src/client/render/terrain_chunk/SplatMapGpuCache.h"

#include <vector>

namespace engine::render::terrain_chunk
{
	uint64_t SplatMapGpuCache::PackCoord(engine::world::GlobalChunkCoord c)
	{
		return (static_cast<uint64_t>(static_cast<uint32_t>(c.x)) << 32)
		     | static_cast<uint64_t>(static_cast<uint32_t>(c.z));
	}

	void SplatMapGpuCache::Init(IGpuImageAllocator* alloc, ChunkRuntime* runtime)
	{
		m_alloc = alloc;
		m_runtime = runtime;
		m_cache.clear();
	}

	void SplatMapGpuCache::Shutdown()
	{
		if (!m_alloc) return;
		for (auto& [key, entry] : m_cache)
		{
			m_alloc->DestroyImage(entry.maps.image0, entry.maps.view0);
			m_alloc->DestroyImage(entry.maps.image1, entry.maps.view1);
		}
		m_cache.clear();
	}

	SplatMapGpu SplatMapGpuCache::GetOrUpload(engine::world::GlobalChunkCoord coord,
		const engine::world::terrain::SplatMap& splat)
	{
		const uint64_t key = PackCoord(coord);
		auto it = m_cache.find(key);
		if (it != m_cache.end()) return it->second.maps;

		// Convertit le blob 8-channel interleaved en 2 blobs 4-channel
		// (RGBA8). image0 = layers 0..3, image1 = layers 4..7. Aligné avec
		// le shader fragment terrain_chunk.frag (M100.9).
		const uint32_t res = splat.resolution;
		const size_t cellCount = static_cast<size_t>(res) * res;
		std::vector<uint8_t> blob0(cellCount * 4u, 0u);
		std::vector<uint8_t> blob1(cellCount * 4u, 0u);
		for (size_t cell = 0; cell < cellCount; ++cell)
		{
			for (uint32_t c = 0; c < 4u; ++c)
			{
				blob0[cell * 4u + c] = splat.weights[cell * splat.layerCount + c];
				blob1[cell * 4u + c] = splat.weights[cell * splat.layerCount + (4u + c)];
			}
		}

		Entry entry;
		m_alloc->CreateAndUploadRGBA8Image(res, res, blob0.data(),
			entry.maps.image0, entry.maps.view0);
		m_alloc->CreateAndUploadRGBA8Image(res, res, blob1.data(),
			entry.maps.image1, entry.maps.view1);
		entry.bytes = blob0.size() + blob1.size();

		m_cache.emplace(key, entry);

		// Track budget dans le runtime si attaché.
		if (m_runtime != nullptr)
		{
			auto slot = m_runtime->GetOrAllocateSlot(coord);
			m_runtime->AddResidentBytes(slot, entry.bytes);
		}
		return entry.maps;
	}

	SplatMapGpu SplatMapGpuCache::Lookup(engine::world::GlobalChunkCoord coord) const
	{
		const uint64_t key = PackCoord(coord);
		auto it = m_cache.find(key);
		if (it == m_cache.end()) return SplatMapGpu{};
		return it->second.maps;
	}

	void SplatMapGpuCache::Evict(engine::world::GlobalChunkCoord coord)
	{
		const uint64_t key = PackCoord(coord);
		auto it = m_cache.find(key);
		if (it == m_cache.end()) return;
		if (m_alloc != nullptr)
		{
			m_alloc->DestroyImage(it->second.maps.image0, it->second.maps.view0);
			m_alloc->DestroyImage(it->second.maps.image1, it->second.maps.view1);
		}
		m_cache.erase(it);
	}
}
