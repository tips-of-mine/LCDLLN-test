#pragma once

#include "src/world_editor/presets/ToolPreset.h"

#include <string>
#include <vector>

namespace engine::editor::world::presets
{
	/// Résultat du parsing d'un fichier `tool_presets/<id>.json`.
	struct ToolPresetFile
	{
		std::string             toolId;
		std::string             defaultPreset;
		std::vector<ToolPreset> presets;
	};

	/// Parse le contenu JSON d'un fichier de presets (M100.45, Phase 12).
	/// Parseur hand-rolled (pattern du repo, pas de dépendance lib).
	///
	/// Format attendu :
	/// ```json
	/// {
	///   "toolId": "hydraulic_erosion",
	///   "defaultPreset": "realistic",
	///   "presets": [
	///     { "id": "...", "displayName": "...", "description": "...",
	///       "parameters": { "numDroplets": 30000, "erosionRate": 0.15 } }
	///   ]
	/// }
	/// ```
	///
	/// Tolérant : un `presets` absent donne une liste vide ; un preset sans
	/// `id` est ignoré. \return false + `outError` renseigné uniquement sur
	/// erreur structurelle (JSON non parsable, `toolId` manquant).
	bool ParseToolPresetJson(const std::string& jsonText, ToolPresetFile& out,
		std::string& outError);
}
