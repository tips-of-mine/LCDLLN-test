#include "LoadTester.h"

#include "engine/core/Log.h"
#include "engine/network/AuthRegisterPayloads.h"
#include "engine/network/PacketBuilder.h"
#include "engine/network/ProtocolV1Constants.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <string_view>
#include <thread>

namespace tools::load_tester
{
	using engine::network::BuildAuthRequestPayload;
	using engine::network::ParseAuthResponsePayload;
	using engine::network::NetClient;
	using engine::network::NetClientState;
	using engine::network::RequestResponseDispatcher;
	using engine::network::kOpcodeAuthRequest;

	namespace
	{
		double ToMs(const std::chrono::steady_clock::duration& d)
		{
			using namespace std::chrono;
			return duration_cast<duration<double, std::milli>>(d).count();
		}

		std::pair<double, double> PercentilesP50P95(std::vector<double>& v)
		{
			if (v.empty())
				return {0.0, 0.0};
			std::sort(v.begin(), v.end());
			const size_t n = v.size();
			const size_t i50 = static_cast<size_t>(std::floor(0.50 * (n - 1)));
			const size_t i95 = static_cast<size_t>(std::floor(0.95 * (n - 1)));
			return {v[i50], v[i95]};
		}
	}

	LoadTester::LoadTester(LoadTestConfig cfg)
		: m_cfg(std::move(cfg))
	{
		LOG_INFO(LoadTester, "[LoadTester] Constructor (clients={})", m_cfg.clients);
	}

	LoadTester::~LoadTester()
	{
		LOG_INFO(LoadTester, "[LoadTester] Destroyed");
	}

	size_t LoadTester::EffectiveClientCount() const
	{
		if (m_cfg.instances <= 1)
			return static_cast<size_t>(m_cfg.clients);
		const uint32_t n = m_cfg.clients;
		const uint32_t inst = m_cfg.instances;
		const uint32_t idx = m_cfg.instanceIndex;
		const uint32_t base = (inst == 0u) ? n : (n / inst);
		const uint32_t rem = (inst == 0u) ? 0u : (n % inst);
		const uint32_t add = (idx < rem) ? 1u : 0u;
		return static_cast<size_t>(base + add);
	}

	bool LoadTester::Validate() const
	{
		if (m_cfg.masterHost.empty())
		{
			LOG_ERROR(LoadTester, "[LoadTester] Validate FAILED: masterHost empty");
			return false;
		}
		if (m_cfg.masterPort == 0)
		{
			LOG_ERROR(LoadTester, "[LoadTester] Validate FAILED: masterPort invalid");
			return false;
		}
		if (m_cfg.scenario != Scenario::ConnectOnly)
		{
			if (m_cfg.login.empty())
			{
				LOG_ERROR(LoadTester, "[LoadTester] Validate FAILED: login empty (scenario not ConnectOnly)");
				return false;
			}
			if (m_cfg.clientHash.empty())
			{
				LOG_ERROR(LoadTester, "[LoadTester] Validate FAILED: clientHash empty (scenario not ConnectOnly)");
				return false;
			}
		}
		// TLS in NetClient is enabled only if expected fingerprint is non-empty.
		if (m_cfg.serverFingerprintHex.empty())
		{
			LOG_ERROR(LoadTester, "[LoadTester] Validate FAILED: serverFingerprintHex empty (TLS requires it)");
			return false;
		}
		if (m_cfg.instances == 0u)
		{
			LOG_ERROR(LoadTester, "[LoadTester] Validate FAILED: instances=0");
			return false;
		}
		if (m_cfg.instanceIndex >= m_cfg.instances)
		{
			LOG_ERROR(LoadTester, "[LoadTester] Validate FAILED: instanceIndex out of range ({}/{})",
				m_cfg.instanceIndex, m_cfg.instances);
			return false;
		}
		if (m_cfg.clients == 0u)
		{
			LOG_ERROR(LoadTester, "[LoadTester] Validate FAILED: clients=0");
			return false;
		}
		if (m_cfg.durationSec == 0u)
		{
			LOG_ERROR(LoadTester, "[LoadTester] Validate FAILED: durationSec=0");
			return false;
		}
		return true;
	}

	void LoadTester::SetupClient(NetClient& c) const
	{
		c.SetAllowInsecureDev(m_cfg.allowInsecureDev);
		c.SetExpectedServerFingerprint(m_cfg.serverFingerprintHex);
	}

