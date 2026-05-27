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

	/// Parsed SHARD_HEARTBEAT payload: shard_id, current_load, timestamp (M22.3).
	struct ShardHeartbeatPayload
	{
		uint32_t shard_id = 0;
		uint32_t current_load = 0;
		uint64_t timestamp = 0;
	};
	std::optional<ShardHeartbeatPayload> ParseShardHeartbeatPayload(const uint8_t* payload, size_t payloadSize);

	/// Builds SHARD_HEARTBEAT payload (Shard→Master). timestamp: e.g. seconds since epoch or monotonic.
	std::vector<uint8_t> BuildShardHeartbeatPayload(uint32_t shard_id, uint32_t current_load, uint64_t timestamp = 0);

	/// Parsed MASTER_TO_SHARD_ADMIT_CHARACTER payload : (account_id, character_id, character_name).
	/// Émis par le master (CharacterEnterWorldHandler) à destination du shard via la
	/// connexion TCP persistante établie par ShardToMasterClient. Le shard l'utilise pour
	/// admettre (account_id, character_id) dans son AdmittedCharacterRegistry → le Hello
	/// UDP du client (clientNonce=character_id) sera ensuite accepté. TD.5 — `character_name`
	/// vient de la table SQL `characters.name` et permet au shard (en mode no-DB en
	/// particulier) de remplir `ConnectedClient.characterName`, donc la plaque de nom
	/// des avatars distants côté client.
	struct AdmitCharacterPayload
	{
		uint64_t account_id = 0;
		uint64_t character_id = 0;
		std::string character_name;
	};

	/// Parses MASTER_TO_SHARD_ADMIT_CHARACTER payload. Returns nullopt if truncated.
	std::optional<AdmitCharacterPayload> ParseAdmitCharacterPayload(const uint8_t* payload, size_t payloadSize);

	/// Builds MASTER_TO_SHARD_ADMIT_CHARACTER packet (Master→Shard, push, request_id=0).
	/// \param character_name nom du personnage (table SQL characters.name) ; sera tronqué
	///        à 32 caractères côté master avant l'appel (cohérent avec la contrainte SQL).
	std::vector<uint8_t> BuildAdmitCharacterPacket(uint64_t account_id, uint64_t character_id,
		std::string_view character_name);
}
