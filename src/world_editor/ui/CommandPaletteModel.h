#pragma once
// CommandPaletteModel — logique pure (sans ImGui) de la palette de commandes
// Ctrl+P (réorganisation UI 2026-07-17, PR 3). Le filtrage/classement des
// actions est une fonction pure testable sous ctest Linux ; la fenêtre ImGui
// (WorldEditorImGui::RenderCommandPalette) ne fait que la consommer.

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace engine::editor::world::palette
{
	/// Entrée candidate de la palette : projection texte d'une action du
	/// registre (id + libellé + catégorie + raccourci déjà résolus).
	struct PaletteEntry
	{
		std::string id;           ///< id d'action ("file.save")
		std::string label;        ///< libellé FR affiché
		std::string categoryFr;   ///< catégorie affichée ("Fichier", "Outils"…)
		std::string shortcutText; ///< raccourci affiché ("" si aucun)
		bool enabled = true;      ///< prédicat `enabled` évalué au moment du snapshot
	};

	/// Normalisation pour la recherche : minuscules ASCII + translittération
	/// des accents français courants (é→e, à→a, ç→c, œ→oe…). Idempotente.
	/// Sans effet de bord.
	std::string NormalizeForSearch(std::string_view text);

	/// Filtre et classe les entrées pour une requête utilisateur.
	/// Insensible à la casse et aux accents. Règles de classement :
	///   1. préfixe du libellé (ou d'un mot du libellé) → avant
	///   2. sous-chaîne du libellé ou de la catégorie → après
	///   3. à rang égal, l'ordre d'origine est préservé (tri stable).
	/// Une requête vide (ou espaces) retourne TOUTES les entrées dans
	/// l'ordre d'origine.
	/// \return indices dans `entries`, ordonnés du meilleur match au moins bon.
	/// Sans effet de bord.
	std::vector<size_t> FilterPaletteEntries(std::string_view query,
		const std::vector<PaletteEntry>& entries);
}
