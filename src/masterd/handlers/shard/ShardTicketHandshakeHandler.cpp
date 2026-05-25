#include "src/masterd/handlers/shard/ShardTicketHandshakeHandler.h"
#include "src/shared/network/NetServer.h"
#include "src/masterd/handlers/shard/ShardTicketValidator.h"
#include "src/shared/network/ShardTicketPayloads.h"
#include "src/shared/network/ProtocolV1Constants.h"
#include "src/shardd/world/AdmittedCharacterRegistry.h"
#include "src/shared/core/Log.h"

#include <chrono>
#include <cstdio>

namespace
{
	/// Horloge monotone en ms, base partagée avec le gate UDP (steady_clock process-wide).
	uint64_t NowMonotonicMs()
	{
		return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::steady_clock::now().time_since_epoch()).count());
	}
}

namespace engine::server
{
	void ShardTicketHandshakeHandler::SetServer(NetServer* server) { m_server = server; }
	void ShardTicketHandshakeHandler::SetValidator(ShardTicketValidator* validator) { m_validator = validator; }

	void ShardTicketHandshakeHandler::OnConnectionClosed(uint32_t connId)
	{
		m_handshakeDone.erase(connId);
		// Nettoie aussi les maps account<->conn pour eviter de laisser un
		// account orphelin (si reconnexion plus tard, on n'evicterait pas
		// la "fausse" conn deja fermee). Si l'account avait une autre
		// connexion entre temps, m_accountToConn[account] != connId donc
		// on ne purge pas par erreur.
		auto it = m_connToAccount.find(connId);
		if (it != m_connToAccount.end())
		{
			const uint64_t accountId = it->second;
			auto acctIt = m_accountToConn.find(accountId);
			if (acctIt != m_accountToConn.end() && acctIt->second == connId)
				m_accountToConn.erase(acctIt);
			m_connToAccount.erase(it);
		}
		// TA.3 : révoque l'admission UDP du personnage associé à cette connexion.
		auto charIt = m_connToCharacter.find(connId);
		if (charIt != m_connToCharacter.end())
		{
			if (m_admittedRegistry != nullptr)
				m_admittedRegistry->Revoke(charIt->second);
			m_connToCharacter.erase(charIt);
		}
	}

	void ShardTicketHandshakeHandler::HandlePacket(uint32_t connId, uint16_t opcode, uint32_t requestId, uint64_t /*sessionIdHeader*/,
		const uint8_t* payload, size_t payloadSize)
	{
		using namespace engine::network;
		LOG_DEBUG(Server, "[TICKET_HS] HandlePacket connId={} opcode={}", connId, opcode);
		if (!m_server || !m_validator)
		{
			LOG_WARN(Core, "[ShardTicketHandshakeHandler] HandlePacket: server or validator not set");
			return;
		}
		if (m_handshakeDone.count(connId) != 0)
			return;
		if (opcode != kOpcodePresentShardTicket)
		{
			LOG_WARN(Core, "[ShardTicketHandshakeHandler] Conn {} first packet not PRESENT_SHARD_TICKET (opcode={}), rejecting", connId, opcode);
			auto pkt = BuildShardTicketRejectedPacket(requestId, "ticket required");
			if (!pkt.empty())
				m_server->Send(connId, pkt);
			m_server->CloseConnection(connId, DisconnectReason::InvalidPacket);
			return;
		}
		auto accept = m_validator->VerifyAndConsume(payload, payloadSize);
		LOG_DEBUG(Server, "[TICKET_HS] VerifyAndConsume result={}", accept ? "ACCEPTED" : "REJECTED");
		if (!accept)
		{
			LOG_WARN(Core, "[ShardTicketHandshakeHandler] Conn {} ticket invalid or expired, rejecting", connId);
			auto pkt = BuildShardTicketRejectedPacket(requestId, "invalid or expired ticket");
			if (!pkt.empty())
				m_server->Send(connId, pkt);
			m_server->CloseConnection(connId, DisconnectReason::InvalidPacket);
			return;
		}
		// Eviction du duplicate-ticket : si cet account a deja une connexion shard active,
		// on la ferme avant d'accepter la nouvelle. Suivi du fix #584 cote master.
		// Sans cette eviction, la fenetre HeartbeatTimeout (~60s) permettait au precedent
		// client de jouer le meme personnage en parallele du nouveau.
		{
			auto prev = m_accountToConn.find(accept->account_id);
			if (prev != m_accountToConn.end() && prev->second != connId)
			{
				const uint32_t oldConnId = prev->second;
				LOG_INFO(Core,
					"[ShardTicketHandshakeHandler] evicting prior connection (account_id={} oldConnId={} newConnId={})",
					accept->account_id, oldConnId, connId);
				m_server->CloseConnection(oldConnId, DisconnectReason::KickedByDuplicateLogin);
				// On nettoie nos maps en avance (sans attendre OnConnectionClosed du NetServer
				// qui peut etre asynchrone) : la nouvelle conn va inserer juste apres.
				m_handshakeDone.erase(oldConnId);
				m_connToAccount.erase(oldConnId);
				m_accountToConn.erase(prev);
				// TA.3 : révoque l'admission UDP du personnage de l'ancienne connexion.
				auto oldChar = m_connToCharacter.find(oldConnId);
				if (oldChar != m_connToCharacter.end())
				{
					if (m_admittedRegistry != nullptr)
						m_admittedRegistry->Revoke(oldChar->second);
					m_connToCharacter.erase(oldChar);
				}
			}
		}

		auto pkt = BuildShardTicketAcceptedPacket(requestId);
		if (pkt.empty() || !m_server->Send(connId, pkt))
		{
			LOG_ERROR(Core, "[ShardTicketHandshakeHandler] Send TICKET_ACCEPTED failed (connId={})", connId);
			m_server->CloseConnection(connId, DisconnectReason::SendError);
			return;
		}
		m_handshakeDone.insert(connId);
		m_accountToConn[accept->account_id] = connId;
		m_connToAccount[connId] = accept->account_id;
		// TA.3 : admet le personnage (character_id authentifié du ticket) pour que le gate
		// UDP ServerApp::HandleHello accepte son Hello. character_id=0 (ticket émis avant
		// EnterWorld) n'est pas admis → le client devra entrer en monde avant de jouer.
		if (m_admittedRegistry != nullptr && accept->character_id != 0u)
		{
			m_admittedRegistry->Admit(accept->character_id, accept->account_id, NowMonotonicMs());
			m_connToCharacter[connId] = accept->character_id;
		}
		LOG_INFO(Core, "[ShardTicketHandshakeHandler] Ticket accepted (connId={} account_id={} character_id={} target_shard_id={})", connId, accept->account_id, accept->character_id, accept->target_shard_id);
	}
}
