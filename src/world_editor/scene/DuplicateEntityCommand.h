#pragma once

#include "src/world_editor/core/CommandStack.h"
#include "src/world_editor/scene/EntityEditOps.h"

namespace engine::editor::world
{
	/// Lot 5 (2026-07-18) — Commande undoable de DUPLICATION de l'entité de
	/// scène sélectionnée (LayoutInstance / MeshInsert / DungeonPortal).
	/// Découplée des documents concrets via les foncteurs `EntityEditOps`
	/// installés par l'Engine (pattern `SetEntityTransformCommand::Writer`).
	///
	/// `Execute` (1er appel) capture l'entité source puis crée une copie
	/// (nouveau guid + léger décalage de position, cf. le foncteur
	/// `duplicate`) et mémorise le snapshot de la copie ; les Redo suivants
	/// réinsèrent LA MÊME copie (guid conservé — historique stable). `Undo`
	/// retire la copie (par guid).
	///
	/// Contrainte thread : main thread (comme tout `ICommand`).
	class DuplicateEntityCommand final : public ICommand
	{
	public:
		/// \param id  Entité source à dupliquer (kind + index de scène AU
		///        MOMENT du Push — converti en snapshot au 1er Execute).
		/// \param ops Foncteurs d'édition structurelle (doivent survivre à la
		///        commande dans la pile undo — capture Engine `[this]`).
		DuplicateEntityCommand(scene::EntityId id, scene::EntityEditOps ops);

		const char* GetLabel() const override { return "Dupliquer l'entité"; }
		size_t GetMemoryFootprint() const override;
		void Execute() override;
		void Undo() override;

	private:
		scene::EntityId       m_id;
		scene::EntityEditOps  m_ops;
		/// Snapshot de la COPIE créée au 1er Execute (guid définitif — les
		/// Redo réinsèrent ce snapshot tel quel).
		scene::EntitySnapshot m_copy;
		/// true dès que le 1er Execute a créé la copie avec succès ; tant que
		/// false, Undo est un no-op (rien n'a été ajouté).
		bool m_created = false;
	};
}
