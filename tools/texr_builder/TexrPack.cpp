#include "TexrPack.h"

#include "engine/texr/TexrOuterCrypto.h"
#include "engine/texr/TexrPath.h"
#include "engine/texr/TexrReader.h"

#include <lz4.h>

#include <array>
#include <cstdio>
#include <cstdlib>
#include <openssl/evp.h>
#include <openssl/rand.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace texr {
namespace {

constexpr std::array<std::uint8_t, 8> kMagic{{'T', 'E', 'X', 'R', 0, 0, 0, 0}};
constexpr std::uint32_t kOuterVersion = 1;
constexpr std::uint32_t kFormatMajor = 1;
constexpr std::uint32_t kFormatMinor = 0;
constexpr std::size_t kOuterHeaderSize = 64;
constexpr std::size_t kInnerHeaderSize = 128;
constexpr std::size_t kIndexRecordSize = 40;
constexpr std::size_t kPayloadAlign = 64;

// texr_asset_types_v1.md
constexpr std::uint32_t kTypeTextureDds = 1;
constexpr std::uint32_t kTypeTexturePng = 2;
constexpr std::uint32_t kTypeFont = 3;
constexpr std::uint32_t kTypeJson = 4;
constexpr std::uint32_t kTypeStylesheet = 5;
constexpr std::uint32_t kTypeShader = 6;
constexpr std::uint32_t kTypeText = 7;
constexpr std::uint32_t kTypeAudio = 8;
constexpr std::uint32_t kTypeBinary = 9;

constexpr std::uint32_t kCompressionNone = 0;
constexpr std::uint32_t kCompressionLz4 = 1;

inline std::uint64_t Align64(std::uint64_t x)
{
	return (x + (kPayloadAlign - 1)) & ~std::uint64_t{kPayloadAlign - 1};
}

inline void WriteU32LE(std::vector<std::uint8_t>& out, std::uint32_t v)
{
	out.push_back(static_cast<std::uint8_t>(v & 0xFF));
	out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFF));
	out.push_back(static_cast<std::uint8_t>((v >> 16) & 0xFF));
	out.push_back(static_cast<std::uint8_t>((v >> 24) & 0xFF));
}

inline void WriteU64LE(std::vector<std::uint8_t>& out, std::uint64_t v)
{
	for (int i = 0; i < 8; ++i)
		out.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xFF));
}

void WriteMagic(std::vector<std::uint8_t>& out)
{
	out.insert(out.end(), kMagic.begin(), kMagic.end());
}

bool RunTexconv(const fs::path& texconv_exe, const fs::path& png_path, const fs::path& out_dir, std::string& err)
{
	err.clear();
	const std::string cmd = "\"" + texconv_exe.string() + "\" -f BC7_UNORM_SRGB -nologo -y -o \"" + out_dir.string()
	                        + "\" \"" + png_path.string() + "\"";
	const int rc = std::system(cmd.c_str());
	if (rc != 0)
	{
		err = "texconv failed (exit " + std::to_string(rc) + ")";
		return false;
	}
	return true;
}

std::uint32_t AssetTypeForPath(const fs::path& p)
{
	const std::string ext = p.extension().string();
	if (ext == ".dds")
		return kTypeTextureDds;
	if (ext == ".png")
		return kTypeTexturePng;
	if (ext == ".ttf" || ext == ".otf" || ext == ".ttc")
		return kTypeFont;
	if (ext == ".json")
		return kTypeJson;
	if (ext == ".qss")
		return kTypeStylesheet;
	if (ext == ".vert" || ext == ".frag" || ext == ".comp")
		return kTypeShader;
	if (ext == ".txt")
		return kTypeText;
	if (ext == ".ogg" || ext == ".wav")
		return kTypeAudio;
	return kTypeBinary;
}

struct FileEntry
{
	std::string logical_path;
	std::vector<std::uint8_t> bytes;
	std::uint32_t asset_type{};
	std::uint32_t store_comp{kCompressionNone};
	std::uint32_t store_csize{};
	std::vector<std::uint8_t> store_bytes;
};

