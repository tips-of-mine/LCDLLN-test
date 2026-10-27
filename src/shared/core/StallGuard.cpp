// ETAPE5.2 — garde contre les jugements portés juste après un calage. Voir StallGuard.h.

#include "src/shared/core/StallGuard.h"

#include <algorithm>
#include <cmath>

namespace engine::core
{
	StallGuardConfig SanitizeStallGuard(StallGuardConfig c)
	{
		const StallGuardConfig d{};
		c.stallSeconds = std::isfinite(c.stallSeconds) ? std::clamp(c.stallSeconds, 0.2, 600.0) : d.stallSeconds;
		c.graceSeconds = std::isfinite(c.graceSeconds) ? std::clamp(c.graceSeconds, 0.0, 3600.0) : d.graceSeconds;
		return c;
	}

	StallGuard::StallGuard(StallGuardConfig config)
	{
		Configure(config);
	}

	void StallGuard::Configure(StallGuardConfig config)
	{
		m_config = SanitizeStallGuard(config);
	}

	bool StallGuard::OnLoop(double nowSeconds)
	{
		if (!std::isfinite(nowSeconds))
			return false;
		if (!m_hasPrevious)
		{
			m_hasPrevious = true;
			m_previous = nowSeconds;
			return false;
		}
		const double gap = nowSeconds - m_previous;
		m_previous = nowSeconds;
		// Une horloge qui recule (ne devrait pas arriver en monotone) n'est pas un calage.
		if (!(gap >= m_config.stallSeconds))
			return false;
		++m_stallCount;
		m_lastStallSeconds = gap;
		m_suspendedUntil = nowSeconds + m_config.graceSeconds;
		return true;
	}

	bool StallGuard::ChecksAllowed(double nowSeconds) const
	{
		if (!m_config.enabled)
			return true;
		return !(nowSeconds < m_suspendedUntil);
	}
}
