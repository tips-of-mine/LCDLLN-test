// M100.34 — Implémentation DeleteCommand (ICommand réversible).

#include "src/world_editor/DeleteCommand.h"

#include <algorithm>

namespace engine::editor::world
{
	using engine::world::instances::PropInstance;

	DeleteCommand::DeleteCommand(std::vector<PropInstance>& target, std::vector<uint32_t> instanceIds)
		: m_target(target)
		, m_ids(std::move(instanceIds))
	{
	}

	size_t DeleteCommand::GetMemoryFootprint() const
	{
		return sizeof(DeleteCommand)
			+ m_ids.capacity() * sizeof(uint32_t)
			+ m_removed.capacity() * sizeof(std::pair<size_t, PropInstance>);
	}

	void DeleteCommand::Execute()
	{
		m_removed.clear();
		// Parcourt en mémorisant l'index d'origine ; reconstruit le vecteur sans
		// les ids supprimés. Index d'origine = position AVANT suppression, pour
		// une réinsertion exacte au Undo.
		std::vector<PropInstance> kept;
		kept.reserve(m_target.size());
		for (size_t i = 0; i < m_target.size(); ++i)
		{
			const PropInstance& p = m_target[i];
			const bool toDelete = std::find(m_ids.begin(), m_ids.end(), p.instanceId) != m_ids.end();
			if (toDelete)
				m_removed.emplace_back(i, p);
			else
				kept.push_back(p);
		}
		m_target = std::move(kept);
	}

	void DeleteCommand::Undo()
	{
		// Réinsère dans l'ordre croissant des index d'origine. Comme chaque
		// insertion décale les suivants, et que m_removed est trié par index
		// croissant, insérer à `index` reconstruit l'ordre initial.
		for (const auto& [index, inst] : m_removed)
		{
			const size_t clamped = index <= m_target.size() ? index : m_target.size();
			m_target.insert(m_target.begin() + static_cast<std::ptrdiff_t>(clamped), inst);
		}
		m_removed.clear();
	}
}
