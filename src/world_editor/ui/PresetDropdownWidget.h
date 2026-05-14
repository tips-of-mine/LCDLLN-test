#pragma once

#include "src/world_editor/presets/ToolPreset.h"

#include <functional>

namespace engine::editor::world::ui
{
	/// Widget réutilisable « dropdown de presets » (M100.45 A.6, Phase B).
	/// Rendu en haut de chaque `ToolPropertiesPanel` migré.
	///
	/// Comportement :
	///   - Liste les presets de `toolId` depuis `ToolPresetRegistry`.
	///   - La sélection courante est lue depuis `UserPrefsStore`
	///     (`lastPresetByTool`) — source de vérité unique, pas d'état
	///     local au widget. À défaut, le `defaultPreset` du catalogue.
	///   - À la sélection d'un preset : invoque `onApply(preset)` puis
	///     persiste le choix dans `user_prefs.json`.
	///   - Affiche une icône info avec la description du preset en tooltip.
	///   - No-op visuel si l'outil n'a aucun preset (outil non encore
	///     pourvu d'un fichier `tool_presets/<id>.json`).
	///
	/// \param toolId  identifiant stable de l'outil (= `toolId` du JSON).
	/// \param onApply callback appelé avec le preset choisi ; l'outil y
	///        mappe les `parameters` vers son struct interne (cf.
	///        `presets::ApplyHydraulicErosionPreset`).
	void RenderPresetDropdown(const char* toolId,
		const std::function<void(const presets::ToolPreset&)>& onApply);
}
