#pragma once

/**
 * @file LruCache.h
 * @brief LRU cache for decompressed blobs, keyed by asset/chunk file (M10.3).
 *
 * Capacity 1-4GB configurable via config. Hit cache in IO stage to skip disk.
 */

#include <cstddef>
#include <list>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace engine::streaming {

/**
 * @brief LRU cache: key (asset/chunk file path) -> decompressed blob.
 *
 * Capacity in bytes (config streaming.cache_size_mb, 1024-4096 MB).
 * Get updates LRU order; Put evicts least recently used when over capacity.
 */
class LruCache {
public:
    LruCache() = default;

    /**
     * @brief Sets maximum capacity in bytes. Evictions occur when total size exceeds this.
     */
    void SetCapacityBytes(size_t bytes) noexcept { m_capacity = bytes; }

    /**
     * @brief Returns current total size of cached blobs in bytes.
     */
    [[nodiscard]] size_t SizeBytes() const noexcept { return m_totalSize; }

    /**
     * @brief Gets blob for key if present; moves key to most recently used.
     * @return Pointer and size to cached data, or nullopt on miss.
     */
    [[nodiscard]] std::optional<std::pair<const void*, size_t>> Get(const std::string& key);

    /**
     * @brief Inserts or replaces blob for key. Evicts LRU entries if over capacity.
     */
    void Put(const std::string& key, const void* data, size_t size);

    /**
     * @brief Clears all entries and resets size.
     */
    void Clear() noexcept;

private:
    size_t m_capacity = 0;
    size_t m_totalSize = 0;
    std::list<std::string> m_lruOrder;
    struct Entry {
        std::vector<std::byte> data;
        std::list<std::string>::iterator it;
    };
    std::unordered_map<std::string, Entry> m_store;

    void EvictUntilUnderCapacity();
};

} // namespace engine::streaming
