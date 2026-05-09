#pragma once

// M25.4 — Basic DDoS mitigation: per-IP connection caps + accept throttle + temporary deny on handshake failures.
//
// This helper is designed for NetServer's epoll IO thread (single-threaded usage).

#include "engine/core/Log.h"
#include "engine/server/NetServer.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <string>
#include <unordered_map>

namespace engine::server
{
	class ConnectionDDoSProtector final
	{
	public:
		using Clock = std::chrono::steady_clock;

		struct Config
		{
			// Max concurrent connections allowed per IP. 0 disables.
			uint32_t maxConnectionsPerIp = 0u;
			// Global accept throttle: max accepts per second. 0 disables.
			double maxAcceptsPerSec = 0.0;
			// After N handshake failures, deny the IP for denyDurationSec. 0 disables.
			uint32_t handshakeFailuresBeforeDeny = 0u;
			uint32_t handshakeDenyDurationSec = 0u;
		};

		bool Init(const Config& cfg)
		{
			m_cfg = cfg;
			if (m_cfg.maxAcceptsPerSec < 0.0)
			{
				LOG_WARN(Net, "[DDoS] Protector Init: maxAcceptsPerSec < 0 -> disabled");
				m_cfg.maxAcceptsPerSec = 0.0;
			}
			if (m_cfg.handshakeFailuresBeforeDeny > 0u && m_cfg.handshakeDenyDurationSec == 0u)
			{
				LOG_WARN(Net,
					"[DDoS] Protector Init: handshakeFailuresBeforeDeny set but denyDurationSec=0 -> handshake deny disabled");
				m_cfg.handshakeFailuresBeforeDeny = 0u;
			}
			m_acceptTokens = (m_cfg.maxAcceptsPerSec > 0.0) ? m_cfg.maxAcceptsPerSec : 0.0;
			m_acceptLastRefill = Clock::now();

			LOG_INFO(Net,
				"[DDoS] Protector Init OK (maxConnectionsPerIp={} maxAcceptsPerSec={} handshakeFailuresBeforeDeny={} handshakeDenyDurationSec={})",
				m_cfg.maxConnectionsPerIp, m_cfg.maxAcceptsPerSec,
				m_cfg.handshakeFailuresBeforeDeny, m_cfg.handshakeDenyDurationSec);
			return true;
		}

		void PurgeExpired(Clock::time_point now)
		{
			// Remove expired denies and old failure entries.
			for (auto it = m_ipDeniedUntil.begin(); it != m_ipDeniedUntil.end(); )
			{
				if (now >= it->second)
					it = m_ipDeniedUntil.erase(it);
				else
					++it;
			}

			for (auto it = m_ipHandshakeFail.begin(); it != m_ipHandshakeFail.end(); )
			{
				const uint32_t ageSec = static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::seconds>(now - it->second.lastFail).count());
				if (m_cfg.handshakeDenyDurationSec > 0u && ageSec > m_cfg.handshakeDenyDurationSec * 2u)
					it = m_ipHandshakeFail.erase(it);
				else
					++it;
			}
		}

		bool TryConsumeAcceptToken(Clock::time_point now)
		{
			if (m_cfg.maxAcceptsPerSec <= 0.0)
				return true;

			const double elapsed = std::chrono::duration<double>(now - m_acceptLastRefill).count();
			if (elapsed > 0.0)
			{
				m_acceptTokens = std::min(m_cfg.maxAcceptsPerSec, m_acceptTokens + elapsed * m_cfg.maxAcceptsPerSec);
				m_acceptLastRefill = now;
			}

			if (m_acceptTokens < 1.0)
				return false;

			m_acceptTokens -= 1.0;
			return true;
		}

		bool TryAcceptForIp(uint32_t ipHostOrder, Clock::time_point now)
		{
			if (IsIpDenied(ipHostOrder, now))
				return false;

			const uint32_t cap = m_cfg.maxConnectionsPerIp;
			if (cap > 0u)
			{
				uint32_t active = 0u;
				auto it = m_ipActiveByIp.find(ipHostOrder);
				if (it != m_ipActiveByIp.end())
					active = it->second;
				if (active >= cap)
					return false;
			}

			++m_ipActiveByIp[ipHostOrder];
			return true;
		}

		void OnConnectionClosed(uint32_t ipHostOrder, DisconnectReason reason, Clock::time_point now)
		{
			// Update active counter.
			{
				auto it = m_ipActiveByIp.find(ipHostOrder);
				if (it != m_ipActiveByIp.end())
				{
					if (it->second > 0u)
						--it->second;
					if (it->second == 0u)
						m_ipActiveByIp.erase(it);
				}
			}

			// Track handshake failures and temporary deny.
			if (m_cfg.handshakeFailuresBeforeDeny == 0u || m_cfg.handshakeDenyDurationSec == 0u)
				return;

			if (reason != DisconnectReason::TlsHandshakeFailed && reason != DisconnectReason::HandshakeTimeout)
				return;

			IpFail& st = m_ipHandshakeFail[ipHostOrder];
			++st.failCount;
			st.lastFail = now;

			if (st.failCount < m_cfg.handshakeFailuresBeforeDeny)
				return;

			// Deny and reset counter.
			const auto until = now + std::chrono::seconds(m_cfg.handshakeDenyDurationSec);
			m_ipDeniedUntil[ipHostOrder] = until;
			st.failCount = 0u;

			LOG_WARN(Net,
				"[DDoS] IP temporarily denied (ip={}.{}.{}.{} deny_sec={} reason={})",
				(ipHostOrder >> 24) & 0xFFu, (ipHostOrder >> 16) & 0xFFu,
				(ipHostOrder >> 8) & 0xFFu, ipHostOrder & 0xFFu,
				m_cfg.handshakeDenyDurationSec,
				static_cast<uint32_t>(reason));
		}

		uint32_t GetActiveCount(uint32_t ipHostOrder) const
		{
			auto it = m_ipActiveByIp.find(ipHostOrder);
			return it == m_ipActiveByIp.end() ? 0u : it->second;
		}

		bool IsIpDenied(uint32_t ipHostOrder, Clock::time_point now) const
		{
			auto it = m_ipDeniedUntil.find(ipHostOrder);
			if (it == m_ipDeniedUntil.end())
				return false;
			return now < it->second;
		}

	private:
		struct IpFail
		{
			uint32_t failCount = 0u;
			Clock::time_point lastFail{};
		};

		Config m_cfg{};

		// Accept throttle state.
		double m_acceptTokens = 0.0;
		Clock::time_point m_acceptLastRefill{};

		// Active connections per IP.
		std::unordered_map<uint32_t, uint32_t> m_ipActiveByIp;

		// Deny until timestamps.
		std::unordered_map<uint32_t, Clock::time_point> m_ipDeniedUntil;

		// Handshake failures tracking.
		std::unordered_map<uint32_t, IpFail> m_ipHandshakeFail;
	};
}