bool ReadWholeFile(const fs::path& path, std::vector<std::uint8_t>& out, std::string& err)
{
	std::ifstream f(path, std::ios::binary | std::ios::ate);
	if (!f)
	{
		err = "cannot open file: " + path.string();
		return false;
	}
	const auto sz = static_cast<std::size_t>(f.tellg());
	if (sz > 0x7FFFFFFFu)
	{
		err = "file too large: " + path.string();
		return false;
	}
	f.seekg(0);
	out.resize(sz);
	if (sz > 0 && !f.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(sz)))
	{
		err = "read failed: " + path.string();
		return false;
	}
	return true;
}

bool Sha256Hex(const std::vector<std::uint8_t>& data, std::string& out_hex)
{
	unsigned char md[EVP_MAX_MD_SIZE];
	unsigned int md_len = 0;
	EVP_MD_CTX* ctx = EVP_MD_CTX_new();
	if (!ctx)
		return false;
	if (EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr) != 1
	    || EVP_DigestUpdate(ctx, data.data(), data.size()) != 1
	    || EVP_DigestFinal_ex(ctx, md, &md_len) != 1)
	{
		EVP_MD_CTX_free(ctx);
		return false;
	}
	EVP_MD_CTX_free(ctx);
	static const char* kHex = "0123456789abcdef";
	out_hex.resize(md_len * 2);
	for (unsigned i = 0; i < md_len; ++i)
	{
		out_hex[i * 2] = kHex[(md[i] >> 4) & 0xF];
		out_hex[i * 2 + 1] = kHex[md[i] & 0xF];
	}
	return true;
}

bool Sha256HexFile(const fs::path& path, std::string& out_hex, std::string& err)
{
	std::ifstream f(path, std::ios::binary);
	if (!f)
	{
		err = "cannot open for hash: " + path.string();
		return false;
	}
	EVP_MD_CTX* ctx = EVP_MD_CTX_new();
	if (!ctx)
		return false;
	if (EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr) != 1)
	{
		EVP_MD_CTX_free(ctx);
		return false;
	}
	std::array<char, 65536> buf{};
	while (f)
	{
		f.read(buf.data(), static_cast<std::streamsize>(buf.size()));
		const auto n = f.gcount();
		if (n > 0 && EVP_DigestUpdate(ctx, buf.data(), static_cast<std::size_t>(n)) != 1)
		{
			EVP_MD_CTX_free(ctx);
			return false;
		}
	}
	unsigned char md[EVP_MAX_MD_SIZE];
	unsigned int md_len = 0;
	if (EVP_DigestFinal_ex(ctx, md, &md_len) != 1)
	{
		EVP_MD_CTX_free(ctx);
		return false;
	}
	EVP_MD_CTX_free(ctx);
	static const char* kHex = "0123456789abcdef";
	out_hex.resize(md_len * 2);
	for (unsigned i = 0; i < md_len; ++i)
	{
		out_hex[i * 2] = kHex[(md[i] >> 4) & 0xF];
		out_hex[i * 2 + 1] = kHex[md[i] & 0xF];
	}
	return true;
}

}  // namespace

