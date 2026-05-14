#pragma once

#include <cstddef>
#include <cstdint>

namespace engine::server::db
{
	class ConnectionPool;
}

namespace engine::server
{
	class NetServer;
	class SessionManager;
	class ConnectionSessionMap;

	/// M100.44 — Master-side handler pour `kOpcodeEnterDungeonRequest`
	/// (opcode 197), clôture de la Phase 11 « Volumes 3D ».
	///
	/// Câble les opcodes 197/198 réservés par M100.43. Le client
	/// déclenche un portail de donjon posé par l'éditeur monde ; le
	/// master :
	///   1. valide la session (connId → sessionId → accountId),
	///   2. valide l'ownership du personnage (SELECT characters gated
	///      par account_id),
	///   3. INSERT une ligne dans `dungeon_instances` (migration 0063)
	///      avec le `dungeon_template_id` + `owner_character_id` +
	///      `difficulty`,
	///   4. renvoie un `EnterDungeonResponsePayload` (opcode 198) avec
	///      le `instanceId` fraîchement créé.
	///
	/// Le `shardEndpoint` reste vide en M100.44 : la résolution shard
	/// multi-instance (un shard dédié par dungeon-instance) est un
	/// follow-up. Le client recevra l'instanceId mais le routage vers
	/// le shard de donjon viendra plus tard — pour l'instant le handler
	/// prouve le round-trip wire complet + la persistance DB.
	///
	/// MVP : pas de cap d'instances actives (kEnterDungeonErrorInstanceFull
	/// jamais renvoyé), pas de validation de progression
	/// (kEnterDungeonErrorDifficultyLocked jamais renvoyé). Ces gating
	/// gameplay viendront avec le contenu donjon réel.
	class EnterDungeonHandler
	{
	public:
		void SetServer(NetServer* server);
		void SetSessionManager(SessionManager* sessions);
		void SetConnectionSessionMap(ConnectionSessionMap* map);
		void SetConnectionPool(engine::server::db::ConnectionPool* pool);

		void HandlePacket(uint32_t connId, uint16_t opcode, uint32_t requestId,
			uint64_t sessionIdHeader, const uint8_t* payload, size_t payloadSize);

	private:
		NetServer*                          m_server   = nullptr;
		SessionManager*                     m_sessions = nullptr;
		ConnectionSessionMap*               m_connMap  = nullptr;
		engine::server::db::ConnectionPool* m_pool     = nullptr;
	};
}
