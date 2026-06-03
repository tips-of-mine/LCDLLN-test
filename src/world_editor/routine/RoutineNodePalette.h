#pragma once

// M101.5 — Palette de nœuds (schema-driven). Rendu ImGui guardé Windows.

namespace engine::editor::world
{
	class CommandStack;
	struct RoutineGraphDocument;

	/// Rend la palette : liste les schémas valides pour la cible du graphe
	/// courant ; un clic insère le nœud correspondant via AddNodeCommand.
	/// La fonction est définie sur toutes les plateformes (no-op hors Windows).
	void RenderRoutineNodePalette(RoutineGraphDocument& doc, CommandStack& undo);
}
