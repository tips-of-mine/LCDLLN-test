#pragma once

// Roadmap-8 (2026-07-19) — Commande d'ajout d'une zone de gameplay
// (undo/redo). Header-only, miroir d'AddSplineCommand (M100.29).

#include "src/world_editor/core/CommandStack.h"
#include "src/world_editor/ZoneDocument.h"

namespace engine::editor::world
{
	/// Ajoute une `GameplayZone` complète au document (Execute) ; Undo retire
	/// la dernière (LIFO — sûr tant que les ajouts de zones passent tous par
	/// cette commande, ce qui est le cas depuis le câblage Roadmap-8).
	/// Contrainte thread : main thread (comme tout `ICommand`).
	class AddGameplayZoneCommand final : public ICommand
	{
	public:
		AddGameplayZoneCommand(ZoneDocument& doc, engine::world::zones::GameplayZone zone)
			: m_doc(doc), m_zone(std::move(zone)) {}

		const char* GetLabel() const override { return "Ajouter une zone de gameplay"; }
		size_t GetMemoryFootprint() const override
		{
			return sizeof(*this) + m_zone.name.capacity()
				+ m_zone.polygon.size() * sizeof(engine::math::Vec3);
		}
		void Execute() override { m_doc.Add(m_zone); }
		void Undo() override { m_doc.RemoveLast(); }

	private:
		ZoneDocument& m_doc;
		engine::world::zones::GameplayZone m_zone;
	};
}
