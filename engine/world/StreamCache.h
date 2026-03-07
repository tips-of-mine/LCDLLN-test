#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace engine::core { class Config; }

namespace engine::world
{
	/// LRU cache for decompressed blobs keyed by asset/chunk file path (M10.3).
	/// Capacity 1–4 GB configurable via config; hit avoids re-IO when re-entering a zone.
	class StreamCache
	{
	public:
		StreamCache() = default;

		/// Initializes cache with max size in bytes from config (streaming.cache_size_mb, default 1024, clamp 1024–4096).
		void Init(const engine::core::Config& config);

		/// Returns cached blob for \p key if present and touches LRU; otherwise nullopt (caller may load from disk and Insert).
		std::optional<std::vector<uint8_t>> Lookup(std::string_view key);

		/// Inserts or replaces blob for \p key; evicts LRU entries if over capacity.
		void Insert(std::string_view key, const void* data, size_t size);

		/// Inserts or replaces blob for \p key from vector; evicts LRU entries if over capacity.
		void Insert(std::string_view key, const std::vector<uint8_t>& blob);

		/// Returns current total cached size in bytes.
		size_t GetCurrentSizeBytes() const { return m_currentSizeBytes; }
		/// Returns max capacity in bytes.
		size_t GetMaxSizeBytes() const { return m_maxSizeBytes; }
		/// Returns number of cache hits since last reset (for hit rate measurement).
		uint64_t GetHitCount() const { return m_hitCount; }
		/// Returns number of cache misses since last reset.
		uint64_t GetMissCount() const { return m_missCount; }
		/// Resets hit/miss counters (e.g. for periodic stats).
		void ResetStats() { m_hitCount = 0; m_missCount = 0; }

	private:
		struct Entry
		{
			std::vector<uint8_t> blob;
			size_t sizeBytes = 0;
		};
		std::unordered_map<std::string, Entry> m_map;
		std::vector<std::string> m_lruOrder;
		size_t m_maxSizeBytes = 1024 * 1024 * 1024; // 1 GB default
		size_t m_currentSizeBytes = 0;
		uint64_t m_hitCount = 0;
		uint64_t m_missCount = 0;

		void EvictLruUntilWithinCapacity();
		void TouchLru(std::string_view key);
	};
}
