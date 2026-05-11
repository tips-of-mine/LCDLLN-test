#pragma once
// CMANGOS.04 (Phase 2.04a) — Spline<T> : interpolation de controle
// points pour les chemins de NPCs (patrouilles, IA, scripts). Header-
// only, testable en isolation.
//
// **Modes supportes** :
//   - Linear : interpolation droite point-a-point. Tres simple, jamais
//     de smooth corner.
//   - CatmullRom : passe par chaque control point (sauf le premier et
//     le dernier qui servent de "tangentes virtuelles"). C'est le mode
//     classique des chemins cmangos.
//
// **Convention** : la spline a N control points. Le parametre global
// t s'etend sur [0, segmentCount()] ou segmentCount() depend du mode :
//   - Linear     : N-1 segments (entre chaque paire consecutive)
//   - CatmullRom : N-3 segments (premier et dernier point = tangentes)
//
// `Evaluate(t)` decompose t en {segment_index, local_t} et appelle la
// formule appropriee. `LengthApprox(steps)` retourne la longueur en
// echantillonnant N pas (utile pour parametriser par distance).
//
// Pure : pas d'allocation hors le vector des control points. Concept
// requis sur T :
//   - T() default-constructible
//   - T operator+(T)
//   - T operator-(T)
//   - T operator*(float)
//   - T.LengthSq() ou .Length() pour LengthApprox (Vec3 OK).

#include "src/shardd/internals/movement/MovementTypedefs.h"

#include <cmath>
#include <cstddef>
#include <vector>

namespace engine::server::shard::movement
{
	enum class SplineMode : uint8_t
	{
		Linear     = 0,
		CatmullRom = 1,
	};

	template<typename T>
	class Spline
	{
	public:
		Spline() = default;

		void SetMode(SplineMode m) noexcept { m_mode = m; }
		SplineMode Mode() const noexcept { return m_mode; }

		void SetPoints(std::vector<T> pts) { m_points = std::move(pts); }
		const std::vector<T>& Points() const noexcept { return m_points; }

		/// Nombre de segments evaluables. 0 si pas assez de points pour
		/// le mode courant (Linear : need ≥ 2 ; CatmullRom : need ≥ 4).
		size_t SegmentCount() const noexcept
		{
			switch (m_mode)
			{
				case SplineMode::Linear:
					return m_points.size() < 2 ? 0 : m_points.size() - 1;
				case SplineMode::CatmullRom:
					return m_points.size() < 4 ? 0 : m_points.size() - 3;
			}
			return 0;
		}

		/// Echantillonne la spline a un parametre global t ∈ [0, SegmentCount()].
		/// Hors de cet intervalle, t est clampe.
		T Evaluate(float t) const
		{
			const size_t segs = SegmentCount();
			if (segs == 0)
				return m_points.empty() ? T{} : m_points.front();

			if (t < 0.0f) t = 0.0f;
			if (t > static_cast<float>(segs)) t = static_cast<float>(segs);

			size_t segIdx = static_cast<size_t>(t);
			if (segIdx >= segs) segIdx = segs - 1;
			const float localT = t - static_cast<float>(segIdx);

			switch (m_mode)
			{
				case SplineMode::Linear:
					return EvalLinear(segIdx, localT);
				case SplineMode::CatmullRom:
					return EvalCatmullRom(segIdx, localT);
			}
			return T{};
		}

		/// Approxime la longueur totale en echantillonnant la spline en
		/// `stepsPerSegment` morceaux uniformes. Plus on monte, plus
		/// c'est precis. 16 est un bon defaut pour des chemins cmangos.
		///
		/// Concept additionnel sur T : T.Length() doit exister.
		float LengthApprox(size_t stepsPerSegment = 16) const
		{
			if (stepsPerSegment == 0)
				stepsPerSegment = 1;
			const size_t segs = SegmentCount();
			if (segs == 0)
				return 0.0f;
			float total = 0.0f;
			const size_t totalSteps = segs * stepsPerSegment;
			T prev = Evaluate(0.0f);
			for (size_t i = 1; i <= totalSteps; ++i)
			{
				const float t = (static_cast<float>(i) / static_cast<float>(totalSteps))
					* static_cast<float>(segs);
				T cur = Evaluate(t);
				const T diff = cur - prev;
				total += diff.Length();
				prev = cur;
			}
			return total;
		}

	private:
		T EvalLinear(size_t segIdx, float localT) const
		{
			const T& p0 = m_points[segIdx];
			const T& p1 = m_points[segIdx + 1];
			return p0 + (p1 - p0) * localT;
		}

		/// Catmull-Rom uniforme, 4 control points par segment.
		/// segIdx ∈ [0, N-4]. Les points utilises sont [segIdx ..segIdx+3].
		/// Le segment retourne va de m_points[segIdx+1] a m_points[segIdx+2].
		T EvalCatmullRom(size_t segIdx, float t) const
		{
			const T& p0 = m_points[segIdx + 0];
			const T& p1 = m_points[segIdx + 1];
			const T& p2 = m_points[segIdx + 2];
			const T& p3 = m_points[segIdx + 3];

			const float t2 = t * t;
			const float t3 = t2 * t;

			// Formule classique : 0.5 * ((2*P1) + (-P0+P2)*t +
			//                            (2*P0-5*P1+4*P2-P3)*t^2 +
			//                            (-P0+3*P1-3*P2+P3)*t^3)
			T result =
				p1 * 2.0f
				+ (p2 - p0) * t
				+ (p0 * 2.0f - p1 * 5.0f + p2 * 4.0f - p3) * t2
				+ (p1 * 3.0f - p0 - p2 * 3.0f + p3) * t3;
			return result * 0.5f;
		}

		std::vector<T> m_points;
		SplineMode     m_mode = SplineMode::Linear;
	};
}
