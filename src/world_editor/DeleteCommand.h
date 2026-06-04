#pragma once

// M100.34 — DeleteCommand : suppression réversible d'un ensemble de props
// (sélection). Implémente ICommand (undo/redo). Opère directement sur le
// vecteur de PropInstance du document éditeur (référence non-owning).
//
// Execute : retire les props dont l'instanceId est dans le set ; mémorise les
// instances retirées + leur index d'origine pour pouvoir les réinsérer.
// Undo : réinsère les instances à leur position d'origine (ordre préservé).

#include <cstdint>
#include <utility>
#include <vector>

#include "src/client/world/instances/PropInstances.h"
#include "src/world_editor/core/CommandStack.h"

namespace engine::editor::world
{
	/// Commande de suppression d'une sélection de props (réversible).
	class DeleteCommand : public ICommand
	{
	public:
		/// \param target       vecteur de props du document (modifié en place).
		/// \param instanceIds  identifiants d'instance à supprimer.
		DeleteCommand(std::vector<engine::world::instances::PropInstance>& target,
			std::vector<uint32_t> instanceIds);

		const char* GetLabel() const override { return "Delete selection"; }
		size_t GetMemoryFootprint() const override;
		void Execute() override;
		void Undo() override;

	private:
		std::vector<engine::world::instances::PropInstance>& m_target;
		std::vector<uint32_t> m_ids;
		/// Sauvegarde pour Undo : (indexOriginal, instance). Trié par index
		/// croissant pour réinsertion déterministe.
		std::vector<std::pair<size_t, engine::world::instances::PropInstance>> m_removed;
	};
}
