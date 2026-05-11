#pragma once
// Wave 24 — StubNavmeshProvider : impl mock pour tests et bootstrap.
// Retourne UN chemin direct (from, to) si la map est connue, sans aucune
// logique de navmesh reelle (pas de collision, pas d'obstacles).
//
// Permet aux tests upstream (PathFollowMotion, WaypointMgr) de fonctionner
// sans dependance vcpkg recastnavigation. Sera remplace par
// RecastNavmeshProvider une fois la dep ajoutee (PR humaine ulterieure).

#include "src/shardd/Movement/INavmeshProvider.h"

#include <unordered_set>

namespace engine::server::movement
{
	class StubNavmeshProvider final : public INavmeshProvider
	{
	public:
		StubNavmeshProvider() = default;

		/// Enregistre une mapId comme "connue". FindPath retournera un chemin
		/// direct si la map est dans le set.
		void RegisterMap(MapId mapId) { m_knownMaps.insert(mapId); }

		bool HasMap(MapId mapId) const override
		{
			return m_knownMaps.count(mapId) > 0;
		}

		/// Stub : retourne {from, to} si la map est connue, vide sinon.
		/// Pas d'obstacle, pas de raycast.
		PathResult FindPath(const PathRequest& req) const override
		{
			PathResult result;
			if (!HasMap(req.mapId)) return result;
			result.waypoints.push_back(req.from);
			result.waypoints.push_back(req.to);
			return result;
		}

	private:
		std::unordered_set<MapId> m_knownMaps;
	};
}
