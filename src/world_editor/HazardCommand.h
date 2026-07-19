#pragma once

// Roadmap-8 (2026-07-19) — Commande de pose d'un hazard (undo/redo).
// Header-only, miroir d'AddSplineCommand (M100.29).

#include "src/world_editor/core/CommandStack.h"
#include "src/world_editor/HazardDocument.h"

namespace engine::editor::world
{
	/// Pose un `HazardVolume` complet dans le document (Execute) ; Undo retire
	/// le dernier (LIFO — sûr tant que les poses passent toutes par cette
	/// commande, ce qui est le cas depuis le câblage Roadmap-8).
	/// Contrainte thread : main thread (comme tout `ICommand`).
	class AddHazardCommand final : public ICommand
	{
	public:
		AddHazardCommand(HazardDocument& doc, engine::world::hazard::HazardVolume hazard)
			: m_doc(doc), m_hazard(hazard) {}

		const char* GetLabel() const override { return "Poser un danger"; }
		size_t GetMemoryFootprint() const override { return sizeof(*this); }
		void Execute() override { m_doc.Add(m_hazard); }
		void Undo() override
		{
			auto& list = m_doc.Mutable();
			if (!list.empty()) list.pop_back();
		}

	private:
		HazardDocument& m_doc;
		engine::world::hazard::HazardVolume m_hazard;
	};
}
