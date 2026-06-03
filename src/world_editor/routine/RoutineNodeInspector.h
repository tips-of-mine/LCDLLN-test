#pragma once

// M101.5 — Inspecteur de propriétés du nœud sélectionné. Rendu ImGui guardé.

namespace engine::editor::world
{
	class CommandStack;
	struct RoutineGraphDocument;

	/// Rend les propriétés du nœud sélectionné, éditables par type ; toute
	/// modification passe par SetNodePropertyCommand (undo/redo). Définie sur
	/// toutes les plateformes (no-op hors Windows).
	void RenderRoutineNodeInspector(RoutineGraphDocument& doc, CommandStack& undo);
}
