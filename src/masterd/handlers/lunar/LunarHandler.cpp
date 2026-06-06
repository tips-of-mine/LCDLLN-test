// Implementation LunarHandler : dispatch + push broadcast (Phase 5 Lunar).

#include "src/masterd/handlers/lunar/LunarHandler.h"

#include "src/masterd/handlers/worldclock/WorldClockHandler.h"
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

	LunarPhaseInfo LunarHandler::ComputePhaseNow(uint64_t realNowMs) const
	{
		// Unifie : la phase derive de l'horloge MONDE (temps de jeu) si disponible.
		// On garde LunarCalendar comme point de calcul : seule son ENTREE change
		// (gameMs / periodMs derives de l'horloge au lieu de realNowMs / cycle reel).
		if (m_worldClock)
		{
			const engine::world::WorldClockParams p = m_worldClock->GetParams();
			const double gameSec = engine::world::GameSeconds(realNowMs, p);
			const uint64_t gameMs = static_cast<uint64_t>(gameSec * 1000.0);
			const uint64_t periodMs = static_cast<uint64_t>(p.lunarPeriodGameSec * 1000.0);
			return LunarCalendar::Compute(gameMs, 0ull, periodMs);
		}
		// Fallback (horloge non cablee) : ancien comportement temps reel.
		return LunarCalendar::Compute(realNowMs, m_cycleStartMs, m_cycleDurationMs);
	}

	uint8_t LunarHandler::CurrentPhase() const
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		const uint64_t nowMs = static_cast<uint64_t>(
			std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::system_clock::now().time_since_epoch()).count());
		return ComputePhaseNow(nowMs).phase;
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

		LOG_INFO(Net, "[LunarHandler] HandleStateRequest received (connId={} requestId={} sessionIdHeader={})",
			connId, requestId, static_cast<unsigned long long>(sessionIdHeader));

		// Validation session : si pas de session valide -> Unauthorized.
		if (m_connMap && m_sessionMgr)
		{
			auto sessIdOpt = m_connMap->GetSessionId(connId);
			const uint64_t sessIdResolved = sessIdOpt.has_value() ? *sessIdOpt : 0u;
			if (!sessIdOpt || *sessIdOpt == 0u || sessionIdHeader == 0u
				|| *sessIdOpt != sessionIdHeader)
			{
				resp.status = LunarStatus::Unauthorized;
				LOG_WARN(Net, "[LunarHandler] auth REJECTED (connId={} sessIdResolved={} sessionIdHeader={} match={})",
					connId, static_cast<unsigned long long>(sessIdResolved),
					static_cast<unsigned long long>(sessionIdHeader),
					(sessIdOpt.has_value() && *sessIdOpt == sessionIdHeader) ? 1 : 0);
			}
			else
			{
				auto accIdOpt = m_sessionMgr->GetAccountId(*sessIdOpt);
				if (!accIdOpt || *accIdOpt == 0u)
				{
					resp.status = LunarStatus::Unauthorized;
					LOG_WARN(Net, "[LunarHandler] auth REJECTED account_id missing (connId={} sessId={})",
						connId, static_cast<unsigned long long>(sessIdResolved));
				}
				else
				{
					LOG_INFO(Net, "[LunarHandler] auth OK (connId={} sessId={} accountId={})",
						connId, static_cast<unsigned long long>(sessIdResolved),
						static_cast<unsigned long long>(*accIdOpt));
				}
			}
		}
		else
		{
			// Handler pas cable -> repond Unauthorized par defaut (defensive).
			resp.status = LunarStatus::Unauthorized;
			LOG_WARN(Net, "[LunarHandler] handler not wired (connMap={} sessionMgr={})",
				m_connMap ? 1 : 0, m_sessionMgr ? 1 : 0);
		}

		if (resp.status == LunarStatus::Ok)
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			const uint64_t nowMs = static_cast<uint64_t>(
				std::chrono::duration_cast<std::chrono::milliseconds>(
					std::chrono::system_clock::now().time_since_epoch()).count());
			auto info = ComputePhaseNow(nowMs);
			resp.phase = info.phase;
			resp.illumination = info.illumination;
			// Champs cycle (informationnels : le client utilise phase + illumination).
			// Si l'horloge monde est branchee, on renvoie l'epoch + la periode lunaire
			// derivees de l'horloge (temps de jeu) pour rester coherent avec la source
			// reelle de la phase. Sinon, anciennes valeurs temps reel. Wire inchange.
			if (m_worldClock)
			{
				const engine::world::WorldClockParams p = m_worldClock->GetParams();
				resp.cycleStartMs = p.epochRefUnixMs;
				resp.cycleDurationMs = static_cast<uint64_t>(p.lunarPeriodGameSec * 1000.0);
			}
			else
			{
				resp.cycleStartMs = m_cycleStartMs;
				resp.cycleDurationMs = m_cycleDurationMs;
			}
			LOG_INFO(Net, "[LunarHandler] sending LunarStateResponse (connId={} phase={} illumination={:.3f} cycleStartMs={} cycleDurationMs={})",
				connId, static_cast<unsigned>(resp.phase), resp.illumination,
				static_cast<unsigned long long>(resp.cycleStartMs),
				static_cast<unsigned long long>(resp.cycleDurationMs));
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
			auto info = ComputePhaseNow(realNowMs);
			if (info.phase != m_lastBroadcastPhase)
			{
				newPhase = info.phase;
				newIllumination = info.illumination;
				// nextChangeTsMs (informationnel) : si l'horloge monde est branchee,
				// la phase derive du temps de jeu et il n'y a pas de mapping direct
				// vers un timestamp Unix de prochain changement (la cadence depend du
				// timeScale, qui peut changer). On laisse 0 dans ce cas (le client
				// rafraichit via le push 194 a chaque changement de phase). Sinon,
				// calcul temps reel d'origine.
				nextChangeTs = m_worldClock
					? 0ull
					: LunarCalendar::NextChangeTsMs(realNowMs, m_cycleStartMs, m_cycleDurationMs);
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
