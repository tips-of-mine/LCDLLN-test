#pragma once

#include "src/world_editor/core/CommandStack.h"        // ICommand
#include "src/world_editor/scene/EditorSelection.h"     // EntityId
#include "src/world_editor/ui/WorldMapEditDocument.h"   // WorldMapEditLayoutInstance
#include "src/world_editor/volumes/MeshInsertInstance.h"
#include "src/world_editor/volumes/dungeons/DungeonPortalInstance.h"

#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

namespace engine::editor::scene
{
	/// Snapshot des entités retirées (index d'origine + copie) par type, pour
	/// l'undo exact. Rempli par le `RemoveFn`, consommé par le `RestoreFn`.
	struct DeletedEntities
	{
		std::vector<std::pair<uint32_t, engine::editor::WorldMapEditLayoutInstance>>     layout;
		std::vector<std::pair<uint32_t, engine::editor::world::volumes::MeshInsertInstance>>          mesh;
		std::vector<std::pair<uint32_t, engine::editor::world::volumes::dungeons::DungeonPortalInstance>> dungeon;
	};

	/// Commande réversible de suppression d'un ensemble d'entités sélectionnées
	/// (modèle vivant : layoutInstances + mesh inserts + dungeon portals).
	///
	/// Découplée des documents concrets via deux callbacks fournis par l'Engine :
	/// `RemoveFn` retire les entités et renvoie le snapshot ; `RestoreFn` les
	/// réinsère depuis le snapshot. Implémente le contrat ICommand undo/redo.
	///
	/// Limite assumée (cf. spec §4.4) : `EntityId.index` n'est pas stable après
	/// édition structurelle ; la commande suppose un undo/redo linéaire immédiat
	/// (même compromis que SetEntityTransformCommand).
	class DeleteEntitiesCommand : public engine::editor::world::ICommand
	{
	public:
		using RemoveFn  = std::function<DeletedEntities(const std::vector<EntityId>&)>;
		using RestoreFn = std::function<void(const DeletedEntities&)>;

		DeleteEntitiesCommand(std::vector<EntityId> ids, RemoveFn remove, RestoreFn restore)
			: m_ids(std::move(ids)), m_remove(std::move(remove)), m_restore(std::move(restore)) {}

		const char* GetLabel() const override { return "Supprimer la sélection"; }
		size_t GetMemoryFootprint() const override;
		void Execute() override;
		void Undo() override;

	private:
		std::vector<EntityId> m_ids;
		RemoveFn  m_remove;
		RestoreFn m_restore;
		DeletedEntities m_snapshot;
	};
}
