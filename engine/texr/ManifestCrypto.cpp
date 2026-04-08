#include "engine/texr/ManifestCrypto.h"

#include <openssl/bio.h>
#include <openssl/evp.h>

#include <fstream>

namespace lcdlln::manifest {

bool Base64Decode(std::string_view b64, std::vector<std::uint8_t>& out, std::string& err)
{
	out.clear();
	err.clear();
	if (b64.empty())
	{
		err = "base64 empty";
		return false;
	}
	BIO* bio = BIO_new_mem_buf(b64.data(), static_cast<int>(b64.size()));
	if (!bio)
	{
		err = "BIO_new_mem_buf failed";
		return false;
	}
	BIO* b64f = BIO_new(BIO_f_base64());
	if (!b64f)
	{
		BIO_free(bio);
		err = "BIO_f_base64 failed";
		return false;
	}
	BIO_set_flags(b64f, BIO_FLAGS_BASE64_NO_NL);
	bio = BIO_push(b64f, bio);
	char buf[4096];
	int n = 0;
	while ((n = BIO_read(bio, buf, static_cast<int>(sizeof(buf)))) > 0)
	{
		out.insert(out.end(), buf, buf + n);
	}
	BIO_free_all(bio);
	if (n < 0)
	{
		err = "base64 decode read error";
		out.clear();
		return false;
	}
	return true;
}

bool HexDecode64(std::string_view hex64, std::array<std::uint8_t, 32>& out, std::string& err)
{
	err.clear();
	if (hex64.size() != 64)
	{
		err = "expected 64 hex chars for 32-byte key";
		return false;
	}
	auto hexVal = [](char c) -> int {
		if (c >= '0' && c <= '9')
			return c - '0';
		if (c >= 'a' && c <= 'f')
			return 10 + (c - 'a');
		if (c >= 'A' && c <= 'F')
			return 10 + (c - 'A');
		return -1;
	};
	for (std::size_t i = 0; i < 32; ++i)
	{
		const int hi = hexVal(hex64[i * 2]);
		const int lo = hexVal(hex64[i * 2 + 1]);
		if (hi < 0 || lo < 0)
		{
			err = "invalid hex digit";
			return false;
		}
		out[i] = static_cast<std::uint8_t>((hi << 4) | lo);
	}
	return true;
}

bool Ed25519Verify(std::span<const std::uint8_t, 32> pubkey, std::string_view message_utf8,
                   std::span<const std::uint8_t, 64> signature, std::string& err)
{
	err.clear();
	EVP_PKEY* pkey = EVP_PKEY_new_raw_public_key(EVP_PKEY_ED25519, nullptr, pubkey.data(), 32);
	if (!pkey)
	{
		err = "EVP_PKEY_new_raw_public_key failed";
		return false;
	}
	EVP_MD_CTX* mdctx = EVP_MD_CTX_new();
	if (!mdctx)
	{
		err = "EVP_MD_CTX_new failed";
		EVP_PKEY_free(pkey);
		return false;
	}
	if (EVP_DigestVerifyInit(mdctx, nullptr, nullptr, nullptr, pkey) != 1)
	{
		err = "EVP_DigestVerifyInit failed";
		EVP_MD_CTX_free(mdctx);
		EVP_PKEY_free(pkey);
		return false;
	}
	const auto* mptr = reinterpret_cast<const unsigned char*>(message_utf8.data());
	const int v =
	    EVP_DigestVerify(mdctx, signature.data(), 64, mptr, message_utf8.size());
	EVP_MD_CTX_free(mdctx);
	EVP_PKEY_free(pkey);
	if (v != 1)
	{
		err = "Ed25519 verify failed";
		return false;
	}
	return true;
}

bool Sha256BufferHexLower(std::span<const std::uint8_t> data, std::string& out_hex64, std::string& err)
{
	err.clear();
	out_hex64.clear();
	EVP_MD_CTX* ctx = EVP_MD_CTX_new();
	if (!ctx)
	{
		err = "EVP_MD_CTX_new failed";
		return false;
	}
	unsigned char md[EVP_MAX_MD_SIZE];
	unsigned int md_len = 0;
	if (EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr) != 1
	    || EVP_DigestUpdate(ctx, data.data(), data.size()) != 1 || EVP_DigestFinal_ex(ctx, md, &md_len) != 1
	    || md_len != 32)
	{
		EVP_MD_CTX_free(ctx);
		err = "SHA-256 buffer failed";
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

bool Sha256FileHexLower(const std::filesystem::path& path, std::string& out_hex64, std::string& err)
{
	err.clear();
	out_hex64.clear();
	std::ifstream f(path, std::ios::binary);
	if (!f)
	{
		err = "cannot open for sha256: " + path.string();
		return false;
	}
	EVP_MD_CTX* ctx = EVP_MD_CTX_new();
	if (!ctx)
	{
		err = "EVP_MD_CTX_new failed";
		return false;
	}
	if (EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr) != 1)
	{
		EVP_MD_CTX_free(ctx);
		err = "EVP_DigestInit_ex failed";
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
			err = "EVP_DigestUpdate failed";
			return false;
		}
	}
	unsigned char md[EVP_MAX_MD_SIZE];
	unsigned int md_len = 0;
	if (EVP_DigestFinal_ex(ctx, md, &md_len) != 1 || md_len != 32)
	{
		EVP_MD_CTX_free(ctx);
		err = "EVP_DigestFinal_ex failed";
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

bool FileByteSize(const std::filesystem::path& path, std::uint64_t& out_size, std::string& err)
{
	err.clear();
	std::error_code ec;
	const auto sz = std::filesystem::file_size(path, ec);
	if (ec)
	{
		err = ec.message();
		return false;
	}
	out_size = static_cast<std::uint64_t>(sz);
	return true;
}

}  // namespace lcdlln::manifest
