#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace engine::auth
{
	/**
	 * Argon2 v1 parameters (configurable, versioned for future upgrades).
	 * Used for both client_hash and final_hash in the double-hash protocol.
	 */
	struct Argon2Params
	{
		/** Time cost (number of iterations). */
		uint32_t time_cost = 2;
		/** Memory cost in KiB (e.g. 65536 = 64 MiB). */
		uint32_t memory_kib = 65536;
		/** Parallelism (lanes/threads). */
		uint32_t parallelism = 1;
		/** Hash output length in bytes. */
		uint32_t hash_len = 32;
	};

	/** Default salt length in bytes (recommended >= 16). */
	constexpr std::size_t kArgon2SaltLength = 16;

	/**
	 * Hashes a secret (password or client_hash) with Argon2id using the given salt and params.
	 * Output is the standard argon2id encoded string (contains params + salt + hash).
	 * @param secret Secret bytes (e.g. password or client_hash; never logged or stored as plaintext).
	 * @param salt Salt bytes (e.g. from GenerateSalt()).
	 * @param params Argon2 parameters (stored/versioned for upgrades).
	 * @return Encoded hash string, or empty string on failure.
	 */
	std::string Hash(std::string_view secret, std::string_view salt, const Argon2Params& params);

	/**
	 * Overload: salt as raw bytes.
	 */
	std::string Hash(std::string_view secret, const std::vector<std::uint8_t>& salt, const Argon2Params& params);

	/**
	 * Verifies a secret against a stored encoded hash (argon2id string).
	 * The stored string embeds salt and params; no plaintext password is compared.
	 * @param secret Secret to verify (e.g. client_hash on server).
	 * @param stored_encoded_hash Encoded hash string previously produced by Hash().
	 * @return true if verification succeeds, false otherwise.
	 */
	bool Verify(std::string_view secret, std::string_view stored_encoded_hash);

	/**
	 * Generates a cryptographically secure random salt (CSPRNG).
	 * Use for client_salt and server_salt; server_salt must be unique per account.
	 * @param byte_count Length in bytes (default kArgon2SaltLength).
	 * @return Salt bytes, or empty vector on failure.
	 */
	std::vector<std::uint8_t> GenerateSalt(std::size_t byte_count = kArgon2SaltLength);

	/**
	 * Derives the client-side Argon2 password salt from the trimmed login (SHA-256 prefix, 16 bytes).
	 * Must match web-portal `deriveClientPasswordSalt` and stay stable across sessions so AUTH matches REGISTER.
	 * Empty input (after trim) returns an empty vector.
	 */
	std::vector<std::uint8_t> DeriveClientPasswordSaltFromLogin(std::string_view login);
}
