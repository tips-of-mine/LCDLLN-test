#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace lcdlln::texr {

/// Parses 64 hex chars → 32-byte AES-256 key.
bool ParseAes256KeyHex(std::string_view hex, std::array<std::uint8_t, 32>& out_key, std::string& err);

/// Encrypt full plaintext with AES-256-GCM, 12-byte random IV (caller may use RAND_bytes). AAD empty.
bool Aes256GcmEncrypt(const std::uint8_t* plaintext,
                      std::size_t plaintext_len,
                      const std::array<std::uint8_t, 32>& key,
                      std::vector<std::uint8_t>& iv_out,
                      std::vector<std::uint8_t>& ciphertext_out,
                      std::array<std::uint8_t, 16>& tag_out,
                      std::string& err);

/// Decrypt; `tag` is 16-byte auth tag from end of file.
bool Aes256GcmDecrypt(const std::uint8_t* iv,
                      std::size_t iv_len,
                      const std::uint8_t* ciphertext,
                      std::size_t ciphertext_len,
                      const std::array<std::uint8_t, 16>& tag,
                      const std::array<std::uint8_t, 32>& key,
                      std::vector<std::uint8_t>& plaintext_out,
                      std::string& err);

}  // namespace lcdlln::texr
