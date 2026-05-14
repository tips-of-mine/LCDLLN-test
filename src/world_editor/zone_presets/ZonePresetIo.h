#pragma once

#include "src/world_editor/zone_presets/ZonePreset.h"

#include <string>

namespace engine::editor::world::zone_presets
{
	/// Parse le contenu JSON d'un fichier `zone_presets/<id>.json`
	/// (M100.46, parseur hand-rolled — pattern du repo, pas de dépendance
	/// lib JSON générique).
	///
	/// Format attendu (cf. spec M100.46 §A) :
	/// ```json
	/// {
	///   "version": 1,
	///   "id": "rocky_coast",
	///   "displayName": { "fr": "...", "en": "..." },
	///   "description": { "fr": "...", "en": "..." },
	///   "thumbnail": "thumbnails/rocky_coast.png",
	///   "tags": ["coastal", "mountainous"],
	///   "estimatedExecutionSeconds": 45,
	///   "operations": [
	///     { "type": "mountain_macro", "preset": "pre_alpine",
	///       "polyline": [...], "affectedBy": ["relief"] }
	///   ],
	///   "decoration": []
	/// }
	/// ```
	///
	/// Chaque opération est conservée en `rawJson` (objet brut) ; seuls
	/// `type`, `preset` (→ toolPresetId) et `affectedBy` sont extraits en
	/// structuré. La section `decoration` n'est pas parsée en détail —
	/// seul son nombre d'entrées est compté (`decorationEntryCount`).
	///
	/// \return false + `outError` renseigné sur erreur structurelle
	///         (JSON non parsable, `id` ou `operations` manquant).
	bool ParseZonePresetJson(const std::string& jsonText, ZonePreset& out,
		std::string& outError);
}
