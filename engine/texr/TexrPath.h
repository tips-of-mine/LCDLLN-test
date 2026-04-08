#pragma once

#include <filesystem>
#include <string>

namespace lcdlln::texr {

/// Logical path: generic_string with '/' and ASCII A–Z folded to lower (spec game/data).
std::string NormalizeRelativePath(const std::filesystem::path& relative_path);

}  // namespace lcdlln::texr
