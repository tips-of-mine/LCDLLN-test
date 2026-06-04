#pragma once

// M101.7 (partiel) — Pont de traduction graphe npc_routine -> EventAIRow.
//
// Décision actée : la cible PNJ réutilise l'IA data-driven existante (EventAI)
// plutôt que d'introduire une seconde IA concurrente. Ce pont génère des
// EventAIRow consommables par l'EventAIRuntime existant, pour le SOUS-ENSEMBLE
// de nœuds qui a un hook existant. Les nœuds sans hook (déplacement vers un
// point de rôle / Smart Object, capteur de portée) sont signalés dans
// `outWarnings` — leur support reste BLOQUÉ tant que l'infra PNJ (Role
// Registry, Smart Objects, action MoveTo) n'est pas livrée.

#include <string>
#include <vector>

#include "src/shardd/ai/EventAI.h"
#include "src/shared/routine/RoutineGraph.h"

namespace engine::server::ai
{
	/// Traduit un graphe npc_routine en lignes EventAI (déterministe : ordre
	/// stable par id de nœud). Renseigne `outWarnings` (une ligne par nœud non
	/// supporté) sans échouer : le reste du graphe est traduit normalement.
	std::vector<EventAIRow> RoutineToEventAI(const engine::routine::RoutineGraph& graph,
	                                         std::string& outWarnings);
}
