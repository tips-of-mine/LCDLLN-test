// M101.6 — Implémentation de la validation de graphe (pure, déterministe).

#include "src/shared/routine/RoutineGraphValidation.h"

#include "src/shared/routine/RoutineNodeSchema.h"

#include <algorithm>
#include <functional>
#include <unordered_map>
#include <unordered_set>

namespace engine::routine::validation
{
	namespace
	{
		bool IsEventRoot(RoutineNodeType t)
		{
			return t == RoutineNodeType::EventOnZoneEnter ||
			       t == RoutineNodeType::EventOnZoneExit ||
			       t == RoutineNodeType::EventOnInteract;
		}

		bool IsRootForKind(const RoutineNode& n, RoutineGraphKind kind)
		{
			if (kind == RoutineGraphKind::ZoneEvent) return IsEventRoot(n.type);
			return n.type == RoutineNodeType::NpcStateRoot;
		}

		const RoutineNode* FindNode(const RoutineGraph& g, uint32_t id)
		{
			for (const auto& n : g.nodes) if (n.id == id) return &n;
			return nullptr;
		}

		const RoutinePin* FindPin(const RoutineNode& n, uint32_t pinId)
		{
			for (const auto& p : n.pins) if (p.id == pinId) return &p;
			return nullptr;
		}

		bool HasExecInput(const RoutineNode& n)
		{
			for (const auto& p : n.pins)
				if (p.kind == PinKind::Exec && p.direction == PinDirection::Input) return true;
			return false;
		}
	} // namespace