	Scenario LoadTester::ParseScenario(std::string_view v, bool& ok)
	{
		ok = true;
		if (v == "connect-only")
			return Scenario::ConnectOnly;
		if (v == "auth-only")
			return Scenario::AuthOnly;
		if (v == "heartbeat-only")
			return Scenario::HeartbeatOnly;
		if (v == "mix")
			return Scenario::Mix;
		ok = false;
		return Scenario::Mix;
	}

	bool LoadTester::Run()
	{
		if (!Validate())
			return false;

		const size_t n = EffectiveClientCount();
		if (n == 0)
		{
			LOG_ERROR(LoadTester, "[LoadTester] Run FAILED: effective client count is 0");
			return false;
		}

		m_clients.clear();
		m_connectLatMs.clear();
		m_authLatMs.clear();
		m_clients.reserve(n);
		m_connectLatMs.reserve(n);
		m_authLatMs.reserve(n);

		for (size_t i = 0; i < n; ++i)
		{
			auto ctx = std::make_unique<ClientContext>();
			SetupClient(ctx->client);
			m_clients.push_back(std::move(ctx));
		}

		const bool needsAuth =
			(m_cfg.scenario == Scenario::AuthOnly) ||
			(m_cfg.scenario == Scenario::HeartbeatOnly) ||
			(m_cfg.scenario == Scenario::Mix);
		const bool needsHeartbeat =
			(m_cfg.scenario == Scenario::HeartbeatOnly) ||
			(m_cfg.scenario == Scenario::Mix);

		LOG_INFO(LoadTester, "[LoadTester] Run start scenario={} clients={} (effective={}) rampUpSec={}",
			static_cast<int>(m_cfg.scenario), m_cfg.clients, n, m_cfg.rampUpSec);

		const auto t0 = std::chrono::steady_clock::now();
		const auto deadline = t0 + std::chrono::seconds(m_cfg.durationSec);

		size_t nextToStart = 0;
		const auto sleepTick = std::chrono::milliseconds(10);
		const auto pollDrainEvery = std::chrono::milliseconds(50);

		while (std::chrono::steady_clock::now() < deadline)
		{
			const auto now = std::chrono::steady_clock::now();
			const float elapsedSec = std::chrono::duration<float>(now - t0).count();

			// Ramp-up: start new connections linearly across ramp-up window.
			if (m_cfg.rampUpSec <= 0.0f)
			{
				while (nextToStart < n)
				{
					auto& ctx = *m_clients[nextToStart];
					ctx.connectStart = now;
					ctx.connectIssued = true;
					ctx.connectLatencyRecorded = false;
					ctx.client.Connect(m_cfg.masterHost, m_cfg.masterPort);
					++nextToStart;
				}
			}
			else
			{
				while (nextToStart < n)
				{
					const float idx = (n <= 1) ? 1.0f : static_cast<float>(nextToStart) / static_cast<float>(n - 1);
					const float target = idx * m_cfg.rampUpSec;
					if (elapsedSec + 1e-3f < target)
						break;

					auto& ctx = *m_clients[nextToStart];
					ctx.connectStart = now;
					ctx.connectIssued = true;
					ctx.connectLatencyRecorded = false;
					ctx.client.Connect(m_cfg.masterHost, m_cfg.masterPort);
					++nextToStart;
				}
			}

			// Drive each client.
			for (size_t i = 0; i < nextToStart; ++i)
			{
				auto& ctx = *m_clients[i];

				const bool connected = (ctx.client.GetState() == NetClientState::Connected);
				if (ctx.connectIssued && !ctx.connectLatencyRecorded && connected)
				{
					const double latMs = ToMs(now - ctx.connectStart);
					m_connectLatMs.push_back(latMs);
					ctx.connectLatencyRecorded = true;
				}

				// Connect-only: just drain events periodically to avoid queue growth.
				if (!needsAuth)
				{
					if (ctx.lastDrain.time_since_epoch().count() == 0)
						ctx.lastDrain = now;
					if (now - ctx.lastDrain >= pollDrainEvery)
					{
						(void)ctx.client.PollEvents();
						ctx.lastDrain = now;
					}
					continue;
				}

				// Auth/heartbeat scenarios: drive protocol v1 request/response and timeouts.
				if (ctx.dispatcher)
					ctx.dispatcher->Pump();

				// Ensure dispatcher exists once the connection is up.
				if (needsAuth && connected && !ctx.authSent)
				{
					if (!ctx.dispatcher)
					{
						ctx.dispatcher = std::make_unique<RequestResponseDispatcher>(&ctx.client);
					}

					const auto authPayload = BuildAuthRequestPayload(m_cfg.login, m_cfg.clientHash);
					if (authPayload.empty())
					{
						LOG_ERROR(LoadTester, "[LoadTester] AUTH payload build failed (client={})", i);
						ctx.authSent = true;
						ctx.authDone = true;
						ctx.authSuccess = false;
						ctx.client.Disconnect("auth payload build failed");
						continue;
					}

					ctx.authStart = now;
					ctx.authSent = true;

					ClientContext* ctxPtr = &ctx;
					const bool ok = ctx.dispatcher->SendRequest(
						kOpcodeAuthRequest,
						std::span<const uint8_t>(authPayload.data(), authPayload.size()),
						[this, ctxPtr](uint32_t, bool timeout, std::vector<uint8_t> payload)
						{
							ctxPtr->authDone = true;

							if (timeout)
							{
								ctxPtr->authSuccess = false;
								return;
							}
							if (payload.empty())
							{
								ctxPtr->authSuccess = false;
								return;
							}
							auto parsed = ParseAuthResponsePayload(payload.data(), payload.size());
							if (!parsed)
							{
								ctxPtr->authSuccess = false;
								return;
							}

							ctxPtr->authSuccess = parsed->success != 0;
							if (ctxPtr->authSuccess)
							{
								ctxPtr->sessionId = parsed->session_id;
								if (ctxPtr->dispatcher)
									ctxPtr->dispatcher->SetSessionId(ctxPtr->sessionId);
								const double latMs = ToMs(std::chrono::steady_clock::now() - ctxPtr->authStart);
								m_authLatMs.push_back(latMs);
							}
							// Fail -> keep connection cleanup in end-of-test.
						});

					if (!ok)
					{
						LOG_ERROR(LoadTester, "[LoadTester] AUTH SendRequest failed (client={})", i);
						ctx.authDone = true;
						ctx.authSuccess = false;
						ctx.client.Disconnect("AUTH SendRequest failed");
					}
				}

				// Heartbeat loop (after auth success).
				if (ctx.dispatcher && needsHeartbeat && ctx.authSuccess && !ctx.authDone)
				{
					// This branch should not happen (authDone set on callback).
				}
				if (ctx.dispatcher && needsHeartbeat && ctx.authSuccess)
				{
					ctx.dispatcher->TickHeartbeat();
				}

				// For auth-only: disconnect immediately after auth is done.
				if (m_cfg.scenario == Scenario::AuthOnly && ctx.dispatcher && ctx.authDone)
				{
					ctx.client.Disconnect("auth-only done");
				}
			}

			std::this_thread::sleep_for(sleepTick);
		}

		// Stop all clients.
		for (size_t i = 0; i < nextToStart; ++i)
		{
			auto& ctx = *m_clients[i];
			ctx.client.Disconnect("load test end");
		}

		std::this_thread::sleep_for(std::chrono::milliseconds(200));

		// Summary.
		const double connectP50 = [&]() {
			std::vector<double> tmp = m_connectLatMs;
			return PercentilesP50P95(tmp).first;
		}();
		const double connectP95 = [&]() {
			std::vector<double> tmp = m_connectLatMs;
			return PercentilesP50P95(tmp).second;
		}();

		double authP50 = 0.0;
		double authP95 = 0.0;
		if (!m_authLatMs.empty())
		{
			auto p = PercentilesP50P95(m_authLatMs);
			authP50 = p.first;
			authP95 = p.second;
		}

		if (m_cfg.scenario == Scenario::ConnectOnly)
		{
			const uint32_t ok = static_cast<uint32_t>(m_connectLatMs.size());
			const uint32_t fail = static_cast<uint32_t>(n - ok);
			LOG_WARN(LoadTester, "[LoadTester] Summary connect-only OK={} FAIL={} connect(p50ms={:.3f}, p95ms={:.3f})",
				ok, fail, connectP50, connectP95);
		}
		else
		{
			const uint32_t ok = static_cast<uint32_t>(m_authLatMs.size());
			const uint32_t fail = static_cast<uint32_t>(n - ok);
			LOG_WARN(LoadTester,
				"[LoadTester] Summary scenario={} OK={} FAIL={} connect(p50ms={:.3f}, p95ms={:.3f}) auth(p50ms={:.3f}, p95ms={:.3f})",
				static_cast<int>(m_cfg.scenario),
				ok, fail,
				connectP50, connectP95,
				authP50, authP95);
		}

		LOG_INFO(LoadTester, "[LoadTester] Run finished");
		return true;
	}
}

