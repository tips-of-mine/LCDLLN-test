#include "engine/server/shard/movement/MoveSpline.h"

#include <cmath>

namespace engine::server::shard::movement
{
	void MoveSpline::Launch(uint32_t id, Spline<Vec3> path, MoveSplineFlag flags,
		float velocity, TimePoint startTime)
	{
		m_path      = std::move(path);
		m_flags     = flags;
		m_splineId  = id;
		m_velocity  = velocity;
		m_startTime = startTime;
		m_distance  = 0.0f;
		m_finished  = false;

		// Mode CR si flag, sinon Linear.
		m_path.SetMode(HasFlag(flags, MoveSplineFlag::Catmullrom)
			? SplineMode::CatmullRom : SplineMode::Linear);

		m_totalLen = m_path.LengthApprox(16);

		// Position initiale = debut de la spline. En CR, c'est P1
		// (premier point intérieur), pas P0.
		m_currentPos = m_path.Evaluate(0.0f);
	}

	bool MoveSpline::UpdatePosition(TimePoint now)
	{
		if (m_finished || m_velocity <= 0.0f || m_totalLen <= 0.0f)
			return !m_finished;

		const auto elapsedSec = std::chrono::duration_cast<
			std::chrono::duration<float>>(now - m_startTime).count();
		if (elapsedSec < 0.0f)
			return true;  // before launch — pas de progression

		float traveled = elapsedSec * m_velocity;

		// Cyclic : on enroule sur la longueur totale.
		const bool cyclic = HasFlag(m_flags, MoveSplineFlag::Cyclic);
		if (cyclic && m_totalLen > 0.0f)
		{
			traveled = std::fmod(traveled, m_totalLen);
		}
		else if (traveled >= m_totalLen)
		{
			// Fin du mouvement (mode non-cyclic).
			m_distance = m_totalLen;
			const float segs = static_cast<float>(m_path.SegmentCount());
			m_currentPos = m_path.Evaluate(segs);
			m_finished = true;
			return false;
		}

		m_distance = traveled;

		// Mapper la distance lineaire sur le parametre global [0, segCount].
		// Approximation : distance / totalLen * segCount. Pour des splines
		// CR avec courbure variable, c'est inexact mais suffisant en
		// premiere mise (les chemins typiques cmangos ont peu de courbure
		// extreme).
		const float segs = static_cast<float>(m_path.SegmentCount());
		const float t = (m_totalLen > 0.0f)
			? (traveled / m_totalLen) * segs
			: 0.0f;
		m_currentPos = m_path.Evaluate(t);
		return true;
	}
}
