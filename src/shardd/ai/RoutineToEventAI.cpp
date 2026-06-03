// M101.7 (partiel) — Implémentation du pont npc_routine -> EventAIRow.

#include "src/shardd/ai/RoutineToEventAI.h"

#include <algorithm>

namespace engine::server::ai
{
	namespace
	{
		const engine::routine::RoutineProperty* FindProp(
			const engine::routine::RoutineNode& n, std::string_view key)
		{
			for (const auto& p : n.properties)
				if (p.key == key) return &p;
			return nullptr;
		}

		void Warn(std::string& out, const std::string& msg)
		{
			if (!out.empty()) out.push_back('\n');
			out += msg;
		}
	} // namespace

	std::vector<EventAIRow> RoutineToEventAI(const engine::routine::RoutineGraph& graph,
	                                         std::string& outWarnings)
	{
		using namespace engine::routine;

		std::vector<EventAIRow> rows;
		outWarnings.clear();

		if (graph.kind != RoutineGraphKind::NpcRoutine)
		{
			Warn(outWarnings, "graphe non npc_routine : aucune ligne EventAI generee");
			return rows;
		}

		// Ordre déterministe : tri des nœuds par id.
		std::vector<const RoutineNode*> nodes;
		nodes.reserve(graph.nodes.size());
		for (const auto& n : graph.nodes) nodes.push_back(&n);
		std::sort(nodes.begin(), nodes.end(),
		          [](const RoutineNode* a, const RoutineNode* b) { return a->id < b->id; });

		uint32_t nextEventId = 1;
		for (const RoutineNode* np : nodes)
		{
			const RoutineNode& n = *np;
			switch (n.type)
			{
				case RoutineNodeType::TaskPlayAnim:
				{
					EventAIRow row;
					row.eventId = nextEventId++;
					row.trigger = EventTrigger::Timer;
					row.param1 = 5000; // période par défaut (ms) ; affinable plus tard.
					row.param2 = 5000;
					row.action = EventAction::Custom;
					if (const auto* p = FindProp(n, "animId")) row.actionString = "anim:" + p->sValue;
					rows.push_back(row);
					break;
				}
				case RoutineNodeType::TaskSetEmotion:
				{
					EventAIRow row;
					row.eventId = nextEventId++;
					row.trigger = EventTrigger::OnSpawn;
					row.action = EventAction::Custom;
					if (const auto* p = FindProp(n, "emotion")) row.actionString = "emotion:" + p->sValue;
					row.oneShot = true;
					rows.push_back(row);
					break;
				}
				case RoutineNodeType::TaskMoveTo:
					// Hook manquant : EventAction n'a pas d'action de déplacement ;
					// la cible (point de rôle / Smart Object) n'existe pas encore.
					Warn(outWarnings, "noeud " + std::to_string(n.id) +
					     " (TaskMoveTo) non supporte : hook MoveTo / Role Registry / Smart Object manquant (Blocked)");
					break;
				case RoutineNodeType::SensorPlayerInRange:
					// Hook manquant : EventAI n'a pas de trigger 'joueur a portee'.
					Warn(outWarnings, "noeud " + std::to_string(n.id) +
					     " (SensorPlayerInRange) non supporte : pas de trigger de portee dans EventAI (Blocked)");
					break;
				case RoutineNodeType::NpcStateRoot:
				case RoutineNodeType::NpcState:
				case RoutineNodeType::SensorTimeOfDay:
				case RoutineNodeType::Comment:
					// Structurels / non encore mappes : silencieux.
					break;
				default:
					Warn(outWarnings, "noeud " + std::to_string(n.id) +
					     " : type non gere par la traduction PNJ");
					break;
			}
		}
		return rows;
	}
}
