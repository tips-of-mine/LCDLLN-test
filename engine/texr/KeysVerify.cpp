#include "engine/texr/KeysVerify.h"

#include "engine/texr/ManifestCanonicalJson.h"
#include "engine/texr/ManifestCrypto.h"

#include <nlohmann/json.hpp>

#include <cstring>
#include <span>

namespace lcdlln::manifest {

bool VerifyKeysJson(std::string_view keys_json_utf8, const std::array<std::uint8_t, 32>& k_embedded, SigningKeys& out_known,
                    std::string& err)
{
	out_known.clear();
	err.clear();
	nlohmann::json root;
	try
	{
		root = nlohmann::json::parse(keys_json_utf8.begin(), keys_json_utf8.end());
	}
	catch (const std::exception& e)
	{
		err = std::string("keys.json parse error: ") + e.what();
		return false;
	}
	if (!root.is_object())
	{
		err = "keys.json root must be object";
		return false;
	}
	if (!root.contains("signature") || !root["signature"].is_string() || !root.contains("delegations")
	    || !root["delegations"].is_array() || !root.contains("keys_version"))
	{
		err = "keys.json missing signature, delegations or keys_version";
		return false;
	}
	if (!root["keys_version"].is_number_integer())
	{
		err = "keys_version must be integer";
		return false;
	}

	const std::string sig_b64 = root["signature"].get<std::string>();
	std::vector<std::uint8_t> sig_raw;
	if (!Base64Decode(sig_b64, sig_raw, err) || sig_raw.size() != 64)
	{
		err = "keys.json root signature: invalid base64 or length (!=64)";
		return false;
	}

	nlohmann::json root_body = root;
	root_body.erase("signature");
	const std::string root_msg = CanonicalStringify(root_body);
	std::array<std::uint8_t, 64> sig_arr{};
	std::memcpy(sig_arr.data(), sig_raw.data(), 64);
	if (!Ed25519Verify(std::span<const std::uint8_t, 32>(k_embedded.data(), 32), root_msg, sig_arr, err))
	{
		return false;
	}

	const auto& dels = root["delegations"];
	for (const auto& item : dels)
	{
		if (!item.is_object())
		{
			err = "delegations[] entry must be object";
			return false;
		}
		if (!item.contains("signature") || !item["signature"].is_string() || !item.contains("delegate_id")
		    || !item.contains("issuer_id") || !item.contains("public_key"))
		{
			err = "delegation missing required fields";
			return false;
		}
		if (!item["delegate_id"].is_string() || !item["issuer_id"].is_string() || !item["public_key"].is_string())
		{
			err = "delegation id/issuer/public_key must be strings";
			return false;
		}

		const std::string del_sig_b64 = item["signature"].get<std::string>();
		std::vector<std::uint8_t> del_sig_raw;
		if (!Base64Decode(del_sig_b64, del_sig_raw, err) || del_sig_raw.size() != 64)
		{
			err = "delegation signature invalid base64";
			return false;
		}

		nlohmann::json dbody = item;
		dbody.erase("signature");
		const std::string dmsg = CanonicalStringify(dbody);
		std::array<std::uint8_t, 64> dsig{};
		std::memcpy(dsig.data(), del_sig_raw.data(), 64);

		const std::string issuer_id = item["issuer_id"].get<std::string>();
		bool vok = false;
		if (issuer_id == "embedded")
		{
			vok = Ed25519Verify(std::span<const std::uint8_t, 32>(k_embedded.data(), 32), dmsg, dsig, err);
		}
		else
		{
			const auto it = out_known.find(issuer_id);
			if (it == out_known.end())
			{
				err = "delegation issuer_id not in Known: " + issuer_id;
				return false;
			}
			vok = Ed25519Verify(std::span<const std::uint8_t, 32>(it->second.data(), 32), dmsg, dsig, err);
		}
		if (!vok)
		{
			return false;
		}

		const std::string delegate_id = item["delegate_id"].get<std::string>();
		if (out_known.count(delegate_id) != 0)
		{
			err = "duplicate delegate_id: " + delegate_id;
			return false;
		}

		const std::string pk_b64 = item["public_key"].get<std::string>();
		std::vector<std::uint8_t> pk_raw;
		if (!Base64Decode(pk_b64, pk_raw, err) || pk_raw.size() != 32)
		{
			err = "delegation public_key invalid (expect 32 raw bytes base64)";
			return false;
		}
		std::array<std::uint8_t, 32> pk{};
		std::memcpy(pk.data(), pk_raw.data(), 32);
		out_known[delegate_id] = pk;
	}

	return true;
}

}  // namespace lcdlln::manifest
