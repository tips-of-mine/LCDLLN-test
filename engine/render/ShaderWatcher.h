#pragma once

/**
 * @file ShaderWatcher.h
 * @brief File watcher for shader hot-reload (timestamp/hash, no frame freeze).
 *
 * Ticket: M01.5 — Shader management + hot reload.
 *
 * Watches a set of shader paths (relative to content root). Poll() checks
 * file modification time; when a file changes, invalidates cache and
 * triggers recompile. Success/failure is logged. One file per Poll() to
 * avoid freezing the frame.
 */

#include "engine/render/ShaderCache.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace engine::render {

/**
 * @brief Watches shader files and triggers hot-reload on change.
 *
 * Call Poll() once per frame (or at a fixed interval). Uses file
 * modification time to detect changes. No blocking.
 */
class ShaderWatcher {
public:
    /**
     * @brief Constructs a watcher that uses the given cache.
     */
    explicit ShaderWatcher(ShaderCache* cache);

    /**
     * @brief Adds a shader path to watch (relative to content root).
     *
     * @param relativePath  e.g. "shaders/main.vert"
     */
    void Watch(std::string_view relativePath);

    /**
     * @brief Checks for changed files and triggers reload.
     *
     * Processes at most one changed file per call to avoid frame freeze.
     * Logs success or failure of the reload.
     *
     * @param defines  Defines to use when recompiling (default empty).
     * @return         true if a reload was triggered this call.
     */
    bool Poll(const std::vector<std::string>& defines = {});

private:
    ShaderCache* m_cache = nullptr;
    std::vector<std::string> m_watchedPaths;
    /// Last known modification time per path (index matches m_watchedPaths).
    std::vector<std::optional<std::filesystem::file_time_type>> m_lastMtime;
    /// Index of next path to check (round-robin to avoid freeze).
    size_t m_nextCheckIndex = 0;
};

} // namespace engine::render
