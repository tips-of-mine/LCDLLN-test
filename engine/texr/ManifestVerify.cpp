#include "engine/texr/ManifestVerify.h"

#include "engine/texr/ManifestCanonicalJson.h"
#include "engine/texr/ManifestCrypto.h"

#include <nlohmann/json.hpp>

#include <cstring>
#include <span>

namespace lcdlln::manifest {
namespace {

void LowerHexInPlace(std::string& s)
{
	for (char& c : s)
	{
		if (c >= 'A' && c <= 'Z')
		{
			c = static_cast<char>(c - 'A' + 'a');
		}
	}
}

}  // namespace

bool VerifyManifestJson(std::string_view manifest_utf8, const SigningKeys& known,
                        const std::array<std::uint8_t, 32>& k_embedded, ParsedManifest& out, std::string& err)
{
	out = ParsedManifest{};
	err.clear();
	nlohmann::json root;
	try
	{
		root = nlohmann::json::parse(manifest_utf8.begin(), manifest_utf8.end());
	}
	catch (const std::exception& e)
	{
		err = std::string("manifest parse error: ") + e.what();
		return false;
	}
	if (!root.is_object())
	{
		err = "manifest root must be object";
		return false;
	}
	if (!root.contains("signature") || !root["signature"].is_string() || !root.contains("manifest_version")
	    || !root.contains("artifacts"))
	{
		err = "manifest missing signature, manifest_version or artifacts";
		return false;
	}
	if (!root["manifest_version"].is_number_integer())
	{
		err = "manifest_version must be integer";
		return false;
	}
	out.manifest_version = root["manifest_version"].get<int>();
	if (out.manifest_version != 1)
	{
		err = "unsupported manifest_version (expected 1)";
		return false;
	}
	if (!root["artifacts"].is_object())
	{
		err = "artifacts must be object";
		return false;
	}

	const std::string sig_b64 = root["signature"].get<std::string>();
	std::vector<std::uint8_t> sig_raw;
	if (!Base64Decode(sig_b64, sig_raw, err) || sig_raw.size() != 64)
	{
		err = "manifest signature invalid base64";
		return false;
	}

	nlohmann::json body = root;
	body.erase("signature");
	const std::string msg = CanonicalStringify(body);
	std::array<std::uint8_t, 64> sig_arr{};
	std::memcpy(sig_arr.data(), sig_raw.data(), 64);

	std::array<std::uint8_t, 32> sign_key{};
	const std::array<std::uint8_t, 32>* sign_key_ptr = nullptr;
	if (root.contains("signing_key_id") && root["signing_key_id"].is_string())
	{
		out.signing_key_id = root["signing_key_id"].get<std::string>();
	}
	if (!out.signing_key_id.empty())
	{
		const auto it = known.find(out.signing_key_id);
		if (it == known.end())
		{
			err = "signing_key_id not in keys.json Known: " + out.signing_key_id;
			return false;
		}
		sign_key = it->second;
		sign_key_ptr = &sign_key;
	}
	else
	{
		sign_key_ptr = &k_embedded;
	}

	if (!Ed25519Verify(std::span<const std::uint8_t, 32>(sign_key_ptr->data(), 32), msg, sig_arr, err))
	{
		return false;
	}

	if (root.contains("mirrors") && root["mirrors"].is_array())
	{
		for (const auto& m : root["mirrors"])
		{
			if (m.is_string())
			{
				out.mirror_manifest_urls.push_back(m.get<std::string>());
			}
		}
	}

	for (auto it = root["artifacts"].begin(); it != root["artifacts"].end(); ++it)
	{
		const std::string id = it.key();
		const auto& a = it.value();
		if (!a.is_object())
		{
			err = "artifact " + id + " must be object";
			return false;
		}
		if (!a.contains("cipher_size") || !a.contains("hash_cipher") || !a.contains("hash_plain")
		    || !a.contains("relative_path") || !a.contains("version"))
		{
			err = "artifact " + id + " missing required fields";
			return false;
		}
		if (!a["cipher_size"].is_number_unsigned() && !a["cipher_size"].is_number_integer())
		{
			err = "artifact cipher_size must be integer";
			return false;
		}
		if (!a["hash_cipher"].is_string() || !a["hash_plain"].is_string() || !a["relative_path"].is_string()
		    || !a["version"].is_string())
		{
			err = "artifact string fields invalid";
			return false;
		}
		ArtifactDescriptor d;
		d.cipher_size = a["cipher_size"].get<std::uint64_t>();
		d.hash_cipher = a["hash_cipher"].get<std::string>();
		d.hash_plain = a["hash_plain"].get<std::string>();
		d.relative_path = a["relative_path"].get<std::string>();
		d.version = a["version"].get<std::string>();
		LowerHexInPlace(d.hash_cipher);
		LowerHexInPlace(d.hash_plain);
		if (d.hash_cipher.size() != 64 || d.hash_plain.size() != 64)
		{
			err = "artifact hashes must be 64 hex chars";
			return false;
		}
		out.artifacts[id] = std::move(d);
	}

	return true;
}

}  // namespace lcdlln::manifest
