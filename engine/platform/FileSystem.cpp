/**
 * @file FileSystem.cpp
 * @brief File I/O and path utilities — engine::platform::FileSystem.
 *
 * Uses C++17 std::filesystem for directory traversal / existence checks and
 * standard fstream for reading file content.  All paths are normalized to
 * forward slashes internally.
 */

#include "engine/platform/FileSystem.h"
#include "engine/core/Config.h"
#include "engine/core/Log.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace fs = std::filesystem;

namespace engine::platform {

// ---------------------------------------------------------------------------
// Internal state
// ---------------------------------------------------------------------------
namespace {

/// Resolved content root path (relative to CWD).  Set by Init().
std::string g_contentRoot = "game/data";

} // anonymous namespace

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

/*static*/
void FileSystem::Init() {
    using namespace engine::core;

    // Read content root from config; fall back to "game/data".
    g_contentRoot = Config::GetString("paths.content", "game/data");

    // Normalise: strip trailing slash if present.
    while (!g_contentRoot.empty() && (g_contentRoot.back() == '/' ||
                                       g_contentRoot.back() == '\\')) {
        g_contentRoot.pop_back();
    }

    if (g_contentRoot.empty()) {
        g_contentRoot = "game/data";
    }

    // Warn (non-fatal) if the directory is absent — it may be created later.
    std::error_code ec;
    if (!fs::is_directory(g_contentRoot, ec)) {
        LOG_WARN(Platform, "FileSystem: content root '{}' does not exist (will be created on demand)",
                 g_contentRoot);
    } else {
        LOG_INFO(Platform, "FileSystem: content root = '{}'", g_contentRoot);
    }
}

/*static*/
std::string_view FileSystem::ContentRoot() noexcept {
    return g_contentRoot;
}

// ---------------------------------------------------------------------------
// Path utilities
// ---------------------------------------------------------------------------

/*static*/
std::string FileSystem::JoinPath(std::string_view base, std::string_view rel) {
    if (base.empty()) { return std::string{rel}; }
    if (rel.empty())  { return std::string{base}; }

    // Use std::filesystem to normalise separator handling.
    const fs::path joined = fs::path{base} / fs::path{rel};
    // Normalise to forward slashes (cross-platform friendliness).
    std::string result = joined.generic_string();
    return result;
}

/*static*/
std::string FileSystem::FullPath(std::string_view relativePath) {
    return JoinPath(g_contentRoot, relativePath);
}

// ---------------------------------------------------------------------------
// File I/O
// ---------------------------------------------------------------------------

/*static*/
bool FileSystem::Exists(std::string_view relativePath) noexcept {
    const std::string full = FullPath(relativePath);
    std::error_code ec;
    return fs::exists(full, ec);
}

/*static*/
std::optional<std::vector<uint8_t>>
FileSystem::ReadAllBytes(std::string_view relativePath) {
    const std::string full = FullPath(relativePath);

    std::ifstream f{full, std::ios::binary};
    if (!f.is_open()) {
        LOG_ERROR(Platform, "FileSystem::ReadAllBytes: cannot open '{}'", full);
        return std::nullopt;
    }

    // Determine file size.
    f.seekg(0, std::ios::end);
    const auto size = f.tellg();
    f.seekg(0, std::ios::beg);

    if (size < 0) {
        LOG_ERROR(Platform, "FileSystem::ReadAllBytes: seekg failed for '{}'", full);
        return std::nullopt;
    }

    std::vector<uint8_t> buffer(static_cast<std::size_t>(size));
    if (size > 0) {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
        f.read(reinterpret_cast<char*>(buffer.data()),
               static_cast<std::streamsize>(size));
    }

    if (!f) {
        LOG_ERROR(Platform, "FileSystem::ReadAllBytes: read error for '{}'", full);
        return std::nullopt;
    }

    LOG_DEBUG(Platform, "ReadAllBytes: '{}' — {} bytes", full, buffer.size());
    return buffer;
}

/*static*/
std::optional<std::string>
FileSystem::ReadAllText(std::string_view relativePath) {
    const std::string full = FullPath(relativePath);

    std::ifstream f{full};
    if (!f.is_open()) {
        LOG_ERROR(Platform, "FileSystem::ReadAllText: cannot open '{}'", full);
        return std::nullopt;
    }

    std::string content{std::istreambuf_iterator<char>{f},
                        std::istreambuf_iterator<char>{}};

    if (!f && !f.eof()) {
        LOG_ERROR(Platform, "FileSystem::ReadAllText: read error for '{}'", full);
        return std::nullopt;
    }

    LOG_DEBUG(Platform, "ReadAllText: '{}' — {} chars", full, content.size());
    return content;
}

/*static*/
std::vector<std::string>
FileSystem::ListFiles(std::string_view relativeDir) {
    const std::string full = FullPath(relativeDir);

    std::error_code ec;
    if (!fs::is_directory(full, ec)) {
        LOG_WARN(Platform, "FileSystem::ListFiles: '{}' is not a directory", full);
        return {};
    }

    std::vector<std::string> result;
    for (const auto& entry : fs::directory_iterator{full, ec}) {
        if (ec) { break; }
        if (entry.is_regular_file(ec) && !ec) {
            result.push_back(entry.path().filename().generic_string());
        }
    }

    LOG_DEBUG(Platform, "ListFiles: '{}' — {} file(s) found", full, result.size());
    return result;
}

} // namespace engine::platform
