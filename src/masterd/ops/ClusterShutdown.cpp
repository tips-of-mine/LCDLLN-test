// ETAPE5.3 — arrêt ordonné du cluster. Voir ClusterShutdown.h.

#include "src/masterd/ops/ClusterShutdown.h"

#include <algorithm>
#include <cmath>

namespace engine::server
{
	namespace
	{
		/// Paliers d'annonce (secondes restantes), du plus grand au plus petit.
		constexpr uint32_t kSteps[] = { 600u, 300u, 120u, 60u, 30u, 15u, 10u, 5u, 4u, 3u, 2u, 1u };

		uint32_t CeilSeconds(double s)
		{
			if (!(s > 0.0))
				return 0u;
			const double c = std::ceil(s);
			return c >= 4294967295.0 ? 4294967295u : static_cast<uint32_t>(c);
		}
	}

	ClusterShutdown::ClusterShutdown(double drainSeconds)
	{
		m_drainSeconds = std::isfinite(drainSeconds) ? std::clamp(drainSeconds, 1.0, 600.0) : 15.0;
	}

	bool ClusterShutdown::Start(double nowSeconds, uint32_t delaySeconds, std::string reason)
	{
		if (!std::isfinite(nowSeconds))
			return false;
		std::lock_guard lock(m_mutex);
		if (m_phase == ClusterShutdownPhase::Draining || m_phase == ClusterShutdownPhase::Done)
			return false;
		const uint32_t delay = (std::min)(delaySeconds, kMaxDelaySeconds);
		m_phase = ClusterShutdownPhase::Countdown;
		m_deadline = nowSeconds + static_cast<double>(delay);
		m_reason = std::move(reason);
		m_announceStart = true;
		// Les paliers supérieurs ou égaux au délai sont « déjà passés » : seule l'annonce
		// de départ les couvre (pas de « dans 10 min » juste après « dans 7 min »).
		m_lastAnnouncedStep = delay + 1u;
		return true;
	}

	bool ClusterShutdown::Cancel()
	{
		std::lock_guard lock(m_mutex);
		if (m_phase != ClusterShutdownPhase::Countdown)
			return false;
		m_phase = ClusterShutdownPhase::Idle;
		m_reason.clear();
		m_announceStart = false;
		m_lastAnnouncedStep = 0;
		return true;
	}

	std::vector<ClusterShutdownAction> ClusterShutdown::Tick(double nowSeconds)
	{
		std::vector<ClusterShutdownAction> out;
		if (!std::isfinite(nowSeconds))
			return out;
		std::lock_guard lock(m_mutex);
		if (m_phase == ClusterShutdownPhase::Countdown)
		{
			const double remaining = m_deadline - nowSeconds;
			if (remaining > 0.0)
			{
				const uint32_t left = CeilSeconds(remaining);
				if (m_announceStart)
				{
					m_announceStart = false;
					out.push_back({ ClusterShutdownAction::Kind::Announce, left });
					// Le palier courant est couvert par l'annonce de départ.
					for (uint32_t step : kSteps)
					{
						if (step >= left && step < m_lastAnnouncedStep)
							m_lastAnnouncedStep = step;
					}
				}
				else
				{
					// Le plus petit palier franchi depuis la dernière annonce (une seule
					// annonce par tour, même après un saut d'horloge).
					uint32_t crossed = 0;
					for (uint32_t step : kSteps)
					{
						if (step >= left && step < m_lastAnnouncedStep)
							crossed = step;
					}
					if (crossed != 0u)
					{
						m_lastAnnouncedStep = crossed;
						// Durée réelle (égale au palier en marche normale, plus petite après
						// un saut d'horloge).
						out.push_back({ ClusterShutdownAction::Kind::Announce, left });
					}
				}
				return out;
			}
			// Arrêt immédiat (délai 0) : une annonce « maintenant » précède le vidage.
			if (m_announceStart)
			{
				m_announceStart = false;
				out.push_back({ ClusterShutdownAction::Kind::Announce, 0u });
			}
			m_phase = ClusterShutdownPhase::Draining;
			m_exitAt = nowSeconds + m_drainSeconds;
			out.push_back({ ClusterShutdownAction::Kind::StopShards, 0u });
			return out;
		}
		if (m_phase == ClusterShutdownPhase::Draining && nowSeconds >= m_exitAt)
		{
			m_phase = ClusterShutdownPhase::Done;
			out.push_back({ ClusterShutdownAction::Kind::Exit, 0u });
		}
		return out;
	}

	ClusterShutdownPhase ClusterShutdown::Phase() const
	{
		std::lock_guard lock(m_mutex);
		return m_phase;
	}

	bool ClusterShutdown::AdmissionsBlocked() const
	{
		std::lock_guard lock(m_mutex);
		return m_phase != ClusterShutdownPhase::Idle;
	}

	std::string ClusterShutdown::Reason() const
	{
		std::lock_guard lock(m_mutex);
		return m_reason;
	}

	uint32_t ClusterShutdown::RemainingSeconds(double nowSeconds) const
	{
		std::lock_guard lock(m_mutex);
		if (m_phase != ClusterShutdownPhase::Countdown)
			return 0u;
		return CeilSeconds(m_deadline - nowSeconds);
	}

	std::string FormatShutdownAnnouncement(uint32_t remainingSeconds, const std::string& reason)
	{
		std::string text = "Arrêt du serveur ";
		if (remainingSeconds == 0u)
			text += "maintenant";
		else if (remainingSeconds >= 60u && remainingSeconds % 60u == 0u)
			text += "dans " + std::to_string(remainingSeconds / 60u) + " min";
		else if (remainingSeconds >= 60u)
			text += "dans " + std::to_string(remainingSeconds / 60u) + " min " + std::to_string(remainingSeconds % 60u) + " s";
		else
			text += "dans " + std::to_string(remainingSeconds) + " s";
		if (!reason.empty())
			text += " : " + reason;
		text += ". Votre personnage sera sauvegardé.";
		return text;
	}
}
