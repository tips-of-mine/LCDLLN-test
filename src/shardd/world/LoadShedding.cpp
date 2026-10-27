// ETAPE4.5 — délestage du shard avec hystérésis. Voir LoadShedding.h.

#include "src/shardd/world/LoadShedding.h"

#include <algorithm>
#include <cmath>

namespace engine::server
{
	namespace
	{
		/// Échantillon maximal pris en compte (en budgets de tick).
		constexpr double kMaxSampleLoad = 4.0;

		double Finite(double value, double fallback)
		{
			return std::isfinite(value) ? value : fallback;
		}
	}

	LoadSheddingConfig SanitizeLoadShedding(LoadSheddingConfig c)
	{
		const LoadSheddingConfig d{};
		c.enterLoad = std::clamp(Finite(c.enterLoad, d.enterLoad), 0.05, 10.0);
		c.exitLoad = std::clamp(Finite(c.exitLoad, d.exitLoad), 0.01, 10.0);
		if (c.exitLoad >= c.enterLoad)
			c.exitLoad = c.enterLoad * 0.7; // la sortie doit rester sous l'entrée
		c.enterHoldSeconds = std::clamp(Finite(c.enterHoldSeconds, d.enterHoldSeconds), 0.0, 600.0);
		c.exitHoldSeconds = std::clamp(Finite(c.exitHoldSeconds, d.exitHoldSeconds), 0.0, 600.0);
		c.minDwellSeconds = std::clamp(Finite(c.minDwellSeconds, d.minDwellSeconds), 0.0, 600.0);
		c.smoothing = std::clamp(Finite(c.smoothing, d.smoothing), 0.001, 1.0);
		return c;
	}

	LoadShedder::LoadShedder(LoadSheddingConfig config)
	{
		Configure(config);
	}

	void LoadShedder::Configure(LoadSheddingConfig config)
	{
		m_config = SanitizeLoadShedding(config);
		if (!m_config.enabled)
		{
			m_active = false;
			m_aboveSince = -1.0;
			m_belowSince = -1.0;
		}
	}

	SheddingTransition LoadShedder::OnTick(double tickSeconds, double budgetSeconds, double nowSeconds)
	{
		if (!(budgetSeconds > 0.0) || !std::isfinite(tickSeconds) || !std::isfinite(nowSeconds))
			return SheddingTransition::None;
		// Un tick exceptionnel (premier tick après le démarrage, chargement) compte au plus
		// pour 4 budgets : il ne fige pas la moyenne au-dessus du seuil pendant des secondes.
		const double load = (std::min)((std::max)(tickSeconds, 0.0) / budgetSeconds, kMaxSampleLoad);
		m_smoothed = m_hasSample ? m_smoothed + (load - m_smoothed) * m_config.smoothing : load;
		m_hasSample = true;
		// Désactivé : la charge reste mesurée (télémétrie, réglage des seuils), sans bascule.
		if (!m_config.enabled)
			return SheddingTransition::None;

		// Suivi des périodes au-dessus / en dessous des seuils.
		if (m_smoothed >= m_config.enterLoad)
		{
			if (m_aboveSince < 0.0)
				m_aboveSince = nowSeconds;
		}
		else
		{
			m_aboveSince = -1.0;
		}
		if (m_smoothed <= m_config.exitLoad)
		{
			if (m_belowSince < 0.0)
				m_belowSince = nowSeconds;
		}
		else
		{
			m_belowSince = -1.0;
		}

		const bool dwellOk = nowSeconds - m_lastSwitch >= m_config.minDwellSeconds;
		if (!m_active && m_aboveSince >= 0.0 && nowSeconds - m_aboveSince >= m_config.enterHoldSeconds && dwellOk)
		{
			m_active = true;
			m_lastSwitch = nowSeconds;
			m_belowSince = -1.0;
			++m_enterCount;
			return SheddingTransition::Entered;
		}
		if (m_active && m_belowSince >= 0.0 && nowSeconds - m_belowSince >= m_config.exitHoldSeconds && dwellOk)
		{
			m_active = false;
			m_lastSwitch = nowSeconds;
			m_aboveSince = -1.0;
			return SheddingTransition::Exited;
		}
		return SheddingTransition::None;
	}
}
