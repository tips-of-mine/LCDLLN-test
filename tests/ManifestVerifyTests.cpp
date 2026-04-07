/// Tests manifest canonical JSON + keys.json + manifest.json (Ed25519) — sans réseau.
#include "engine/texr/KeysVerify.h"
#include "engine/texr/ManifestCanonicalJson.h"
#include "engine/texr/ManifestCrypto.h"
#include "engine/texr/ManifestVerify.h"

#include <nlohmann/json.hpp>

#include <openssl/evp.h>

#include <array>
#include <cstdlib>
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

bool Ed25519SignRaw(EVP_PKEY* priv, std::string_view message, std::array<std::uint8_t, 64>& sig_out)
{
	EVP_MD_CTX* ctx = EVP_MD_CTX_new();
	if (!ctx)
	{
		return false;
	}
	size_t siglen = 64;
	const bool ok =
	    (EVP_DigestSignInit(ctx, nullptr, nullptr, nullptr, priv) == 1
	     && EVP_DigestSignUpdate(ctx, message.data(), message.size()) == 1
	     && EVP_DigestSignFinal(ctx, sig_out.data(), &siglen) == 1 && siglen == 64);
	EVP_MD_CTX_free(ctx);
	return ok;
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
	EVP_PKEY* embedded = EVP_PKEY_Q_keygen(nullptr, nullptr, "ED25519", nullptr);
	EVP_PKEY* delegate = EVP_PKEY_Q_keygen(nullptr, nullptr, "ED25519", nullptr);
	if (!embedded || !delegate)
	{
		Fail("EVP_PKEY_Q_keygen");
	}

	std::array<std::uint8_t, 32> emb_pub{};
	std::array<std::uint8_t, 32> del_pub{};
	size_t len = 32;
	if (EVP_PKEY_get_raw_public_key(embedded, emb_pub.data(), &len) != 1 || len != 32)
	{
		Fail("get embedded pubkey");
	}
	len = 32;
	if (EVP_PKEY_get_raw_public_key(delegate, del_pub.data(), &len) != 1 || len != 32)
	{
		Fail("get delegate pubkey");
	}

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
	TestCanonicalOrder();
	TestKeysAndManifestChain();
	std::cerr << "manifest_verify_tests: OK\n";
	return 0;
}
