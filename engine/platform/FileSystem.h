#pragma once

#include "engine/core/Config.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace engine::platform
{
	/// File system helpers (read/exists/list/path join) and content-path resolution via Config.
	struct FileSystem final
	{
		/// Join path segments (`a/b`), without normalizing to absolute paths.
		static std::filesystem::path Join(std::string_view a, std::string_view b);

		/// Resolve a relative content path using `paths.content` from config.
		static std::filesystem::path ResolveContentPath(const engine::core::Config& cfg, std::string_view relativeContentPath);

		/// Return true if the path exists.
		static bool Exists(const std::filesystem::path& path);

		/// List direct children of a directory (non-recursive). Returns empty if missing.
		static std::vector<std::filesystem::path> ListDirectory(const std::filesystem::path& dir);

		/// Read the entire file as bytes. Returns empty vector on failure.
		static std::vector<uint8_t> ReadAllBytes(const std::filesystem::path& path);

		/// Read the entire file as text (UTF-8 assumed). Returns empty string on failure.
		static std::string ReadAllText(const std::filesystem::path& path);

		/// Read a content file as bytes (relative to `paths.content`).
		static std::vector<uint8_t> ReadAllBytesContent(const engine::core::Config& cfg, std::string_view relativeContentPath);

		/// Read a content file as text (relative to `paths.content`).
		static std::string ReadAllTextContent(const engine::core::Config& cfg, std::string_view relativeContentPath);

		/// Write the entire file as text (UTF-8 assumed). Returns true on success.
		static bool WriteAllText(const std::filesystem::path& path, std::string_view text);

		/// Write a content file as text (relative to `paths.content`). Creates parent directories if needed.
		static bool WriteAllTextContent(const engine::core::Config& cfg, std::string_view relativeContentPath, std::string_view text);
	};
}

