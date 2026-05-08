#pragma once
// CMANGOS.04 (Phase 2.04b) — MoveSpline : runtime state d'un
// deplacement scriptable (NPC / IA). S'appuie sur Spline<Vec3> (#493)
// pour la geometrie + MoveSplineFlag pour le mode.
//
// **Couvre** :
//   - Etat runtime : spline + parametre courant + vitesse + flags +
//     identifiant monotone (`splineId`).
//   - `UpdatePosition(now)` : avance le parametre selon le temps
//     ecoule et la vitesse. Retourne true tant que le mouvement n'est
//     pas termine.
//   - `CurrentPosition()` : interpolation Vec3 actuelle.
//   - `IsFinished()` : true si on a atteint la fin (ou jamais lance).
//   - Cyclic : boucle au lieu de finir.
//
// **Hors scope** :
//   - Builder fluide `MoveSplineInit` (cf. MoveSplineInit.h).
//   - Sérialisation reseau (sub-PR ulterieure : MoveSplinePacketBuilder).
//   - Interpolation client (sub-PR client : MoveSplineInterpolator).

#include "engine/server/shard/movement/MoveSplineFlag.h"
#include "engine/server/shard/movement/MovementTypedefs.h"
#include "engine/server/shard/movement/Spline.h"

#include <chrono>
#include <cstdint>

namespace engine::server::shard::movement
{
	class MoveSpline
	{
	public:
		using Clock     = std::chrono::steady_clock;
		using TimePoint = Clock::time_point;

		MoveSpline() = default;

		/// Identifiant monotone strictement croissant. Permet au client
		/// d'ignorer les paquets out-of-order (cf. audit §8 "Spline ID
		/// monotone").
		uint32_t SplineId() const noexcept { return m_splineId; }

		/// Flags bitmask. Default `None`.
		MoveSplineFlag Flags() const noexcept { return m_flags; }

		/// Acces direct a la spline geometrique (debug / serialisation).
		const Spline<Vec3>& Path() const noexcept { return m_path; }

		/// Vitesse en unites/sec (typiquement m/s). 0 → mouvement fige.
		float Velocity() const noexcept { return m_velocity; }

		/// Temps de depart (steady_clock).
		TimePoint StartTime() const noexcept { return m_startTime; }

		/// Position courante apres `UpdatePosition(now)`. Si le
		/// mouvement n'a pas encore commence, retourne le premier
		/// control point (ou Vec3 zero si la spline est vide).
		Vec3 CurrentPosition() const noexcept { return m_currentPos; }

		/// Distance parcourue (cumulee depuis StartTime).
		float DistanceTraveled() const noexcept { return m_distance; }

		/// True si le mouvement est arrive a la fin (et que cyclic est
		/// false). False pour un mouvement en cours OU pour une spline
		/// non encore lancee.
		bool IsFinished() const noexcept { return m_finished; }

		/// Avance la simulation jusqu'au temps \p now. A appeler chaque
		/// tick depuis le shard. Premier appel = "Launch" implicite si
		/// SplineId != 0. Retourne `true` si le mouvement est encore
		/// actif (peut etre re-tick), `false` une fois termine.
		bool UpdatePosition(TimePoint now);

		/// Setup interne — utilise par MoveSplineInit::Launch.
		///
		/// \param id      Identifiant monotone (caller doit garantir
		///                strictement croissant cote shard).
		/// \param path    Spline geometrique (deja peuplee).
		/// \param flags   Bitmask MoveSplineFlag.
		/// \param velocity Unites/sec. > 0.
		/// \param startTime Temps de depart (steady_clock).
		void Launch(uint32_t id, Spline<Vec3> path, MoveSplineFlag flags,
			float velocity, TimePoint startTime);

	private:
		Spline<Vec3>    m_path;
		MoveSplineFlag  m_flags     = MoveSplineFlag::None;
		uint32_t        m_splineId  = 0;
		float           m_velocity  = 0.0f;
		TimePoint       m_startTime{};
		Vec3            m_currentPos{};
		float           m_distance  = 0.0f;     ///< distance cumulee parcourue
		float           m_totalLen  = 0.0f;     ///< longueur totale de la spline
		bool            m_finished  = true;     ///< true tant qu'on n'est pas Launch
	};
}
