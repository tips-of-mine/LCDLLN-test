#pragma once

#include <array>
#include <cstdint>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace lcdlln::texr {

/// Reads v1 .texr (optional AES-256-GCM outer; inner with optional LZ4 per entry).
class TexrReader
{
public:
	TexrReader() = default;
	TexrReader(const TexrReader&) = delete;
	TexrReader& operator=(const TexrReader&) = delete;
	TexrReader(TexrReader&&) noexcept = default;
	TexrReader& operator=(TexrReader&&) noexcept = default;

	~TexrReader() = default;

	/// 64 hex chars (32 bytes). Empty clears the key. Invalid hex returns false and sets err.
	bool SetAes256KeyFromHex(std::string_view hex64_or_empty, std::string& err);

	void ClearDecryptKey() noexcept { decrypt_key_.reset(); }

	/// Load package from disk. Encrypted packages: use SetAes256KeyFromHex or env `TEXR_AES_KEY_HEX`.
	bool Open(const std::filesystem::path& path, std::string& err);

	void Close();

	[[nodiscard]] bool IsOpen() const noexcept { return !inner_.empty(); }

	[[nodiscard]] std::size_t EntryCount() const noexcept { return index_.size(); }
	[[nodiscard]] std::string_view PathAt(std::size_t i) const;
	[[nodiscard]] std::uint32_t TypeAt(std::size_t i) const;

	/// logical_path must already be normalized (use NormalizeRelativePath on relative paths).
	bool ReadAsset(std::string_view logical_path, std::vector<std::uint8_t>& out, std::uint32_t& out_type,
	               std::string& err);

	/// Structural validation + optional LZ4 decompress check per entry. Uses env `TEXR_AES_KEY_HEX` if needed.
	static int ValidateFile(const std::filesystem::path& path, std::string& err);

	/// SHA-256 hex minuscule (64 chars) du InnerFile après déchiffrement outer (aligné manifest `hash_plain`).
	static bool ComputeInnerSha256Hex(const std::filesystem::path& path, std::string& out_hex64, std::string& err);

private:
	std::optional<std::array<std::uint8_t, 32>> decrypt_key_{};

	struct IndexEntry
	{
		std::string path;
		std::uint32_t type{};
		std::uint32_t comp{};
		std::uint32_t csize{};
		std::uint32_t usize{};
		std::uint64_t poff{};
	};

	std::vector<std::uint8_t> inner_;
	std::vector<IndexEntry> index_;

	[[nodiscard]] const IndexEntry* FindEntry(std::string_view logical_path) const;
};

}  // namespace lcdlln::texr
