#pragma once

// M101.2 — VM data-flow déterministe pour les graphes `zone_event`.
//
// Modèle Blueprint : un nœud Event racine est déclenché, le flux suit les
// « exec wires », les pins de données sont évalués en pull. Exécution cliente
// (lib pure, via IRoutineHost). Déterministe : mêmes entrées → même trace.

#include "src/shared/routine/RoutineGraph.h"
#include "src/shared/routine/vm/IRoutineHost.h"

namespace engine::routine::vm
{
	class ZoneEventVm
	{
	public:
		/// Construit la VM pour un graphe (doit être de kind ZoneEvent ; sinon
		/// Fire renverra false).
		explicit ZoneEventVm(const RoutineGraph& graph);

		/// Déclenche l'exécution à partir du nœud Event du type donné.
		/// Retourne false si aucun nœud Event de ce type n'existe (ou mauvais kind).
		bool Fire(RoutineNodeType eventType, const RoutineRunContext& ctx, IRoutineHost& host);

	private:
		const RoutineNode* FindNode(uint32_t id) const;
		const RoutineNode* FindEventNode(RoutineNodeType type) const;
		const RoutineProperty* FindProp(const RoutineNode& n, std::string_view key) const;
		uint32_t OutputExecPinByName(const RoutineNode& n, std::string_view name) const;
		uint32_t FirstOutputExecPin(const RoutineNode& n) const;
		uint32_t InputDataPinByName(const RoutineNode& n, std::string_view name) const;

		// Suit les exec wires depuis (node, outPinId) et exécute les cibles.
		void ExecFrom(const RoutineNode& node, uint32_t outPinId,
		              const RoutineRunContext& ctx, IRoutineHost& host, int depth);
		// Exécute un nœud (effet + suite du flux).
		void ExecuteNode(const RoutineNode& node,
		                 const RoutineRunContext& ctx, IRoutineHost& host, int depth);
		// Évalue en pull un pin Data d'entrée booléen (défaut false si non lié).
		bool EvalBoolInput(const RoutineNode& node, uint32_t inPinId,
		                   const RoutineRunContext& ctx, IRoutineHost& host) const;

		const RoutineGraph& m_graph;
		static constexpr int kMaxDepth = 256; // garde-fou anti-boucle.
	};
}
