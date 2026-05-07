#include "engine/world/StreamCache.h"
#include "engine/core/Config.h"
#include "engine/core/Log.h"
#include "engine/world/terrain/TerrainChunk.h"
#include "engine/world/terrain/TerrainChunkLoader.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <string>

namespace engine::world
{
	namespace
	{
		constexpr size_t kMinCacheMb = 1024;
		constexpr size_t kMaxCacheMb = 4096;
		constexpr size_t kBytesPerMb = 1024 * 1024;
	}

	void StreamCache::Init(const engine::core::Config& config)
	{
		const int64_t mb = config.GetInt("streaming.cache_size_mb", 1024);
		const size_t clampedMb = static_cast<size_t>(std::clamp(mb, static_cast<int64_t>(kMinCacheMb), static_cast<int64_t>(kMaxCacheMb)));
		m_maxSizeBytes = clampedMb * kBytesPerMb;
		m_currentSizeBytes = 0;
		m_map.clear();
		m_lruOrder.clear();
		m_hitCount = 0;
		m_missCount = 0;
		LOG_INFO(World, "[StreamCache] Init OK (capacity_mb={})", clampedMb);
	}

	void StreamCache::TouchLru(std::string_view key)
	{
		const std::string keyStr(key);
		auto it = std::find(m_lruOrder.begin(), m_lruOrder.end(), keyStr);
		if (it != m_lruOrder.end())
		{
			m_lruOrder.erase(it);
			m_lruOrder.push_back(keyStr);
		}
	}

	void StreamCache::EvictLruUntilWithinCapacity()
	{
		while (m_currentSizeBytes > m_maxSizeBytes && !m_lruOrder.empty())
		{
			const std::string& oldest = m_lruOrder.front();
			auto mapIt = m_map.find(oldest);
			if (mapIt != m_map.end())
			{
				m_currentSizeBytes -= mapIt->second.sizeBytes;
				m_map.erase(mapIt);
			}
			m_lruOrder.erase(m_lruOrder.begin());
		}
	}

	std::optional<std::vector<uint8_t>> StreamCache::Lookup(std::string_view key)
	{
		const std::string keyStr(key);
		auto it = m_map.find(keyStr);
		if (it == m_map.end())
		{
			++m_missCount;
			return std::nullopt;
		}
		++m_hitCount;
		TouchLru(key);
		return it->second.blob;
	}

	void StreamCache::Insert(std::string_view key, const void* data, size_t size)
	{
		const std::string keyStr(key);
		auto it = m_map.find(keyStr);
		if (it != m_map.end())
		{
			m_currentSizeBytes -= it->second.sizeBytes;
			TouchLru(key);
		}
		else
		{
			m_lruOrder.push_back(keyStr);
		}
		std::vector<uint8_t> blob(size);
		if (size > 0 && data != nullptr)
			std::memcpy(blob.data(), data, size);
		m_map[keyStr] = Entry{ std::move(blob), size };
		m_currentSizeBytes += size;
		EvictLruUntilWithinCapacity();
	}

	void StreamCache::Insert(std::string_view key, const std::vector<uint8_t>& blob)
	{
		Insert(key, blob.data(), blob.size());
	}

	void StreamCache::Clear()
	{
		const size_t entryCount = m_map.size();
		m_map.clear();
		m_lruOrder.clear();
		m_currentSizeBytes = 0;
		m_hitCount = 0;
		m_missCount = 0;
		LOG_INFO(World, "[StreamCache] Cleared (entries={})", entryCount);
	}

	std::shared_ptr<engine::world::terrain::TerrainChunk> StreamCache::LoadTerrainChunk(
		const engine::core::Config& config, int chunkX, int chunkZ)
	{
		const std::string cacheKey =
			engine::world::terrain::MakeTerrainCacheKey(chunkX, chunkZ);

		// 1) Tentative cache : un hit évite la lecture disque.
		std::string err;
		if (auto cached = engine::world::terrain::LoadFromCache(*this, cacheKey, err))
			return cached;

		// 2) Miss : lire depuis disque sous `<paths.content>/<cacheKey>`.
		const std::string contentRoot = config.GetString("paths.content", "game/data");
		const std::string fullPath = contentRoot + "/" + cacheKey;
		std::ifstream f(fullPath, std::ios::binary);
		if (!f.good())
		{
			LOG_WARN(World, "[StreamCache] terrain.bin absent: {}", fullPath);
			return nullptr;
		}
		f.seekg(0, std::ios::end);
		const std::streamsize fileSize = f.tellg();
		f.seekg(0, std::ios::beg);
		if (fileSize <= 0)
		{
			LOG_WARN(World, "[StreamCache] terrain.bin vide: {}", fullPath);
			return nullptr;
		}
		std::vector<uint8_t> blob(static_cast<size_t>(fileSize));
		f.read(reinterpret_cast<char*>(blob.data()), fileSize);
		if (!f.good() && !f.eof())
		{
			LOG_WARN(World, "[StreamCache] read fail: {}", fullPath);
			return nullptr;
		}

		// 3) Désérialisation + insertion dans le cache.
		auto chunk = std::make_shared<engine::world::terrain::TerrainChunk>();
		std::span<const uint8_t> bytes(blob.data(), blob.size());
		if (!engine::world::terrain::LoadTerrainBin(bytes, *chunk, err))
		{
			LOG_WARN(World, "[StreamCache] LoadTerrainBin fail ({}): {}", fullPath, err);
			return nullptr;
		}
		Insert(cacheKey, blob);
		return chunk;
	}
}
