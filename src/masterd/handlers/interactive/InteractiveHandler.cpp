// M100.32 — Implémentation InteractiveHandler (relai sans validation gameplay).

#include "src/masterd/handlers/interactive/InteractiveHandler.h"

#include "src/masterd/session/ConnectionSessionMap.h"
#include "src/masterd/session/SessionManager.h"
#include "src/shared/core/Log.h"
#include "src/shared/network/InteractivePayloads.h"
#include "src/shared/network/NetServer.h"
#include "src/shared/network/ProtocolV1Constants.h"

#include <chrono>
#include <utility>
#include <vector>

namespace engine::server
{
	using engine::server::interactive::ChangeResult;

	namespace
	{
		/// system_clock now en ms depuis epoch (horloge serveur, pour la
		/// compensation de latence côté clients distants).
		uint64_t NowMs()
		{
			const auto v = std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::system_clock::now().time_since_epoch()).count();
			return static_cast<uint64_t>(v);
		}
	}

	void InteractiveHandler::SeedInteractive(uint64_t id, uint8_t initialState)
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_relay.Seed(id, initialState);
	}

	void InteractiveHandler::HandlePacket(uint32_t connId, uint16_t opcode, uint32_t /*requestId*/,
		uint64_t sessionIdHeader, const uint8_t* payload, size_t payloadSize)
	{
		using namespace engine::network;

		if (opcode != kOpInteractiveStateChange)
			return; // 201/202 sont des pushes serveur→client, jamais reçus.

		if (!m_server || !m_sessionMgr || !m_connMap)
		{
			LOG_WARN(Net, "[InteractiveHandler] Drop opcode={} : handler not fully wired", opcode);
			return;
		}

		// Résolution session/account. Session requise (mais aucune validation
		// gameplay au-delà : pas de droit d'ouverture, pas de portée).
		bool sessionOk = false;
		auto connSessionId = m_connMap->GetSessionId(connId);
		if (connSessionId && *connSessionId != 0u
			&& sessionIdHeader != 0u && *connSessionId == sessionIdHeader)
		{
			auto acc = m_sessionMgr->GetAccountId(*connSessionId);
			if (acc && *acc != 0u)
				sessionOk = true;
		}
		if (!sessionOk)
		{
			LOG_WARN(Net, "[InteractiveHandler] StateChange dropped connId={} : no valid session", connId);
			return;
		}

		auto parsed = ParseInteractiveStateChangePayload(payload, payloadSize);
		if (!parsed)
		{
			LOG_WARN(Net, "[InteractiveHandler] StateChange malformed payload connId={}", connId);
			return;
		}

		ChangeResult result;
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			result = m_relay.ApplyStateChange(parsed->id, parsed->newState);
		}

		if (result == ChangeResult::UnknownId)
		{
			// Critère d'acceptation : id inconnu → warning + ignore, SANS
			// erreur de protocole.
			LOG_WARN(Net, "[InteractiveHandler] StateChange id={} inconnu (ignore, pas d'erreur)", parsed->id);
			return;
		}

		LOG_INFO(Net, "[InteractiveHandler] StateChange id={} newState={} connId={} -> broadcast",
			parsed->id, static_cast<unsigned>(parsed->newState), connId);

		BroadcastStateChange(connId, parsed->id, parsed->newState);
	}

	void InteractiveHandler::BroadcastStateChange(uint32_t exceptConn, uint64_t id, uint8_t newState)
	{
		using namespace engine::network;
		if (!m_server || !m_connMap) return;

		const uint64_t serverTimeMs = NowMs();
		const auto snapshot = m_connMap->Snapshot();
		size_t delivered = 0;
		for (const auto& [destConn, destSession] : snapshot)
		{
			if (destConn == exceptConn) continue; // l'émetteur a déjà animé localement.
			if (destSession == 0u) continue;
			auto pkt = BuildInteractiveStateBroadcastPacket(id, newState, serverTimeMs, destSession);
			if (pkt.empty()) continue;
			if (m_server->Send(destConn, pkt))
				++delivered;
		}
		LOG_INFO(Net, "[InteractiveHandler] Broadcast id={} state={} delivered={}/{}",
			id, static_cast<unsigned>(newState), delivered, snapshot.size());
	}

	bool InteractiveHandler::SendInitialSync(uint32_t connId)
	{
		using namespace engine::network;
		if (!m_server || !m_connMap || connId == 0u)
			return false;

		auto sid = m_connMap->GetSessionId(connId);
		if (!sid || *sid == 0u)
		{
			LOG_WARN(Net, "[InteractiveHandler] SendInitialSync connId={} : no session (skip)", connId);
			return false;
		}

		std::vector<InteractiveSyncEntry> entries;
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			auto snap = m_relay.Snapshot();
			entries.reserve(snap.size());
			for (const auto& [id, state] : snap)
				entries.push_back(InteractiveSyncEntry{ id, state });
		}

		auto pkt = BuildInteractiveStateSyncPacket(entries, *sid);
		if (pkt.empty())
		{
			LOG_WARN(Net, "[InteractiveHandler] SendInitialSync build failed connId={}", connId);
			return false;
		}
		m_server->Send(connId, pkt);
		LOG_INFO(Net, "[InteractiveHandler] InitialSync connId={} count={}", connId, entries.size());
		return true;
	}
}
