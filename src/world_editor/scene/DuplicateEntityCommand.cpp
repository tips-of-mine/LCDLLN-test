// Lot 5 (2026-07-18) — implémentation de DuplicateEntityCommand. Voir le header.

#include "src/world_editor/scene/DuplicateEntityCommand.h"

#include <utility>

namespace engine::editor::world
{
	DuplicateEntityCommand::DuplicateEntityCommand(scene::EntityId id, scene::EntityEditOps ops)
		: m_id(id)
		, m_ops(std::move(ops))
	{
	}

	/// Empreinte approx. : la commande + les chaînes du snapshot de la copie.
	/// Stable entre Execute et Undo (la copie ne change plus après création).
	size_t DuplicateEntityCommand::GetMemoryFootprint() const
	{
		return sizeof(DuplicateEntityCommand)
			+ m_copy.layout.guid.size()
			+ m_copy.layout.gltfContentRelativePath.size()
			+ m_copy.layout.speciesId.size()
			+ m_copy.meshInsert.gltfRelativePath.size()
			+ m_copy.meshInsert.insertCategory.size()
			+ m_copy.meshInsert.displayName.size()
			+ m_copy.portal.dungeonTemplateId.size()
			+ m_copy.portal.displayName.size()
			+ m_copy.portal.decorativeMeshPath.size();
	}

	/// 1er appel : capture la source puis crée la copie (nouveau guid +
	/// décalage). Redo : réinsère la même copie (guid conservé).
	void DuplicateEntityCommand::Execute()
	{
		if (!m_ops.IsInstalled()) return;
		if (!m_created)
		{
			scene::EntitySnapshot src;
			if (m_ops.capture(m_id, src))
			{
				m_created = m_ops.duplicate(src, m_copy);
			}
		}
		else
		{
			(void)m_ops.restore(m_copy);
		}
	}

	/// Retire la copie créée (no-op si le 1er Execute avait échoué).
	void DuplicateEntityCommand::Undo()
	{
		if (m_created && m_ops.IsInstalled())
		{
			(void)m_ops.remove(m_copy);
		}
	}
}
