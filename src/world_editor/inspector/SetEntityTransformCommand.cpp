#include "src/world_editor/inspector/SetEntityTransformCommand.h"

#include <utility>

namespace engine::editor::world
{
	namespace
	{
		/// Encode l'`EntityId` en clé de fusion non nulle pour les entités
		/// réelles (kind >= 1). Pour `None`/index 0 → 0 (= pas de fusion).
		CommandMergeKey MakeMergeKey(scene::EntityId id)
		{
			return (static_cast<CommandMergeKey>(static_cast<uint32_t>(id.kind)) << 32)
				| static_cast<CommandMergeKey>(id.index);
		}
	}

	SetEntityTransformCommand::SetEntityTransformCommand(scene::EntityId id,
		const scene::EntityTransform& oldT,
		const scene::EntityTransform& newT,
		Writer writer)
		: m_id(id)
		, m_old(oldT)
		, m_new(newT)
		, m_writer(std::move(writer))
		, m_mergeKey(MakeMergeKey(id))
	{
	}

	void SetEntityTransformCommand::Execute()
	{
		if (m_writer) m_writer(m_id, m_new);
	}

	void SetEntityTransformCommand::Undo()
	{
		if (m_writer) m_writer(m_id, m_old);
	}

	bool SetEntityTransformCommand::TryMerge(const ICommand& other)
	{
		const auto* o = dynamic_cast<const SetEntityTransformCommand*>(&other);
		if (o == nullptr) return false;
		if (o->m_id != m_id) return false;
		// La commande au sommet (this) absorbe : la cible finale devient le
		// 'new' de la plus récente, mais on garde notre 'm_old' initial pour
		// qu'un Undo ramène à l'état d'avant le tout début du geste.
		m_new = o->m_new;
		return true;
	}
}
