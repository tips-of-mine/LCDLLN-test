#pragma once

#include "src/shared/network/ProtocolV1Constants.h"
#include "src/shared/network/ServerMeta.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace engine::network
{
	/// Parsed SHARD_REGISTER payload: name, endpoint, max_capacity, current_load,
	/// build_version, plus présentation publique (display_name + game_mode + ruleset).
	struct ShardRegisterPayload
	{
		std::string name;            ///< Identifiant technique unique (clé de dédup à la reconnexion).
		std::string endpoint;
		std::string udp_endpoint;    ///< TB.1: endpoint UDP gameplay du shard (host:port). Vide si non annoncé.
		uint32_t max_capacity = 0;
		uint32_t current_load = 0;
		std::string build_version;
		std::string display_name;    ///< Nom public affiché au joueur (ne PAS confondre avec name).
		ShardGameMode game_mode = ShardGameMode::PvE;
		ShardRuleset ruleset = ShardRuleset::Cooperative;
		std::string region;          ///< Région annoncée (texte libre, ex. « eu-west »), exposée par l'API /status.
	};

	/// Parses SHARD_REGISTER payload. Returns nullopt if truncated or invalid.
	std::optional<ShardRegisterPayload> ParseShardRegisterPayload(const uint8_t* payload, size_t payloadSize);

	/// Builds SHARD_REGISTER payload (Shard→Master). TB.1: \a udp_endpoint annonce l'endpoint
	/// UDP gameplay du shard (relayé au client via SERVER_LIST).
	std::vector<uint8_t> BuildShardRegisterPayload(std::string_view name, std::string_view endpoint,
		std::string_view udp_endpoint, uint32_t max_capacity, uint32_t current_load, std::string_view build_version,
		std::string_view display_name = {}, ShardGameMode game_mode = ShardGameMode::PvE,
		ShardRuleset ruleset = ShardRuleset::Cooperative, std::string_view region = {});

	/// Parsed SHARD_REGISTER_OK payload: shard_id.
	struct ShardRegisterOkPayload
	{
		uint32_t shard_id = 0;
	};
	std::optional<ShardRegisterOkPayload> ParseShardRegisterOkPayload(const uint8_t* payload, size_t payloadSize);

	/// Builds SHARD_REGISTER_OK packet (Master→Shard). requestId from register request.
	std::vector<uint8_t> BuildShardRegisterOkPacket(uint32_t shard_id, uint32_t requestId);

	/// Error code for SHARD_REGISTER_ERROR (internal, not NetErrorCode).
	enum class ShardRegisterErrorCode : uint32_t
	{
		DuplicateName = 1,
		InvalidPayload = 2,
	};

	/// Builds SHARD_REGISTER_ERROR packet (Master→Shard).
	std::vector<uint8_t> BuildShardRegisterErrorPacket(ShardRegisterErrorCode code, uint32_t requestId);

	/// Présence d'un joueur en jeu remontée par le shard dans le heartbeat enrichi
	/// (protocole v9). Alimente la présence enrichie exposée au web-portal.
	struct ShardPlayerPresence
	{
		uint64_t accountId = 0;
		uint64_t characterId = 0;
		uint32_t level = 0;
		uint32_t zoneId = 0;
	};

	/// Parsed SHARD_HEARTBEAT payload: shard_id, current_load, timestamp (M22.3).
	/// v9 : `players` est rempli si le payload contient le tableau optionnel en queue
	/// (vide pour un heartbeat legacy sans joueurs).
	struct ShardHeartbeatPayload
	{
		uint32_t shard_id = 0;
		uint32_t current_load = 0;
		uint64_t timestamp = 0;
		std::vector<ShardPlayerPresence> players;
	};
	std::optional<ShardHeartbeatPayload> ParseShardHeartbeatPayload(const uint8_t* payload, size_t payloadSize);

	/// Builds SHARD_HEARTBEAT payload (Shard→Master). timestamp: e.g. seconds since epoch or monotonic.
	/// \param players liste optionnelle des joueurs en jeu (présence enrichie v9). Vide = heartbeat legacy.
	std::vector<uint8_t> BuildShardHeartbeatPayload(uint32_t shard_id, uint32_t current_load, uint64_t timestamp = 0,
		const std::vector<ShardPlayerPresence>& players = {});

	/// Parsed MASTER_TO_SHARD_ADMIT_CHARACTER payload : (account_id, character_id, character_name, gender).
	/// Émis par le master (CharacterEnterWorldHandler) à destination du shard via la
	/// connexion TCP persistante établie par ShardToMasterClient. Le shard l'utilise pour
	/// admettre (account_id, character_id) dans son AdmittedCharacterRegistry → le Hello
	/// UDP du client (clientNonce=character_id) sera ensuite accepté. TD.5 — `character_name`
	/// vient de la table SQL `characters.name` et permet au shard (en mode no-DB en
	/// particulier) de remplir `ConnectedClient.characterName`, donc la plaque de nom
	/// des avatars distants côté client. TD.6 — `gender` vient de `characters.gender`
	/// (migration 0067, "male"/"female") et permet au client de choisir le mesh skinné
	/// correct pour les avatars distants.
	struct AdmitCharacterPayload
	{
		uint64_t account_id = 0;
		uint64_t character_id = 0;
		std::string character_name;
		std::string gender;
		/// Roadmap-7 (2026-07-19) — guilde du compte (table guild_members_v2 côté
		/// master, un compte = au plus une guilde). 0 = sans guilde OU master
		/// antérieur à cette extension (champ optionnel en queue, cf. Parse).
		/// Permet au shard (y compris no-DB) de connaître l'appartenance de
		/// guilde des joueurs connectés (ex. partage du buff gâteau, dette #991).
		uint64_t guild_id = 0;
	};

	/// Parses MASTER_TO_SHARD_ADMIT_CHARACTER payload. Returns nullopt if truncated.
	std::optional<AdmitCharacterPayload> ParseAdmitCharacterPayload(const uint8_t* payload, size_t payloadSize);

	/// Builds MASTER_TO_SHARD_ADMIT_CHARACTER packet (Master→Shard, push, request_id=0).
	/// \param character_name nom du personnage (table SQL characters.name) ; sera tronqué
	///        à 32 caractères côté master avant l'appel (cohérent avec la contrainte SQL).
	/// \param gender genre du personnage ("male"/"female", cf. migration 0067).
	/// \param guild_id Roadmap-7 — guilde du compte (0 = sans guilde). Champ ADDITIF en
	///        queue de payload : un shard ancien l'ignore, un master ancien ne l'émet pas.
	std::vector<uint8_t> BuildAdmitCharacterPacket(uint64_t account_id, uint64_t character_id,
		std::string_view character_name, std::string_view gender, uint64_t guild_id = 0);
}
