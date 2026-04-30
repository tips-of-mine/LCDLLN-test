#include "engine/server/ChatRelayHandler.h"

#include "engine/core/Log.h"
#include "engine/net/ChatSystem.h"
#include "engine/network/ChatPayloads.h"
#include "engine/network/ErrorPacket.h"
#include "engine/network/ProtocolV1Constants.h"
#include "engine/server/AccountStore.h"
#include "engine/server/AccountRecord.h"
#include "engine/server/ConnectionSessionMap.h"
#include "engine/server/NetServer.h"
#include "engine/server/SessionManager.h"

#include <chrono>
#include <string>

namespace engine::server
{
	namespace
	{
		uint64_t NowUnixMsUtc()
		{
			using namespace std::chrono;
			return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
		}

		bool IsValidChannelByte(uint8_t channel)
		{
			// Aligné sur engine::net::ChatChannel : Say(0)..Friends(9). Tout au-delà rejeté.
			return channel <= static_cast<uint8_t>(engine::net::ChatChannel::Friends);
		}
	}

	void ChatRelayHandler::SetServer(NetServer* server)                       { m_server = server; }
	void ChatRelayHandler::SetSessionManager(SessionManager* sessions)        { m_sessions = sessions; }
	void ChatRelayHandler::SetConnectionSessionMap(ConnectionSessionMap* map) { m_connMap = map; }
	void ChatRelayHandler::SetAccountStore(AccountStore* accounts)            { m_accounts = accounts; }

	void ChatRelayHandler::HandlePacket(uint32_t connId, uint16_t opcode, uint32_t requestId, uint64_t sessionIdHeader,
		const uint8_t* payload, size_t payloadSize)
	{
		using namespace engine::network;
		if (opcode != kOpcodeChatSendRequest || !m_server || !m_sessions || !m_connMap || !m_accounts)
			return;

		auto parsed = ParseChatSendRequestPayload(payload, payloadSize);
		if (!parsed)
		{
			auto pkt = BuildErrorPacket(NetErrorCode::BAD_REQUEST, "invalid chat payload", requestId, sessionIdHeader);
			if (!pkt.empty())
				m_server->Send(connId, pkt);
			return;
		}

		if (!IsValidChannelByte(parsed->channel))
		{
			auto pkt = BuildErrorPacket(NetErrorCode::BAD_REQUEST, "invalid channel", requestId, sessionIdHeader);
			if (!pkt.empty())
				m_server->Send(connId, pkt);
			return;
		}

		if (parsed->text.empty())
		{
			// Empty message : on ignore silencieusement (pas d'erreur, l'utilisateur a sans
			// doute juste pressé Entrée sans contenu).
			return;
		}
		if (parsed->text.size() > kMaxChatTextBytes)
		{
			parsed->text.resize(kMaxChatTextBytes);
		}

		auto connSessionId = m_connMap->GetSessionId(connId);
		if (!connSessionId || *connSessionId == 0 || sessionIdHeader == 0 || *connSessionId != sessionIdHeader)
		{
			auto pkt = BuildErrorPacket(NetErrorCode::INVALID_CREDENTIALS, "session required", requestId, sessionIdHeader);
			if (!pkt.empty())
				m_server->Send(connId, pkt);
			return;
		}
		auto accountId = m_sessions->GetAccountId(*connSessionId);
		if (!accountId)
		{
			auto pkt = BuildErrorPacket(NetErrorCode::INVALID_CREDENTIALS, "account missing", requestId, sessionIdHeader);
			if (!pkt.empty())
				m_server->Send(connId, pkt);
			return;
		}

		auto accountRec = m_accounts->FindByAccountId(*accountId);
		if (!accountRec)
		{
			auto pkt = BuildErrorPacket(NetErrorCode::INTERNAL_ERROR, "account lookup failed", requestId, sessionIdHeader);
			if (!pkt.empty())
				m_server->Send(connId, pkt);
			return;
		}

		const std::string sender = accountRec->login;

		// Whisper non encore implémenté : on renvoie une notice "Server" channel à l'expéditeur seulement.
		if (parsed->channel == static_cast<uint8_t>(engine::net::ChatChannel::Whisper))
		{
			const uint64_t ts = NowUnixMsUtc();
			auto notice = BuildChatRelayPacket(ts,
				static_cast<uint8_t>(engine::net::ChatChannel::Server),
				"system",
				"Whisper not yet supported (Phase 4.x).",
				sessionIdHeader);
			if (!notice.empty())
				m_server->Send(connId, notice);
			LOG_INFO(Net, "[ChatRelayHandler] Whisper requested by '{}' rejected (not implemented)", sender);
			return;
		}

		// Broadcast : un même paquet à toutes les sessions actives. Chaque destinataire reçoit
		// le packet avec SON propre sessionId dans l'en-tête (assemblage par paquet, pas un seul
		// PacketBuilder réutilisé) — sinon le client filtrerait éventuellement par session.
		const uint64_t ts = NowUnixMsUtc();
		const auto snapshot = m_connMap->Snapshot();
		size_t delivered = 0;
		for (const auto& [destConn, destSession] : snapshot)
		{
			auto pkt = BuildChatRelayPacket(ts, parsed->channel, sender, parsed->text, destSession);
			if (pkt.empty())
				continue;
			if (m_server->Send(destConn, pkt))
				++delivered;
		}
		LOG_INFO(Net, "[ChatRelayHandler] Chat broadcast sender='{}' channel={} text_len={} delivered={}/{}",
			sender, static_cast<unsigned>(parsed->channel), parsed->text.size(), delivered, snapshot.size());
	}
}
