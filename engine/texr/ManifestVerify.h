#pragma once

#include "engine/texr/KeysVerify.h"

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace lcdlln::manifest {

struct ArtifactDescriptor
{
	std::uint64_t cipher_size = 0;
	std::string hash_cipher;
	std::string hash_plain;
	std::string relative_path;
	std::string version;
};

struct ParsedManifest
{
	int manifest_version = 0;
	std::string signing_key_id;
	std::map<std::string, ArtifactDescriptor> artifacts;
	/// URLs HTTPS de manifests de repli (signés indépendamment).
	std::vector<std::string> mirror_manifest_urls;
};

/// Vérifie la signature Ed25519 et extrait les artefacts (clé = signing_key_id dans `known`, sinon `k_embedded` si id absent).
bool VerifyManifestJson(std::string_view manifest_utf8, const SigningKeys& known, const std::array<std::uint8_t, 32>& k_embedded,
                        ParsedManifest& out, std::string& err);

}  // namespace lcdlln::manifest
