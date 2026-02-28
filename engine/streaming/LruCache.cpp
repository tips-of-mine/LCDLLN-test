/**
 * @file LruCache.cpp
 * @brief LRU cache implementation (M10.3).
 */

#include "engine/streaming/LruCache.h"

#include <cstring>

namespace engine::streaming {

std::optional<std::pair<const void*, size_t>> LruCache::Get(const std::string& key) {
    auto it = m_store.find(key);
    if (it == m_store.end()) return std::nullopt;
    Entry& e = it->second;
    m_lruOrder.erase(e.it);
    m_lruOrder.push_front(key);
    e.it = m_lruOrder.begin();
    return std::make_pair(static_cast<const void*>(e.data.data()), e.data.size());
}

void LruCache::Put(const std::string& key, const void* data, size_t size) {
    if (data == nullptr && size > 0) return;
    auto it = m_store.find(key);
    if (it != m_store.end()) {
        m_totalSize -= it->second.data.size();
        m_lruOrder.erase(it->second.it);
    }
    Entry e;
    e.data.resize(size);
    if (size > 0 && data != nullptr)
        std::memcpy(e.data.data(), data, size);
    m_lruOrder.push_front(key);
    e.it = m_lruOrder.begin();
    m_totalSize += size;
    m_store[key] = std::move(e);
    EvictUntilUnderCapacity();
}

void LruCache::Clear() noexcept {
    m_store.clear();
    m_lruOrder.clear();
    m_totalSize = 0;
}

void LruCache::EvictUntilUnderCapacity() {
    while (m_capacity > 0 && m_totalSize > m_capacity && !m_lruOrder.empty()) {
        const std::string& back = m_lruOrder.back();
        auto it = m_store.find(back);
        if (it != m_store.end()) {
            m_totalSize -= it->second.data.size();
            m_store.erase(it);
        }
        m_lruOrder.pop_back();
    }
}

} // namespace engine::streaming
