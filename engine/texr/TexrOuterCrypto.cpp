#include "engine/texr/TexrOuterCrypto.h"

#include <openssl/evp.h>
#include <openssl/rand.h>

#include <cctype>
#include <cstring>

namespace lcdlln::texr {
namespace {

bool IsHexDigit(char c)
{
	return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

int HexVal(char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return 10 + (c - 'a');
	if (c >= 'A' && c <= 'F')
		return 10 + (c - 'A');
	return -1;
}

}  // namespace

bool ParseAes256KeyHex(std::string_view hex, std::array<std::uint8_t, 32>& out_key, std::string& err)
{
	err.clear();
	std::string compact;
	compact.reserve(hex.size());
	for (char c : hex)
	{
		if (std::isspace(static_cast<unsigned char>(c)))
			continue;
		if (!IsHexDigit(c))
		{
			err = "AES key hex: invalid character";
			return false;
		}
		compact.push_back(c);
	}
	if (compact.size() != 64)
	{
		err = "AES key hex: expected 64 hex digits (32 bytes)";
		return false;
	}
	for (std::size_t i = 0; i < 32; ++i)
	{
		const int hi = HexVal(compact[i * 2]);
		const int lo = HexVal(compact[i * 2 + 1]);
		if (hi < 0 || lo < 0)
		{
			err = "AES key hex: parse error";
			return false;
		}
		out_key[i] = static_cast<std::uint8_t>((hi << 4) | lo);
	}
	return true;
}

bool Aes256GcmEncrypt(const std::uint8_t* plaintext,
                      std::size_t plaintext_len,
                      const std::array<std::uint8_t, 32>& key,
                      std::vector<std::uint8_t>& iv_out,
                      std::vector<std::uint8_t>& ciphertext_out,
                      std::array<std::uint8_t, 16>& tag_out,
                      std::string& err)
{
	err.clear();
	iv_out.resize(12);
	if (RAND_bytes(iv_out.data(), 12) != 1)
	{
		err = "RAND_bytes failed for GCM IV";
		return false;
	}
	ciphertext_out.resize(plaintext_len + EVP_MAX_BLOCK_LENGTH);
	tag_out.fill(0);

	EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
	if (!ctx)
	{
		err = "EVP_CIPHER_CTX_new failed";
		return false;
	}
	int len = 0;
	int len_final = 0;
	bool ok = false;
	do
	{
		if (EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1)
			break;
		if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, 12, nullptr) != 1)
			break;
		if (EVP_EncryptInit_ex(ctx, nullptr, nullptr, key.data(), iv_out.data()) != 1)
			break;
		if (EVP_EncryptUpdate(ctx, ciphertext_out.data(), &len, plaintext, static_cast<int>(plaintext_len)) != 1)
			break;
		if (EVP_EncryptFinal_ex(ctx, ciphertext_out.data() + len, &len_final) != 1)
			break;
		ciphertext_out.resize(static_cast<std::size_t>(len + len_final));
		if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, 16, tag_out.data()) != 1)
			break;
		ok = true;
	} while (false);
	EVP_CIPHER_CTX_free(ctx);
	if (!ok)
	{
		err = "AES-256-GCM encrypt failed";
		return false;
	}
	return true;
}

bool Aes256GcmDecrypt(const std::uint8_t* iv,
                      std::size_t iv_len,
                      const std::uint8_t* ciphertext,
                      std::size_t ciphertext_len,
                      const std::array<std::uint8_t, 16>& tag,
                      const std::array<std::uint8_t, 32>& key,
                      std::vector<std::uint8_t>& plaintext_out,
                      std::string& err)
{
	err.clear();
	plaintext_out.resize(ciphertext_len + EVP_MAX_BLOCK_LENGTH);

	EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
	if (!ctx)
	{
		err = "EVP_CIPHER_CTX_new failed";
		return false;
	}
	int len = 0;
	int len_final = 0;
	bool ok = false;
	do
	{
		if (EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1)
			break;
		if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, static_cast<int>(iv_len), nullptr) != 1)
			break;
		if (EVP_DecryptInit_ex(ctx, nullptr, nullptr, key.data(), iv) != 1)
			break;
		if (EVP_DecryptUpdate(ctx, plaintext_out.data(), &len, ciphertext, static_cast<int>(ciphertext_len)) != 1)
			break;
		if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, 16, const_cast<std::uint8_t*>(tag.data())) != 1)
			break;
		if (EVP_DecryptFinal_ex(ctx, plaintext_out.data() + len, &len_final) != 1)
			break;
		plaintext_out.resize(static_cast<std::size_t>(len + len_final));
		ok = true;
	} while (false);
	EVP_CIPHER_CTX_free(ctx);
	if (!ok)
	{
		err = "AES-256-GCM decrypt failed (wrong key or corrupt data)";
		plaintext_out.clear();
		return false;
	}
	return true;
}

}  // namespace lcdlln::texr
