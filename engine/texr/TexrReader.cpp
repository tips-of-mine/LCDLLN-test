#include "engine/texr/TexrReader.h"

#include "engine/texr/TexrOuterCrypto.h"

#include <lz4.h>
#include <openssl/evp.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <optional>

namespace lcdlln::texr {
namespace {

constexpr std::array<std::uint8_t, 8> kMagic{{'T', 'E', 'X', 'R', 0, 0, 0, 0}};
constexpr std::uint32_t kOuterVersion = 1;
constexpr std::uint32_t kFormatMajor = 1;
constexpr std::size_t kOuterHeaderSize = 64;
constexpr std::size_t kInnerHeaderSize = 128;
constexpr std::size_t kIndexRecordSize = 40;
constexpr std::size_t kPayloadAlign = 64;

constexpr std::uint32_t kCompressionNone = 0;
constexpr std::uint32_t kCompressionLz4 = 1;

constexpr std::uint64_t kMaxUncompressedAsset = 0x40000000ull;  // 1 GiB

inline std::uint32_t RdU32(const std::uint8_t* p)
{
	return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8)
	       | (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}

inline std::uint64_t RdU64(const std::uint8_t* p)
{
	std::uint64_t v = 0;
	for (int i = 0; i < 8; ++i)
		v |= static_cast<std::uint64_t>(p[i]) << (8 * i);
	return v;
}

bool VerifyPayload(const std::uint8_t* inner_base, std::uint64_t inner_size, std::uint32_t comp,
                   std::uint32_t csize, std::uint32_t usize, std::uint64_t poff, std::uint64_t data_offset,
                   std::uint64_t data_end, std::string& err)
{
	if (usize > kMaxUncompressedAsset)
	{
		err = "asset uncompressed_size exceeds 1 GiB cap";
		return false;
	}
	if (poff < data_offset || poff + csize > data_end || poff + csize > inner_size)
	{
		err = "payload out of bounds";
		return false;
	}
	if ((poff % kPayloadAlign) != 0)
	{
		err = "payload_offset not aligned";
		return false;
	}
	const std::uint8_t* src = inner_base + poff;
	if (comp == kCompressionNone)
	{
		if (csize != usize)
		{
			err = "none compression: compressed_size != uncompressed_size";
			return false;
		}
		return true;
	}
	if (comp == kCompressionLz4)
	{
		std::vector<std::uint8_t> dst(usize);
		const int r = LZ4_decompress_safe(reinterpret_cast<const char*>(src), reinterpret_cast<char*>(dst.data()),
		                                  static_cast<int>(csize), static_cast<int>(usize));
		if (r != static_cast<int>(usize))
		{
			err = "LZ4_decompress_safe failed or size mismatch";
			return false;
		}
		return true;
	}
	err = "unsupported compression codec";
	return false;
}

struct ParsedEntry
{
	std::string path;
	std::uint32_t type{};
	std::uint32_t comp{};
	std::uint32_t csize{};
	std::uint32_t usize{};
	std::uint64_t poff{};
};

/// Parses inner bytes, fills out_index, optionally verifies every payload.
bool ParseInner(const std::vector<std::uint8_t>& inner, std::vector<ParsedEntry>& out_index, std::string& err,
                bool verify_payloads)
{
	out_index.clear();
	err.clear();
	if (inner.size() < kInnerHeaderSize)
	{
		err = "inner too small";
		return false;
	}
	const std::uint8_t* base = inner.data();
	if (std::memcmp(base, kMagic.data(), 8) != 0)
	{
		err = "bad inner magic";
		return false;
	}
	const std::uint32_t format_major = RdU32(base + 8);
	if (format_major > kFormatMajor)
	{
		err = "format_major newer than reader";
		return false;
	}
	const std::uint32_t entry_count = RdU32(base + 20);
	const std::uint64_t index_offset = RdU64(base + 24);
	const std::uint64_t index_size = RdU64(base + 32);
	const std::uint64_t data_offset = RdU64(base + 40);
	const std::uint64_t data_size = RdU64(base + 48);

	if (index_offset < kInnerHeaderSize)
	{
		err = "index_offset invalid";
		return false;
	}
	if (data_offset < index_offset + index_size)
	{
		err = "data_offset before end of index";
		return false;
	}
	if (data_offset + data_size > inner.size())
	{
		err = "data section past inner end";
		return false;
	}
	if (index_offset + index_size > inner.size())
	{
		err = "index past inner end";
		return false;
	}
	if (index_offset + 8 + static_cast<std::uint64_t>(entry_count) * kIndexRecordSize > inner.size())
	{
		err = "index records out of range";
		return false;
	}
	const std::uint32_t ec_read = RdU32(base + static_cast<std::size_t>(index_offset));
	if (ec_read != entry_count)
	{
		err = "entry_count mismatch";
		return false;
	}

	const std::uint64_t strings_base = index_offset + 8 + static_cast<std::uint64_t>(entry_count) * kIndexRecordSize;
	if (strings_base > index_offset + index_size)
	{
		err = "string pool base out of index";
		return false;
	}

	const std::uint64_t data_end = data_offset + data_size;
	out_index.reserve(entry_count);

	for (std::uint32_t i = 0; i < entry_count; ++i)
	{
		const std::uint64_t rec_off = index_offset + 8 + static_cast<std::uint64_t>(i) * kIndexRecordSize;
		const std::uint8_t* rp = base + static_cast<std::size_t>(rec_off);
		const std::uint32_t path_off = RdU32(rp);
		const std::uint16_t path_len = static_cast<std::uint16_t>(rp[4]) | (static_cast<std::uint16_t>(rp[5]) << 8);
		const std::uint32_t asset_type = RdU32(rp + 8);
		const std::uint32_t compression = RdU32(rp + 12);
		const std::uint32_t compressed_size = RdU32(rp + 16);
		const std::uint32_t uncompressed_size = RdU32(rp + 20);
		const std::uint64_t payload_offset = RdU64(rp + 24);

		if (path_len == 0 || path_len > 1024)
		{
			err = "bad path_len";
			return false;
		}
		if (strings_base + static_cast<std::uint64_t>(path_off) + path_len > index_offset + index_size)
		{
			err = "path string out of index";
			return false;
		}
		if (compression != kCompressionNone && compression != kCompressionLz4)
		{
			err = "unsupported compression codec";
			return false;
		}

		ParsedEntry e;
		e.path.assign(reinterpret_cast<const char*>(base + strings_base + path_off), path_len);
		e.type = asset_type;
		e.comp = compression;
		e.csize = compressed_size;
		e.usize = uncompressed_size;
		e.poff = payload_offset;

		if (verify_payloads && !VerifyPayload(base, inner.size(), compression, compressed_size, uncompressed_size,
		                                      payload_offset, data_offset, data_end, err))
			return false;

		out_index.push_back(std::move(e));
	}

	for (std::size_t i = 1; i < out_index.size(); ++i)
	{
		if (out_index[i - 1].path > out_index[i].path)
		{
			err = "index paths not sorted";
			return false;
		}
	}

	return true;
}

bool LoadOuterToInner(const std::vector<std::uint8_t>& outer, std::vector<std::uint8_t>& inner_out,
                      const std::optional<std::array<std::uint8_t, 32>>& aes_key, std::string& err)
{
	err.clear();
	inner_out.clear();
	if (outer.size() < kOuterHeaderSize)
	{
		err = "file too small";
		return false;
	}
	if (std::memcmp(outer.data(), kMagic.data(), 8) != 0)
	{
		err = "bad outer magic";
		return false;
	}
	if (RdU32(outer.data() + 8) != kOuterVersion)
	{
		err = "unsupported outer_version";
		return false;
	}
	const std::uint32_t outer_flags = RdU32(outer.data() + 12);
	const std::uint64_t inner_len = RdU64(outer.data() + 16);
	const std::uint64_t ciphertext_len = RdU64(outer.data() + 24);

	if ((outer_flags & 1u) == 0)
	{
		if (ciphertext_len != 0)
		{
			err = "expected ciphertext_len 0 for plaintext outer";
			return false;
		}
		if (kOuterHeaderSize + inner_len != outer.size())
		{
			err = "outer size mismatch";
			return false;
		}
		inner_out.assign(outer.begin() + static_cast<std::ptrdiff_t>(kOuterHeaderSize), outer.end());
		return true;
	}

	// Encrypted: header || IV(12) || ciphertext || tag(16)
	if (!aes_key.has_value())
	{
		err = "encrypted package: set TEXR_AES_KEY_HEX or TexrReader::SetAes256KeyFromHex";
		return false;
	}
	const std::uint64_t expected = kOuterHeaderSize + 12 + ciphertext_len + 16;
	if (outer.size() != expected)
	{
		err = "outer file size mismatch for encrypted package";
		return false;
	}
	if (inner_len > 0x7FFFFFFFULL)
	{
		err = "inner_plaintext_len too large";
		return false;
	}
	const std::uint8_t* iv = outer.data() + kOuterHeaderSize;
	const std::uint8_t* ct = iv + 12;
	const std::uint8_t* tagp = ct + ciphertext_len;
	std::array<std::uint8_t, 16> tag{};
	std::memcpy(tag.data(), tagp, 16);
	if (!Aes256GcmDecrypt(iv, 12, ct, static_cast<std::size_t>(ciphertext_len), tag, *aes_key, inner_out, err))
		return false;
	if (inner_out.size() != static_cast<std::size_t>(inner_len))
	{
		err = "decrypted length does not match inner_plaintext_len";
		inner_out.clear();
		return false;
	}
	return true;
}

}  // namespace

bool TexrReader::Open(const std::filesystem::path& path, std::string& err)
{
	Close();
	std::ifstream f(path, std::ios::binary | std::ios::ate);
	if (!f)
	{
		err = "cannot open: " + path.string();
		return false;
	}
	const auto fsz = static_cast<std::uint64_t>(f.tellg());
	if (fsz < kOuterHeaderSize)
	{
		err = "file too small";
		return false;
	}
	f.seekg(0);
	std::vector<std::uint8_t> outer(static_cast<std::size_t>(fsz));
	if (!f.read(reinterpret_cast<char*>(outer.data()), static_cast<std::streamsize>(fsz)))
	{
		err = "read failed";
		return false;
	}

	std::optional<std::array<std::uint8_t, 32>> key;
	if ((RdU32(outer.data() + 12) & 1u) != 0)
	{
		key = decrypt_key_;
		if (!key.has_value())
		{
			const char* env = std::getenv("TEXR_AES_KEY_HEX");
			if (env != nullptr && env[0] != '\0')
			{
				std::array<std::uint8_t, 32> k{};
				std::string kerr;
				if (!ParseAes256KeyHex(env, k, kerr))
				{
					err = kerr;
					return false;
				}
				key = k;
			}
		}
	}

	std::vector<std::uint8_t> inner;
	if (!LoadOuterToInner(outer, inner, key, err))
		return false;
	std::vector<ParsedEntry> parsed;
	if (!ParseInner(inner, parsed, err, true))
		return false;
	inner_ = std::move(inner);
	index_.clear();
	index_.reserve(parsed.size());
	for (auto& p : parsed)
	{
		IndexEntry e;
		e.path = std::move(p.path);
		e.type = p.type;
		e.comp = p.comp;
		e.csize = p.csize;
		e.usize = p.usize;
		e.poff = p.poff;
		index_.push_back(std::move(e));
	}
	return true;
}

void TexrReader::Close()
{
	inner_.clear();
	index_.clear();
}

const TexrReader::IndexEntry* TexrReader::FindEntry(std::string_view logical_path) const
{
	const auto it = std::lower_bound(
	    index_.begin(), index_.end(), logical_path,
	    [](const IndexEntry& e, std::string_view key) { return e.path < key; });
	if (it == index_.end() || it->path != logical_path)
		return nullptr;
	return &*it;
}

bool TexrReader::ReadAsset(const std::string_view logical_path, std::vector<std::uint8_t>& out,
                           std::uint32_t& out_type, std::string& err)
{
	out.clear();
	err.clear();
	if (inner_.empty())
	{
		err = "not open";
		return false;
	}
	const IndexEntry* e = FindEntry(logical_path);
	if (!e)
	{
		err = "asset not found";
		return false;
	}
	if (e->poff + e->csize > inner_.size())
	{
		err = "payload out of bounds";
		return false;
	}
	const std::uint8_t* src = inner_.data() + e->poff;
	out_type = e->type;
	if (e->comp == kCompressionNone)
	{
		if (e->csize != e->usize)
		{
			err = "corrupt entry sizes";
			return false;
		}
		out.assign(src, src + e->csize);
		return true;
	}
	if (e->comp == kCompressionLz4)
	{
		out.resize(e->usize);
		const int r = LZ4_decompress_safe(reinterpret_cast<const char*>(src), reinterpret_cast<char*>(out.data()),
		                                static_cast<int>(e->csize), static_cast<int>(e->usize));
		if (r != static_cast<int>(e->usize))
		{
			out.clear();
			err = "LZ4_decompress_safe failed";
			return false;
		}
		return true;
	}
	err = "unsupported compression";
	return false;
}

int TexrReader::ValidateFile(const std::filesystem::path& path, std::string& err)
{
	err.clear();
	std::ifstream f(path, std::ios::binary | std::ios::ate);
	if (!f)
	{
		err = "cannot open: " + path.string();
		return 1;
	}
	const auto fsz = static_cast<std::uint64_t>(f.tellg());
	if (fsz < kOuterHeaderSize)
	{
		err = "file too small";
		return 1;
	}
	f.seekg(0);
	std::vector<std::uint8_t> outer(static_cast<std::size_t>(fsz));
	if (!f.read(reinterpret_cast<char*>(outer.data()), static_cast<std::streamsize>(fsz)))
	{
		err = "read failed";
		return 1;
	}
	std::optional<std::array<std::uint8_t, 32>> key;
	if ((RdU32(outer.data() + 12) & 1u) != 0)
	{
		const char* env = std::getenv("TEXR_AES_KEY_HEX");
		if (env != nullptr && env[0] != '\0')
		{
			std::array<std::uint8_t, 32> k{};
			std::string kerr;
			if (!ParseAes256KeyHex(env, k, kerr))
			{
				err = kerr;
				return 1;
			}
			key = k;
		}
	}

	std::vector<std::uint8_t> inner;
	if (!LoadOuterToInner(outer, inner, key, err))
		return 1;
	std::vector<ParsedEntry> parsed;
	if (!ParseInner(inner, parsed, err, true))
		return 1;
	return 0;
}

bool Sha256BytesHexLower(const std::vector<std::uint8_t>& data, std::string& out_hex64)
{
	out_hex64.clear();
	EVP_MD_CTX* ctx = EVP_MD_CTX_new();
	if (!ctx)
	{
		return false;
	}
	unsigned char md[EVP_MAX_MD_SIZE];
	unsigned int md_len = 0;
	if (EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr) != 1 || EVP_DigestUpdate(ctx, data.data(), data.size()) != 1
	    || EVP_DigestFinal_ex(ctx, md, &md_len) != 1 || md_len != 32)
	{
		EVP_MD_CTX_free(ctx);
		return false;
	}
	EVP_MD_CTX_free(ctx);
	static const char* kHex = "0123456789abcdef";
	out_hex64.resize(64);
	for (unsigned i = 0; i < 32; ++i)
	{
		out_hex64[i * 2] = kHex[(md[i] >> 4) & 0xF];
		out_hex64[i * 2 + 1] = kHex[md[i] & 0xF];
	}
	return true;
}

