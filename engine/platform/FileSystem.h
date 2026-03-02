/**
 * @file FileSystem.h
 * @brief File system abstraction: read file, exists, list, path join. Paths relative to content root.
 */

#pragma once

#include <string>
#include <vector>

namespace engine::platform {

/**
 * FileSystem: read files, exists, list directory. Use SetContentRoot for paths relative to content.
 */
class FileSystem {
public:
    /** Set base path for content (e.g. from Config paths.content). Paths in Read/Exists/List are relative to this. */
    static void SetContentRoot(std::string rootPath);

    /** Get current content root. */
    static const std::string& GetContentRoot();

    /** Join path segments (no leading/trailing slashes required). */
    static std::string PathJoin(const std::string& a, const std::string& b);

    /** Read entire file as binary. Path relative to content root if not absolute. Returns empty on failure. */
    static std::vector<uint8_t> ReadAllBytes(const std::string& path);

    /** Read entire file as text. Path relative to content root if not absolute. Returns empty on failure. */
    static std::string ReadAllText(const std::string& path);

    /** True if path exists (file or directory). Path relative to content root if not absolute. */
    static bool Exists(const std::string& path);

    /** List entries in directory (names only). Path relative to content root if not absolute. Returns empty on failure. */
    static std::vector<std::string> List(const std::string& path);
};

} // namespace engine::platform