	std::vector<ValidationIssue> Validate(const RoutineGraph& graph)
	{
		std::vector<ValidationIssue> issues;

		// --- Conformité au schéma (type valide pour la cible) ---
		for (const auto& n : graph.nodes)
		{
			const RoutineNodeSchema* s = FindSchema(n.type);
			if (!s)
			{
				issues.push_back({ IssueSeverity::Error, IssueKind::SchemaMismatch, n.id, 0,
				                   "type de noeud inconnu" });
				continue;
			}
			if (!SchemaValidForKind(*s, graph.kind))
			{
				issues.push_back({ IssueSeverity::Error, IssueKind::SchemaMismatch, n.id, 0,
				                   std::string("noeud '") + s->displayName + "' invalide pour cette cible" });
			}
			// EntityRef vide → avertissement (impossible de résoudre à l'édition).
			for (const auto& pr : n.properties)
			{
				if (pr.type == RoutineDataType::EntityRef && pr.sValue.empty())
				{
					issues.push_back({ IssueSeverity::Warning, IssueKind::UnknownEntityRef, n.id, 0,
					                   "reference d'entite vide : '" + pr.key + "'" });
				}
			}
		}

		// --- Pins des liens : direction + compatibilité kind/type ---
		for (const auto& l : graph.links)
		{
			const RoutineNode* from = FindNode(graph, l.fromNodeId);
			const RoutineNode* to = FindNode(graph, l.toNodeId);
			if (!from || !to)
			{
				issues.push_back({ IssueSeverity::Error, IssueKind::IncompatiblePins, 0, l.id,
				                   "lien vers un noeud inexistant" });
				continue;
			}
			const RoutinePin* fp = FindPin(*from, l.fromPinId);
			const RoutinePin* tp = FindPin(*to, l.toPinId);
			if (!fp || !tp)
			{
				issues.push_back({ IssueSeverity::Error, IssueKind::IncompatiblePins, 0, l.id,
				                   "lien vers un pin inexistant" });
				continue;
			}
			if (fp->direction != PinDirection::Output || tp->direction != PinDirection::Input)
			{
				issues.push_back({ IssueSeverity::Error, IssueKind::IncompatiblePins, 0, l.id,
				                   "lien doit aller d'une sortie vers une entree" });
			}
			if (fp->kind != tp->kind)
			{
				issues.push_back({ IssueSeverity::Error, IssueKind::IncompatiblePins, 0, l.id,
				                   "melange de pins exec et data" });
			}
			else if (fp->kind == PinKind::Data && fp->dataType != tp->dataType)
			{
				issues.push_back({ IssueSeverity::Error, IssueKind::IncompatiblePins, 0, l.id,
				                   "types de donnees incompatibles" });
			}
		}

		// --- Cardinalité des racines ---
		uint32_t rootCount = 0;
		uint32_t npcRootCount = 0;
		for (const auto& n : graph.nodes)
		{
			if (IsRootForKind(n, graph.kind)) ++rootCount;
			if (n.type == RoutineNodeType::NpcStateRoot) ++npcRootCount;
		}
		if (rootCount == 0)
		{
			issues.push_back({ IssueSeverity::Error, IssueKind::RootCardinality, 0, 0,
			                   "aucun noeud racine (Event / NpcStateRoot)" });
		}
		if (graph.kind == RoutineGraphKind::NpcRoutine && npcRootCount > 1)
		{
			issues.push_back({ IssueSeverity::Error, IssueKind::RootCardinality, 0, 0,
			                   "un seul NpcStateRoot autorise" });
		}

		// --- Détection de cycle d'exécution + accessibilité (exec wires) ---
		// Adjacence exec : nodeId -> nodeIds atteints par un lien exec sortant.
		std::unordered_map<uint32_t, std::vector<uint32_t>> adj;
		for (const auto& l : graph.links)
		{
			const RoutineNode* from = FindNode(graph, l.fromNodeId);
			if (!from) continue;
			const RoutinePin* fp = FindPin(*from, l.fromPinId);
			if (fp && fp->kind == PinKind::Exec) adj[l.fromNodeId].push_back(l.toNodeId);
		}

		// DFS de détection de cycle (gris/noir).
		std::unordered_set<uint32_t> visiting, visited;
		bool cycleReported = false;
		std::function<void(uint32_t)> dfs = [&](uint32_t id)
		{
			if (cycleReported) return;
			visiting.insert(id);
			auto it = adj.find(id);
			if (it != adj.end())
			{
				for (uint32_t nxt : it->second)
				{
					if (visiting.count(nxt))
					{
						issues.push_back({ IssueSeverity::Error, IssueKind::ExecCycle, nxt, 0,
						                   "cycle d'execution detecte" });
						cycleReported = true;
						return;
					}
					if (!visited.count(nxt)) dfs(nxt);
				}
			}
			visiting.erase(id);
			visited.insert(id);
		};
		for (const auto& n : graph.nodes)
		{
			if (!visited.count(n.id) && !cycleReported) dfs(n.id);
		}

		// Accessibilité depuis les racines (orphelins).
		std::unordered_set<uint32_t> reachable;
		std::vector<uint32_t> stack;
		for (const auto& n : graph.nodes)
			if (IsRootForKind(n, graph.kind)) { reachable.insert(n.id); stack.push_back(n.id); }
		while (!stack.empty())
		{
			uint32_t id = stack.back(); stack.pop_back();
			auto it = adj.find(id);
			if (it == adj.end()) continue;
			for (uint32_t nxt : it->second)
				if (reachable.insert(nxt).second) stack.push_back(nxt);
		}
		for (const auto& n : graph.nodes)
		{
			if (n.type == RoutineNodeType::Comment) continue;        // libre
			if (IsRootForKind(n, graph.kind)) continue;              // racine
			if (!HasExecInput(n)) continue;                          // capteur data-only
			if (!reachable.count(n.id))
			{
				issues.push_back({ IssueSeverity::Warning, IssueKind::OrphanNode, n.id, 0,
				                   "noeud non atteignable depuis une racine" });
			}
		}

		// Tri stable et déterministe.
		std::sort(issues.begin(), issues.end(),
		          [](const ValidationIssue& a, const ValidationIssue& b)
		          {
			          if (a.kind != b.kind) return static_cast<int>(a.kind) < static_cast<int>(b.kind);
			          if (a.nodeId != b.nodeId) return a.nodeId < b.nodeId;
			          return a.linkId < b.linkId;
		          });
		return issues;
	}

	bool HasError(const std::vector<ValidationIssue>& issues)
	{
		for (const auto& i : issues)
			if (i.severity == IssueSeverity::Error) return true;
		return false;
	}
}
