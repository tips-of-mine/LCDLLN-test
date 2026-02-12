#include "engine/platform/FileSystem.h"

#include <algorithm>
#include <fstream>
#include <iterator>

namespace engine::platform {

FileSystem::FileSystem(std::filesystem::path contentRoot)
    : m_contentRoot(std::move(contentRoot)) {}

bool FileSystem::Exists(const std::string& relativePath) const {
    return std::filesystem::exists(Join(relativePath));
}

std::vector<std::uint8_t> FileSystem::ReadAllBytes(const std::string& relativePath) const {
    std::ifstream input(Join(relativePath), std::ios::binary);
    if (!input.is_open()) {
        return {};
    }

    return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

std::string FileSystem::ReadAllText(const std::string& relativePath) const {
    std::ifstream input(Join(relativePath));
    if (!input.is_open()) {
        return {};
    }

    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

std::vector<std::string> FileSystem::List(const std::string& relativeDirectory) const {
    std::vector<std::string> entries;
    const auto dir = Join(relativeDirectory);
    if (!std::filesystem::exists(dir) || !std::filesystem::is_directory(dir)) {
        return entries;
    }

    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
        entries.push_back(entry.path().filename().string());
    }

    std::sort(entries.begin(), entries.end());
    return entries;
}

std::filesystem::path FileSystem::Join(const std::string& relativePath) const {
    const std::filesystem::path rel(relativePath);
    const auto safeRelative = rel.is_absolute() ? rel.relative_path() : rel;
    return (m_contentRoot / safeRelative).lexically_normal();
}

} // namespace engine::platform
