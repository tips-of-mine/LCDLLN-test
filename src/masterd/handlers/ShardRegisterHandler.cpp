/// @file ShardRegisterHandler.cpp
/// @brief Implémentation du gestionnaire d'enregistrement des shards côté Master.
///
/// Responsabilités :
///   - Valider et parser les paquets SHARD_REGISTER / SHARD_HEARTBEAT.
///   - Maintenir la cohérence entre les connexions réseau (connId) et les entrées
///     du ShardRegistry (shard_id). Un shard qui se reconnecte reçoit son shard_id
///     originel plutôt qu'un nouvel identifiant.
///   - Émettre les accusés REGISTER_OK ou REGISTER_ERROR via NetServer::Send.
/// Thread-safety : non — doit être appelé depuis le thread réseau du Master uniquement.

#include "engine/server/ShardRegisterHandler.h"
#include "engine/server/NetServer.h"
#include "engine/server/ShardRegistry.h"
#include "engine/network/ShardPayloads.h"
#include "engine/network/ProtocolV1Constants.h"
#include "engine/core/Log.h"

#include <cstdio>

namespace engine::server
{
	void ShardRegisterHandler::SetServer(NetServer* server) { m_server = server; }
	void ShardRegisterHandler::SetShardRegistry(ShardRegistry* registry) { m_registry = registry; }

	void ShardRegisterHandler::HandlePacket(uint32_t connId, uint16_t opcode, uint32_t requestId, uint64_t /*sessionIdHeader*/,
		const uint8_t* payload, size_t payloadSize)
	{
		using namespace engine::network;
		if (!m_server || !m_registry)
		{
			LOG_WARN(Core, "[ShardRegisterHandler] HandlePacket: server or registry not set");
			return;
		}
		if (opcode == kOpcodeShardRegister)
		{
			HandleRegister(connId, requestId, payload, payloadSize);
			return;
		}
		if (opcode == kOpcodeShardHeartbeat)
		{
			HandleHeartbeat(connId, payload, payloadSize);
			return;
		}
	}

	void ShardRegisterHandler::HandleRegister(uint32_t connId, uint32_t requestId, const uint8_t* payload, size_t payloadSize)
	{
		using namespace engine::network;
		LOG_DEBUG(Server, "[SREG] HandleRegister connId={}", connId);

		// --- Étape 1 : parsing du payload -----------------------------------------
		// ParseShardRegisterPayload décode le format binaire (nom, endpoint, max_capacity,
		// current_load). En cas d'échec, on répond immédiatement avec REGISTER_ERROR.
		auto parsed = ParseShardRegisterPayload(payload, payloadSize);
		if (!parsed)
		{
			LOG_WARN(Core, "[ShardRegisterHandler] Register: invalid payload (connId={})", connId);
			auto pkt = BuildShardRegisterErrorPacket(ShardRegisterErrorCode::InvalidPayload, requestId);
			if (!pkt.empty())
				m_server->Send(connId, pkt);
			LOG_WARN(Server, "[SREG] REGISTER_ERROR sent connId={}", connId);
			return;
		}

		// On conserve nom et charge avant de déplacer les strings dans RegisterShard.
		std::string name = parsed->name;
		std::string endpoint = parsed->endpoint;
		uint32_t current_load = parsed->current_load;

		// --- Étape 2 : enregistrement dans le registre ----------------------------
		// RegisterShard retourne nullopt si le nom est déjà présent (cas de reconnexion).
		auto id = m_registry->RegisterShard(std::move(parsed->name), std::move(parsed->endpoint), parsed->max_capacity, {});
		LOG_DEBUG(Server, "[SREG] RegisterShard id={} (0=duplicate)", id ? *id : 0u);

		if (!id)
		{
			// --- Cas reconnexion : le shard possède déjà un enregistrement actif -----
			// On cherche l'entrée existante par nom et on retourne le même shard_id
			// plutôt que de rejeter la reconnexion. Cela permet aux shards de se
			// reconnecter proprement après un crash du lien réseau.
			auto list = m_registry->ListShards();
			for (const auto& s : list)
			{
				if (s.name == name)
				{
					m_registry->UpdateHeartbeat(s.shard_id, current_load);
					auto pkt = BuildShardRegisterOkPacket(s.shard_id, requestId);
					if (!pkt.empty() && m_server->Send(connId, pkt))
					{
						LOG_INFO(Core, "[ShardRegisterHandler] Re-register OK (connId={}, shard_id={})", connId, s.shard_id);
					}
					return;
				}
			}
			// Doublon sans entrée retrouvable — situation anormale (ne devrait pas arriver).
			LOG_WARN(Core, "[ShardRegisterHandler] Register: duplicate name (connId={})", connId);
			auto pkt = BuildShardRegisterErrorPacket(ShardRegisterErrorCode::DuplicateName, requestId);
			if (!pkt.empty())
				m_server->Send(connId, pkt);
			LOG_WARN(Server, "[SREG] REGISTER_ERROR sent connId={}", connId);
			return;
		}

		// --- Étape 3 : mise à jour du heartbeat et envoi de la réponse OK ----------
		// Le premier UpdateHeartbeat fait passer l'état de Registering → Online.
		m_registry->UpdateHeartbeat(*id, current_load);
		auto pkt = BuildShardRegisterOkPacket(*id, requestId);
		if (!pkt.empty() && m_server->Send(connId, pkt))
		{
			LOG_INFO(Core, "[ShardRegisterHandler] Register OK (connId={}, shard_id={})", connId, *id);
		}
		else
		{
			LOG_ERROR(Core, "[ShardRegisterHandler] Register: send REGISTER_OK failed (connId={})", connId);
		}
	}

	void ShardRegisterHandler::HandleHeartbeat(uint32_t /*connId*/, const uint8_t* payload, size_t payloadSize)
	{
		using namespace engine::network;

		// Parse le payload (shard_id + current_load) et délègue la mise à jour au registre.
		// L'horodatage du dernier heartbeat est mis à jour dans ShardRegistry::UpdateHeartbeat,
		// ce qui réinitialise le chronomètre d'éviction (EvictStaleHeartbeats).
		// Aucune réponse réseau n'est émise (heartbeat fire-and-forget).
		auto parsed = ParseShardHeartbeatPayload(payload, payloadSize);
		if (!parsed)
		{
			LOG_WARN(Server, "[SREG] HandleHeartbeat: parse failed");
			return;
		}
		m_registry->UpdateHeartbeat(parsed->shard_id, parsed->current_load);
	}
}
