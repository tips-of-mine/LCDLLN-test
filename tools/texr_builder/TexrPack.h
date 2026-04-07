#pragma once

#include <filesystem>
#include <string>

namespace texr {

struct PackOptions
{
	bool write_meta_json{};
	bool encrypt{};
	/// 64 hex chars; nullptr → `TEXR_AES_KEY_HEX` when encrypt is true
	const char* aes_key_hex{};
	/// Si non vide : conversion PNG → DDS (BC7 sRGB) via texconv avant inclusion ; sinon PNG brut.
	std::filesystem::path texconv_exe{};
};

/// Builds a v1 .texr from a directory tree (LZ4 when smaller; optional AES-256-GCM outer).
int PackDirectory(const std::filesystem::path& input_dir,
                  const std::filesystem::path& output_texr,
                  const PackOptions& options,
                  std::string& error_message);

/// Validates layout + payloads (uses `TEXR_AES_KEY_HEX` if file is encrypted).
int ValidateFile(const std::filesystem::path& texr_path, std::string& error_message);

/// Prints outer header + index summary to stdout.
int InspectFile(const std::filesystem::path& texr_path, std::string& error_message);

}  // namespace texr
