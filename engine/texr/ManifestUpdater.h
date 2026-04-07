#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace lcdlln::manifest {

struct ManifestUpdateConfig
{
	std::string keys_url;
	/// URL principale du manifest (HTTPS ou file://).
	std::string manifest_url;
	/// URLs additionnelles si la principale échoue (réseau / vérif).
	std::vector<std::string> extra_manifest_urls;
	/// Base CDN (sans slash final accepté) pour `relative_path` des artefacts.
	std::string cdn_base;
	std::array<std::uint8_t, 32> k_embedded{};
	/// Répertoire local d’écriture des `.texr` téléchargés.
	std::filesystem::path cache_dir;
	/// Clé dans `artifacts` (ex. `core.ui`).
	std::string artifact_id;
};

/// Télécharge keys.json + manifest, vérifie signatures, télécharge le `.texr`, vérifie taille + hash_cipher + hash_plain.
[[nodiscard]] bool DownloadVerifiedTexr(const ManifestUpdateConfig& cfg, std::filesystem::path& out_texr_path,
                                        std::string& err);

}  // namespace lcdlln::manifest
