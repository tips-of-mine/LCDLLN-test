#pragma once
// ToolPaletteModel — modèle pur (sans ImGui) de la palette d'outils latérale
// (réorganisation UI 2026-07-17, PR 2). Source unique des familles d'outils
// et des libellés FR, partagée entre la palette dockée (ToolPalettePanel),
// le menu « Outils » et la palette de commandes Ctrl+P (PR 3). Testable sous
// ctest Linux (aucune dépendance shell/ImGui).

#include "src/world_editor/core/ActiveTool.h"

#include <vector>

namespace engine::editor::world
{
	/// Groupe d'outils affiché par la palette (en-tête repliable + boutons).
	struct ToolPaletteGroup
	{
		/// Titre FR de la famille (« Terrain », « Eau », « Macro »,
		/// « Structures ») — mêmes chaînes que les sections du registre
		/// d'actions (menu Outils) pour rester une source unique.
		const char* titleFr = "";
		/// Outils de la famille, dans l'ordre d'affichage.
		std::vector<ActiveTool> tools;
	};

	/// Les 4 familles de la palette, couvrant exactement les 15 outils de
	/// l'enum `ActiveTool` (hors None), ordre d'affichage stable. Construit
	/// une fois (static locale), sans effet de bord.
	const std::vector<ToolPaletteGroup>& GetToolPaletteGroups();

	/// Libellé français court d'un outil (« Sculpture du terrain », « Lac »…).
	/// `None` retourne "Aucun outil" ; une valeur hors enum retourne "?".
	/// Sans effet de bord ; chaînes statiques (durée de vie programme).
	const char* ToolLabelFr(ActiveTool tool);
}
