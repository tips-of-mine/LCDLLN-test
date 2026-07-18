#pragma once

#include "src/world_editor/core/CommandStack.h"
#include "src/world_editor/scene/EntityEditOps.h"

namespace engine::editor::world
{
	/// Lot 5 (2026-07-18) — Commande undoable de SUPPRESSION de l'entité de
	/// scène sélectionnée (LayoutInstance / MeshInsert / DungeonPortal).
	/// Découplée des documents concrets via les foncteurs `EntityEditOps`
	/// installés par l'Engine (pattern `SetEntityTransformCommand::Writer`).
	///
	/// `Execute` (1er appel) capture un snapshot complet de l'entité puis la
	/// retire ; les appels suivants (Redo) retirent le snapshot mémorisé (par
	/// guid stable). `Undo` réinsère le snapshot (au rang d'origine pour les
	/// layout instances, par guid pour les volumes).
	///
	/// Contrainte thread : main thread (comme tout `ICommand`).
	class DeleteEntityCommand final : public ICommand
	{
	public:
		/// \param id  Entité à supprimer (kind + index de scène AU MOMENT du
		///        Push — l'index n'est utilisé qu'au 1er Execute, la capture
		///        immédiate le convertit en snapshot à guid stable).
		/// \param ops Foncteurs d'édition structurelle (doivent survivre à la
		///        commande dans la pile undo — capture Engine `[this]`).
		DeleteEntityCommand(scene::EntityId id, scene::EntityEditOps ops);

		const char* GetLabel() const override { return "Supprimer l'entité"; }
		size_t GetMemoryFootprint() const override;
		void Execute() override;
		void Undo() override;

	private:
		scene::EntityId       m_id;
		scene::EntityEditOps  m_ops;
		scene::EntitySnapshot m_snapshot;
		/// true dès que le 1er Execute a capturé le snapshot avec succès ;
		/// tant que false, Undo est un no-op (rien n'a été supprimé).
		bool m_captured = false;
	};
}
