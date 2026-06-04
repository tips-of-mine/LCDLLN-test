// M101.2 — Implémentation de la VM data-flow `zone_event`.

#include "src/shared/routine/vm/ZoneEventVm.h"

#include <cstdlib>

namespace engine::routine::vm
{
	ZoneEventVm::ZoneEventVm(const RoutineGraph& graph) : m_graph(graph) {}

	const RoutineNode* ZoneEventVm::FindNode(uint32_t id) const
	{
		for (const auto& n : m_graph.nodes)
			if (n.id == id) return &n;
		return nullptr;
	}

	const RoutineNode* ZoneEventVm::FindEventNode(RoutineNodeType type) const
	{
		for (const auto& n : m_graph.nodes)
			if (n.type == type) return &n;
		return nullptr;
	}

	const RoutineProperty* ZoneEventVm::FindProp(const RoutineNode& n, std::string_view key) const
	{
		for (const auto& p : n.properties)
			if (p.key == key) return &p;
		return nullptr;
	}

	uint32_t ZoneEventVm::OutputExecPinByName(const RoutineNode& n, std::string_view name) const
	{
		for (const auto& p : n.pins)
			if (p.kind == PinKind::Exec && p.direction == PinDirection::Output && p.name == name)
				return p.id;
		return 0;
	}

	uint32_t ZoneEventVm::FirstOutputExecPin(const RoutineNode& n) const
	{
		for (const auto& p : n.pins)
			if (p.kind == PinKind::Exec && p.direction == PinDirection::Output)
				return p.id;
		return 0;
	}

	uint32_t ZoneEventVm::InputDataPinByName(const RoutineNode& n, std::string_view name) const
	{
		for (const auto& p : n.pins)
			if (p.kind == PinKind::Data && p.direction == PinDirection::Input && p.name == name)
				return p.id;
		return 0;
	}

	bool ZoneEventVm::Fire(RoutineNodeType eventType, const RoutineRunContext& ctx, IRoutineHost& host)
	{
		if (m_graph.kind != RoutineGraphKind::ZoneEvent) return false;
		const RoutineNode* ev = FindEventNode(eventType);
		if (!ev) return false;
		host.Trace(ToString(eventType));
		const uint32_t outPin = FirstOutputExecPin(*ev);
		if (outPin != 0)
			ExecFrom(*ev, outPin, ctx, host, 0);
		return true;
	}

	void ZoneEventVm::ExecFrom(const RoutineNode& node, uint32_t outPinId,
	                           const RoutineRunContext& ctx, IRoutineHost& host, int depth)
	{
		if (depth >= kMaxDepth)
		{
			host.Trace("__depth_limit__");
			return;
		}
		for (const auto& link : m_graph.links)
		{
			if (link.fromNodeId == node.id && link.fromPinId == outPinId)
			{
				const RoutineNode* tgt = FindNode(link.toNodeId);
				if (tgt) ExecuteNode(*tgt, ctx, host, depth + 1);
			}
		}
	}

	void ZoneEventVm::ExecuteNode(const RoutineNode& node,
	                              const RoutineRunContext& ctx, IRoutineHost& host, int depth)
	{
		host.Trace(ToString(node.type));

		switch (node.type)
		{
			case RoutineNodeType::BranchIf:
			{
				const uint32_t condPin = InputDataPinByName(node, "cond");
				const bool cond = EvalBoolInput(node, condPin, ctx, host);
				const uint32_t branchPin = OutputExecPinByName(node, cond ? "true" : "false");
				if (branchPin != 0) ExecFrom(node, branchPin, ctx, host, depth);
				return;
			}
			case RoutineNodeType::ActionOpenInteractive:
			{
				uint64_t id = 0;
				if (const RoutineProperty* p = FindProp(node, "interactiveId"); p && !p->sValue.empty())
					id = std::strtoull(p->sValue.c_str(), nullptr, 10);
				bool open = false;
				if (const RoutineProperty* p = FindProp(node, "open")) open = p->bValue;
				host.OpenInteractive(id, open);
				break;
			}
			case RoutineNodeType::ActionBroadcastSeason:
			{
				int idx = 0;
				if (const RoutineProperty* p = FindProp(node, "seasonIndex")) idx = static_cast<int>(p->iValue);
				host.BroadcastSeason(idx);
				break;
			}
			case RoutineNodeType::ActionBroadcastWeather:
			{
				int idx = 0;
				if (const RoutineProperty* p = FindProp(node, "weatherIndex")) idx = static_cast<int>(p->iValue);
				host.BroadcastWeather(idx);
				break;
			}
			default:
				// Nœud sans effet propre (commentaire, etc.) : on suit quand même
				// le flux d'exécution.
				break;
		}

		// Suite du flux par le pin Exec de sortie standard ("out"), sinon le
		// premier pin Exec de sortie disponible.
		uint32_t outPin = OutputExecPinByName(node, "out");
		if (outPin == 0) outPin = FirstOutputExecPin(node);
		if (outPin != 0) ExecFrom(node, outPin, ctx, host, depth);
	}

	bool ZoneEventVm::EvalBoolInput(const RoutineNode& node, uint32_t inPinId,
	                                const RoutineRunContext& ctx, IRoutineHost& host) const
	{
		if (inPinId == 0) return false;
		// Cherche un lien Data alimentant ce pin d'entrée.
		for (const auto& link : m_graph.links)
		{
			if (link.toNodeId == node.id && link.toPinId == inPinId)
			{
				const RoutineNode* src = FindNode(link.fromNodeId);
				if (!src) return false;
				switch (src->type)
				{
					case RoutineNodeType::SensorPlayerInRange:
					{
						float range = 0.0f;
						if (const RoutineProperty* p = FindProp(*src, "rangeMeters")) range = p->fValue;
						return host.IsPlayerInRange(ctx.eventEntityId, range);
					}
					default:
						return false;
				}
			}
		}
		return false; // non lié → défaut déterministe.
	}
}
