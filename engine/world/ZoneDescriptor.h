#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace engine::world
{
	/// Schéma de domaine pour `zone.json` (QCM : métadonnées par domaine).
	struct ZoneSchema final
	{
		std::string name;
		int version = 0;
	};

	/// World Editor — descripteur de zone (JSON, data-driven).
	/// Chemins du fichier heightmap sont relatifs au dossier contenant `zone.json`.
	/// Le binaire terrain doit être au format `.r16h` (voir `HeightmapLoader`).
	///
	/// `world_editor_format` 1 : champs historiques ; `heightmap_width` / `heightmap_height`
	/// optionnels — s'ils sont présents, la validation fichier peut vérifier le `.r16h`.
	///
	/// `world_editor_format` 2 : exige `zone_schema` { name: "lcdlln.zone", version: 1 },
	/// `heightmap_width` / `heightmap_height` obligatoires et cohérents avec le fichier.
	///
	/// Les **instances** d'objets restent la responsabilité de `layout.json`, pas de dupliquer
	/// une vérité concurrente dans `zone.json`.
	struct ZoneDescriptorV1 final
	{
		static constexpr int kMinWorldEditorFormat = 1;
		static constexpr int kMaxWorldEditorFormat = 2;
		static constexpr const char* kExpectedZoneSchemaName = "lcdlln.zone";
		static constexpr int kExpectedZoneSchemaVersion = 1;

		int format_version = 0;
		ZoneSchema zone_schema;
		bool has_zone_schema = false;
		uint32_t heightmap_width = 0;
		uint32_t heightmap_height = 0;
		bool has_heightmap_dims = false;
		std::string zone_id;
		/// Chemin relatif au dossier de la zone (ex. "terrain/height.r16h").
		std::string heightmap_r16h;
		/// Optionnel ; 0 = non défini dans le JSON.
		int64_t seed = 0;
		bool has_seed = false;
		/// Couches de matériau / splat (chemins ou ids logiques), extensible.
		std::vector<std::string> texture_layers;
	};

	/// Parse `zone.json` UTF-8. Ne vérifie pas l’existence des fichiers référencés.
	bool ParseZoneDescriptorJson(std::string_view jsonUtf8, ZoneDescriptorV1& out, std::string& err);

	/// Chemin absolu du heightmap : parent(`zoneJsonPath`) / `desc.heightmap_r16h`.
	[[nodiscard]] std::filesystem::path ResolveZoneHeightmapPath(const std::filesystem::path& zoneJsonPath,
	                                                             const ZoneDescriptorV1& desc);

	/// Si `desc` exige des dimensions (format ≥ 2 ou champs présents en format 1),
	/// vérifie que le `.r16h` existe et que largeur/hauteur correspondent à l’en-tête HAMP.
	bool ValidateZoneHeightmapAgainstFile(const std::filesystem::path& zoneJsonPath,
	                                      const ZoneDescriptorV1& desc, std::string& err);
}
