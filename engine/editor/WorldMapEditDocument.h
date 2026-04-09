#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace engine::editor
{
	/// Document d’édition carte (JSON lisible, versionné). Les chemins \c heightmap* sont relatifs à \c paths.content.
	struct WorldMapEditDocument
	{
		static constexpr int kFormatVersion = 1;

		std::string zoneId = "untitled_zone";
		int formatVersion = kFormatVersion;
		/// Résolution N×N du heightmap (fichier .r16h).
		uint32_t heightmapResolution = 256;
		bool hasSeed = false;
		int64_t seed = 0;
		std::string heightmapContentRelativePath;
		std::vector<std::string> textureAssets;
		/// Identifiants de préfabs / objets (MVP : chaînes libres).
		std::vector<std::string> objectPrefabIds;
	};

} // namespace engine::editor
