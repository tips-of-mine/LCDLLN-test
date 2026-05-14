#pragma once

#include "src/world_editor/modes/EditorMode.h"

#include <string>
#include <unordered_map>

namespace engine::editor::world::prefs
{
	/// Préférences éditeur persistées par utilisateur (M100.45, Phase 12).
	/// Sérialisé dans `game/data/editor/user_prefs.json` — fichier **non
	/// committé** (cf. `game/data/editor/.gitignore`).
	///
	/// Schéma versionné, lecture tolérante : un champ manquant retombe sur
	/// son défaut, jamais de crash. Les tickets suivants de la Phase 12
	/// consomment certains champs déjà présents ici :
	///   - M100.47 (Tooltips) → `showAdvancedTooltips`
	///   - M100.49 (Tutoriel) → `tutorialCompletionFlags`
	struct UserPrefs
	{
		int                                          version = 1;
		modes::EditorMode                            editorMode = modes::EditorMode::Simple;
		std::unordered_map<std::string, std::string> lastPresetByTool;
		bool                                         showAdvancedTooltips = false;
		std::unordered_map<std::string, bool>        tutorialCompletionFlags;
	};

	/// Version de schéma courante écrite dans le champ `version`.
	constexpr int kUserPrefsSchemaVersion = 1;
}