int PackDirectory(const fs::path& input_dir, const fs::path& output_texr, const PackOptions& options,
                  std::string& error_message)
{
	error_message.clear();
	std::error_code ec;
	const fs::path root = fs::weakly_canonical(input_dir, ec);
	if (ec || !fs::is_directory(root))
	{
		error_message = "input is not a directory: " + input_dir.string();
		return 1;
	}

	fs::path texconv_ws;
	if (!options.texconv_exe.empty())
	{
		texconv_ws = root / ".texr_texconv_tmp";
		std::error_code dec;
		fs::remove_all(texconv_ws, dec);
		fs::create_directories(texconv_ws, dec);
		if (dec)
		{
			error_message = "cannot create texconv workspace: " + texconv_ws.string();
			return 1;
		}
	}
	struct RemoveDirGuard
	{
		fs::path dir;
		~RemoveDirGuard()
		{
			if (!dir.empty())
			{
				std::error_code rm;
				fs::remove_all(dir, rm);
			}
		}
	} remove_texconv{texconv_ws};

	std::vector<FileEntry> files;
	for (fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied), end;
	     it != end; ++it)
	{
		ec.clear();
		if (!it->is_regular_file(ec) || ec)
			continue;
		const fs::path p = it->path();
		std::error_code sec;
		if (fs::is_symlink(p, sec) && !sec)
		{
			error_message = "symlink not allowed: " + p.string();
			return 2;
		}
		fs::path rel = fs::relative(p, root, ec);
		if (ec)
		{
			error_message = "relative path failed: " + p.string();
			return 2;
		}
		const std::string logical = lcdlln::texr::NormalizeRelativePath(rel);
		if (logical.empty())
			continue;
		if (logical.size() > 1024)
		{
			error_message = "path too long (>1024): " + logical;
			return 2;
		}
		if (p.extension() == ".png" && !options.texconv_exe.empty())
		{
			std::string conv_err;
			if (RunTexconv(options.texconv_exe, p, texconv_ws, conv_err))
			{
				const fs::path dds_path = texconv_ws / (p.stem().string() + ".dds");
				std::error_code ex;
				if (fs::is_regular_file(dds_path, ex))
				{
					FileEntry e;
					const fs::path rel_dds = rel.parent_path() / (p.stem().string() + ".dds");
					e.logical_path = lcdlln::texr::NormalizeRelativePath(rel_dds);
					e.asset_type = kTypeTextureDds;
					if (!ReadWholeFile(dds_path, e.bytes, error_message))
					{
						return 2;
					}
					files.push_back(std::move(e));
					continue;
				}
			}
		}
		FileEntry e;
		e.logical_path = logical;
		e.asset_type = AssetTypeForPath(p);
		if (!ReadWholeFile(p, e.bytes, error_message))
			return 2;
		files.push_back(std::move(e));
	}

	std::sort(files.begin(), files.end(),
	          [](const FileEntry& a, const FileEntry& b) { return a.logical_path < b.logical_path; });

	for (std::size_t i = 1; i < files.size(); ++i)
	{
		if (files[i].logical_path == files[i - 1].logical_path)
		{
			error_message = "duplicate logical path: " + files[i].logical_path;
			return 2;
		}
	}

	for (auto& f : files)
	{
		const auto psz = static_cast<std::uint32_t>(f.bytes.size());
		f.store_comp = kCompressionNone;
		f.store_csize = psz;
		f.store_bytes = f.bytes;
		if (psz == 0)
			continue;
		const int bound = LZ4_compressBound(static_cast<int>(psz));
		if (bound <= 0)
			continue;
		std::vector<std::uint8_t> tmp(static_cast<std::size_t>(bound));
		const int clen = LZ4_compress_default(reinterpret_cast<const char*>(f.bytes.data()),
		                                      reinterpret_cast<char*>(tmp.data()), static_cast<int>(psz), bound);
		if (clen > 0 && static_cast<std::uint32_t>(clen) < psz)
		{
			f.store_comp = kCompressionLz4;
			f.store_csize = static_cast<std::uint32_t>(clen);
			f.store_bytes.assign(tmp.begin(), tmp.begin() + clen);
		}
	}

	const std::uint32_t entry_count = static_cast<std::uint32_t>(files.size());
	const std::uint64_t index_offset = kInnerHeaderSize;  // 128, aligned 64

	// string pool offsets (relative to pool start)
	std::vector<std::uint32_t> path_rel_off;
	path_rel_off.reserve(files.size());
	std::uint32_t pool_size = 0;
	for (const auto& f : files)
	{
		path_rel_off.push_back(pool_size);
		const auto plen = static_cast<std::uint32_t>(f.logical_path.size());
		if (plen == 0 || plen > 1024)
		{
			error_message = "invalid path length";
			return 2;
		}
		pool_size += plen;
	}

	const std::uint64_t strings_base = index_offset + 8 + static_cast<std::uint64_t>(entry_count) * kIndexRecordSize;
	const std::uint64_t index_end = strings_base + pool_size;
	const std::uint64_t index_byte_size = index_end - index_offset;
	const std::uint64_t data_offset = Align64(index_end);

	// Build payloads region size
	std::uint64_t data_cursor = data_offset;
	std::vector<std::uint64_t> payload_offsets;
	payload_offsets.reserve(files.size());
	for (const auto& f : files)
	{
		payload_offsets.push_back(data_cursor);
		data_cursor += f.store_csize;
		data_cursor = Align64(data_cursor);
	}
	const std::uint64_t data_size = data_cursor - data_offset;
	const std::uint64_t inner_size = data_cursor;

	if (inner_size > 0x7FFFFFFFULL)
	{
		error_message = "inner file exceeds 2 GiB limit";
		return 2;
	}

	std::vector<std::uint8_t> inner;
	inner.reserve(static_cast<std::size_t>(inner_size));
	inner.resize(static_cast<std::size_t>(inner_size), 0);

	// InnerHeader
	std::size_t o = 0;
	std::memcpy(inner.data() + o, kMagic.data(), 8);
	o += 8;
	auto put32 = [&inner](std::size_t& off, std::uint32_t v) {
		inner[off++] = static_cast<std::uint8_t>(v & 0xFF);
		inner[off++] = static_cast<std::uint8_t>((v >> 8) & 0xFF);
		inner[off++] = static_cast<std::uint8_t>((v >> 16) & 0xFF);
		inner[off++] = static_cast<std::uint8_t>((v >> 24) & 0xFF);
	};
	auto put64 = [&inner](std::size_t& off, std::uint64_t v) {
		for (int i = 0; i < 8; ++i)
			inner[off++] = static_cast<std::uint8_t>((v >> (8 * i)) & 0xFF);
	};
	put32(o, kFormatMajor);
	put32(o, kFormatMinor);
	put32(o, 0);  // inner_flags
	put32(o, entry_count);
	put64(o, index_offset);
	put64(o, index_byte_size);  // exact index section size (excl. padding before data)
	put64(o, data_offset);
	put64(o, data_size);
	if (RAND_bytes(inner.data() + o, 16) != 1)
	{
		error_message = "RAND_bytes failed for package_id";
		return 2;
	}
	o += 16;
	while (o < kInnerHeaderSize)
		inner[o++] = 0;

	// Index header + records + pool
	std::size_t io = static_cast<std::size_t>(index_offset);
	put32(io, entry_count);
	put32(io, 0);
	for (std::uint32_t i = 0; i < entry_count; ++i)
	{
		const auto& f = files[i];
		const std::uint32_t plen = static_cast<std::uint32_t>(f.logical_path.size());
		const std::uint32_t psz = static_cast<std::uint32_t>(f.bytes.size());
		put32(io, path_rel_off[i]);
		inner[io++] = static_cast<std::uint8_t>(plen & 0xFF);
		inner[io++] = static_cast<std::uint8_t>((plen >> 8) & 0xFF);
		inner[io++] = 0;
		inner[io++] = 0;
		put32(io, f.asset_type);
		put32(io, f.store_comp);
		put32(io, f.store_csize);
		put32(io, psz);
		put64(io, payload_offsets[i]);
		for (int z = 0; z < 8; ++z)
			inner[io++] = 0;
	}
	if (io != static_cast<std::size_t>(strings_base))
	{
		error_message = "internal: index layout mismatch";
		return 2;
	}
	for (std::uint32_t i = 0; i < entry_count; ++i)
	{
		const auto& f = files[i];
		std::memcpy(inner.data() + io, f.logical_path.data(), f.logical_path.size());
		io += f.logical_path.size();
	}
	if (io != static_cast<std::size_t>(index_end))
	{
		error_message = "internal: string pool size mismatch";
		return 2;
	}
	while (io < static_cast<std::size_t>(data_offset))
		inner[io++] = 0;

	for (std::uint32_t i = 0; i < entry_count; ++i)
	{
		const auto& f = files[i];
		const std::uint64_t off = payload_offsets[i];
		std::memcpy(inner.data() + off, f.store_bytes.data(), f.store_bytes.size());
	}

	// Outer file (plaintext inner or AES-256-GCM)
	std::vector<std::uint8_t> outer;
	if (!options.encrypt)
	{
		outer.reserve(kOuterHeaderSize + inner.size());
		WriteMagic(outer);
		WriteU32LE(outer, kOuterVersion);
		WriteU32LE(outer, 0);
		WriteU64LE(outer, inner.size());
		WriteU64LE(outer, 0);
		for (int i = 0; i < 32; ++i)
			outer.push_back(0);
		outer.insert(outer.end(), inner.begin(), inner.end());
	}
	else
	{
		const char* hex = options.aes_key_hex;
		if (hex == nullptr || hex[0] == '\0')
			hex = std::getenv("TEXR_AES_KEY_HEX");
		if (hex == nullptr || hex[0] == '\0')
		{
			error_message = "encrypt: use --key-hex or set TEXR_AES_KEY_HEX";
			return 2;
		}
		std::array<std::uint8_t, 32> aes_key{};
		if (!lcdlln::texr::ParseAes256KeyHex(hex, aes_key, error_message))
			return 2;
		std::vector<std::uint8_t> iv;
		std::vector<std::uint8_t> ciphertext;
		std::array<std::uint8_t, 16> tag{};
		if (!lcdlln::texr::Aes256GcmEncrypt(inner.data(), inner.size(), aes_key, iv, ciphertext, tag,
		                                    error_message))
			return 3;
		outer.reserve(kOuterHeaderSize + 12 + ciphertext.size() + 16);
		WriteMagic(outer);
		WriteU32LE(outer, kOuterVersion);
		WriteU32LE(outer, 1u);
		WriteU64LE(outer, inner.size());
		WriteU64LE(outer, ciphertext.size());
		for (int i = 0; i < 32; ++i)
			outer.push_back(0);
		outer.insert(outer.end(), iv.begin(), iv.end());
		outer.insert(outer.end(), ciphertext.begin(), ciphertext.end());
		outer.insert(outer.end(), tag.begin(), tag.end());
	}

	{
		std::ofstream outf(output_texr, std::ios::binary | std::ios::trunc);
		if (!outf.write(reinterpret_cast<const char*>(outer.data()),
		              static_cast<std::streamsize>(outer.size())))
		{
			error_message = "failed to write output: " + output_texr.string();
			return 3;
		}
	}

	if (options.write_meta_json)
	{
		std::string hash_plain;
		std::string hash_cipher;
		if (!Sha256Hex(inner, hash_plain))
		{
			error_message = "SHA256 inner failed";
			return 3;
		}
		std::string herr;
		if (!Sha256HexFile(output_texr, hash_cipher, herr))
		{
			error_message = herr;
			return 3;
		}
		const fs::path meta_path = output_texr.string() + ".meta.json";
		std::ofstream mf(meta_path, std::ios::trunc);
		if (!mf)
		{
			error_message = "cannot write meta: " + meta_path.string();
			return 3;
		}
		mf << "{\n"
		   << "  \"entry_count\": " << entry_count << ",\n"
		   << "  \"inner_size\": " << inner.size() << ",\n"
		   << "  \"outer_size\": " << outer.size() << ",\n"
		   << "  \"hash_plain\": \"" << hash_plain << "\",\n"
		   << "  \"hash_cipher\": \"" << hash_cipher << "\"\n"
		   << "}\n";
	}

	return 0;
}

