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
		c.maxSuspendSeconds = std::isfinite(c.maxSuspendSeconds)
			? std::clamp(c.maxSuspendSeconds, c.graceSeconds, 7200.0)
			: (std::max)(d.maxSuspendSeconds, c.graceSeconds);
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
		// Nouvelle série si la suspension précédente était terminée.
		if (!(nowSeconds < m_suspendedUntil))
			m_chainStart = nowSeconds;
		m_suspendedUntil = nowSeconds + m_config.graceSeconds;
		return true;
	}

	void StallGuard::Resync(double nowSeconds)
	{
		if (!std::isfinite(nowSeconds))
			return;
		m_hasPrevious = true;
		m_previous = nowSeconds;
	}

	bool StallGuard::ChecksAllowed(double nowSeconds) const
	{
		if (!m_config.enabled)
			return true;
		if (!(nowSeconds < m_suspendedUntil))
			return true;
		// Suspension plafonnée depuis le premier calage de la série.
		return nowSeconds - m_chainStart >= m_config.maxSuspendSeconds;
	}

	bool StallGuard::SuspensionCapped(double nowSeconds) const
	{
		return m_config.enabled && nowSeconds < m_suspendedUntil
			&& nowSeconds - m_chainStart >= m_config.maxSuspendSeconds;
	}
}
