#pragma once

#include <array>
#include <cstdint>
#include <cstddef>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace lcdlln::manifest {

bool Base64Decode(std::string_view b64, std::vector<std::uint8_t>& out, std::string& err);

/// 64 caractères hex (32 octets).
bool HexDecode64(std::string_view hex64, std::array<std::uint8_t, 32>& out, std::string& err);

bool Ed25519Verify(std::span<const std::uint8_t, 32> pubkey, std::string_view message_utf8,
                   std::span<const std::uint8_t, 64> signature, std::string& err);

bool Sha256FileHexLower(const std::filesystem::path& path, std::string& out_hex64, std::string& err);

bool Sha256BufferHexLower(std::span<const std::uint8_t> data, std::string& out_hex64, std::string& err);

[[nodiscard]] bool FileByteSize(const std::filesystem::path& path, std::uint64_t& out_size, std::string& err);

}  // namespace lcdlln::manifest
