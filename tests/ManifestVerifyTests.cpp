/// Tests manifest canonical JSON + keys.json + manifest.json (Ed25519) — sans réseau.
#include "engine/texr/KeysVerify.h"
#include "engine/texr/ManifestCanonicalJson.h"
#include "engine/texr/ManifestCrypto.h"
#include "engine/texr/ManifestVerify.h"

#include <nlohmann/json.hpp>

#include <openssl/evp.h>
#include <openssl/opensslv.h>
#include <openssl/rand.h>
#if OPENSSL_VERSION_NUMBER >= 0x30000000L
#include <openssl/provider.h>
#endif

#include <array>
#include <iostream>
#include <string>
#include <vector>

namespace {

void Fail(const char* msg)
{
	std::cerr << "FAIL: " << msg << '\n';
	std::exit(1);
}

std::string Base64Encode(const std::vector<std::uint8_t>& raw)
{
	if (raw.empty())
	{
		return {};
	}
	std::vector<unsigned char> buf(static_cast<std::size_t>(4 * ((raw.size() + 2) / 3) + 1));
	const int n = EVP_EncodeBlock(buf.data(), raw.data(), static_cast<int>(raw.size()));
	if (n < 0)
	{
		Fail("EVP_EncodeBlock");
	}
	return std::string(reinterpret_cast<const char*>(buf.data()), static_cast<std::size_t>(n));
}

/// Signature Ed25519 sur le message brut (interop avec EVP_DigestVerify* côté `ManifestCrypto`).
bool Ed25519SignRaw(EVP_PKEY* priv, std::string_view message, std::array<std::uint8_t, 64>& sig_out)
{
	const auto* mptr = reinterpret_cast<const unsigned char*>(message.data());
	const size_t mlen = message.size();

	EVP_PKEY_CTX* pctx = EVP_PKEY_CTX_new(priv, nullptr);
	if (!pctx)
	{
		return false;
	}
	if (EVP_PKEY_sign_init(pctx) != 1)
	{
		EVP_PKEY_CTX_free(pctx);
		return false;
	}
	size_t need = 0;
	if (EVP_PKEY_sign(pctx, nullptr, &need, mptr, mlen) != 1 || need != 64)
	{
		EVP_PKEY_CTX_free(pctx);
		return false;
	}
	if (EVP_PKEY_sign_init(pctx) != 1)
	{
		EVP_PKEY_CTX_free(pctx);
		return false;
	}
	size_t siglen = sig_out.size();
	const int ok = EVP_PKEY_sign(pctx, sig_out.data(), &siglen, mptr, mlen);
	EVP_PKEY_CTX_free(pctx);
	return ok == 1 && siglen == 64;
}

/// Clé Ed25519 32 octets (seed OpenSSL) — évite EVP_PKEY_Q_keygen (comportement variable selon build).
EVP_PKEY* NewEd25519Key(std::array<std::uint8_t, 32>& pub_out)
{
	std::array<std::uint8_t, 32> priv{};
	if (RAND_bytes(priv.data(), static_cast<int>(priv.size())) != 1)
	{
		return nullptr;
	}
	EVP_PKEY* pkey = EVP_PKEY_new_raw_private_key(EVP_PKEY_ED25519, nullptr, priv.data(), priv.size());
	if (!pkey)
	{
		return nullptr;
	}
	size_t len = pub_out.size();
	if (EVP_PKEY_get_raw_public_key(pkey, pub_out.data(), &len) != 1 || len != pub_out.size())
	{
		EVP_PKEY_free(pkey);
		return nullptr;
	}
	return pkey;
}

void TestCanonicalOrder()
{
	nlohmann::json j;
	j["b"] = 1;
	j["a"] = 2;
	const std::string c = lcdlln::manifest::CanonicalStringify(j);
	if (c != R"({"a":2,"b":1})")
	{
		Fail("canonical object key order");
	}
}

void TestKeysAndManifestChain()
{
	std::array<std::uint8_t, 32> emb_pub_arr{};
	std::array<std::uint8_t, 32> del_pub_arr{};
	EVP_PKEY* embedded = NewEd25519Key(emb_pub_arr);
	EVP_PKEY* delegate = NewEd25519Key(del_pub_arr);
	if (!embedded || !delegate)
	{
		Fail("NewEd25519Key");
	}

	const std::array<std::uint8_t, 32>& emb_pub = emb_pub_arr;
	const std::array<std::uint8_t, 32>& del_pub = del_pub_arr;

	nlohmann::json del_obj;
	del_obj["delegate_id"] = "test.signer";
	del_obj["issuer_id"] = "embedded";
	del_obj["public_key"] = Base64Encode(std::vector<std::uint8_t>(del_pub.begin(), del_pub.end()));
	const std::string del_canon = lcdlln::manifest::CanonicalStringify(del_obj);
	std::array<std::uint8_t, 64> del_sig{};
	if (!Ed25519SignRaw(embedded, del_canon, del_sig))
	{
		Fail("sign delegation");
	}
	del_obj["signature"] = Base64Encode(std::vector<std::uint8_t>(del_sig.begin(), del_sig.end()));

	nlohmann::json keys_root;
	keys_root["keys_version"] = 1;
	keys_root["delegations"] = nlohmann::json::array({ del_obj });
	const std::string keys_body = lcdlln::manifest::CanonicalStringify(keys_root);
	std::array<std::uint8_t, 64> keys_root_sig{};
	if (!Ed25519SignRaw(embedded, keys_body, keys_root_sig))
	{
		Fail("sign keys root");
	}
	keys_root["signature"] = Base64Encode(std::vector<std::uint8_t>(keys_root_sig.begin(), keys_root_sig.end()));
	const std::string keys_json = keys_root.dump();

	lcdlln::manifest::SigningKeys known;
	std::string err;
	if (!lcdlln::manifest::VerifyKeysJson(keys_json, emb_pub, known, err))
	{
		std::cerr << err << '\n';
		Fail("VerifyKeysJson");
	}
	if (known.count("test.signer") != 1)
	{
		Fail("Known missing delegate");
	}

	nlohmann::json art;
	art["cipher_size"] = 1;
	art["hash_cipher"] = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
	art["hash_plain"] = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
	art["relative_path"] = "v1/test.texr";
	art["version"] = "1";

	nlohmann::json man;
	man["manifest_version"] = 1;
	man["artifacts"] = nlohmann::json::object({ { "core.ui", art } });
	man["signing_key_id"] = "test.signer";
	const std::string man_body = lcdlln::manifest::CanonicalStringify(man);
	std::array<std::uint8_t, 64> man_sig{};
	if (!Ed25519SignRaw(delegate, man_body, man_sig))
	{
		Fail("sign manifest");
	}
	man["signature"] = Base64Encode(std::vector<std::uint8_t>(man_sig.begin(), man_sig.end()));
	const std::string man_json = man.dump();

	lcdlln::manifest::ParsedManifest parsed;
	if (!lcdlln::manifest::VerifyManifestJson(man_json, known, emb_pub, parsed, err))
	{
		std::cerr << err << '\n';
		Fail("VerifyManifestJson");
	}
	if (parsed.artifacts.count("core.ui") == 0)
	{
		Fail("artifact missing");
	}

	EVP_PKEY_free(embedded);
	EVP_PKEY_free(delegate);
}

}  // namespace

int main()
{
#if OPENSSL_VERSION_NUMBER >= 0x30000000L
	(void)OSSL_PROVIDER_load(nullptr, "default");
#endif
	TestCanonicalOrder();
	TestKeysAndManifestChain();
	std::cerr << "manifest_verify_tests: OK\n";
	return 0;
}
