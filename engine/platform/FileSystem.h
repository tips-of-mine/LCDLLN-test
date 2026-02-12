#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace engine::platform {

class FileSystem {
public:
    explicit FileSystem(std::filesystem::path contentRoot);

    bool Exists(const std::string& relativePath) const;
    std::vector<std::uint8_t> ReadAllBytes(const std::string& relativePath) const;
    std::string ReadAllText(const std::string& relativePath) const;
    std::vector<std::string> List(const std::string& relativeDirectory) const;

    std::filesystem::path Join(const std::string& relativePath) const;

private:
    std::filesystem::path m_contentRoot;
};

} // namespace engine::platform
