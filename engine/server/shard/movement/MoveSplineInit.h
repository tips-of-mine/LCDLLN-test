#pragma once
// CMANGOS.04 (Phase 2.04b) — MoveSplineInit : builder fluide pour
// configurer un MoveSpline avant de le lancer. Pattern classique
// cmangos (`MoveSplineInit init(unit); init.MoveTo(p); init.Launch();`).
//
// Pas de side-effect avant `Launch` : on accumule les points et les
// flags, puis `Launch` configure le MoveSpline du caller. Le builder
// est jetable (un par mouvement).

#include "engine/server/shard/movement/MoveSpline.h"

#include <cstdint>
#include <utility>
#include <vector>

namespace engine::server::shard::movement
{
	class MoveSplineInit
	{
	public:
		MoveSplineInit() = default;

		/// Ajoute un control point a la trajectoire.
		MoveSplineInit& MoveTo(const Vec3& point)
		{
			m_points.push_back(point);
			return *this;
		}

		/// Definit la vitesse (unites/sec). Default 0 = inerte.
		MoveSplineInit& SetVelocity(float v)
		{
			m_velocity = v;
			return *this;
		}

		/// Active un flag (cumulatif).
		MoveSplineInit& AddFlag(MoveSplineFlag f)
		{
			m_flags = m_flags | f;
			return *this;
		}

		/// Sucre syntaxique pour les flags les plus courants.
		MoveSplineInit& SetWalking()      { return AddFlag(MoveSplineFlag::Walking); }
		MoveSplineInit& SetFlying()       { return AddFlag(MoveSplineFlag::Flying); }
		MoveSplineInit& SetSwimming()     { return AddFlag(MoveSplineFlag::Swimming); }
		MoveSplineInit& SetCyclic()       { return AddFlag(MoveSplineFlag::Cyclic); }
		MoveSplineInit& SetCatmullRom()   { return AddFlag(MoveSplineFlag::Catmullrom); }

		/// Configure le \p out MoveSpline avec les valeurs accumulees.
		/// \param newId Identifiant monotone strictement croissant
		///              (caller responsable).
		/// \param now   Temps de depart (steady_clock).
		/// \return false si la configuration est invalide (pas assez de
		///         points pour le mode choisi). Dans ce cas, \p out
		///         n'est pas modifie.
		bool Launch(MoveSpline& out, uint32_t newId, MoveSpline::TimePoint now) const
		{
			// Validation : Linear needs ≥ 2 points, CR ≥ 4.
			const bool cr = HasFlag(m_flags, MoveSplineFlag::Catmullrom);
			const size_t minPoints = cr ? 4 : 2;
			if (m_points.size() < minPoints)
				return false;

			Spline<Vec3> path;
			path.SetMode(cr ? SplineMode::CatmullRom : SplineMode::Linear);
			path.SetPoints(m_points);
			out.Launch(newId, std::move(path), m_flags, m_velocity, now);
			return true;
		}

		/// Acces aux donnees accumulees (pour tests/debug).
		const std::vector<Vec3>& Points() const noexcept { return m_points; }
		MoveSplineFlag Flags() const noexcept { return m_flags; }
		float Velocity() const noexcept { return m_velocity; }

	private:
		std::vector<Vec3> m_points;
		MoveSplineFlag    m_flags    = MoveSplineFlag::None;
		float             m_velocity = 0.0f;
	};
}
