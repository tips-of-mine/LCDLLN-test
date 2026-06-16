#pragma once

#include "src/world_editor/structures/BuildingPreset.h"

#include <string>

namespace engine::editor::world::structures
{
	/// Parse le JSON d'un preset de bâtiment (parseur hand-rolled, pattern repo).
	///
	/// Format :
	/// ```json
	/// {
	///   "id": "auberge_demo",
	///   "displayName": "Auberge (démo)",
	///   "spawnAnchor": { "x": 0.0, "y": 0.0, "z": 2.0 },
	///   "elements": [
	///     { "mesh": "meshes/props/Floor_WoodDark.gltf",
	///       "x": 0, "y": 0, "z": 0, "yaw_deg": 0, "scale": 1,
	///       "collision_radius": 0, "solid": false }
	///   ]
	/// }
	/// ```
	/// \return false + `outError` sur erreur structurelle (JSON illisible, `id`
	/// manquant). Un élément sans `mesh` est ignoré (tolérant).
	bool ParseBuildingPresetJson(const std::string& jsonText, BuildingPreset& out,
		std::string& outError);

	/// Sérialise un preset en JSON (indentation simple). Round-trip avec
	/// ParseBuildingPresetJson.
	std::string SerializeBuildingPresetJson(const BuildingPreset& preset);
}
