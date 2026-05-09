// M25.4 — ConnectionDDoSProtector unit tests (no sockets)

#include "engine/core/Log.h"
#include "engine/server/ConnectionDDoSProtector.h"

#include <chrono>
#include <cstdint>
#include <string_view>

namespace
{
	static int s_failCount = 0;

	void Assert(bool cond, std::string_view msg)
	{
		if (!cond)
		{
			++s_failCount;
			LOG_ERROR(Net, "[ConnectionDDoSProtectorTests] {}", msg);
		}
	}
}

int main()
{
	engine::core::LogSettings logSettings;
	logSettings.level = engine::core::LogLevel::Info;
	logSettings.console = true;
	logSettings.flushAlways = true;
	logSettings.filePath = "";
	engine::core::Log::Init(logSettings);

	LOG_INFO(Net, "[ConnectionDDoSProtectorTests] Boot");

	using Clock = std::chrono::steady_clock;
	const Clock::time_point t0 = Clock::now();
	const uint32_t ip = (127u << 24) | (0u << 16) | (0u << 8) | 1u;

	// Test 1: accept throttle
	{
		engine::server::ConnectionDDoSProtector p;
		engine::server::ConnectionDDoSProtector::Config cfg;
		cfg.maxAcceptsPerSec = 2.0;
		p.Init(cfg);

		Assert(p.TryConsumeAcceptToken(t0), "accept token 1 allowed");
		Assert(p.TryConsumeAcceptToken(t0), "accept token 2 allowed");
		Assert(!p.TryConsumeAcceptToken(t0), "accept token 3 denied");

		const Clock::time_point t1 = t0 + std::chrono::seconds(1);
		Assert(p.TryConsumeAcceptToken(t1), "accept token refilled after 1s");
	}

	// Test 2: per-IP concurrent cap
	{
		engine::server::ConnectionDDoSProtector p;
		engine::server::ConnectionDDoSProtector::Config cfg;
		cfg.maxConnectionsPerIp = 1u;
		p.Init(cfg);

		Assert(p.TryAcceptForIp(ip, t0), "first accept for ip allowed");
		Assert(p.GetActiveCount(ip) == 1u, "active count is 1");
		Assert(!p.TryAcceptForIp(ip, t0), "second accept for ip denied by cap");

		p.OnConnectionClosed(ip, engine::server::DisconnectReason::PeerClosed, t0);
		Assert(p.GetActiveCount(ip) == 0u, "active count decremented");
	}

	// Test 3: handshake deny temporary
	{
		engine::server::ConnectionDDoSProtector p;
		engine::server::ConnectionDDoSProtector::Config cfg;
		cfg.handshakeFailuresBeforeDeny = 2u;
		cfg.handshakeDenyDurationSec = 10u;
		p.Init(cfg);

		p.OnConnectionClosed(ip, engine::server::DisconnectReason::TlsHandshakeFailed, t0);
		p.OnConnectionClosed(ip, engine::server::DisconnectReason::TlsHandshakeFailed, t0);

		Assert(!p.TryAcceptForIp(ip, t0), "ip denied after handshake failures");

		const Clock::time_point t1 = t0 + std::chrono::seconds(11);
		Assert(p.TryAcceptForIp(ip, t1), "ip allowed after deny duration");
	}

	LOG_INFO(Net, "[ConnectionDDoSProtectorTests] Completed failures={}", s_failCount);
	engine::core::Log::Shutdown();
	return s_failCount != 0 ? 1 : 0;
}

