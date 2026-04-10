/**
 * M20.2: Unit tests for Argon2 hashing wrapper (Hash, Verify, GenerateSalt).
 * No external test framework; returns 0 if all pass, non-zero on first failure.
 * No plaintext password is stored or transmitted; only hashes and salts in tests.
 */

#include "engine/auth/Argon2Hash.h"
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace
{
	static int s_failCount = 0;

	void Assert(bool cond, const char* msg)
	{
		if (!cond)
		{
			++s_failCount;
			std::cerr << "[FAIL] " << msg << std::endl;
		}
	}
}

using namespace engine::auth;

static bool TestHashReturnsEncodedString()
{
	Argon2Params params;
	params.time_cost = 1;
	params.memory_kib = 64;
	params.parallelism = 1;
	params.hash_len = 32;

	std::string secret = "test_password";
	std::vector<std::uint8_t> salt = GenerateSalt(kArgon2SaltLength);
	Assert(salt.size() == kArgon2SaltLength, "GenerateSalt size");

	std::string encoded = Hash(secret, salt, params);
	Assert(!encoded.empty(), "Hash returns non-empty");
	Assert(encoded.find("$argon2id$") == 0u, "Hash returns argon2id encoded string");
	return s_failCount == 0;
}

static bool TestVerifyMatchesStoredHash()
{
	Argon2Params params;
	params.time_cost = 1;
	params.memory_kib = 64;
	params.parallelism = 1;
	params.hash_len = 32;

	std::string secret = "client_hash_or_password";
	std::vector<std::uint8_t> salt = GenerateSalt(kArgon2SaltLength);
	std::string stored = Hash(secret, salt, params);
	Assert(!stored.empty(), "Hash produces stored string");

	bool ok = Verify(secret, stored);
	Assert(ok, "Verify(secret, Hash(secret,salt)) succeeds");
	return s_failCount == 0;
}

static bool TestVerifyRejectsWrongSecret()
{
	Argon2Params params;
	params.time_cost = 1;
	params.memory_kib = 64;
	params.parallelism = 1;
	params.hash_len = 32;

	std::string secret = "correct";
	std::vector<std::uint8_t> salt = GenerateSalt(kArgon2SaltLength);
	std::string stored = Hash(secret, salt, params);
	Assert(!stored.empty(), "Hash produces stored string");

	bool ok = Verify("wrong_secret", stored);
	Assert(!ok, "Verify(wrong_secret, stored) fails");
	return s_failCount == 0;
}

static bool TestGenerateSaltCSPRNG()
{
	std::vector<std::uint8_t> a = GenerateSalt(16);
	std::vector<std::uint8_t> b = GenerateSalt(16);
	Assert(a.size() == 16u, "GenerateSalt(16) size");
	Assert(b.size() == 16u, "GenerateSalt(16) size");
	bool diff = false;
	for (std::size_t i = 0; i < 16; ++i)
		if (a[i] != b[i]) { diff = true; break; }
	Assert(diff, "GenerateSalt yields different values");
	return s_failCount == 0;
}

static bool TestVerifyEmptyStoredHash()
{
	bool ok = Verify("any", "");
	Assert(!ok, "Verify(any, empty) fails");
	return s_failCount == 0;
}

static bool TestDeriveClientSaltStable()
{
	std::vector<std::uint8_t> a = DeriveClientPasswordSaltFromLogin("  player_one  ");
	std::vector<std::uint8_t> b = DeriveClientPasswordSaltFromLogin("player_one");
	Assert(a.size() == kArgon2SaltLength, "Derive size");
	Assert(b.size() == kArgon2SaltLength, "Derive size");
	Assert(a == b, "Derive trims like server NormaliseLoginView");

	std::vector<std::uint8_t> c = DeriveClientPasswordSaltFromLogin("other");
	Assert(c != a, "Different login yields different salt");

	std::vector<std::uint8_t> empty = DeriveClientPasswordSaltFromLogin("   ");
	Assert(empty.empty(), "Whitespace-only login yields empty salt");
	return s_failCount == 0;
}

static bool TestDoubleHashWithDerivedSaltMatchesVerify()
{
	Argon2Params params; // defaults: t=2, m=65536, p=1, len=32 — production

	const std::string login = "portal_test_user";
	std::vector<std::uint8_t> clientSalt = DeriveClientPasswordSaltFromLogin(login);
	Assert(clientSalt.size() == kArgon2SaltLength, "client salt ok");

	const std::string password = "testPass1";
	std::string clientHash = Hash(password, clientSalt, params);
	Assert(!clientHash.empty(), "inner hash ok");

	std::vector<std::uint8_t> serverSalt = GenerateSalt(kArgon2SaltLength);
	std::string finalHash = Hash(clientHash, serverSalt, params);
	Assert(!finalHash.empty(), "final hash ok");

	Assert(Verify(clientHash, finalHash), "Verify(clientHash, finalHash)");
	Assert(!Verify(Hash("wrongPass1", clientSalt, params), finalHash), "wrong password fails");
	return s_failCount == 0;
}

int main()
{
	TestHashReturnsEncodedString();
	TestVerifyMatchesStoredHash();
	TestVerifyRejectsWrongSecret();
	TestGenerateSaltCSPRNG();
	TestVerifyEmptyStoredHash();
	TestDeriveClientSaltStable();
	TestDoubleHashWithDerivedSaltMatchesVerify();

	if (s_failCount != 0)
		return static_cast<int>(s_failCount);
	return 0;
}
