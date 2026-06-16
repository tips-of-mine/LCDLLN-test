#pragma once

// Auberge éditable (T4) — Export : preset + transform de groupe → entrées
// world.scenery (config.json) + ancre respawn "inn". Logique pure de
// construction/sérialisation ; le splice fichier est textuel (marqueurs).

#include <cstdint>
#include <string>
#include <vector>

#include "src/world_editor/structures/BuildingPreset.h"

namespace engine::editor::world::structures
{
	/// Une entrée world.scenery destinée à config.json.
	struct SceneryEntry
	{
		std::string meshPath;
		float x = 0.0f, z = 0.0f, yawDeg = 0.0f, scale = 1.0f;
		float collisionRadius = 0.0f;
		bool solid = true;
	};

	/// Construit les entrées scenery d'un preset posé au pivot (x,z) monde avec
	/// rotation `groupYawDeg`. (y de l'offset ignoré : world.scenery pose Y au
	/// sol au runtime.)
	std::vector<SceneryEntry> BuildSceneryEntries(const BuildingPreset& preset,
		float pivotX, float pivotZ, float groupYawDeg);

	/// Sérialise les entrées en lignes JSON `"<index>": { ... },` à partir de
	/// `startIndex` (numérotation des clés de l'objet world.scenery).
	std::string SerializeSceneryEntries(const std::vector<SceneryEntry>& entries,
		int startIndex);

	/// Remplace le texte entre les sentinelles `"_comment_auberge"` (incluse) et
	/// `"_comment_auberge_end"` (exclue) par `newBlock`. \return false si une
	/// sentinelle manque. (Le bloc auberge devient ainsi régénérable sans
	/// toucher au reste de config.json.)
	bool SpliceSceneryBlock(const std::string& configText,
		const std::string& newBlock, std::string& out, std::string& err);

	/// Remplace (ou ajoute) la ligne `inn` de respawn_points.txt pour la zone
	/// `zoneId` aux coordonnées (x,z). Conserve les autres lignes.
	std::string SpliceInnRespawn(const std::string& respawnText, uint32_t zoneId,
		float x, float z);
}
