// Implementation LunarHandler : dispatch + push broadcast (Phase 5 Lunar).

#include "src/masterd/handlers/lunar/LunarHandler.h"

#include "src/masterd/session/ConnectionSessionMap.h"
#include "src/masterd/session/SessionManager.h"
#include "src/shared/core/Log.h"
#include "src/shared/network/NetServer.h"
#include "src/shared/network/PacketBuilder.h"
#include "src/shared/network/ProtocolV1Constants.h"
#include "src/shared/network/LunarPayloads.h"

#include <chrono>
#include <span>
#include <vector>

namespace engine::server
{
	using engine::server::world::LunarCalendar;
	using engine::server::world::LunarPhaseInfo;

	void LunarHandler::SetCycleParams(uint64_t cycleStartMs, uint64_t cycleDurationMs)
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_cycleStartMs = cycleStartMs;
		m_cycleDurationMs = cycleDurationMs;
	}

	uint8_t LunarHandler::CurrentPhase() const
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		const uint64_t nowMs = static_cast<uint64_t>(
			std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::system_clock::now().time_since_epoch()).count());
		auto info = LunarCalendar::Compute(nowMs, m_cycleStartMs, m_cycleDurationMs);
		return info.phase;
	}

	void LunarHandler::HandlePacket(uint32_t connId, uint16_t opcode, uint32_t requestId,
	                                 uint64_t sessionIdHeader, const uint8_t* /*payload*/, size_t /*payloadSize*/)
	{
		using namespace engine::network;
		if (opcode == kOpcodeLunarStateRequest)
		{
			HandleStateRequest(connId, requestId, sessionIdHeader);
		}
	}

	void LunarHandler::HandleStateRequest(uint32_t connId, uint32_t requestId, uint64_t sessionIdHeader)
	{
		using namespace engine::network::lunar;
		using namespace engine::network;

		LunarStateResponse resp;

		// Validation session : si pas de session valide -> Unauthorized.
		if (m_connMap && m_sessionMgr)
		{
			auto sessIdOpt = m_connMap->GetSessionId(connId);
			if (!sessIdOpt || *sessIdOpt == 0u || sessionIdHeader == 0u
				|| *sessIdOpt != sessionIdHeader)
			{
				resp.status = LunarStatus::Unauthorized;
			}
			else
			{
				auto accIdOpt = m_sessionMgr->GetAccountId(*sessIdOpt);
				if (!accIdOpt || *accIdOpt == 0u)
				{
					resp.status = LunarStatus::Unauthorized;
				}
			}
		}
		else
		{
			// Handler pas cable -> repond Unauthorized par defaut (defensive).
			resp.status = LunarStatus::Unauthorized;
		}

		if (resp.status == LunarStatus::Ok)
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			const uint64_t nowMs = static_cast<uint64_t>(
				std::chrono::duration_cast<std::chrono::milliseconds>(
					std::chrono::system_clock::now().time_since_epoch()).count());
			auto info = LunarCalendar::Compute(nowMs, m_cycleStartMs, m_cycleDurationMs);
			resp.phase = info.phase;
			resp.illumination = info.illumination;
			resp.cycleStartMs = m_cycleStartMs;
			resp.cycleDurationMs = m_cycleDurationMs;
		}

		std::vector<uint8_t> payload;
		BuildLunarStateResponsePayload(resp, payload);

		// Construction du packet complet via PacketBuilder (header + payload).
		PacketBuilder builder;
		ByteWriter w = builder.PayloadWriter();
		if (w.Remaining() < payload.size()) return;
		if (!w.WriteBytes(payload.data(), payload.size())) return;
		if (!builder.Finalize(kOpcodeLunarStateResponse, 0u, requestId, sessionIdHeader, payload.size())) return;
		if (m_server)
			m_server->Send(connId, builder.Data());
	}

	void LunarHandler::Tick(uint64_t realNowMs)
	{
		uint8_t newPhase = 0;
		float newIllumination = 0.0f;
		uint64_t nextChangeTs = 0;
		bool needBroadcast = false;

		{
			std::lock_guard<std::mutex> lock(m_mutex);
			auto info = LunarCalendar::Compute(realNowMs, m_cycleStartMs, m_cycleDurationMs);
			if (info.phase != m_lastBroadcastPhase)
			{
				newPhase = info.phase;
				newIllumination = info.illumination;
				nextChangeTs = LunarCalendar::NextChangeTsMs(realNowMs, m_cycleStartMs, m_cycleDurationMs);
				m_lastBroadcastPhase = info.phase;
				needBroadcast = true;
			}
		}

		if (needBroadcast)
		{
			LOG_INFO(Net, "[LunarHandler] phase change broadcast: phase={} illumination={:.3f} nextChangeTs={}",
				static_cast<unsigned>(newPhase), newIllumination,
				static_cast<unsigned long long>(nextChangeTs));
			PushPhaseChangeBroadcast(newPhase, newIllumination, nextChangeTs);
		}
	}

	void LunarHandler::PushPhaseChangeBroadcast(uint8_t newPhase, float newIllumination, uint64_t nextChangeTsMs)
	{
		using namespace engine::network::lunar;
		using namespace engine::network;

		LunarPhaseChangeNotification notif;
		notif.newPhase = newPhase;
		notif.newIllumination = newIllumination;
		notif.nextChangeTsMs = nextChangeTsMs;

		std::vector<uint8_t> payload;
		BuildLunarPhaseChangeNotificationPayload(notif, payload);

		// Push asynchrone : requestId=0, sessionId=0 dans le header.
		auto packet = BuildPushPacket(kOpcodeLunarPhaseChangeNotification,
			std::span<const uint8_t>(payload.data(), payload.size()));
		if (packet.empty()) return;

		if (!m_server || !m_connMap) return;
		auto snapshot = m_connMap->Snapshot();
		for (const auto& [connId, sessId] : snapshot)
		{
			(void)sessId;
			m_server->Send(connId, packet);
		}
	}
}
