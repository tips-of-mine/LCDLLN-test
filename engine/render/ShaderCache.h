#pragma once

/**
 * @file ShaderCache.h
 * @brief Runtime shader cache (key = path + defines) with fallback on compile failure.
 *
 * Ticket: M01.5 — Shader management + hot reload.
 *
 * Caches compiled SPIR-V per (relativePath, defines). On compile failure,
 * keeps and returns the last valid version so rendering remains functional.
 */

#include "engine/render/ShaderCompiler.h"

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace engine::render {

/**
 * @brief Builds a deterministic cache key from path and defines.
 */
std::string ShaderCacheKey(std::string_view relativePath,
                           const std::vector<std::string>& defines);

/**
 * @brief Runtime cache for compiled SPIR-V shaders.
 *
 * Get() returns cached bytes or compiles and caches. On compile failure,
 * returns the previous valid entry if any (fallback).
 */
class ShaderCache {
public:
    ShaderCache() = default;

    /**
     * @brief Returns compiled SPIR-V for the given shader.
     *
     * If not in cache, compiles and stores. If compilation fails and a
     * previous version exists, returns that (fallback).
     *
     * @param relativePath  Path relative to content root (e.g. "shaders/main.vert").
     * @param defines       Optional preprocessor defines.
     * @return              SPIR-V bytes, or empty if never compiled successfully.
     */
    [[nodiscard]] std::vector<uint8_t> Get(
        std::string_view relativePath,
        const std::vector<std::string>& defines = {});

    /**
     * @brief Invalidates the cache entry for a path (and any defines variant).
     *
     * Next Get() will recompile. Used when forcing a refresh.
     *
     * @param relativePath  Path relative to content root.
     */
    void Invalidate(std::string_view relativePath);

    /**
     * @brief Tries to recompile the shader and updates cache on success.
     *
     * On compile failure, keeps the previous cached version (fallback).
     *
     * @param relativePath  Path relative to content root.
     * @param defines       Defines to use.
     * @return              true if recompilation succeeded and cache was updated.
     */
    bool Reload(std::string_view relativePath,
                const std::vector<std::string>& defines = {});

    /**
     * @brief Forces recompilation of the given shader on next Get().
     *
     * Same as Invalidate but semantic name for hot-reload.
     */
    void RequestReload(std::string_view relativePath) {
        Invalidate(relativePath);
    }

    /**
     * @brief Returns the last compiler error message for a path, if any.
     */
    [[nodiscard]] std::string_view LastError(std::string_view relativePath) const;

private:
    struct Entry {
        std::vector<uint8_t> spirv;
        std::string lastError;
    };
    std::unordered_map<std::string, Entry> m_cache;
};

} // namespace engine::render
