#pragma once

#include <cstdint>
#include <unordered_map>
#include <unordered_set>

namespace engine::server
{
	class NetServer;
	class ShardTicketValidator;
	class AdmittedCharacterRegistry;
}

namespace engine::server
{
	/// Shard-side handler: first packet from each connection must be PRESENT_SHARD_TICKET; validates and responds TICKET_ACCEPTED or TICKET_REJECTED (M22.6).
	///
	/// Suivi de #584 (master-side kick) : quand un nouveau ticket arrive pour un account_id
	/// deja en monde sur ce shard, on evicte la connexion precedente (close TCP + cleanup).
	/// Sans cette eviction, le shard accepte les 2 tickets et garde les 2 TCP alive jusqu'au
	/// HeartbeatTimeout (~60s), permettant un duplicate-play pendant cette fenetre. Le map
	/// account_id -> connId est tenue ici (handler du shard) car le shard n'a pas de
	/// SessionManager equivalent au master.
	class ShardTicketHandshakeHandler
	{
	public:
		ShardTicketHandshakeHandler() = default;

		void SetServer(NetServer* server);
		void SetValidator(ShardTicketValidator* validator);
		/// TA.3 : registre où inscrire le personnage admis (character_id du ticket validé),
		/// consulté par le gate UDP ServerApp::HandleHello. Optionnel.
		void SetAdmittedCharacterRegistry(AdmittedCharacterRegistry* registry) { m_admittedRegistry = registry; }

		/// Handles PRESENT_SHARD_TICKET only for connections that have not yet completed handshake. Others are ignored.
		void HandlePacket(uint32_t connId, uint16_t opcode, uint32_t requestId, uint64_t sessionIdHeader,
			const uint8_t* payload, size_t payloadSize);

		/// Call when a connection is closed (optional cleanup). If not called, set may grow.
		void OnConnectionClosed(uint32_t connId);

	private:
		NetServer* m_server = nullptr;
		ShardTicketValidator* m_validator = nullptr;
		AdmittedCharacterRegistry* m_admittedRegistry = nullptr;
		std::unordered_set<uint32_t> m_handshakeDone;
		/// connId -> character_id admis, pour révoquer l'admission à la fermeture / éviction.
		std::unordered_map<uint32_t, uint64_t> m_connToCharacter;
		/// account_id -> connId pour eviction sur duplicate-ticket. Le shard
		/// ne maintient pas de SessionManager : on s'appuie sur cette map locale
		/// pour ne garder qu'une connexion par account a la fois.
		std::unordered_map<uint64_t, uint32_t> m_accountToConn;
		/// connId -> account_id pour reverse lookup au cleanup (OnConnectionClosed).
		std::unordered_map<uint32_t, uint64_t> m_connToAccount;
	};
}
