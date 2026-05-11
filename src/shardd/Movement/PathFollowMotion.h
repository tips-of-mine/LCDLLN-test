#pragma once
// Wave 24 — PathFollowMotion : motion generator qui fait suivre une
// patrouille a une creature. Consume INavmeshProvider pour calculer le
// chemin entre 2 waypoints adjacents (gere les obstacles intermediates).
//
// State machine simplifiee :
//
//   Idle ──Start──> MovingToWaypoint(i) ──Reached──> Waiting(i, waitMs)
//                       │                                  │
//                       │                                  Tick consume wait
//                       │                                  │
//                       Tick advance position              ▼
//                                                   MovingToWaypoint(i+1)
//
// Quand la patrouille est terminee (dernier waypoint atteint), boucle au
// debut (les patrouilles cmangos sont cycliques par defaut).
//
// SCAFFOLD Wave 24a : la position avance "teleport-like" frame par frame
// (pas de smoothing). L'integration avec Spline + interpolation viendra
// dans une Wave ulterieure.

#include "src/shardd/Movement/INavmeshProvider.h"
#include "src/shardd/Movement/WaypointMgr.h"

#include <cmath>
#include <cstdint>

namespace engine::server::movement
{
	enum class PathFollowState : uint8_t
	{
		Idle              = 0,
		MovingToWaypoint  = 1,
		Waiting           = 2,
		Done              = 3,   ///< patrouille complete (avant loop optionnel)
	};

	class PathFollowMotion
	{
	public:
		/// \param waypointMgr lifetime garanti par caller (registry global typiquement)
		/// \param navmesh     lifetime garanti par caller
		PathFollowMotion(const WaypointMgr& waypointMgr, const INavmeshProvider& navmesh)
			: m_waypoints(&waypointMgr), m_navmesh(&navmesh)
		{}

		/// Demarre la patrouille pour \p creatureGuid sur \p mapId. Retourne
		/// false si pas de patrouille enregistree ou map inconnue du navmesh.
		bool Start(CreatureGuid creatureGuid, MapId mapId, PathPoint startPos)
		{
			if (!m_waypoints->HasPath(creatureGuid)) return false;
			if (!m_navmesh->HasMap(mapId)) return false;
			m_creatureGuid = creatureGuid;
			m_mapId        = mapId;
			m_currentPos   = startPos;
			m_currentIdx   = 0;
			m_state        = PathFollowState::MovingToWaypoint;
			m_waitElapsed  = 0;
			return true;
		}

		/// Avance la state machine. \p deltaMs ms ecoulees depuis le dernier
		/// tick. \p speedMetersPerSec vitesse de la creature.
		///
		/// Retourne true si la creature est sur un waypoint ET le scriptId
		/// associe doit etre execute (cas script_id != 0). Le caller dispatch
		/// alors le DBScript. False sinon.
		bool Tick(uint32_t deltaMs, float speedMetersPerSec)
		{
			if (m_state == PathFollowState::Idle || m_state == PathFollowState::Done)
				return false;

			const auto& path = m_waypoints->Path(m_creatureGuid);
			if (path.empty())
			{
				m_state = PathFollowState::Done;
				return false;
			}

			if (m_state == PathFollowState::Waiting)
			{
				const uint32_t targetWait = path[m_currentIdx].waitMs;
				m_waitElapsed += deltaMs;
				if (m_waitElapsed >= targetWait)
				{
					// Avance au waypoint suivant (loop a la fin).
					m_waitElapsed = 0;
					m_currentIdx = (m_currentIdx + 1) % path.size();
					m_state = PathFollowState::MovingToWaypoint;
				}
				return false;
			}

			// MovingToWaypoint : avance vers path[m_currentIdx].
			const Waypoint& target = path[m_currentIdx];
			const float dx = target.position.x - m_currentPos.x;
			const float dy = target.position.y - m_currentPos.y;
			const float dz = target.position.z - m_currentPos.z;
			const float distSq = dx*dx + dy*dy + dz*dz;

			// Tolerance de 0.5m pour "atteint".
			constexpr float kReachedToleranceSq = 0.25f;
			if (distSq <= kReachedToleranceSq)
			{
				m_currentPos = target.position;
				m_state = (target.waitMs > 0)
					? PathFollowState::Waiting
					: PathFollowState::MovingToWaypoint;
				if (m_state == PathFollowState::MovingToWaypoint)
				{
					m_currentIdx = (m_currentIdx + 1) % path.size();
				}
				// Si scriptId non-zero, signal au caller.
				return target.scriptId != 0;
			}

			// Avance teleport-like (scaffold ; interpolation Spline plus tard).
			// step = vitesse * deltaMs / 1000
			const float step = speedMetersPerSec * (static_cast<float>(deltaMs) / 1000.0f);
			const float distInv = 1.0f / std::sqrt(distSq);
			m_currentPos.x += dx * distInv * step;
			m_currentPos.y += dy * distInv * step;
			m_currentPos.z += dz * distInv * step;
			return false;
		}

		/// Stop la patrouille. State -> Idle.
		void Stop() noexcept
		{
			m_state = PathFollowState::Idle;
			m_currentIdx = 0;
			m_waitElapsed = 0;
		}

		PathFollowState State() const noexcept { return m_state; }
		PathPoint       CurrentPos() const noexcept { return m_currentPos; }
		uint32_t        CurrentIdx() const noexcept { return m_currentIdx; }
		CreatureGuid    Creature() const noexcept { return m_creatureGuid; }

	private:
		const WaypointMgr*       m_waypoints;
		const INavmeshProvider*  m_navmesh;
		CreatureGuid             m_creatureGuid = 0;
		MapId                    m_mapId        = 0;
		PathPoint                m_currentPos{};
		uint32_t                 m_currentIdx   = 0;
		uint32_t                 m_waitElapsed  = 0;
		PathFollowState          m_state        = PathFollowState::Idle;
	};
}
