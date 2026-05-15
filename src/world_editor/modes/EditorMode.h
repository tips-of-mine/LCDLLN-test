#pragma once

#include <cstdint>

namespace engine::editor::world::modes
{
	/// Niveau d'exposition des paramètres de l'éditeur (M100.45, Phase 12
	/// « Accessibilité éditeur »).
	///
	/// - `Simple` : chaque `ToolPropertiesPanel` n'affiche que 3-4 sliders
	///   essentiels. Mode par défaut au premier lancement.
	/// - `Advanced` : l'intégralité des paramètres est exposée (parité avec
	///   le comportement historique pré-M100.45).
	enum class EditorMode : uint8_t
	{
		Simple   = 0,
		Advanced = 1,
	};

	/// Convertit en chaîne stable pour la persistance JSON (`user_prefs.json`).
	inline const char* ToString(EditorMode mode)
	{
		return (mode == EditorMode::Advanced) ? "Advanced" : "Simple";
	}

	/// Parse une chaîne de persistance. Tolérant : toute valeur non
	/// reconnue retombe sur `Simple` (le défaut sûr).
	inline EditorMode FromString(const char* s)
	{
		if (s != nullptr && s[0] == 'A') return EditorMode::Advanced;
		return EditorMode::Simple;
	}
}
