#pragma once

// M101.4 / M101.5 — Panneau nodal dockable (canvas + palette + inspecteur).
//
// Implémente IPanel. Le rendu ImGui est compilé uniquement sur Windows (comme
// les autres panneaux du repo : ImGui n'est lié que sur WIN32) ; sur Linux le
// corps de Render est vide. Toute mutation du graphe passe par le CommandStack
// (undo/redo). Le panneau possède son RoutineGraphDocument.

#include "src/world_editor/core/IPanel.h"
#include "src/world_editor/routine/RoutineGraphDocument.h"

namespace engine::editor::world
{
	class CommandStack;

	class RoutineGraphPanel final : public IPanel
	{
	public:
		/// \param stack Pile undo/redo possédée par WorldEditorShell (doit
		/// survivre au panneau).
		explicit RoutineGraphPanel(CommandStack* stack) : m_stack(stack) {}

		const char* GetName() const override { return "Routines"; }
		void Render() override;
		bool IsVisible() const override { return m_visible; }
		void SetVisible(bool visible) override { m_visible = visible; }

		/// Accès au document (tests / intégration Playtest M101.9).
		RoutineGraphDocument& Document() { return m_doc; }
		const RoutineGraphDocument& Document() const { return m_doc; }

	private:
		CommandStack* m_stack = nullptr;
		RoutineGraphDocument m_doc;
		bool m_visible = false;

		// État de drag d'un nœud (pour pousser un MoveNodeCommand au relâchement).
		bool m_dragging = false;
		uint32_t m_dragNodeId = 0;
		float m_dragStartX = 0.0f;
		float m_dragStartY = 0.0f;
	};
}
