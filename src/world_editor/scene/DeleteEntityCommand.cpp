// Lot 5 (2026-07-18) — implémentation de DeleteEntityCommand. Voir le header.

#include "src/world_editor/scene/DeleteEntityCommand.h"

#include <utility>

namespace engine::editor::world
{
	DeleteEntityCommand::DeleteEntityCommand(scene::EntityId id, scene::EntityEditOps ops)
		: m_id(id)
		, m_ops(std::move(ops))
	{
	}

	/// Empreinte approx. : la commande + les chaînes du snapshot (chemins glTF,
	/// guid string, noms). Stable entre Execute et Undo (le snapshot ne change
	/// plus après la 1re capture).
	size_t DeleteEntityCommand::GetMemoryFootprint() const
	{
		return sizeof(DeleteEntityCommand)
			+ m_snapshot.layout.guid.size()
			+ m_snapshot.layout.gltfContentRelativePath.size()
			+ m_snapshot.layout.speciesId.size()
			+ m_snapshot.meshInsert.gltfRelativePath.size()
			+ m_snapshot.meshInsert.insertCategory.size()
			+ m_snapshot.meshInsert.displayName.size()
			+ m_snapshot.portal.dungeonTemplateId.size()
			+ m_snapshot.portal.displayName.size()
			+ m_snapshot.portal.decorativeMeshPath.size();
	}

	/// 1er appel : capture le snapshot (index → données + guid stable) puis
	/// retire l'entité. Redo : retire le snapshot mémorisé (par guid).
	void DeleteEntityCommand::Execute()
	{
		if (!m_ops.IsInstalled()) return;
		if (!m_captured)
		{
			m_captured = m_ops.capture(m_id, m_snapshot);
		}
		if (m_captured)
		{
			(void)m_ops.remove(m_snapshot);
		}
	}

	/// Réinsère le snapshot capturé (no-op si le 1er Execute avait échoué).
	void DeleteEntityCommand::Undo()
	{
		if (m_captured && m_ops.IsInstalled())
		{
			(void)m_ops.restore(m_snapshot);
		}
	}
}
