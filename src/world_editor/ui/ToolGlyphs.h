#pragma once
// ToolGlyphs — icônes vectorielles des 15 outils de l'éditeur monde
// (lot follow-up 2026-07-18). Dessinées à la volée via ImDrawList (nettes à
// toute échelle, pas d'assets PNG à pipeliner) — même approche que les
// glyphes de la barre d'actions (EditorToolbar). Consommées par la palette
// d'outils (ToolPalettePanel) à la place des pastilles de couleur unies.

#include "src/world_editor/core/ActiveTool.h"

// ImDrawList/ImVec2 ne sont disponibles que sous Windows (ImGui gaté WIN32
// dans le CMake) : l'API est déclarée partout, définie no-op ailleurs.
struct ImDrawList;

namespace engine::editor::world
{
	/// Dessine le glyphe vectoriel de l'outil \p tool dans le rectangle
	/// écran [minX,minY]..[maxX,maxY] avec la couleur \p colorAbgr
	/// (encodage IM_COL32). Un outil non mappé ne dessine rien (l'appelant
	/// garde son fond de couleur en repli visuel).
	/// Effet de bord : commandes de dessin poussées dans \p drawList.
	/// Contrainte : à appeler pendant une frame ImGui (main thread).
	/// No-op hors Windows.
	void DrawToolGlyph(ImDrawList* drawList, ActiveTool tool,
		float minX, float minY, float maxX, float maxY,
		unsigned int colorAbgr);
}
