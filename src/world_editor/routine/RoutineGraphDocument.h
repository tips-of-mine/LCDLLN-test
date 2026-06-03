#pragma once

// M101.4 — Document d'édition d'un graphe de routine (état éditeur).
//
// Données pures (aucun ImGui) : le RoutineGraph en cours d'édition + l'état de
// sélection et de canvas. Manipulé exclusivement via des ICommand
// (RoutineGraphCommands) pour préserver l'historique undo/redo.

#include <cstdint>

#include "src/shared/routine/RoutineGraph.h"

namespace engine::editor::world
{
	struct RoutineGraphDocument
	{
		engine::routine::RoutineGraph graph;

		// Sélection (0 = aucune).
		uint32_t selectedNodeId = 0;
		uint32_t selectedLinkId = 0;

		// Transform du canvas (édition uniquement).
		float panX = 0.0f;
		float panY = 0.0f;
		float zoom = 1.0f;

		// Allocation d'ids monotone et déterministe (par session d'édition).
		uint32_t nextNodeId = 1;
		uint32_t nextLinkId = 1;
		uint32_t nextPinId = 1;

		// --- accès utilitaires (sans effet d'historique) ---

		engine::routine::RoutineNode* FindNode(uint32_t id)
		{
			for (auto& n : graph.nodes) if (n.id == id) return &n;
			return nullptr;
		}
		const engine::routine::RoutineNode* FindNode(uint32_t id) const
		{
			for (const auto& n : graph.nodes) if (n.id == id) return &n;
			return nullptr;
		}

		bool HasLink(uint32_t id) const
		{
			for (const auto& l : graph.links) if (l.id == id) return true;
			return false;
		}
	};
}
