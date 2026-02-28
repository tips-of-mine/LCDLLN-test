/**
 * @file ShaderCache.cpp
 * @brief Runtime shader cache with fallback on compile failure.
 */

#include "engine/render/ShaderCache.h"
#include "engine/core/Log.h"

#include <algorithm>
#include <string>

namespace engine::render {

std::string ShaderCacheKey(std::string_view relativePath,
                           const std::vector<std::string>& defines) {
    std::string key(relativePath);
    for (const auto& d : defines) {
        key += "|";
        key += d;
    }
    return key;
}

std::vector<uint8_t> ShaderCache::Get(
    std::string_view relativePath,
    const std::vector<std::string>& defines) {

    const std::string key = ShaderCacheKey(relativePath, defines);
    auto it = m_cache.find(key);

    if (it != m_cache.end() && !it->second.spirv.empty()) {
        return it->second.spirv;
    }

    const ShaderCompileResult result = CompileGlslToSpirv(relativePath, defines);

    if (result.success) {
        Entry e;
        e.spirv = result.spirv;
        e.lastError.clear();
        m_cache[key] = std::move(e);
        return m_cache[key].spirv;
    }

    if (it != m_cache.end() && !it->second.spirv.empty()) {
        it->second.lastError = result.errorMessage;
        LOG_WARN(Render, "Shader '{}' compile failed; using previous version. Error: {}",
                 relativePath, result.errorMessage);
        return it->second.spirv;
    }

    Entry e;
    e.lastError = result.errorMessage;
    m_cache[key] = std::move(e);
    return {};
}

void ShaderCache::Invalidate(std::string_view relativePath) {
    const std::string path(relativePath);
    for (auto it = m_cache.begin(); it != m_cache.end(); ) {
        if (it->first.compare(0, path.size(), path) == 0 &&
            (path.size() == it->first.size() || it->first[path.size()] == '|')) {
            it = m_cache.erase(it);
        } else {
            ++it;
        }
    }
}

bool ShaderCache::Reload(std::string_view relativePath,
                        const std::vector<std::string>& defines) {
    const std::string key = ShaderCacheKey(relativePath, defines);
    const ShaderCompileResult result = CompileGlslToSpirv(relativePath, defines);

    if (result.success) {
        Entry e;
        e.spirv = result.spirv;
        e.lastError.clear();
        m_cache[key] = std::move(e);
        return true;
    }

    auto it = m_cache.find(key);
    if (it != m_cache.end()) {
        it->second.lastError = result.errorMessage;
        LOG_WARN(Render, "Shader '{}' reload failed; keeping previous version. Error: {}",
                 relativePath, result.errorMessage);
    }
    return false;
}

std::string_view ShaderCache::LastError(std::string_view relativePath) const {
    const std::string path(relativePath);
    for (const auto& [k, v] : m_cache) {
        if (k.compare(0, path.size(), path) == 0 &&
            (path.size() == k.size() || k[path.size()] == '|')) {
            if (!v.lastError.empty()) {
                return v.lastError;
            }
        }
    }
    return "";
}

} // namespace engine::render