int ValidateFile(const fs::path& texr_path, std::string& error_message)
{
	return lcdlln::texr::TexrReader::ValidateFile(texr_path, error_message);
}

int InspectFile(const fs::path& texr_path, std::string& error_message)
{
	error_message.clear();
	lcdlln::texr::TexrReader reader;
	if (const char* env = std::getenv("TEXR_AES_KEY_HEX"))
	{
		if (env[0] != '\0')
		{
			std::string kerr;
			if (!reader.SetAes256KeyFromHex(env, kerr))
				std::fprintf(stderr, "texr inspect: TEXR_AES_KEY_HEX ignored: %s\n", kerr.c_str());
		}
	}
	if (!reader.Open(texr_path, error_message))
		return 1;
	std::printf("package: %s\n", texr_path.string().c_str());
	std::printf("entries: %zu\n", reader.EntryCount());
	const std::size_t n = reader.EntryCount();
	const std::size_t max_list = (n < 32) ? n : 32;
	for (std::size_t i = 0; i < max_list; ++i)
		std::printf("  [%zu] %s  type=%u\n", i, std::string(reader.PathAt(i)).c_str(), reader.TypeAt(i));
	if (n > max_list)
		std::printf("  ... (%zu more)\n", n - max_list);
	return 0;
}

}  // namespace texr
