#pragma once
// Wave 24 — WaypointMgr : registry des patrouilles creature_movement par
// CreatureGuid. Charges depuis la migration 0062 au boot, accede en
// lecture seule au runtime.
//
// Conception : indexe par creatureGuid -> vector<Waypoint> ordonne par
// pointIdx. Lookup O(1) "donne-moi la patrouille de cette creature".

#include "src/shardd/Movement/Waypoint.h"

#include <algorithm>
#include <cstddef>
#include <unordered_map>
#include <vector>

namespace engine::server::movement
{
	class WaypointMgr
	{
	public:
		WaypointMgr() = default;

		/// Ajoute un waypoint a la patrouille de \p wp.creatureGuid.
		/// Les insertions sont triees par pointIdx (sort lazy a la 1ere
		/// recuperation via Path).
		void AddWaypoint(Waypoint wp)
		{
			auto& list = m_byCreature[wp.creatureGuid];
			list.push_back(wp);
			m_dirty[wp.creatureGuid] = true;
		}

		/// Retourne la patrouille triee de \p creatureGuid. Vide si aucune.
		/// Le tri est fait lazy au premier appel post-AddWaypoint.
		const std::vector<Waypoint>& Path(CreatureGuid creatureGuid) const
		{
			auto it = m_byCreature.find(creatureGuid);
			if (it == m_byCreature.end()) return s_empty;
			// Tri lazy : si dirty, sort + clear flag.
			auto dIt = m_dirty.find(creatureGuid);
			if (dIt != m_dirty.end() && dIt->second)
			{
				auto& vec = const_cast<std::vector<Waypoint>&>(it->second);
				std::sort(vec.begin(), vec.end(),
					[](const Waypoint& a, const Waypoint& b) {
						return a.pointIdx < b.pointIdx;
					});
				m_dirty[creatureGuid] = false;
			}
			return it->second;
		}

		/// True si \p creatureGuid a au moins 1 waypoint enregistre.
		bool HasPath(CreatureGuid creatureGuid) const
		{
			auto it = m_byCreature.find(creatureGuid);
			return it != m_byCreature.end() && !it->second.empty();
		}

		size_t PathCount() const noexcept { return m_byCreature.size(); }

		void Clear() noexcept
		{
			m_byCreature.clear();
			m_dirty.clear();
		}

	private:
		std::unordered_map<CreatureGuid, std::vector<Waypoint>> m_byCreature;
		mutable std::unordered_map<CreatureGuid, bool>          m_dirty;
		static inline const std::vector<Waypoint> s_empty{};
	};
}
