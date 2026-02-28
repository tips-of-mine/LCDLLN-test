/**
 * @file ShaderWatcher.cpp
 * @brief File watcher for shader hot-reload.
 */

#include "engine/render/ShaderWatcher.h"
#include "engine/core/Config.h"
#include "engine/core/Log.h"

#include <filesystem>
#include <string>

namespace fs = std::filesystem;

namespace engine::render {

ShaderWatcher::ShaderWatcher(ShaderCache* cache) : m_cache(cache) {}

void ShaderWatcher::Watch(std::string_view relativePath) {
    m_watchedPaths.emplace_back(relativePath);
    m_lastMtime.push_back(std::nullopt);
}

bool ShaderWatcher::Poll(const std::vector<std::string>& defines) {
    if (!m_cache || m_watchedPaths.empty()) {
        return false;
    }

    const std::string contentRoot = engine::core::Config::GetString("paths.content", "game/data");
    const fs::path rootPath(contentRoot);

    for (size_t n = 0; n < m_watchedPaths.size(); ++n) {
        const size_t i = (m_nextCheckIndex + n) % m_watchedPaths.size();
        m_nextCheckIndex = (i + 1) % m_watchedPaths.size();

        const fs::path fullPath = rootPath / m_watchedPaths[i];
        std::error_code ec;
        if (!fs::exists(fullPath, ec)) {
            continue;
        }

        const auto mtime = fs::last_write_time(fullPath, ec);
        if (ec) {
            continue;
        }

        if (m_lastMtime[i].has_value() && m_lastMtime[i].value() != mtime) {
            m_lastMtime[i] = mtime;
            const bool ok = m_cache->Reload(m_watchedPaths[i], defines);
            if (ok) {
                LOG_INFO(Render, "Shader hot-reload succeeded: {}", m_watchedPaths[i]);
            } else {
                LOG_WARN(Render, "Shader hot-reload failed (using previous): {}", m_watchedPaths[i]);
            }
            return true;
        }
        if (!m_lastMtime[i].has_value()) {
            m_lastMtime[i] = mtime;
        }
    }
    return false;
}

} // namespace engine::render
