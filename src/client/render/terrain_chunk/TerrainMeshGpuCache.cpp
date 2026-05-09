#include "src/client/render/terrain_chunk/TerrainMeshGpuCache.h"

namespace engine::render::terrain_chunk
{
	uint64_t TerrainMeshGpuCache::PackCoord(engine::world::GlobalChunkCoord c)
	{
		return (static_cast<uint64_t>(static_cast<uint32_t>(c.x)) << 32)
		     | static_cast<uint64_t>(static_cast<uint32_t>(c.z));
	}

	void TerrainMeshGpuCache::Init(IGpuBufferAllocator* alloc, ChunkRuntime* runtime)
	{
		m_alloc = alloc;
		m_runtime = runtime;
		m_cache.clear();
	}

	void TerrainMeshGpuCache::Shutdown()
	{
		if (!m_alloc) return;
		for (auto& [key, entry] : m_cache)
		{
			if (entry.mesh.vertexBuffer) m_alloc->DestroyBuffer(entry.mesh.vertexBuffer);
			if (entry.mesh.indexBuffer)  m_alloc->DestroyBuffer(entry.mesh.indexBuffer);
		}
		m_cache.clear();
	}

	engine::world::terrain::TerrainMeshGpu
	TerrainMeshGpuCache::GetOrUpload(engine::world::GlobalChunkCoord coord,
		const engine::world::terrain::TerrainChunk& chunk)
	{
		const uint64_t key = PackCoord(coord);
		auto it = m_cache.find(key);
		if (it != m_cache.end()) return it->second.mesh;

		// Build CPU mesh puis upload via l'allocator.
		auto cpuMesh = engine::world::terrain::BuildLod0Mesh(chunk);
		const size_t vbBytes = cpuMesh.vertices.size() * sizeof(engine::world::terrain::TerrainVertex);
		const size_t ibBytes = cpuMesh.indices.size() * sizeof(uint32_t);

		Entry entry;
		entry.mesh.vertexBuffer = m_alloc->CreateAndUploadVertexBuffer(cpuMesh.vertices.data(), vbBytes);
		entry.mesh.indexBuffer  = m_alloc->CreateAndUploadIndexBuffer(cpuMesh.indices.data(), ibBytes);
		entry.mesh.indexCount   = static_cast<uint32_t>(cpuMesh.indices.size());
		entry.bytes = vbBytes + ibBytes;

		m_cache.emplace(key, entry);

		// Track budget dans le runtime si attaché.
		if (m_runtime != nullptr)
		{
			auto slot = m_runtime->GetOrAllocateSlot(coord);
			m_runtime->AddResidentBytes(slot, entry.bytes);
		}
		return entry.mesh;
	}

	engine::world::terrain::TerrainMeshGpu
	TerrainMeshGpuCache::Lookup(engine::world::GlobalChunkCoord coord) const
	{
		const uint64_t key = PackCoord(coord);
		auto it = m_cache.find(key);
		if (it == m_cache.end()) return engine::world::terrain::TerrainMeshGpu{};
		return it->second.mesh;
	}

	void TerrainMeshGpuCache::Evict(engine::world::GlobalChunkCoord coord)
	{
		const uint64_t key = PackCoord(coord);
		auto it = m_cache.find(key);
		if (it == m_cache.end()) return;
		if (m_alloc != nullptr)
		{
			if (it->second.mesh.vertexBuffer) m_alloc->DestroyBuffer(it->second.mesh.vertexBuffer);
			if (it->second.mesh.indexBuffer)  m_alloc->DestroyBuffer(it->second.mesh.indexBuffer);
		}
		m_cache.erase(it);
	}
}