bool TexrReader::ComputeInnerSha256Hex(const std::filesystem::path& path, std::string& out_hex64, std::string& err)
{
	err.clear();
	out_hex64.clear();
	std::ifstream f(path, std::ios::binary | std::ios::ate);
	if (!f)
	{
		err = "cannot open: " + path.string();
		return false;
	}
	const auto fsz = static_cast<std::uint64_t>(f.tellg());
	if (fsz < kOuterHeaderSize)
	{
		err = "file too small";
		return false;
	}
	f.seekg(0);
	std::vector<std::uint8_t> outer(static_cast<std::size_t>(fsz));
	if (!f.read(reinterpret_cast<char*>(outer.data()), static_cast<std::streamsize>(fsz)))
	{
		err = "read failed";
		return false;
	}
	std::optional<std::array<std::uint8_t, 32>> key;
	if ((RdU32(outer.data() + 12) & 1u) != 0)
	{
		const char* env = std::getenv("TEXR_AES_KEY_HEX");
		if (env != nullptr && env[0] != '\0')
		{
			std::array<std::uint8_t, 32> k{};
			std::string kerr;
			if (!ParseAes256KeyHex(env, k, kerr))
			{
				err = kerr;
				return false;
			}
			key = k;
		}
	}
	std::vector<std::uint8_t> inner;
	if (!LoadOuterToInner(outer, inner, key, err))
	{
		return false;
	}
	if (!Sha256BytesHexLower(inner, out_hex64))
	{
		err = "SHA-256 inner failed";
		return false;
	}
	return true;
}

bool TexrReader::SetAes256KeyFromHex(const std::string_view hex64_or_empty, std::string& err)
{
	err.clear();
	if (hex64_or_empty.empty())
	{
		decrypt_key_.reset();
		return true;
	}
	std::array<std::uint8_t, 32> k{};
	if (!ParseAes256KeyHex(hex64_or_empty, k, err))
	{
		decrypt_key_.reset();
		return false;
	}
	decrypt_key_ = k;
	return true;
}

std::string_view TexrReader::PathAt(const std::size_t i) const
{
	if (i >= index_.size())
		return {};
	return index_[i].path;
}

std::uint32_t TexrReader::TypeAt(const std::size_t i) const
{
	if (i >= index_.size())
		return 0;
	return index_[i].type;
}

}  // namespace lcdlln::texr
