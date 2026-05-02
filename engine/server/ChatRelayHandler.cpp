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
#include "engine/server/SessionCharacterMap.h"
#include "engine/server/SessionManager.h"
#include "engine/server/db/ConnectionPool.h"
#include "engine/server/db/DbHelpers.h"

#include <mysql.h>

#include <chrono>
#include <cstdio>
#include <unordered_set>
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
	void ChatRelayHandler::SetSessionCharacterMap(SessionCharacterMap* charMap) { m_charMap = charMap; }
	void ChatRelayHandler::SetAccountStore(AccountStore* accounts)            { m_accounts = accounts; }
	void ChatRelayHandler::SetConnectionPool(engine::server::db::ConnectionPool* pool) { m_pool = pool; }

	void ChatRelayHandler::HandlePacket(uint32_t connId, uint16_t opcode, uint32_t requestId, uint64_t sessionIdHeader,
		const uint8_t* payload, size_t payloadSize)
	{
		using namespace engine::network;
		if (opcode != kOpcodeChatSendRequest || !m_server || !m_sessions || !m_connMap || !m_accounts)
			return;

		// Trace systématique à l'entrée pour assurer le suivi côté serveur de chaque
		// CHAT_SEND_REQUEST reçu (visibilité demandée par l'opérateur — sinon les
		// rejets précoces / les broadcasts vides ne laissaient aucune trace).
		LOG_INFO(Net, "[ChatRelayHandler] Recv CHAT_SEND_REQUEST conn={} session_hdr={} payload_size={}",
			connId, sessionIdHeader, payloadSize);

		auto parsed = ParseChatSendRequestPayload(payload, payloadSize);
		if (!parsed)
		{
			LOG_WARN(Net, "[ChatRelayHandler] Reject : invalid payload (conn={} payload_size={})",
				connId, payloadSize);
			auto pkt = BuildErrorPacket(NetErrorCode::BAD_REQUEST, "invalid chat payload", requestId, sessionIdHeader);
			if (!pkt.empty())
				m_server->Send(connId, pkt);
			return;
		}

		if (!IsValidChannelByte(parsed->channel))
		{
			LOG_WARN(Net, "[ChatRelayHandler] Reject : invalid channel byte={} (conn={})",
				static_cast<unsigned>(parsed->channel), connId);
			auto pkt = BuildErrorPacket(NetErrorCode::BAD_REQUEST, "invalid channel", requestId, sessionIdHeader);
			if (!pkt.empty())
				m_server->Send(connId, pkt);
			return;
		}

		if (parsed->text.empty())
		{
			LOG_DEBUG(Net, "[ChatRelayHandler] Empty text from conn={} channel={} : ignored silently",
				connId, static_cast<unsigned>(parsed->channel));
			// Empty message : on ignore silencieusement (pas d'erreur, l'utilisateur a sans
			// doute juste pressé Entrée sans contenu).
			return;
		}
		LOG_INFO(Net, "[ChatRelayHandler] Parsed channel={} target='{}' text_len={}",
			static_cast<unsigned>(parsed->channel), parsed->targetToken, parsed->text.size());
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

		// Phase 4 — Sender display name : preference au character_name (post-EnterWorld)
		// via SessionCharacterMap, fallback au login d'account si le client n'a pas encore
		// declaré son perso (ex. message envoyé entre EnterWorld et la confirmation du
		// CHARACTER_ENTER_WORLD_RESPONSE).
		std::string sender;
		if (m_charMap)
		{
			if (auto info = m_charMap->GetByConnId(connId))
				sender = info->characterName;
		}
		if (sender.empty())
		{
			auto accountRec = m_accounts->FindByAccountId(*accountId);
			if (!accountRec)
			{
				auto pkt = BuildErrorPacket(NetErrorCode::INTERNAL_ERROR, "account lookup failed", requestId, sessionIdHeader);
				if (!pkt.empty())
					m_server->Send(connId, pkt);
				return;
			}
			sender = accountRec->login;
		}

		// Phase 4 — Whisper : résolution du target par nom normalisé.
		if (parsed->channel == static_cast<uint8_t>(engine::net::ChatChannel::Whisper))
		{
			if (!m_charMap || parsed->targetToken.empty())
			{
				const uint64_t ts0 = NowUnixMsUtc();
				auto notice = BuildChatRelayPacket(ts0,
					static_cast<uint8_t>(engine::net::ChatChannel::Server),
					"system",
					"Whisper requires a target.",
					sessionIdHeader);
				if (!notice.empty())
					m_server->Send(connId, notice);
				return;
			}
			const std::string targetNorm = SessionCharacterMap::Normalize(parsed->targetToken);
			auto destConnOpt = m_charMap->FindConnByNormalizedName(targetNorm);
			if (!destConnOpt)
			{
				const uint64_t ts0 = NowUnixMsUtc();
				auto notice = BuildChatRelayPacket(ts0,
					static_cast<uint8_t>(engine::net::ChatChannel::Server),
					"system",
					"Player '" + parsed->targetToken + "' is not online.",
					sessionIdHeader);
				if (!notice.empty())
					m_server->Send(connId, notice);
				LOG_INFO(Net, "[ChatRelayHandler] Whisper from '{}' to '{}' : target offline",
					sender, parsed->targetToken);
				return;
			}
			const uint32_t destConn = *destConnOpt;
			const uint64_t ts = NowUnixMsUtc();

			// Pour le destinataire : on doit utiliser SON sessionId. On le récupère via
			// ConnectionSessionMap.
			auto destSessionOpt = m_connMap->GetSessionId(destConn);
			if (destSessionOpt)
			{
				// Texte vu par le destinataire : "[from sender] body" — pour qu'il sache de qui ca vient.
				const std::string toRecipient = "[from " + sender + "] " + parsed->text;
				auto pktDest = BuildChatRelayPacket(ts, parsed->channel, sender, toRecipient, *destSessionOpt);
				if (!pktDest.empty())
					m_server->Send(destConn, pktDest);
			}

			// Echo expéditeur : "[to target] body" — confirmation que le whisper est bien parti.
			const std::string toSender = "[to " + parsed->targetToken + "] " + parsed->text;
			auto pktSender = BuildChatRelayPacket(ts, parsed->channel, sender, toSender, sessionIdHeader);
			if (!pktSender.empty())
				m_server->Send(connId, pktSender);

			LOG_INFO(Net, "[ChatRelayHandler] Whisper '{}' -> '{}' (text_len={})",
				sender, parsed->targetToken, parsed->text.size());
			return;
		}

		// Phase 5.1 — Guild routing : SQL `guild_members` (1) sender's guild, (2) co-members.
		if (parsed->channel == static_cast<uint8_t>(engine::net::ChatChannel::Guild))
		{
			if (!m_pool)
			{
				// Pas de pool DB attaché : fallback broadcast pour ne pas perdre le message.
				LOG_WARN(Net, "[ChatRelayHandler] Guild routing requested but no ConnectionPool ; falling back to broadcast");
			}
			else
			{
				auto guard = m_pool->Acquire();
				MYSQL* mysql = guard.get();
				if (!mysql)
				{
					auto pkt = BuildErrorPacket(NetErrorCode::INTERNAL_ERROR, "database unavailable", requestId, sessionIdHeader);
					if (!pkt.empty())
						m_server->Send(connId, pkt);
					return;
				}

				// Une seule requête : récupère tous les player_id qui partagent la guilde du sender.
				// Si le sender n'a pas de ligne dans guild_members → result vide → on notice.
				char queryBuf[512]{};
				std::snprintf(queryBuf, sizeof(queryBuf),
					"SELECT player_id FROM guild_members WHERE guild_id = "
					"(SELECT guild_id FROM guild_members WHERE player_id = %llu LIMIT 1)",
					static_cast<unsigned long long>(*accountId));

				MYSQL_RES* res = engine::server::db::DbQuery(mysql, queryBuf);
				std::unordered_set<uint64_t> memberAccountIds;
				if (res)
				{
					MYSQL_ROW row;
					while ((row = mysql_fetch_row(res)) != nullptr)
					{
						if (row[0])
						{
							const uint64_t pid = std::strtoull(row[0], nullptr, 10);
							if (pid != 0u)
								memberAccountIds.insert(pid);
						}
					}
					engine::server::db::DbFreeResult(res);
				}

				if (memberAccountIds.empty())
				{
					// Sender pas dans une guilde (ou guilde vide — impossible normalement) → notice.
					const uint64_t ts0 = NowUnixMsUtc();
					auto notice = BuildChatRelayPacket(ts0,
						static_cast<uint8_t>(engine::net::ChatChannel::Server),
						"system",
						"You are not in a guild.",
						sessionIdHeader);
					if (!notice.empty())
						m_server->Send(connId, notice);
					LOG_INFO(Net, "[ChatRelayHandler] Guild from '{}' rejected : not in a guild", sender);
					return;
				}

				// Snapshot connId↔sessionId → on filtre ceux dont l'account_id est dans le set.
				const uint64_t ts = NowUnixMsUtc();
				const auto snapshot = m_connMap->Snapshot();
				size_t delivered = 0;
				for (const auto& [destConn, destSession] : snapshot)
				{
					auto destAccountOpt = m_sessions->GetAccountId(destSession);
					if (!destAccountOpt)
						continue;
					if (memberAccountIds.find(*destAccountOpt) == memberAccountIds.end())
						continue;
					auto pkt = BuildChatRelayPacket(ts, parsed->channel, sender, parsed->text, destSession);
					if (pkt.empty())
						continue;
					if (m_server->Send(destConn, pkt))
						++delivered;
				}
				LOG_INFO(Net, "[ChatRelayHandler] Guild routing sender='{}' members_total={} delivered={}",
					sender, memberAccountIds.size(), delivered);
				return;
			}
		}

		// Phase 5.2 — Friends routing : SQL `friends` (status=1 accepted) → set d'account_id.
		// Le sender est ajouté au set pour qu'il voie son propre message echo (cohérent avec
		// le comportement guild où sender ∈ guild_members).
		if (parsed->channel == static_cast<uint8_t>(engine::net::ChatChannel::Friends))
		{
			if (!m_pool)
			{
				LOG_WARN(Net, "[ChatRelayHandler] Friends routing requested but no ConnectionPool ; falling back to broadcast");
			}
			else
			{
				auto guard = m_pool->Acquire();
				MYSQL* mysql = guard.get();
				if (!mysql)
				{
					auto pkt = BuildErrorPacket(NetErrorCode::INTERNAL_ERROR, "database unavailable", requestId, sessionIdHeader);
					if (!pkt.empty())
						m_server->Send(connId, pkt);
					return;
				}

				char queryBuf[256]{};
				std::snprintf(queryBuf, sizeof(queryBuf),
					"SELECT friend_id FROM friends WHERE player_id = %llu AND status = 1",
					static_cast<unsigned long long>(*accountId));

				MYSQL_RES* res = engine::server::db::DbQuery(mysql, queryBuf);
				std::unordered_set<uint64_t> friendAccountIds;
				friendAccountIds.insert(*accountId); // sender voit son propre echo
				if (res)
				{
					MYSQL_ROW row;
					while ((row = mysql_fetch_row(res)) != nullptr)
					{
						if (row[0])
						{
							const uint64_t fid = std::strtoull(row[0], nullptr, 10);
							if (fid != 0u)
								friendAccountIds.insert(fid);
						}
					}
					engine::server::db::DbFreeResult(res);
				}

				if (friendAccountIds.size() == 1u)
				{
					// Aucune ligne friends → seul le sender est dans le set → notice "no friends online".
					// Distinct du cas "vraiment 0 amis" vs "amis tous offline" — pour MVP on ne discrimine pas.
					const uint64_t ts0 = NowUnixMsUtc();
					auto notice = BuildChatRelayPacket(ts0,
						static_cast<uint8_t>(engine::net::ChatChannel::Server),
						"system",
						"No friends online to receive your message.",
						sessionIdHeader);
					if (!notice.empty())
						m_server->Send(connId, notice);
					LOG_INFO(Net, "[ChatRelayHandler] Friends from '{}' : no recipients (only self)", sender);
					return;
				}

				const uint64_t ts = NowUnixMsUtc();
				const auto snapshot = m_connMap->Snapshot();
				size_t delivered = 0;
				for (const auto& [destConn, destSession] : snapshot)
				{
					auto destAccountOpt = m_sessions->GetAccountId(destSession);
					if (!destAccountOpt)
						continue;
					if (friendAccountIds.find(*destAccountOpt) == friendAccountIds.end())
						continue;
					auto pkt = BuildChatRelayPacket(ts, parsed->channel, sender, parsed->text, destSession);
					if (pkt.empty())
						continue;
					if (m_server->Send(destConn, pkt))
						++delivered;
				}
				LOG_INFO(Net, "[ChatRelayHandler] Friends routing sender='{}' friends_total={} delivered={}",
					sender, friendAccountIds.size() - 1u, delivered);
				return;
			}
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
