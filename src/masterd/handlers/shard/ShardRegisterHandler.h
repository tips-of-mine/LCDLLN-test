#pragma once

/// @file ShardRegisterHandler.h
/// @brief Gestionnaire d'enregistrement des shards sur le serveur Master.
///
/// Ce composant traite les opcodes SHARD_REGISTER et SHARD_HEARTBEAT envoyés par
/// les processus shard lors de leur démarrage et de leur maintien en vie. Il est
/// instancié exclusivement côté Master ; les shards n'utilisent pas ce handler.
/// Thread-safety : aucune — appelé depuis la boucle réseau mono-thread du Master.

#include <cstddef>
#include <cstdint>
#include <string>

namespace engine::server
{
	class NetServer;
	class ShardRegistry;
	class ShardPlayerPresenceCache;
}

namespace engine::server
{
	/// Traite les opcodes SHARD_REGISTER et SHARD_HEARTBEAT côté Master.
	///
	/// Flux typique :
	///   1. Le shard se connecte au Master et envoie SHARD_REGISTER.
	///   2. HandleRegister valide le payload, appelle ShardRegistry::RegisterShard
	///      et renvoie REGISTER_OK (avec le shard_id assigné) ou REGISTER_ERROR.
	///   3. Si le shard se reconnecte (même nom, déjà connu), HandleRegister retrouve
	///      l'entrée existante et réémet REGISTER_OK avec le même shard_id.
	///   4. Périodiquement, le shard envoie SHARD_HEARTBEAT ; HandleHeartbeat met à
	///      jour la charge et l'horodatage dans ShardRegistry.
	class ShardRegisterHandler
	{
	public:
		ShardRegisterHandler() = default;

		/// Injecte le pointeur vers le serveur réseau (requis avant HandlePacket).
		/// @param server  Instance NetServer utilisée pour envoyer les réponses.
		void SetServer(NetServer* server);

		/// Injecte le registre des shards (requis avant HandlePacket).
		/// @param registry  Instance ShardRegistry maintenant l'état de tous les shards.
		void SetShardRegistry(ShardRegistry* registry);

		/// Injecte le cache de présence enrichie (optionnel). Si câblé, HandleHeartbeat
		/// y répercute le tableau de joueurs du heartbeat v9 (présence web-portal).
		/// @param cache  Instance ShardPlayerPresenceCache. Non possédé. Peut être nul.
		void SetPlayerPresenceCache(ShardPlayerPresenceCache* cache);

		/// Sécurité (audit F3) : secret partagé HMAC-SHA256 utilisé pour vérifier le tag
		/// d'authentification préfixé aux payloads SHARD_REGISTER/SHARD_HEARTBEAT (config
		/// `shard.ticket_hmac_secret`, identique au secret câblé côté shard). Si vide,
		/// UnwrapShardAuth rejette systématiquement (aucun shard ne peut s'enregistrer).
		void SetSecret(std::string secret);

		/// Point d'entrée principal : vérifie le tag HMAC préfixe (audit F3) puis dispatche
		/// vers HandleRegister ou HandleHeartbeat.
		///
		/// Ignore silencieusement tout opcode autre que SHARD_REGISTER et SHARD_HEARTBEAT.
		/// Si m_server ou m_registry est nul, émet un LOG_WARN et retourne immédiatement.
		/// Si le tag d'authentification est absent ou invalide, émet un LOG_WARN et
		/// rejette le paquet sans dispatcher (aucune réponse envoyée).
		/// @param connId          Identifiant de connexion réseau de l'appelant (shard).
		/// @param opcode          Code d'opération du paquet reçu.
		/// @param requestId       Identifiant de requête à reporter dans la réponse.
		/// @param sessionIdHeader Champ session de l'en-tête (ignoré pour ces opcodes).
		/// @param payload         Données brutes du corps du paquet, tag HMAC(32) inclus en tête.
		/// @param payloadSize     Taille en octets de \p payload (tag inclus).
		void HandlePacket(uint32_t connId, uint16_t opcode, uint32_t requestId, uint64_t sessionIdHeader,
			const uint8_t* payload, size_t payloadSize);

	private:
		/// Traite l'opcode SHARD_REGISTER.
		///
		/// Parse le payload (nom, endpoint, capacité maximale, charge actuelle).
		/// En cas de succès : appelle ShardRegistry::RegisterShard et répond REGISTER_OK.
		/// Si le nom est déjà présent (reconnexion) : cherche l'entrée existante, met à jour
		/// le heartbeat et répond REGISTER_OK avec le shard_id existant.
		/// En cas d'échec de parsing ou de doublon non résolu : répond REGISTER_ERROR.
		/// @param connId       Identifiant de connexion du shard.
		/// @param requestId    Identifiant de requête à inclure dans la réponse.
		/// @param payload      Corps du paquet SHARD_REGISTER.
		/// @param payloadSize  Taille en octets de \p payload.
		void HandleRegister(uint32_t connId, uint32_t requestId, const uint8_t* payload, size_t payloadSize);

		/// Traite l'opcode SHARD_HEARTBEAT.
		///
		/// Parse le payload (shard_id, charge actuelle), vérifie (audit F2) que \p connId
		/// correspond bien à la connexion enregistrée pour ce shard_id via
		/// ShardRegistry::GetShardConnection (rejette sinon, LOG_WARN), puis délègue à
		/// ShardRegistry::UpdateHeartbeat pour maintenir l'état Online du shard.
		/// Aucune réponse n'est envoyée (fire-and-forget).
		/// @param connId       Identifiant de connexion réseau de l'appelant ; doit correspondre
		///                     à la connId mémorisée lors du REGISTER pour ce shard_id.
		/// @param payload      Corps du paquet SHARD_HEARTBEAT.
		/// @param payloadSize  Taille en octets de \p payload.
		void HandleHeartbeat(uint32_t connId, const uint8_t* payload, size_t payloadSize);

		/// Serveur réseau utilisé pour émettre les paquets de réponse. Non possédé.
		NetServer* m_server = nullptr;
		/// Registre en mémoire des shards connus. Non possédé.
		ShardRegistry* m_registry = nullptr;
		/// Cache de présence enrichie (optionnel, non possédé). Nul = présence enrichie désactivée.
		ShardPlayerPresenceCache* m_presenceCache = nullptr;
		/// Secret HMAC partagé avec les shards (audit F3). Voir SetSecret.
		std::string m_secret;
	};
}
