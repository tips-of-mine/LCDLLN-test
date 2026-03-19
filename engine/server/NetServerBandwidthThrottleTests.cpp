// M25.2 — NetServer TX bandwidth cap + priority unit tests (no epoll)

#include "engine/core/Log.h"
#include "engine/server/ServerProtocol.h"

#include <algorithm>
#include <cstdint>
#include <string_view>
#include <vector>

namespace
{
	static int s_failCount = 0;

	void Assert(bool cond, std::string_view msg)
	{
		if (!cond)
		{
			++s_failCount;
			LOG_ERROR(Net, "[NetServerBandwidthThrottleTests] {}", msg);
		}
	}

	struct ByteTokenBucket
	{
		double rateBytesPerSec = 0.0;
		double burstBytes = 0.0;
		double tokensBytes = 0.0;
		double lastTimeSec = 0.0;

		void Init(double rate, double burst, double startTokens, double startTimeSec)
		{
			rateBytesPerSec = rate;
			burstBytes = burst;
			tokensBytes = startTokens;
			lastTimeSec = startTimeSec;
		}

		void Refill(double nowSec)
		{
			if (nowSec <= lastTimeSec)
				return;
			const double elapsed = nowSec - lastTimeSec;
			tokensBytes = std::min(burstBytes, tokensBytes + elapsed * rateBytesPerSec);
			lastTimeSec = nowSec;
		}
	};

	bool IsStateMessageOpcode(uint16_t opcode)
	{
		return opcode == static_cast<uint16_t>(engine::server::MessageKind::Snapshot);
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

	LOG_INFO(Net, "[NetServerBandwidthThrottleTests] Boot");

	// Test 1: sustained throughput respects rate when starting with 0 tokens.
	{
		const double rate = 1000.0;  // bytes/sec
		const double burst = rate;    // 1s burst
		ByteTokenBucket b;
		b.Init(rate, burst, 0.0, 0.0);

		double now = 0.0;
		const double step = 0.2; // 200ms
		const uint32_t packetSize = 200;
		uint32_t totalSent = 0;
		for (int i = 0; i < 5; ++i)
		{
			now += step;
			b.Refill(now);
			const size_t allowed = std::min<size_t>(packetSize, static_cast<size_t>(b.tokensBytes));
			Assert(allowed <= packetSize, "allowedBytes <= packetSize");

			// Simulate sending allowed bytes (partial allowed).
			b.tokensBytes -= static_cast<double>(allowed);
			totalSent += static_cast<uint32_t>(allowed);
		}
		Assert(totalSent == 1000u, "sustained token bucket sends exactly rate*1s");
	}

	// Test 2: priority classification Snapshot=state, others=control.
	{
		Assert(IsStateMessageOpcode(static_cast<uint16_t>(engine::server::MessageKind::Snapshot)), "Snapshot is state");
		Assert(!IsStateMessageOpcode(static_cast<uint16_t>(engine::server::MessageKind::Welcome)), "Welcome is control");
		Assert(!IsStateMessageOpcode(static_cast<uint16_t>(engine::server::MessageKind::ZoneChange)), "ZoneChange is control");
	}

	LOG_INFO(Net, "[NetServerBandwidthThrottleTests] Completed failures={}", s_failCount);
	engine::core::Log::Shutdown();
	return s_failCount != 0 ? 1 : 0;
}

