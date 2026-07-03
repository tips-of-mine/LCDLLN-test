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

#include "src/masterd/handlers/shard/ShardRegisterHandler.h"
#include "src/shared/network/NetServer.h"
#include "src/masterd/shards/ShardRegistry.h"
#include "src/masterd/shards/ShardPlayerPresenceCache.h"
#include "src/shared/network/ShardPayloads.h"
#include "src/shared/network/ShardWireAuth.h"
#include "src/shared/network/ProtocolV1Constants.h"
#include "src/shared/core/Log.h"

#include <cstdio>
#include <utility>

namespace engine::server
{
	void ShardRegisterHandler::SetServer(NetServer* server) { m_server = server; }
	void ShardRegisterHandler::SetShardRegistry(ShardRegistry* registry) { m_registry = registry; }
	void ShardRegisterHandler::SetPlayerPresenceCache(ShardPlayerPresenceCache* cache) { m_presenceCache = cache; }
	void ShardRegisterHandler::SetSecret(std::string secret) { m_secret = std::move(secret); }

	void ShardRegisterHandler::HandlePacket(uint32_t connId, uint16_t opcode, uint32_t requestId, uint64_t /*sessionIdHeader*/,
		const uint8_t* payload, size_t payloadSize)
	{
		using namespace engine::network;
		if (!m_server || !m_registry)
		{
			LOG_WARN(Core, "[ShardRegisterHandler] HandlePacket: server or registry not set");
			return;
		}
		if (opcode == kOpcodeShardRegister || opcode == kOpcodeShardHeartbeat)
		{
			// Sécurité (audit F3) : le canal shard↔master est authentifié par un tag HMAC préfixe.
			auto body = UnwrapShardAuth(m_secret, payload, payloadSize);
			if (!body)
			{
				LOG_WARN(Server, "[SREG] paquet shard rejeté : authentification HMAC invalide (opcode={}, connId={})", opcode, connId);
				return;
			}
			if (opcode == kOpcodeShardRegister)
				HandleRegister(connId, requestId, body->first, body->second);
			else
				HandleHeartbeat(connId, body->first, body->second);
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
		auto id = m_registry->RegisterShard(std::move(parsed->name), std::move(parsed->endpoint), std::move(parsed->udp_endpoint), parsed->max_capacity,
			std::move(parsed->region), std::move(parsed->display_name), parsed->game_mode, parsed->ruleset);
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
					// TA.3 : memorise la nouvelle connId du shard (re-register implique souvent
					// une nouvelle connexion TCP) pour les push master->shard (AdmitCharacter).
					m_registry->SetShardConnection(s.shard_id, connId);
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
		// TA.3 : memorise la connId du shard pour les push master->shard (AdmitCharacter).
		m_registry->SetShardConnection(*id, connId);
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

	void ShardRegisterHandler::HandleHeartbeat(uint32_t connId, const uint8_t* payload, size_t payloadSize)
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
		// Sécurité (audit F2) : le shard_id du heartbeat doit correspondre à la connId qui a
		// réalisé le REGISTER (mémorisée par SetShardConnection). Sans cette vérification,
		// n'importe quel pair connecté au Master peut forger un heartbeat pour un shard_id
		// qu'il ne possède pas. GetShardConnection renvoie std::optional<uint32_t> (nullopt
		// si le shard_id est inconnu du registre).
		const auto registered = m_registry->GetShardConnection(parsed->shard_id);
		if (!registered || *registered != connId)
		{
			LOG_WARN(Server, "[SREG] HandleHeartbeat rejeté : connId={} ne correspond pas au shard_id={} (attendu connId={})",
				connId, parsed->shard_id, registered ? *registered : 0u);
			return;
		}
		m_registry->UpdateHeartbeat(parsed->shard_id, parsed->current_load);

		// Présence enrichie (v9) : répercute l'ensemble des joueurs rapportés par ce
		// shard dans le cache (remplace les entrées du shard). Tolérant : un heartbeat
		// legacy sans tableau laisse players vide -> le shard est considéré sans joueur.
		if (m_presenceCache)
			m_presenceCache->Update(parsed->shard_id, parsed->players);
	}
}
