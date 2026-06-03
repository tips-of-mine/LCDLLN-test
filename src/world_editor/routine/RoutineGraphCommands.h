#pragma once

// M101.4 / M101.5 — Commandes d'édition de graphe (undo/redo via CommandStack).
//
// Toutes les mutations du RoutineGraphDocument passent par ces ICommand, pour
// que l'historique reste cohérent (cf. HistoryPanel). Logique pure (aucun
// ImGui) : testable headless en CI Linux.

#include <string>
#include <vector>

#include "src/world_editor/core/CommandStack.h"
#include "src/world_editor/routine/RoutineGraphDocument.h"

namespace engine::editor::world
{
	/// Ajoute un nœud (et le sélectionne). Undo le retire.
	class AddNodeCommand final : public ICommand
	{
	public:
		AddNodeCommand(RoutineGraphDocument& doc, engine::routine::RoutineNode node);
		const char* GetLabel() const override { return "Add node"; }
		size_t GetMemoryFootprint() const override;
		void Execute() override;
		void Undo() override;
	private:
		RoutineGraphDocument& m_doc;
		engine::routine::RoutineNode m_node;
		uint32_t m_prevSelected = 0;
	};

	/// Retire un nœud et ses liens incidents. Undo les restaure.
	class RemoveNodeCommand final : public ICommand
	{
	public:
		RemoveNodeCommand(RoutineGraphDocument& doc, uint32_t nodeId);
		const char* GetLabel() const override { return "Remove node"; }
		size_t GetMemoryFootprint() const override;
		void Execute() override;
		void Undo() override;
	private:
		RoutineGraphDocument& m_doc;
		uint32_t m_nodeId;
		engine::routine::RoutineNode m_node;          // copie pour restauration
		std::vector<engine::routine::RoutineLink> m_removedLinks;
		bool m_captured = false;
	};

	/// Déplace un nœud. Coalesçable pendant un drag (même mergeKey = nodeId).
	class MoveNodeCommand final : public ICommand
	{
	public:
		MoveNodeCommand(RoutineGraphDocument& doc, uint32_t nodeId,
		                float oldX, float oldY, float newX, float newY);
		const char* GetLabel() const override { return "Move node"; }
		size_t GetMemoryFootprint() const override { return sizeof(*this); }
		CommandMergeKey GetMergeKey() const override { return m_nodeId; }
		void Execute() override;
		void Undo() override;
		bool TryMerge(const ICommand& other) override;
	private:
		RoutineGraphDocument& m_doc;
		uint32_t m_nodeId;
		float m_oldX, m_oldY, m_newX, m_newY;
	};

	/// Ajoute un lien. Undo le retire.
	class AddLinkCommand final : public ICommand
	{
	public:
		AddLinkCommand(RoutineGraphDocument& doc, engine::routine::RoutineLink link);
		const char* GetLabel() const override { return "Add link"; }
		size_t GetMemoryFootprint() const override { return sizeof(*this); }
		void Execute() override;
		void Undo() override;
	private:
		RoutineGraphDocument& m_doc;
		engine::routine::RoutineLink m_link;
	};

	/// Retire un lien. Undo le restaure.
	class RemoveLinkCommand final : public ICommand
	{
	public:
		RemoveLinkCommand(RoutineGraphDocument& doc, uint32_t linkId);
		const char* GetLabel() const override { return "Remove link"; }
		size_t GetMemoryFootprint() const override { return sizeof(*this); }
		void Execute() override;
		void Undo() override;
	private:
		RoutineGraphDocument& m_doc;
		uint32_t m_linkId;
		engine::routine::RoutineLink m_link;
		bool m_captured = false;
	};

	/// Modifie une propriété d'un nœud (par clé). Undo restaure l'ancienne.
	class SetNodePropertyCommand final : public ICommand
	{
	public:
		SetNodePropertyCommand(RoutineGraphDocument& doc, uint32_t nodeId,
		                       engine::routine::RoutineProperty newValue);
		const char* GetLabel() const override { return "Set node property"; }
		size_t GetMemoryFootprint() const override { return sizeof(*this); }
		void Execute() override;
		void Undo() override;
	private:
		RoutineGraphDocument& m_doc;
		uint32_t m_nodeId;
		engine::routine::RoutineProperty m_newValue;
		engine::routine::RoutineProperty m_oldValue;
		bool m_existed = false;
	};
}
