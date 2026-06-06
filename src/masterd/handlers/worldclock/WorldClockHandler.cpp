// Implementation WorldClockHandler : dispatch + etat mutable + broadcast admin.

#include "src/masterd/handlers/worldclock/WorldClockHandler.h"

#include "src/masterd/session/ConnectionSessionMap.h"
#include "src/masterd/session/SessionManager.h"
#include "src/shared/core/Log.h"
#include "src/shared/network/NetServer.h"
#include "src/shared/network/PacketBuilder.h"
#include "src/shared/network/ProtocolV1Constants.h"
#include "src/shared/network/WorldClockPayloads.h"
#include "src/shared/world/WorldClock.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <span>
#include <vector>

namespace engine::server
{
	using engine::world::GameSeconds;
	using engine::world::WorldClockParams;

	uint64_t WorldClockHandler::NowMs()
	{
		return static_cast<uint64_t>(
			std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::system_clock::now().time_since_epoch()).count());
	}

	double WorldClockHandler::RealSecSinceEpoch(uint64_t now) const
	{
		// Suppose m_mutex deja pris par l'appelant.
		return (now >= m_params.epochRefUnixMs)
			? static_cast<double>(now - m_params.epochRefUnixMs) / 1000.0 : 0.0;
	}

	void WorldClockHandler::Configure(const WorldClockParams& p)
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_params = p;
	}

	WorldClockParams WorldClockHandler::GetParams() const
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		return m_params;
	}

	void WorldClockHandler::HandlePacket(uint32_t connId, uint16_t opcode, uint32_t requestId,
	                                     uint64_t sessionIdHeader, const uint8_t* /*payload*/, size_t /*payloadSize*/)
	{
		using namespace engine::network;
		using namespace engine::network::worldclock;

		if (opcode != kOpcodeWorldClockStateRequest) return;

		WorldClockStateResponse resp;
		resp.serverTimeUnixMs = NowMs();

		LOG_INFO(Net, "[WorldClockHandler] HandleStateRequest received (connId={} requestId={} sessionIdHeader={})",
			connId, requestId, static_cast<unsigned long long>(sessionIdHeader));

		// Validation session : si pas de session valide -> Unauthorized.
		if (m_connMap && m_sessionMgr)
		{
			auto sessIdOpt = m_connMap->GetSessionId(connId);
			const uint64_t sessIdResolved = sessIdOpt.has_value() ? *sessIdOpt : 0u;
			if (!sessIdOpt || *sessIdOpt == 0u || sessionIdHeader == 0u
				|| *sessIdOpt != sessionIdHeader)
			{
				resp.status = WorldClockStatus::Unauthorized;
				LOG_WARN(Net, "[WorldClockHandler] auth REJECTED (connId={} sessIdResolved={} sessionIdHeader={} match={})",
					connId, static_cast<unsigned long long>(sessIdResolved),
					static_cast<unsigned long long>(sessionIdHeader),
					(sessIdOpt.has_value() && *sessIdOpt == sessionIdHeader) ? 1 : 0);
			}
			else
			{
				auto accIdOpt = m_sessionMgr->GetAccountId(*sessIdOpt);
				if (!accIdOpt || *accIdOpt == 0u)
				{
					resp.status = WorldClockStatus::Unauthorized;
					LOG_WARN(Net, "[WorldClockHandler] auth REJECTED account_id missing (connId={} sessId={})",
						connId, static_cast<unsigned long long>(sessIdResolved));
				}
				else
				{
					LOG_INFO(Net, "[WorldClockHandler] auth OK (connId={} sessId={} accountId={})",
						connId, static_cast<unsigned long long>(sessIdResolved),
						static_cast<unsigned long long>(*accIdOpt));
				}
			}
		}
		else
		{
			// Handler pas cable -> repond Unauthorized par defaut (defensive).
			resp.status = WorldClockStatus::Unauthorized;
			LOG_WARN(Net, "[WorldClockHandler] handler not wired (connMap={} sessionMgr={})",
				m_connMap ? 1 : 0, m_sessionMgr ? 1 : 0);
		}

		if (resp.status == WorldClockStatus::Ok)
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			resp.epochRefUnixMs        = m_params.epochRefUnixMs;
			resp.timeScaleRealMinPerDay = m_params.timeScaleRealMinPerDay;
			resp.offsetGameSec         = m_params.offsetGameSec;
			resp.paused                = m_params.paused ? 1u : 0u;
			resp.pausedAtGameSec       = m_params.pausedAtGameSec;
			resp.lunarPeriodGameSec    = m_params.lunarPeriodGameSec;
			LOG_INFO(Net, "[WorldClockHandler] sending WorldClockStateResponse (connId={} timeScale={:.2f} offset={:.1f} paused={} lunarPeriod={:.1f})",
				connId, resp.timeScaleRealMinPerDay, resp.offsetGameSec,
				static_cast<unsigned>(resp.paused), resp.lunarPeriodGameSec);
		}

		std::vector<uint8_t> payload;
		BuildWorldClockStateResponsePayload(resp, payload);

		// Construction du packet complet via PacketBuilder (header + payload).
		PacketBuilder builder;
		ByteWriter w = builder.PayloadWriter();
		if (w.Remaining() < payload.size()) return;
		if (!w.WriteBytes(payload.data(), payload.size())) return;
		if (!builder.Finalize(kOpcodeWorldClockStateResponse, 0u, requestId, sessionIdHeader, payload.size())) return;
		if (m_server)
			m_server->Send(connId, builder.Data());
	}

	bool WorldClockHandler::SetTimeOfDay(float hours)
	{
		if (hours < 0.0f || hours >= 24.0f) return false;
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			const double gs = GameSeconds(NowMs(), m_params);
			double dayComp = std::fmod(gs, 86400.0);
			if (dayComp < 0.0) dayComp += 86400.0;
			m_params.offsetGameSec += (static_cast<double>(hours) * 3600.0 - dayComp);
		}
		LOG_INFO(Net, "[WorldClockHandler] SetTimeOfDay({:.2f}h) applied", hours);
		BroadcastChange();
		return true;
	}

	bool WorldClockHandler::SetPaused(bool paused)
	{
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			if (paused)
			{
				// Fige la valeur courante avant de marquer paused.
				m_params.pausedAtGameSec = GameSeconds(NowMs(), m_params);
				m_params.paused = true;
			}
			else
			{
				// Reprise : recale l'offset pour la continuite (pas de saut).
				const double realSec = RealSecSinceEpoch(NowMs());
				const double gsPerRs = 86400.0 / std::max(1e-6,
					static_cast<double>(m_params.timeScaleRealMinPerDay) * 60.0);
				m_params.offsetGameSec = m_params.pausedAtGameSec - realSec * gsPerRs;
				m_params.paused = false;
			}
		}
		LOG_INFO(Net, "[WorldClockHandler] SetPaused({}) applied", paused ? 1 : 0);
		BroadcastChange();
		return true;
	}

	bool WorldClockHandler::SetTimeScale(float realMinPerDay)
	{
		if (realMinPerDay < 1.0f || realMinPerDay > 1440.0f) return false;
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			// Continuite : on garde la meme heure de jeu a l'instant de bascule.
			const double gsBefore = GameSeconds(NowMs(), m_params);
			if (m_params.paused)
			{
				// En pause : la valeur figee prime, on ne touche pas l'offset.
				// On change uniquement la cadence (effet visible a la reprise).
				m_params.timeScaleRealMinPerDay = realMinPerDay;
			}
			else
			{
				const double realSec = RealSecSinceEpoch(NowMs());
				const double gsPerRs = 86400.0 / (static_cast<double>(realMinPerDay) * 60.0);
				m_params.timeScaleRealMinPerDay = realMinPerDay;
				m_params.offsetGameSec = gsBefore - realSec * gsPerRs;
			}
		}
		LOG_INFO(Net, "[WorldClockHandler] SetTimeScale({:.2f}) applied", realMinPerDay);
		BroadcastChange();
		return true;
	}

	void WorldClockHandler::BroadcastChange()
	{
		using namespace engine::network;
		using namespace engine::network::worldclock;

		// Instantane des params courants (prend le mutex en interne).
		WorldClockParams snap = GetParams();

		WorldClockStateResponse notif;
		notif.status               = WorldClockStatus::Ok;
		notif.serverTimeUnixMs     = NowMs();
		notif.epochRefUnixMs       = snap.epochRefUnixMs;
		notif.timeScaleRealMinPerDay = snap.timeScaleRealMinPerDay;
		notif.offsetGameSec        = snap.offsetGameSec;
		notif.paused               = snap.paused ? 1u : 0u;
		notif.pausedAtGameSec      = snap.pausedAtGameSec;
		notif.lunarPeriodGameSec   = snap.lunarPeriodGameSec;

		std::vector<uint8_t> payload;
		BuildWorldClockChangeNotificationPayload(notif, payload);

		// Push asynchrone : requestId=0, sessionId=0 dans le header.
		auto packet = BuildPushPacket(kOpcodeWorldClockChangeNotification,
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
