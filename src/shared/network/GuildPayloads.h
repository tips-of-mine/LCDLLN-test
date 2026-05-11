#pragma once
// CMANGOS.21 (Phase 5.21 step 3+4 Guilds) — Wire payloads pour les opcodes
// Guild (164-172). 4 paires Request/Response + 1 push notification :
//   - List         (164/165) : liste des guildes (summary).
//   - Members      (166/167) : liste des membres d'une guilde.
//   - Permissions  (168/169) : matrice mask par rang.
//   - Bank         (170/171) : contenu bank tab 0.
//   - MotdUpdateNotification (172 push) : changement MOTD.
//
// Le master tient en memoire un registry de guildes (V1 : 2 guildes hardcodees
// "Les Gardiens" et "L'Ombre" avec membres + bank + permissions WoW defaults).
// Subscriptions et registry sont reinitialises au reboot. Acceptable V1.
//
// Format wire : ByteReader/ByteWriter little-endian. Strings via
// WriteString/ReadString (uint16 length + UTF-8 bytes), arrays via
// WriteArrayCount/ReadArrayCount (uint16 count). Bool serialise en uint8 (0/1).

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace engine::network
{
	// =========================================================================
	// Codes d'erreur — wire-level pour Guild.
	// =========================================================================

	/// Code d'erreur generique pour les opcodes Guild. 0 = OK.
	enum class GuildErrorCode : uint8_t
	{
		Ok            = 0,
		Unauthorized  = 1, ///< Pas de session valide cote master.
		UnknownGuild  = 2, ///< guildId inconnu du registry master.
		NoPermission  = 3, ///< Le rang du joueur ne couvre pas l'op (e.g. BankView).
	};

	// =========================================================================
	// Sous-structs partages.
	// =========================================================================

	/// Resume d'une guilde pour le wire (List response).
	/// Wire format :
	///   uint32 guildId
	///   string name
	///   string motd
	///   uint32 memberCount
	///   string leaderName
	struct GuildSummary
	{
		uint32_t    guildId     = 0;
		std::string name;
		std::string motd;
		uint32_t    memberCount = 0;
		std::string leaderName;
	};

	/// Membre d'une guilde pour le wire (Members response).
	/// Wire format :
	///   string accountName
	///   uint8  rankId
	///   string rankName
	///   uint8  online (0 ou 1)
	struct GuildMember
	{
		std::string accountName;
		uint8_t     rankId   = 0;
		std::string rankName;
		bool        online   = false;
	};

	/// Permissions d'un rang pour le wire (Permissions response).
	/// Wire format :
	///   uint8  rankId
	///   string rankName
	///   uint32 mask
	struct GuildRankPerms
	{
		uint8_t     rankId = 0;
		std::string rankName;
		uint32_t    mask   = 0;
	};

	/// Item de la bank d'une guilde (V1 : tab 0 only).
	/// Wire format :
	///   uint32 slotIndex
	///   string itemName
	///   uint32 count
	struct GuildBankItem
	{
		uint32_t    slotIndex = 0;
		std::string itemName;
		uint32_t    count     = 0;
	};

	// =========================================================================
	// GUILD_LIST — Client to Master : liste des guildes.
	// =========================================================================

	/// Wire format : (vide). L'account est derive de la session cote master.
	struct GuildListRequestPayload
	{
		// (vide)
	};

	/// Wire format :
	///   uint8  error                     (cf. GuildErrorCode)
	///   uint16 guildCount                (si error == 0)
	///   <count> GuildSummary
	struct GuildListResponsePayload
	{
		uint8_t                   error = 0;
		std::vector<GuildSummary> guilds;
	};

	// =========================================================================
	// GUILD_MEMBERS — Client to Master : liste des membres d'une guilde.
	// =========================================================================

	/// Wire format :
	///   uint32 guildId
	struct GuildMembersRequestPayload
	{
		uint32_t guildId = 0;
	};

	/// Wire format :
	///   uint8  error
	///   uint16 memberCount               (si error == 0)
	///   <count> GuildMember
	struct GuildMembersResponsePayload
	{
		uint8_t                  error = 0;
		std::vector<GuildMember> members;
	};

	// =========================================================================
	// GUILD_PERMISSIONS — Client to Master : matrice par rang.
	// =========================================================================

	/// Wire format :
	///   uint32 guildId
	struct GuildPermissionsRequestPayload
	{
		uint32_t guildId = 0;
	};

	/// Wire format :
	///   uint8  error
	///   uint16 rankCount                 (si error == 0)
	///   <count> GuildRankPerms
	struct GuildPermissionsResponsePayload
	{
		uint8_t                     error = 0;
		std::vector<GuildRankPerms> ranks;
	};

	// =========================================================================
	// GUILD_BANK — Client to Master : contenu bank tab 0.
	// =========================================================================

	/// Wire format :
	///   uint32 guildId
	///   uint8  tabIndex (V1 = 0)
	struct GuildBankRequestPayload
	{
		uint32_t guildId  = 0;
		uint8_t  tabIndex = 0;
	};

	/// Wire format :
	///   uint8  error
	///   uint16 itemCount                 (si error == 0)
	///   <count> GuildBankItem
	struct GuildBankResponsePayload
	{
		uint8_t                    error = 0;
		std::vector<GuildBankItem> items;
	};

	// =========================================================================
	// GUILD_MOTD_UPDATE_NOTIFICATION — Master to Client (push, requestId=0).
	// Changement de MOTD pour une guilde donnee.
	// =========================================================================

	/// Wire format :
	///   uint32 guildId
	///   string newMotd
	struct GuildMotdUpdateNotificationPayload
	{
		uint32_t    guildId = 0;
		std::string newMotd;
	};

	// -------------------------------------------------------------------------
	// Parse / Build — Requests
	// -------------------------------------------------------------------------

	std::optional<GuildListRequestPayload>          ParseGuildListRequestPayload         (const uint8_t* payload, size_t payloadSize);
	std::optional<GuildMembersRequestPayload>       ParseGuildMembersRequestPayload      (const uint8_t* payload, size_t payloadSize);
	std::optional<GuildPermissionsRequestPayload>   ParseGuildPermissionsRequestPayload  (const uint8_t* payload, size_t payloadSize);
	std::optional<GuildBankRequestPayload>          ParseGuildBankRequestPayload         (const uint8_t* payload, size_t payloadSize);

	std::vector<uint8_t> BuildGuildListRequestPayload         ();
	std::vector<uint8_t> BuildGuildMembersRequestPayload      (uint32_t guildId);
	std::vector<uint8_t> BuildGuildPermissionsRequestPayload  (uint32_t guildId);
	std::vector<uint8_t> BuildGuildBankRequestPayload         (uint32_t guildId, uint8_t tabIndex);

	// -------------------------------------------------------------------------
	// Parse / Build — Responses & Notifications (payload-only)
	// -------------------------------------------------------------------------

	std::optional<GuildListResponsePayload>                 ParseGuildListResponsePayload               (const uint8_t* payload, size_t payloadSize);
	std::optional<GuildMembersResponsePayload>              ParseGuildMembersResponsePayload            (const uint8_t* payload, size_t payloadSize);
	std::optional<GuildPermissionsResponsePayload>          ParseGuildPermissionsResponsePayload        (const uint8_t* payload, size_t payloadSize);
	std::optional<GuildBankResponsePayload>                 ParseGuildBankResponsePayload               (const uint8_t* payload, size_t payloadSize);
	std::optional<GuildMotdUpdateNotificationPayload>       ParseGuildMotdUpdateNotificationPayload     (const uint8_t* payload, size_t payloadSize);

	std::vector<uint8_t> BuildGuildListResponsePayload              (uint8_t error, const std::vector<GuildSummary>& guilds);
	std::vector<uint8_t> BuildGuildMembersResponsePayload           (uint8_t error, const std::vector<GuildMember>& members);
	std::vector<uint8_t> BuildGuildPermissionsResponsePayload       (uint8_t error, const std::vector<GuildRankPerms>& ranks);
	std::vector<uint8_t> BuildGuildBankResponsePayload              (uint8_t error, const std::vector<GuildBankItem>& items);
	std::vector<uint8_t> BuildGuildMotdUpdateNotificationPayload    (uint32_t guildId, const std::string& newMotd);

	// -------------------------------------------------------------------------
	// Build full packets (header + payload). Utilise cote handler serveur.
	// -------------------------------------------------------------------------

	std::vector<uint8_t> BuildGuildListResponsePacket              (uint8_t error, const std::vector<GuildSummary>& guilds,
	                                                                uint32_t requestId, uint64_t sessionIdHeader);
	std::vector<uint8_t> BuildGuildMembersResponsePacket           (uint8_t error, const std::vector<GuildMember>& members,
	                                                                uint32_t requestId, uint64_t sessionIdHeader);
	std::vector<uint8_t> BuildGuildPermissionsResponsePacket       (uint8_t error, const std::vector<GuildRankPerms>& ranks,
	                                                                uint32_t requestId, uint64_t sessionIdHeader);
	std::vector<uint8_t> BuildGuildBankResponsePacket              (uint8_t error, const std::vector<GuildBankItem>& items,
	                                                                uint32_t requestId, uint64_t sessionIdHeader);

	/// Push asynchrone (request_id=0). Aucun client request en correspondance.
	std::vector<uint8_t> BuildGuildMotdUpdateNotificationPacket    (uint32_t guildId, const std::string& newMotd,
	                                                                uint64_t sessionIdHeader);
}
