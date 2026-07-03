/// @file ShardWireAuth.cpp
/// @brief Implémentation de l'authentification HMAC-SHA256 shard↔master (audit F3).

#include "src/shared/network/ShardWireAuth.h"

#include <openssl/hmac.h>
#include <openssl/evp.h>
#include <openssl/crypto.h>

#include <cstring>

namespace engine::network
{
	namespace
	{
		// Calcule HMAC-SHA256(secret, body) dans outTag (doit pointer vers au moins
		// kShardAuthTagSize octets). Renvoie false si OpenSSL échoue ou si la taille
		// produite ne correspond pas à kShardAuthTagSize (SHA-256 = 32 octets, ne
		// devrait jamais arriver, mais on ne fait pas confiance implicitement à l'ABI).
		bool ComputeTag(std::string_view secret, const uint8_t* body, size_t bodySize, uint8_t* outTag)
		{
			unsigned int len = 0;
			unsigned char* r = HMAC(EVP_sha256(), secret.data(), static_cast<int>(secret.size()),
				body, bodySize, outTag, &len);
			return r != nullptr && len == kShardAuthTagSize;
		}
	}

	std::vector<uint8_t> WrapShardAuth(std::string_view secret, const std::vector<uint8_t>& body)
	{
		if (secret.empty())
			return {};
		std::vector<uint8_t> out(kShardAuthTagSize + body.size(), 0u);
		if (!ComputeTag(secret, body.data(), body.size(), out.data()))
			return {};
		std::memcpy(out.data() + kShardAuthTagSize, body.data(), body.size());
		return out;
	}

	std::optional<std::pair<const uint8_t*, size_t>> UnwrapShardAuth(
		std::string_view secret, const uint8_t* payload, size_t size)
	{
		if (secret.empty() || payload == nullptr || size < kShardAuthTagSize)
			return std::nullopt;
		const uint8_t* body = payload + kShardAuthTagSize;
		const size_t bodySize = size - kShardAuthTagSize;
		uint8_t expected[kShardAuthTagSize];
		if (!ComputeTag(secret, body, bodySize, expected))
			return std::nullopt;
		// Comparaison à temps constant (corrige aussi F27 pour ce chemin).
		if (CRYPTO_memcmp(expected, payload, kShardAuthTagSize) != 0)
			return std::nullopt;
		return std::make_pair(body, bodySize);
	}
}
