#pragma once

/**
 * @file FileSystem.h
 * @brief File I/O and path utilities for the engine platform layer.
 *
 * All public methods work with paths relative to the content root, which is
 * resolved once via Config ("paths.content").  Absolute paths are NOT
 * accepted by the content-aware helpers.
 *
 * Full path resolution follows the rule:
 *   fullPath = FileSystem::ContentRoot() + "/" + relativePath
 *
 * Low-level helpers (ReadAllBytes/ReadAllText/Exists/ListFiles) that accept
 * a relativePath automatically prepend ContentRoot().
 *
 * JoinPath() and the Absolute* helpers are path-manipulation utilities that
 * do not touch the filesystem.
 *
 * Usage:
 *   // Call once at startup, after Config::Init().
 *   FileSystem::Init();
 *
 *   // Read a texture:
 *   auto bytes = FileSystem::ReadAllBytes("textures/diffuse.png");
 *
 *   // Read a JSON config file from content root:
 *   auto text = FileSystem::ReadAllText("zones/zone01/meta.json");
 *
 *   // Check existence:
 *   if (FileSystem::Exists("meshes/crate.glb")) { ... }
 *
 *   // List files in a subdirectory:
 *   for (auto& f : FileSystem::ListFiles("textures")) { ... }
 */

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace engine::platform {

/**
 * @brief Static filesystem utilities for the engine.
 *
 * Methods are not thread-safe unless stated otherwise.  Init() must be called
 * before any other method.
 */
class FileSystem {
public:
    FileSystem() = delete;

    // -----------------------------------------------------------------------
    // Lifecycle
    // -----------------------------------------------------------------------

    /**
     * @brief Reads the content root from Config ("paths.content") and
     *        verifies that the directory exists.
     *
     * Falls back to "game/data" if the key is absent.
     * Logs a warning (not fatal) if the directory does not exist yet — it
     * may be created later.
     *
     * Must be called after Config::Init().
     */
    static void Init();

    /// Returns the content root path (relative to the working directory).
    [[nodiscard]] static std::string_view ContentRoot() noexcept;

    // -----------------------------------------------------------------------
    // File I/O  (paths relative to ContentRoot)
    // -----------------------------------------------------------------------

    /**
     * @brief Reads the entire content of a file as raw bytes.
     *
     * @param relativePath  Path relative to ContentRoot().
     * @return              File contents, or an empty optional on error.
     *
     * Logs LOG_ERROR on failure; never throws.
     */
    [[nodiscard]] static std::optional<std::vector<uint8_t>>
        ReadAllBytes(std::string_view relativePath);

    /**
     * @brief Reads the entire content of a text file as a UTF-8 string.
     *
     * @param relativePath  Path relative to ContentRoot().
     * @return              File contents, or an empty optional on error.
     *
     * Logs LOG_ERROR on failure; never throws.
     */
    [[nodiscard]] static std::optional<std::string>
        ReadAllText(std::string_view relativePath);

    /**
     * @brief Returns true if the file or directory exists.
     *
     * @param relativePath  Path relative to ContentRoot().
     */
    [[nodiscard]] static bool Exists(std::string_view relativePath) noexcept;

    /**
     * @brief Lists regular files (non-recursive) inside a directory.
     *
     * @param relativeDir   Directory path relative to ContentRoot().
     * @return              List of filenames (not full paths); empty on error.
     *
     * Logs LOG_WARN if the directory does not exist.
     */
    [[nodiscard]] static std::vector<std::string>
        ListFiles(std::string_view relativeDir);

    // -----------------------------------------------------------------------
    // Path utilities (no I/O)
    // -----------------------------------------------------------------------

    /**
     * @brief Joins two path segments with a forward slash, avoiding double
     *        slashes.
     *
     * @param base  Left-hand path segment.
     * @param rel   Right-hand path segment.
     * @return      Concatenated path.
     *
     * Example: JoinPath("game/data", "textures/foo.png") → "game/data/textures/foo.png"
     */
    [[nodiscard]] static std::string JoinPath(std::string_view base,
                                              std::string_view rel);

    /**
     * @brief Resolves a relative content path to its full path string.
     *
     * Equivalent to JoinPath(ContentRoot(), relativePath).
     *
     * @param relativePath  Path relative to ContentRoot().
     * @return              Full path string (still relative to the CWD).
     */
    [[nodiscard]] static std::string FullPath(std::string_view relativePath);
};

} // namespace engine::platform
