#include "engine/world/StreamCache.h"
#include "engine/core/Config.h"
#include "engine/core/Log.h"

#include <algorithm>
#include <cstring>
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
}
