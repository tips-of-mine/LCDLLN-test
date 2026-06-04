// M101.4 / M101.5 — Implémentation des commandes d'édition de graphe.

#include "src/world_editor/routine/RoutineGraphCommands.h"

#include <algorithm>

namespace engine::editor::world
{
	using engine::routine::RoutineLink;
	using engine::routine::RoutineNode;
	using engine::routine::RoutineProperty;

	// ---- AddNodeCommand ----

	AddNodeCommand::AddNodeCommand(RoutineGraphDocument& doc, RoutineNode node)
		: m_doc(doc), m_node(std::move(node)) {}

	size_t AddNodeCommand::GetMemoryFootprint() const
	{
		return sizeof(*this) + m_node.pins.size() * sizeof(engine::routine::RoutinePin) +
		       m_node.properties.size() * sizeof(RoutineProperty);
	}

	void AddNodeCommand::Execute()
	{
		m_prevSelected = m_doc.selectedNodeId;
		m_doc.graph.nodes.push_back(m_node);
		m_doc.selectedNodeId = m_node.id;
	}

	void AddNodeCommand::Undo()
	{
		auto& nodes = m_doc.graph.nodes;
		nodes.erase(std::remove_if(nodes.begin(), nodes.end(),
			[this](const RoutineNode& n) { return n.id == m_node.id; }), nodes.end());
		m_doc.selectedNodeId = m_prevSelected;
	}

	// ---- RemoveNodeCommand ----

	RemoveNodeCommand::RemoveNodeCommand(RoutineGraphDocument& doc, uint32_t nodeId)
		: m_doc(doc), m_nodeId(nodeId) {}

	size_t RemoveNodeCommand::GetMemoryFootprint() const
	{
		return sizeof(*this) + m_removedLinks.size() * sizeof(RoutineLink);
	}

	void RemoveNodeCommand::Execute()
	{
		auto& nodes = m_doc.graph.nodes;
		auto& links = m_doc.graph.links;

		if (!m_captured)
		{
			if (const RoutineNode* n = m_doc.FindNode(m_nodeId)) m_node = *n;
			for (const auto& l : links)
				if (l.fromNodeId == m_nodeId || l.toNodeId == m_nodeId)
					m_removedLinks.push_back(l);
			m_captured = true;
		}

		links.erase(std::remove_if(links.begin(), links.end(),
			[this](const RoutineLink& l) { return l.fromNodeId == m_nodeId || l.toNodeId == m_nodeId; }),
			links.end());
		nodes.erase(std::remove_if(nodes.begin(), nodes.end(),
			[this](const RoutineNode& n) { return n.id == m_nodeId; }), nodes.end());
		if (m_doc.selectedNodeId == m_nodeId) m_doc.selectedNodeId = 0;
	}

	void RemoveNodeCommand::Undo()
	{
		m_doc.graph.nodes.push_back(m_node);
		for (const auto& l : m_removedLinks) m_doc.graph.links.push_back(l);
	}

	// ---- MoveNodeCommand ----

	MoveNodeCommand::MoveNodeCommand(RoutineGraphDocument& doc, uint32_t nodeId,
		float oldX, float oldY, float newX, float newY)
		: m_doc(doc), m_nodeId(nodeId), m_oldX(oldX), m_oldY(oldY), m_newX(newX), m_newY(newY) {}

	void MoveNodeCommand::Execute()
	{
		if (RoutineNode* n = m_doc.FindNode(m_nodeId)) { n->canvasX = m_newX; n->canvasY = m_newY; }
	}

	void MoveNodeCommand::Undo()
	{
		if (RoutineNode* n = m_doc.FindNode(m_nodeId)) { n->canvasX = m_oldX; n->canvasY = m_oldY; }
	}

	bool MoveNodeCommand::TryMerge(const ICommand& other)
	{
		const auto* o = dynamic_cast<const MoveNodeCommand*>(&other);
		if (!o || o->m_nodeId != m_nodeId) return false;
		// Le sommet absorbe : la position finale devient celle du nouveau move.
		m_newX = o->m_newX;
		m_newY = o->m_newY;
		return true;
	}

	// ---- AddLinkCommand ----

	AddLinkCommand::AddLinkCommand(RoutineGraphDocument& doc, RoutineLink link)
		: m_doc(doc), m_link(link) {}

	void AddLinkCommand::Execute() { m_doc.graph.links.push_back(m_link); }

	void AddLinkCommand::Undo()
	{
		auto& links = m_doc.graph.links;
		links.erase(std::remove_if(links.begin(), links.end(),
			[this](const RoutineLink& l) { return l.id == m_link.id; }), links.end());
	}

	// ---- RemoveLinkCommand ----

	RemoveLinkCommand::RemoveLinkCommand(RoutineGraphDocument& doc, uint32_t linkId)
		: m_doc(doc), m_linkId(linkId) {}

	void RemoveLinkCommand::Execute()
	{
		auto& links = m_doc.graph.links;
		if (!m_captured)
		{
			for (const auto& l : links)
				if (l.id == m_linkId) { m_link = l; m_captured = true; break; }
		}
		links.erase(std::remove_if(links.begin(), links.end(),
			[this](const RoutineLink& l) { return l.id == m_linkId; }), links.end());
	}

	void RemoveLinkCommand::Undo()
	{
		if (m_captured) m_doc.graph.links.push_back(m_link);
	}

	// ---- SetNodePropertyCommand ----

	SetNodePropertyCommand::SetNodePropertyCommand(RoutineGraphDocument& doc, uint32_t nodeId,
		RoutineProperty newValue)
		: m_doc(doc), m_nodeId(nodeId), m_newValue(std::move(newValue)) {}

	void SetNodePropertyCommand::Execute()
	{
		RoutineNode* n = m_doc.FindNode(m_nodeId);
		if (!n) return;
		for (auto& p : n->properties)
		{
			if (p.key == m_newValue.key)
			{
				m_oldValue = p;
				m_existed = true;
				p = m_newValue;
				return;
			}
		}
		// Propriété absente : on l'ajoute (Undo la retirera).
		m_existed = false;
		n->properties.push_back(m_newValue);
	}

	void SetNodePropertyCommand::Undo()
	{
		RoutineNode* n = m_doc.FindNode(m_nodeId);
		if (!n) return;
		if (m_existed)
		{
			for (auto& p : n->properties)
				if (p.key == m_newValue.key) { p = m_oldValue; return; }
		}
		else
		{
			auto& props = n->properties;
			props.erase(std::remove_if(props.begin(), props.end(),
				[this](const RoutineProperty& p) { return p.key == m_newValue.key; }), props.end());
		}
	}
}
