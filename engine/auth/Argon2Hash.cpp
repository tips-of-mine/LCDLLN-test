#include "engine/auth/Argon2Hash.h"
#include "engine/core/Log.h"
#include <argon2.h>
#include <openssl/rand.h>
#include <cstring>
#include <string>

namespace engine::auth
{
	namespace
	{
		constexpr std::size_t kEncodedHashMaxLen = 512;
	}

	std::string Hash(std::string_view secret, std::string_view salt, const Argon2Params& params)
	{
		char encoded[kEncodedHashMaxLen];
		const int rc = argon2id_hash_encoded(
			params.time_cost,
			params.memory_kib,
			params.parallelism,
			secret.data(),
			secret.size(),
			salt.data(),
			salt.size(),
			params.hash_len,
			encoded,
			sizeof(encoded)
		);
		if (rc != ARGON2_OK)
		{
			LOG_ERROR(Auth, "[Argon2Hash] Hash FAILED: {}", argon2_error_message(rc));
			return {};
		}
		LOG_DEBUG(Auth, "[Argon2Hash] Hash OK (params: t={}, m={} KiB, p={})", params.time_cost, params.memory_kib, params.parallelism);
		return std::string(encoded);
	}

	std::string Hash(std::string_view secret, const std::vector<std::uint8_t>& salt, const Argon2Params& params)
	{
		if (salt.empty())
		{
			LOG_WARN(Auth, "[Argon2Hash] Hash called with empty salt");
			return {};
		}
		return Hash(secret, std::string_view(reinterpret_cast<const char*>(salt.data()), salt.size()), params);
	}

	bool Verify(std::string_view secret, std::string_view stored_encoded_hash)
	{
		if (stored_encoded_hash.empty())
		{
			LOG_WARN(Auth, "[Argon2Hash] Verify called with empty stored hash");
			return false;
		}
		std::string encoded(stored_encoded_hash);
		const int rc = argon2id_verify(encoded.c_str(), secret.data(), secret.size());
		if (rc == ARGON2_OK)
		{
			LOG_DEBUG(Auth, "[Argon2Hash] Verify OK");
			return true;
		}
		if (rc == ARGON2_VERIFY_MISMATCH)
		{
			LOG_DEBUG(Auth, "[Argon2Hash] Verify mismatch");
			return false;
		}
		LOG_WARN(Auth, "[Argon2Hash] Verify FAILED: {}", argon2_error_message(rc));
		return false;
	}

	std::vector<std::uint8_t> GenerateSalt(std::size_t byte_count)
	{
		if (byte_count == 0)
		{
			LOG_WARN(Auth, "[Argon2Hash] GenerateSalt called with byte_count=0");
			return {};
		}
		std::vector<std::uint8_t> salt(byte_count);
		if (RAND_bytes(salt.data(), static_cast<int>(byte_count)) != 1)
		{
			LOG_ERROR(Auth, "[Argon2Hash] GenerateSalt FAILED: RAND_bytes error");
			return {};
		}
		LOG_DEBUG(Auth, "[Argon2Hash] GenerateSalt OK ({} bytes)", byte_count);
		return salt;
	}
}
