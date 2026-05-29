#pragma once

#include "src/shared/network/ReplicationTypes.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <span>
#include <vector>

namespace engine::server
{
	/// Protocol version accepted by the server skeleton.
	/// Phase 3.7.5 — bump 1 → 2 : `HelloMessage::clientNonce` est passé de \c uint32 à \c uint64
	/// pour transporter un \c character_id complet (BIGINT UNSIGNED) sans tronquer.
	/// TC.1 — bump 2 → 3 : `InputMessage` gagne `positionMetersY` + `yawRadians` (le client
	/// envoie désormais son altitude et son orientation, plus seulement X/Z).
	/// TD.4 — bump 3 → 4 : `SnapshotEntity` gagne `playerClientId` (uint32) — sert au client
	/// à afficher une plaque de nom "P<clientId>" au-dessus des avatars distants. Vaut 0 pour
	/// les mobs / loot bags.
	/// TG.1 — bump 4 → 5 : `SnapshotMessage` gagne `chunkIndex` + `chunkCount` (uint16 × 2)
	/// pour autoriser le chunking d'un snapshot AoI dépassant le budget MTU UDP (~1200 o).
	/// `chunkCount = 1` (mono-paquet) reste le cas dominant ; `chunkCount > 1` exige une
	/// réassemblage côté client (cf. UIModelBinding::ApplySnapshot).
	/// TD.5/TD.6 — bump 5 → 6 → 7 : `SnapshotEntity` gagne `characterName` puis `gender`
	/// (chaînes préfixées u16) pour la plaque de nom et la sélection du mesh distant.
	/// TD.8 — bump 7 → 8 : `InputMessage` gagne `animationState` (1 octet, client→shard) et
	/// `SnapshotEntity` gagne `animationState` (1 octet, shard→clients) — propage l'état
	/// d'animation (emote/roulade/run/sprint/saut/…) pour que les autres joueurs voient
	/// les bonnes animations au lieu d'un Idle/Walk dérivé de la vélocité.
	/// Wire-breaking : client + shard doivent se déployer ensemble.
	inline constexpr uint16_t kProtocolVersion = 8;

	/// Message kinds exchanged by the server skeleton.
	enum class MessageKind : uint16_t
	{
		Hello = 1,
		Welcome = 2,
		Input = 3,
		Snapshot = 4,
		Spawn = 5,
		Despawn = 6,
		ZoneChange = 7,
		AttackRequest = 8,
		CombatEvent = 9,
		PickupRequest = 10,
		InventoryDelta = 11,
		TalkRequest = 12,
		QuestDelta = 13,
		EventState = 14,
		/// Client-authored chat line routed by the authoritative server (M29.1).
		ChatSend = 15,
		/// Server relay of one chat line to interested clients (M29.1).
		ChatRelay = 16,
		/// Server relay of one emote gesture to nearby clients (M29.3).
		EmoteRelay = 17,
		/// Client requests to add a friend by name (M32.1).
		FriendRequest = 18,
		/// Server notifies target player of an incoming friend request (M32.1).
		FriendRequestNotify = 19,
		/// Client accepts a pending friend request by requester name (M32.1).
		FriendAccept = 20,
		/// Client declines a pending friend request by requester name (M32.1).
		FriendDecline = 21,
		/// Client removes an accepted friend by name (M32.1).
		FriendRemove = 22,
		/// Server sends the full friends list to a client on login (M32.1).
		FriendListSync = 23,
		/// Server notifies a client of a friend's presence status change (M32.1).
		FriendStatusUpdate = 24,

		// M32.2 — Party system messages ----------------------------------------

		/// Client sends /invite <name> to the server (M32.2).
		PartyInvite = 25,
		/// Server pushes an incoming invite notification to the target client (M32.2).
		PartyInviteNotify = 26,
		/// Invitee accepts the pending invite (M32.2).
		PartyAccept = 27,
		/// Invitee declines the pending invite (M32.2).
		PartyDecline = 28,
		/// Leader kicks a member from the party via /pkick (M32.2).
		PartyKick = 29,
		/// Server broadcasts full party state to all members after any change (M32.2).
		PartyUpdate = 30,
		/// Leader changes the party loot mode via /loot (M32.2).
		PartyLootMode = 31,
		/// Client voluntarily leaves their party via /leave (M32.2).
		PartyLeave = 32,

		// M32.3 — Guild system messages ----------------------------------------

		/// Client sends /guild create <name> to the server (M32.3).
		GuildCreate = 33,
		/// Server result of a guild creation attempt (M32.3).
		GuildCreateResult = 34,
		/// Client sends /ginvite <name> to the server (M32.3).
		GuildInvite = 35,
		/// Server pushes an incoming guild invite notification to the target (M32.3).
		GuildInviteNotify = 36,
		/// Target accepts a pending guild invite (M32.3).
		GuildInviteAccept = 37,
		/// Target declines a pending guild invite (M32.3).
		GuildInviteDecline = 38,
		/// Leader kicks a member from the guild via /gkick (M32.3).
		GuildKick = 39,
		/// Leader/Officer promotes a member via /gpromote (M32.3).
		GuildPromote = 40,
		/// Server broadcasts full guild roster to all members after any change (M32.3).
		GuildRosterSync = 41,
		/// Client or Officer updates the guild MOTD (M32.3).
		GuildMotdUpdate = 42,

		// M35.1 — Multi-currency wallet ----------------------------------------

		/// Server pushes current wallet balances to the owning client.
		WalletUpdate = 43,

		// M35.2 — Vendor shop ---------------------------------------------------

		/// Server opens the shop panel for one client.
		ShopOpen = 44,
		/// Client buys items from the active vendor.
		ShopBuyRequest = 45,
		/// Client sells items back to the vendor (25% buy price server-side).
		ShopSellRequest = 46,

		// M35.4 — Auction house ------------------------------------------------

		/// Client queries listings (filters + sort).
		AuctionBrowseRequest = 47,
		/// Server returns a browse snapshot for one client.
		AuctionBrowseResult = 48,
		/// Client posts item stack with start bid, optional buyout, duration (12/24/48 h).
		AuctionListItemRequest = 49,
		AuctionBidRequest = 50,
		AuctionBuyoutRequest = 51,

		// M35.3 — Direct player-to-player trade --------------------------------

		/// Client → server: /trade <name> initiates a trade request (M35.3).
		TradeRequest = 52,
		/// Server → target: incoming trade request notification (M35.3).
		TradeRequestNotify = 53,
		/// Client → server: accept a pending incoming trade request (M35.3).
		TradeAccept = 54,
		/// Client → server: decline a pending incoming trade request (M35.3).
		TradeDecline = 55,
		/// Client → server: add one item stack to own trade slot (M35.3).
		TradeAddItem = 56,
		/// Client → server: set offered gold amount (replaces previous value) (M35.3).
		TradeSetGold = 57,
		/// Client → server: lock own side — enter review phase (M35.3).
		TradeLock = 58,
		/// Client → server: final confirm after both sides locked (M35.3).
		TradeConfirm = 59,
		/// Client → server: cancel the ongoing trade session (M35.3).
		TradeCancel = 60,
		/// Server → both clients: current trade window state snapshot (M35.3).
		TradeWindowUpdate = 61,
		/// Server → both clients: trade completed successfully (M35.3).
		TradeComplete = 62,
		/// Server → both clients: trade was cancelled (M35.3).
		TradeCancelled = 63,

		// M36.1 — Gathering / harvesting resource nodes ------------------

		/// Client → server: request to start harvesting a resource node (M36.1).
		HarvestRequest = 64,
		/// Server → client: harvest cast bar started (M36.1).
		HarvestStart = 65,
		/// Server → client: harvest completed, items granted (M36.1).
		HarvestComplete = 66,
		/// Client → server: player explicitly cancels an ongoing harvest (M36.1).
		HarvestCancelRequest = 67,
		/// Server → client: harvest was cancelled (movement, combat, server-forced) (M36.1).
		HarvestCancelled = 68,

		// M36.2 — Crafting / profession skill system --------------------

		/// Client → server: request to learn a profession (M36.2).
		LearnProfessionRequest = 69,
		/// Server → client: full profession snapshot for the owning client (M36.2).
		ProfessionUpdate = 70,
		/// Client → server: request the recipe list for a profession (M36.2).
		CraftRecipeListRequest = 71,
		/// Server → client: list of recipes the player can see (M36.2).
		CraftRecipeListResult = 72,
		/// Client → server: start crafting a recipe (M36.2).
		CraftRequest = 73,
		/// Server → client: crafting cast bar started (M36.2).
		CraftStart = 74,
		/// Server → client: crafting completed, output items granted (M36.2).
		CraftComplete = 75,
		/// Client → server: cancel the current crafting session (M36.2).
		CraftCancelRequest = 76,
		/// Server → client: crafting session was cancelled (M36.2).
		CraftCancelled = 77,
		/// Client → server: départ propre du joueur (fermeture / retour menu). Permet au
		/// shard d'évincer immédiatement l'entité au lieu d'attendre le timeout d'inactivité
		/// (sinon l'avatar du joueur parti reste un « fantôme » visible des autres). Ajout
		/// rétro-compatible : un shard qui ne connaît pas cet opcode l'ignore (fallback timeout).
		Goodbye = 78
	};

	/// Initial client handshake sent before any other message.
	/// Phase 3.7.5 — `clientNonce` élargi à \c uint64 pour porter le `character_id` (BIGINT
	/// UNSIGNED) tel quel. Le shard interprète cette valeur comme `tentativeCharacterKey`
	/// (cf. ServerApp::HandleHello). Auparavant tronqué côté client à `& 0xFFFFFFFF`.
	struct HelloMessage
	{
		uint16_t requestedTickHz = 0;
		uint16_t requestedSnapshotHz = 0;
		uint64_t clientNonce = 0;
	};

	/// Minimal input envelope accepted by the server authoritative loop.
	struct InputMessage
	{
		uint32_t clientId = 0;
		uint32_t inputSequence = 0;
		float positionMetersX = 0.0f;
		float positionMetersY = 0.0f; ///< TC.1 : altitude (terrain accidenté).
		float positionMetersZ = 0.0f;
		float yawRadians = 0.0f;      ///< TC.1 : orientation envoyée par le client.
		/// TD.8 : état d'animation courant de l'avatar local (valeur d'AvatarAnimState).
		/// Le shard le stocke et le réémet dans le SnapshotEntity pour que les autres
		/// joueurs voient les emotes/roulades/etc. Payload Input 24 → 25 octets (v7→v8).
		uint8_t animationState = 0;
	};

	/// Handshake acknowledgement emitted after a client is accepted.
	struct WelcomeMessage
	{
		uint32_t clientId = 0;
		uint16_t tickHz = 0;
		uint16_t snapshotHz = 0;
	};

	/// Départ propre du joueur (client → shard). Permet l'éviction immédiate de l'entité.
	struct GoodbyeMessage
	{
		uint32_t clientId = 0;
	};

	/// Snapshot envelope carrying timing, connection stats and entity state count.
	/// TG.1 — `chunkIndex` / `chunkCount` permettent au serveur de découper un snapshot AoI
	/// trop gros pour un seul datagramme (~1200 o MTU). Valeurs par défaut 0/1 = snapshot
	/// mono-paquet (cas dominant). `chunkCount > 1` : le client doit accumuler les
	/// `chunkCount` chunks (même `serverTick`) avant de commiter le snapshot global ;
	/// `entityCount` ne référence alors que les entités de CE chunk.
	struct SnapshotMessage
	{
		uint32_t clientId = 0;
		uint32_t serverTick = 0;
		uint16_t connectedClients = 0;
		uint16_t entityCount = 0;
		uint32_t receivedPackets = 0;
		uint32_t sentPackets = 0;
		uint16_t chunkIndex = 0;
		uint16_t chunkCount = 1;
	};

	/// Server-authoritative zone assignment sent when a transition volume is validated.
	struct ZoneChangeMessage
	{
		uint32_t zoneId = 0;
		float spawnPositionX = 0.0f;
		float spawnPositionY = 0.0f;
		float spawnPositionZ = 0.0f;
	};

	/// Client request asking the authoritative server to attack one target entity.
	struct AttackRequestMessage
	{
		uint32_t clientId = 0;
		EntityId targetEntityId = 0;
	};

	/// Authoritative combat result broadcast to interested clients.
	struct CombatEventMessage
	{
		EntityId attackerEntityId = 0;
		EntityId targetEntityId = 0;
		uint32_t damage = 0;
		uint32_t targetCurrentHealth = 0;
		uint32_t targetMaxHealth = 0;
		uint32_t targetStateFlags = 0;
	};

	/// Client request asking the authoritative server to pick up one loot bag entity.
	struct PickupRequestMessage
	{
		uint32_t clientId = 0;
		EntityId lootBagEntityId = 0;
	};

	/// Inventory delta emitted after a successful authoritative pickup.
	struct InventoryDeltaMessage
	{
		uint32_t clientId = 0;
	};

	/// Client request asking the authoritative server to validate one quest talk target.
	struct TalkRequestMessage
	{
		uint32_t clientId = 0;
		std::string targetId;
	};

	/// One step payload attached to a quest delta message.
	struct QuestDeltaStep
	{
		uint8_t stepType = 0;
		std::string targetId;
		uint32_t currentCount = 0;
		uint32_t requiredCount = 0;
	};

	/// Quest journal delta emitted after one quest state change.
	struct QuestDeltaMessage
	{
		uint32_t clientId = 0;
		uint8_t status = 0;
		std::string questId;
		uint32_t rewardExperience = 0;
		uint32_t rewardGold = 0;
		std::vector<QuestDeltaStep> steps;
		std::vector<ItemStack> rewardItems;
	};

	/// Client chat send request (parsed prefixes applied client-side; server validates + routes).
	struct ChatSendRequestMessage
	{
		uint32_t clientId = 0;
		uint8_t channel = 0;
		EntityId whisperTargetEntityId = 0;
		std::string text;
	};

	/// One chat line replicated from server to clients.
	struct ChatRelayMessage
	{
		uint8_t channel = 0;
		EntityId senderEntityId = 0;
		uint64_t timestampUnixMs = 0;
		std::string senderDisplay;
		std::string text;
	};

	/// One emote playback event replicated from server to nearby clients (M29.3).
	struct EmoteRelayMessage
	{
		EntityId actorEntityId = 0;
		uint8_t emoteId = 0;
		/// Bit 0: loop posture (e.g. sit). Remaining bits reserved.
		uint8_t flags = 0;
		uint32_t serverTick = 0;
	};

	/// Dynamic event state message emitted after one event state or phase change.
	struct EventStateMessage
	{
		uint32_t zoneId = 0;
		uint8_t status = 0;
		uint16_t phaseIndex = 0;
		uint16_t phaseCount = 0;
		uint32_t progressCurrent = 0;
		uint32_t progressRequired = 0;
		std::string eventId;
		std::string notificationText;
		uint32_t rewardExperience = 0;
		uint32_t rewardGold = 0;
		std::vector<ItemStack> rewardItems;
	};

	/// Decode a hello packet and validate the protocol header.
	bool DecodeHello(std::span<const std::byte> packet, HelloMessage& outMessage);

	/// Encode initial client handshake (UDP gameplay client, M35.2).
	std::vector<std::byte> EncodeHello(const HelloMessage& message);

	/// Decode an input packet and validate the protocol header.
	bool DecodeInput(std::span<const std::byte> packet, InputMessage& outMessage);

	/// TC.1 — encode un INPUT (client→shard) : symétrique de DecodeInput. 24 octets de payload
	/// (clientId, inputSequence, posX, posY, posZ, yaw).
	std::vector<std::byte> EncodeInput(const InputMessage& message);

	/// Encode/decode un GOODBYE (client→shard) : départ propre, payload 4 octets (clientId).
	std::vector<std::byte> EncodeGoodbye(const GoodbyeMessage& message);
	bool DecodeGoodbye(std::span<const std::byte> packet, GoodbyeMessage& outMessage);

	/// Read only the packet kind after validating the shared protocol header.
	bool PeekMessageKind(std::span<const std::byte> packet, MessageKind& outKind);

	/// Encode a welcome packet with the protocol header.
	std::vector<std::byte> EncodeWelcome(const WelcomeMessage& message);

	/// Decode a welcome packet and validate the protocol header.
	bool DecodeWelcome(std::span<const std::byte> packet, WelcomeMessage& outMessage);

	/// Encode a snapshot packet with the protocol header.
	std::vector<std::byte> EncodeSnapshot(const SnapshotMessage& message, std::span<const SnapshotEntity> entities);

	/// Decode a snapshot packet and reuse the provided entity buffer for the payload entities.
	bool DecodeSnapshot(std::span<const std::byte> packet, SnapshotMessage& outMessage, std::vector<SnapshotEntity>& outEntities);

	/// Encode a spawn packet with the protocol header.
	std::vector<std::byte> EncodeSpawn(const SpawnEntity& entity);

	/// Encode a despawn packet with the protocol header.
	std::vector<std::byte> EncodeDespawn(const DespawnEntity& entity);

	/// Decode a zone change packet and validate the protocol header.
	bool DecodeZoneChange(std::span<const std::byte> packet, ZoneChangeMessage& outMessage);

	/// Encode a zone change packet with the protocol header.
	std::vector<std::byte> EncodeZoneChange(const ZoneChangeMessage& message);

	/// Decode an attack request packet and validate the protocol header.
	bool DecodeAttackRequest(std::span<const std::byte> packet, AttackRequestMessage& outMessage);

	/// Encode a combat event packet with the protocol header.
	std::vector<std::byte> EncodeCombatEvent(const CombatEventMessage& message);

	/// Decode a combat event packet and validate the protocol header.
	bool DecodeCombatEvent(std::span<const std::byte> packet, CombatEventMessage& outMessage);

	/// Decode a pickup request packet and validate the protocol header.
	bool DecodePickupRequest(std::span<const std::byte> packet, PickupRequestMessage& outMessage);

	/// Encode an inventory delta packet with the protocol header.
	std::vector<std::byte> EncodeInventoryDelta(const InventoryDeltaMessage& message, std::span<const ItemStack> items);

	/// Decode an inventory delta packet and reuse the provided item buffer for the payload items.
	bool DecodeInventoryDelta(std::span<const std::byte> packet, InventoryDeltaMessage& outMessage, std::vector<ItemStack>& outItems);

	/// Decode a talk request packet and validate the protocol header.
	bool DecodeTalkRequest(std::span<const std::byte> packet, TalkRequestMessage& outMessage);

	/// Encode a talk request packet (client → server).
	std::vector<std::byte> EncodeTalkRequest(const TalkRequestMessage& message);

	/// Encode a quest delta packet with the protocol header.
	std::vector<std::byte> EncodeQuestDelta(const QuestDeltaMessage& message);

	/// Decode a quest delta packet and validate the protocol header.
	bool DecodeQuestDelta(std::span<const std::byte> packet, QuestDeltaMessage& outMessage);

	/// Encode a dynamic event state packet with the protocol header.
	std::vector<std::byte> EncodeEventState(const EventStateMessage& message);

	/// Decode a dynamic event state packet and validate the protocol header.
	bool DecodeEventState(std::span<const std::byte> packet, EventStateMessage& outMessage);

	/// Encode one chat send request packet with the protocol header.
	std::vector<std::byte> EncodeChatSend(const ChatSendRequestMessage& message);

	/// Decode a chat send request packet and validate the protocol header.
	bool DecodeChatSend(std::span<const std::byte> packet, ChatSendRequestMessage& outMessage);

	/// Encode one chat relay packet with the protocol header.
	std::vector<std::byte> EncodeChatRelay(const ChatRelayMessage& message);

	/// Convenience: encode a server-authored broadcast on the Server channel (wire=7).
	/// senderEntityId is always 0 and senderDisplay is fixed to "[Serveur]".
	std::vector<std::byte> EncodeServerNotify(const std::string& text, uint64_t timestampUnixMs);

	/// Decode a chat relay packet and validate the protocol header.
	bool DecodeChatRelay(std::span<const std::byte> packet, ChatRelayMessage& outMessage);

	/// Encode one emote relay packet with the protocol header.
	std::vector<std::byte> EncodeEmoteRelay(const EmoteRelayMessage& message);

	/// Decode an emote relay packet and validate the protocol header.
	bool DecodeEmoteRelay(std::span<const std::byte> packet, EmoteRelayMessage& outMessage);

	// -------------------------------------------------------------------------
	// M32.1 — Friend system messages
	// -------------------------------------------------------------------------

	/// Presence status values used by the friend system (wire-stable).
	enum class PresenceStatus : uint8_t
	{
		Offline = 0,
		Online  = 1,
		Away    = 2,
		Busy    = 3
	};

	/// Client request to add a friend by display name (/friend add <name>).
	struct FriendRequestMessage
	{
		uint32_t    clientId = 0;
		std::string targetName;
	};

	/// Server notification pushed to the target player of an incoming request.
	struct FriendRequestNotifyMessage
	{
		std::string requesterName;
	};

	/// Client acceptance of a pending friend request (/friend accept <name>).
	struct FriendAcceptMessage
	{
		uint32_t    clientId = 0;
		std::string requesterName;
	};

	/// Client decline of a pending friend request (/friend decline <name>).
	struct FriendDeclineMessage
	{
		uint32_t    clientId = 0;
		std::string requesterName;
	};

	/// Client removal of an accepted friend (/friend remove <name>).
	struct FriendRemoveMessage
	{
		uint32_t    clientId = 0;
		std::string friendName;
	};

	/// One entry in the friends list sent on login.
	struct FriendListEntry
	{
		std::string    name;
		PresenceStatus presenceStatus = PresenceStatus::Offline;
		/// True when this entry is a pending inbound request (awaiting local player acceptance).
		bool           isPendingInbound = false;
	};

	/// Server-sent full friends list delivered once after successful login.
	struct FriendListSyncMessage
	{
		std::vector<FriendListEntry> friends;
	};

	/// Server notification of one friend's presence change (login, logout, status change).
	struct FriendStatusUpdateMessage
	{
		std::string    friendName;
		PresenceStatus presenceStatus = PresenceStatus::Offline;
	};

	/// Encode a client friend request packet.
	std::vector<std::byte> EncodeFriendRequest(const FriendRequestMessage& message);

	/// Decode a client friend request packet.
	bool DecodeFriendRequest(std::span<const std::byte> packet, FriendRequestMessage& outMessage);

	/// Encode a server friend request notification packet.
	std::vector<std::byte> EncodeFriendRequestNotify(const FriendRequestNotifyMessage& message);

	/// Decode a server friend request notification packet.
	bool DecodeFriendRequestNotify(std::span<const std::byte> packet, FriendRequestNotifyMessage& outMessage);

	/// Encode a client friend accept packet.
	std::vector<std::byte> EncodeFriendAccept(const FriendAcceptMessage& message);

	/// Decode a client friend accept packet.
	bool DecodeFriendAccept(std::span<const std::byte> packet, FriendAcceptMessage& outMessage);

	/// Encode a client friend decline packet.
	std::vector<std::byte> EncodeFriendDecline(const FriendDeclineMessage& message);

	/// Decode a client friend decline packet.
	bool DecodeFriendDecline(std::span<const std::byte> packet, FriendDeclineMessage& outMessage);

	/// Encode a client friend remove packet.
	std::vector<std::byte> EncodeFriendRemove(const FriendRemoveMessage& message);

	/// Decode a client friend remove packet.
	bool DecodeFriendRemove(std::span<const std::byte> packet, FriendRemoveMessage& outMessage);

	/// Encode a server friend list sync packet (sent on login).
	std::vector<std::byte> EncodeFriendListSync(const FriendListSyncMessage& message);

	/// Decode a server friend list sync packet.
	bool DecodeFriendListSync(std::span<const std::byte> packet, FriendListSyncMessage& outMessage);

	/// Encode a server friend status update packet.
	std::vector<std::byte> EncodeFriendStatusUpdate(const FriendStatusUpdateMessage& message);

	/// Decode a server friend status update packet.
	bool DecodeFriendStatusUpdate(std::span<const std::byte> packet, FriendStatusUpdateMessage& outMessage);

	// -------------------------------------------------------------------------
	// M32.2 — Party system messages
	// -------------------------------------------------------------------------

	/// Wire value for LootMode (must stay in sync with engine::server::LootMode).
	enum class WireLootMode : uint8_t
	{
		FreeForAll   = 0,
		RoundRobin   = 1,
		MasterLooter = 2,
		NeedGreed    = 3
	};

	/// One party member entry carried inside a PartyUpdate packet.
	struct PartyMemberEntry
	{
		uint32_t    clientId      = 0;
		uint32_t    currentHealth = 0;
		uint32_t    maxHealth     = 0;
		uint32_t    currentMana   = 0;
		uint32_t    maxMana       = 0;
		std::string displayName;
	};

	/// Client /invite <name> request (M32.2).
	struct PartyInviteMessage
	{
		uint32_t    clientId   = 0;
		std::string targetName; ///< Display name of the player to invite (e.g. "P12").
	};

	/// Server notification pushed to the invite target (M32.2).
	struct PartyInviteNotifyMessage
	{
		std::string inviterName;
	};

	/// Client acceptance of a pending party invite (M32.2).
	struct PartyAcceptMessage
	{
		uint32_t clientId = 0;
	};

	/// Client decline of a pending party invite (M32.2).
	struct PartyDeclineMessage
	{
		uint32_t clientId = 0;
	};

	/// Leader request to kick a member from the party (M32.2).
	struct PartyKickMessage
	{
		uint32_t    clientId   = 0; ///< Leader's clientId.
		std::string targetName;     ///< Display name of the member to kick.
	};

	/// Full party state broadcast to all members after any change (M32.2).
	struct PartyUpdateMessage
	{
		uint32_t partyId  = 0;
		uint32_t leaderId = 0;
		WireLootMode lootMode = WireLootMode::FreeForAll;
		std::vector<PartyMemberEntry> members;
	};

	/// Leader request to change the party loot mode (M32.2).
	struct PartyLootModeMessage
	{
		uint32_t     clientId = 0;
		WireLootMode lootMode = WireLootMode::FreeForAll;
	};

	/// Client voluntary leave request (M32.2).
	struct PartyLeaveMessage
	{
		uint32_t clientId = 0;
	};

	/// Encode a client party invite request packet.
	std::vector<std::byte> EncodePartyInvite(const PartyInviteMessage& message);

	/// Decode a client party invite request packet.
	bool DecodePartyInvite(std::span<const std::byte> packet, PartyInviteMessage& outMessage);

	/// Encode a server party invite notification packet.
	std::vector<std::byte> EncodePartyInviteNotify(const PartyInviteNotifyMessage& message);

	/// Decode a server party invite notification packet.
	bool DecodePartyInviteNotify(std::span<const std::byte> packet, PartyInviteNotifyMessage& outMessage);

	/// Encode a client party accept packet.
	std::vector<std::byte> EncodePartyAccept(const PartyAcceptMessage& message);

	/// Decode a client party accept packet.
	bool DecodePartyAccept(std::span<const std::byte> packet, PartyAcceptMessage& outMessage);

	/// Encode a client party decline packet.
	std::vector<std::byte> EncodePartyDecline(const PartyDeclineMessage& message);

	/// Decode a client party decline packet.
	bool DecodePartyDecline(std::span<const std::byte> packet, PartyDeclineMessage& outMessage);

	/// Encode a leader party kick request packet.
	std::vector<std::byte> EncodePartyKick(const PartyKickMessage& message);

	/// Decode a leader party kick request packet.
	bool DecodePartyKick(std::span<const std::byte> packet, PartyKickMessage& outMessage);

	/// Encode a full party state update packet (server → all party members).
	std::vector<std::byte> EncodePartyUpdate(const PartyUpdateMessage& message);

	/// Decode a full party state update packet.
	bool DecodePartyUpdate(std::span<const std::byte> packet, PartyUpdateMessage& outMessage);

	/// Encode a leader loot-mode change request packet.
	std::vector<std::byte> EncodePartyLootMode(const PartyLootModeMessage& message);

	/// Decode a leader loot-mode change request packet.
	bool DecodePartyLootMode(std::span<const std::byte> packet, PartyLootModeMessage& outMessage);

	/// Encode a client party leave packet.
	std::vector<std::byte> EncodePartyLeave(const PartyLeaveMessage& message);

	/// Decode a client party leave packet.
	bool DecodePartyLeave(std::span<const std::byte> packet, PartyLeaveMessage& outMessage);

	// -------------------------------------------------------------------------
	// M32.3 — Guild system messages
	// -------------------------------------------------------------------------

	/// Client request to create a new guild via /guild create <name> (M32.3).
	struct GuildCreateMessage
	{
		uint32_t    clientId  = 0;
		std::string guildName; ///< Desired guild name, 3-20 chars.
	};

	/// Server result of a guild creation attempt (M32.3).
	struct GuildCreateResultMessage
	{
		uint8_t     success  = 0; ///< 1 = created, 0 = failed.
		uint64_t    guildId  = 0; ///< New guild id on success; 0 on failure.
		std::string guildName;
		std::string errorReason; ///< Human-readable reason on failure.
	};

	/// Client /ginvite <name> request (M32.3).
	struct GuildInviteMessage
	{
		uint32_t    clientId   = 0;
		std::string targetName; ///< Display name of the player to invite.
	};

	/// Server notification pushed to the invite target (M32.3).
	struct GuildInviteNotifyMessage
	{
		std::string inviterName;
		std::string guildName;
	};

	/// Target accepts a pending guild invite (M32.3).
	struct GuildInviteAcceptMessage
	{
		uint32_t clientId = 0;
	};

	/// Target declines a pending guild invite (M32.3).
	struct GuildInviteDeclineMessage
	{
		uint32_t clientId = 0;
	};

	/// Leader request to kick a member from the guild via /gkick (M32.3).
	struct GuildKickMessage
	{
		uint32_t    clientId   = 0; ///< Kicker's clientId.
		std::string targetName; ///< Display name of the member to kick.
	};

	/// Officer/Leader request to promote a member via /gpromote (M32.3).
	struct GuildPromoteMessage
	{
		uint32_t    clientId   = 0; ///< Promoter's clientId.
		std::string targetName; ///< Display name of the member to promote.
	};

	/// One entry in the guild roster sync packet (M32.3).
	struct GuildRosterEntry
	{
		uint64_t    playerId = 0;
		uint8_t     rankId   = 3; ///< 0=GM, 1=Officer, 2=Member, 3=Recruit.
		uint8_t     online   = 0; ///< 1 when the member is currently online.
		std::string playerName;
		std::string rankName;
	};

	/// Full guild roster broadcast to all members after any change (M32.3).
	struct GuildRosterSyncMessage
	{
		uint64_t                   guildId  = 0;
		std::string                guildName;
		std::string                motd;
		std::vector<GuildRosterEntry> members;
	};

	/// Client/Officer MOTD update request (M32.3).
	struct GuildMotdUpdateMessage
	{
		uint32_t    clientId = 0;
		std::string motd;
	};

	// -------------------------------------------------------------------------
	// M35.1 — Wallet replication
	// -------------------------------------------------------------------------

	/// Server → client wallet snapshot (gold, honor, badges, premium).
	struct WalletUpdateMessage
	{
		uint32_t clientId = 0;
		uint32_t gold = 0;
		uint32_t honor = 0;
		uint32_t badges = 0;
		uint32_t premiumCurrency = 0;
	};

	/// Encode a wallet update packet (server authoritative).
	std::vector<std::byte> EncodeWalletUpdate(const WalletUpdateMessage& message);

	/// Decode a wallet update packet.
	bool DecodeWalletUpdate(std::span<const std::byte> packet, WalletUpdateMessage& outMessage);

	// -------------------------------------------------------------------------
	// M35.2 — Vendor shop
	// -------------------------------------------------------------------------

	inline constexpr uint32_t kShopInfiniteStockWire = 0xFFFFFFFFu;
	inline constexpr uint16_t kMaxShopOffersPerPacket = 64;

	/// One row in a ShopOpen grid.
	struct ShopOfferWire
	{
		uint32_t itemId = 0;
		uint32_t buyPrice = 0;
		/// Remaining stock for finite items; \ref kShopInfiniteStockWire when unlimited.
		uint32_t stock = 0;
	};

	/// Server → client shop inventory snapshot.
	struct ShopOpenMessage
	{
		uint32_t vendorId = 0;
		std::string displayName;
		std::vector<ShopOfferWire> offers;
	};

	/// Client → server buy request.
	struct ShopBuyRequestMessage
	{
		uint32_t clientId = 0;
		uint32_t vendorId = 0;
		uint32_t itemId = 0;
		uint32_t quantity = 0;
	};

	/// Client → server sell request.
	struct ShopSellRequestMessage
	{
		uint32_t clientId = 0;
		uint32_t vendorId = 0;
		uint32_t itemId = 0;
		uint32_t quantity = 0;
	};

	std::vector<std::byte> EncodeShopOpen(const ShopOpenMessage& message);
	bool DecodeShopOpen(std::span<const std::byte> packet, ShopOpenMessage& outMessage);

	std::vector<std::byte> EncodeShopBuyRequest(const ShopBuyRequestMessage& message);
	bool DecodeShopBuyRequest(std::span<const std::byte> packet, ShopBuyRequestMessage& outMessage);

	std::vector<std::byte> EncodeShopSellRequest(const ShopSellRequestMessage& message);
	bool DecodeShopSellRequest(std::span<const std::byte> packet, ShopSellRequestMessage& outMessage);

	// -------------------------------------------------------------------------
	// M35.4 — Auction house
	// -------------------------------------------------------------------------

	inline constexpr uint16_t kMaxAuctionBrowseRowsWire = 32;

	/// One listing row in browse results (M35.4).
	struct AuctionListingWireRow
	{
		uint32_t listingId = 0;
		uint32_t itemId = 0;
		uint32_t quantity = 0;
		uint32_t startBid = 0;
		uint32_t buyoutPrice = 0;
		uint32_t currentBid = 0;
		uint32_t expiresAtTick = 0;
	};

	struct AuctionBrowseRequestMessage
	{
		uint32_t clientId = 0;
		uint32_t minPrice = 0;
		uint32_t maxPrice = 0;
		/// 0 = any item.
		uint32_t itemIdFilter = 0;
		/// 0 = price asc, 1 = price desc, 2 = expiry asc.
		uint32_t sortMode = 0;
		uint32_t maxRows = 32;
	};

	struct AuctionBrowseResultMessage
	{
		uint32_t clientId = 0;
		std::vector<AuctionListingWireRow> rows;
	};

	struct AuctionListItemRequestMessage
	{
		uint32_t clientId = 0;
		uint32_t itemId = 0;
		uint32_t quantity = 0;
		uint32_t startBid = 0;
		uint32_t buyoutPrice = 0;
		/// Hours: 12, 24, or 48.
		uint32_t durationHours = 24;
	};

	struct AuctionBidRequestMessage
	{
		uint32_t clientId = 0;
		uint32_t listingId = 0;
		uint32_t bidAmount = 0;
	};

	struct AuctionBuyoutRequestMessage
	{
		uint32_t clientId = 0;
		uint32_t listingId = 0;
	};

	std::vector<std::byte> EncodeAuctionBrowseRequest(const AuctionBrowseRequestMessage& message);
	bool DecodeAuctionBrowseRequest(std::span<const std::byte> packet, AuctionBrowseRequestMessage& outMessage);

	std::vector<std::byte> EncodeAuctionBrowseResult(const AuctionBrowseResultMessage& message);
	bool DecodeAuctionBrowseResult(std::span<const std::byte> packet, AuctionBrowseResultMessage& outMessage);

	std::vector<std::byte> EncodeAuctionListItemRequest(const AuctionListItemRequestMessage& message);
	bool DecodeAuctionListItemRequest(std::span<const std::byte> packet, AuctionListItemRequestMessage& outMessage);

	std::vector<std::byte> EncodeAuctionBidRequest(const AuctionBidRequestMessage& message);
	bool DecodeAuctionBidRequest(std::span<const std::byte> packet, AuctionBidRequestMessage& outMessage);

	std::vector<std::byte> EncodeAuctionBuyoutRequest(const AuctionBuyoutRequestMessage& message);
	bool DecodeAuctionBuyoutRequest(std::span<const std::byte> packet, AuctionBuyoutRequestMessage& outMessage);

	// -------------------------------------------------------------------------
	// M35.3 — Direct player-to-player trade messages
	// -------------------------------------------------------------------------

	/// Maximum number of item stacks offered per side of a trade window (M35.3).
	inline constexpr uint8_t kMaxTradeItemSlots = 8;

	/// Client → server: /trade <name> initiates a trade request (M35.3).
	struct TradeRequestMessage
	{
		uint32_t    clientId = 0;
		std::string targetName; ///< Display name of the player to trade with.
	};

	/// Server → target: incoming trade request notification (M35.3).
	struct TradeRequestNotifyMessage
	{
		std::string initiatorName; ///< Display name of the initiating player.
	};

	/// Client → server: accept a pending incoming trade request (M35.3).
	struct TradeAcceptMessage
	{
		uint32_t clientId = 0;
	};

	/// Client → server: decline a pending incoming trade request (M35.3).
	struct TradeDeclineMessage
	{
		uint32_t clientId = 0;
	};

	/// Client → server: add one item to own trade slot (M35.3).
	struct TradeAddItemMessage
	{
		uint32_t clientId  = 0;
		uint32_t itemId    = 0;
		uint32_t quantity  = 0;
	};

	/// Client → server: set own offered gold amount (M35.3).
	struct TradeSetGoldMessage
	{
		uint32_t clientId   = 0;
		uint32_t goldAmount = 0;
	};

	/// Client → server: lock own side — enter 5 s review phase (M35.3).
	struct TradeLockMessage
	{
		uint32_t clientId = 0;
	};

	/// Client → server: final irreversible confirm (M35.3).
	struct TradeConfirmMessage
	{
		uint32_t clientId = 0;
	};

	/// Client → server: cancel the ongoing trade session (M35.3).
	struct TradeCancelMessage
	{
		uint32_t clientId = 0;
	};

	/// One side of the trade window state replicated to both players (M35.3).
	struct TradeSideWire
	{
		uint32_t                  clientId   = 0;
		uint32_t                  goldAmount = 0;
		uint8_t                   locked     = 0; ///< 1 when this side has pressed Lock.
		uint8_t                   confirmed  = 0; ///< 1 when this side has pressed Confirm.
		std::vector<ItemStack>    items;
	};

	/// Server → both clients: full trade window state snapshot (M35.3).
	struct TradeWindowUpdateMessage
	{
		TradeSideWire self;  ///< The receiving player's side.
		TradeSideWire other; ///< The remote player's side.
		/// Ticks remaining in the review phase (0 when not in review).
		uint32_t reviewTicksRemaining = 0;
	};

	/// Server → both clients: trade completed successfully (M35.3).
	struct TradeCompleteMessage
	{
		uint32_t clientId = 0; ///< Echoed to the receiver for UI confirmation.
	};

	/// Server → both clients: trade was cancelled with a human-readable reason (M35.3).
	struct TradeCancelledMessage
	{
		std::string reason;
	};

	std::vector<std::byte> EncodeTradeRequest(const TradeRequestMessage& message);
	bool DecodeTradeRequest(std::span<const std::byte> packet, TradeRequestMessage& outMessage);

	std::vector<std::byte> EncodeTradeRequestNotify(const TradeRequestNotifyMessage& message);
	bool DecodeTradeRequestNotify(std::span<const std::byte> packet, TradeRequestNotifyMessage& outMessage);

	std::vector<std::byte> EncodeTradeAccept(const TradeAcceptMessage& message);
	bool DecodeTradeAccept(std::span<const std::byte> packet, TradeAcceptMessage& outMessage);

	std::vector<std::byte> EncodeTradeDecline(const TradeDeclineMessage& message);
	bool DecodeTradeDecline(std::span<const std::byte> packet, TradeDeclineMessage& outMessage);

	std::vector<std::byte> EncodeTradeAddItem(const TradeAddItemMessage& message);
	bool DecodeTradeAddItem(std::span<const std::byte> packet, TradeAddItemMessage& outMessage);

	std::vector<std::byte> EncodeTradeSetGold(const TradeSetGoldMessage& message);
	bool DecodeTradeSetGold(std::span<const std::byte> packet, TradeSetGoldMessage& outMessage);

	std::vector<std::byte> EncodeTradeLock(const TradeLockMessage& message);
	bool DecodeTradeLock(std::span<const std::byte> packet, TradeLockMessage& outMessage);

	std::vector<std::byte> EncodeTradeConfirm(const TradeConfirmMessage& message);
	bool DecodeTradeConfirm(std::span<const std::byte> packet, TradeConfirmMessage& outMessage);

	std::vector<std::byte> EncodeTradeCancel(const TradeCancelMessage& message);
	bool DecodeTradeCancel(std::span<const std::byte> packet, TradeCancelMessage& outMessage);

	std::vector<std::byte> EncodeTradeWindowUpdate(const TradeWindowUpdateMessage& message);
	bool DecodeTradeWindowUpdate(std::span<const std::byte> packet, TradeWindowUpdateMessage& outMessage);

	std::vector<std::byte> EncodeTradeComplete(const TradeCompleteMessage& message);
	bool DecodeTradeComplete(std::span<const std::byte> packet, TradeCompleteMessage& outMessage);

	std::vector<std::byte> EncodeTradeCancelled(const TradeCancelledMessage& message);
	bool DecodeTradeCancelled(std::span<const std::byte> packet, TradeCancelledMessage& outMessage);

	// -------------------------------------------------------------------------
	// M36.1 — Gathering / harvesting resource nodes
	// -------------------------------------------------------------------------

	/// Client → server: request to begin harvesting the node with the given id (M36.1).
	struct HarvestRequestMessage
	{
		uint32_t clientId        = 0;
		uint64_t nodeEntityId    = 0; ///< Server-assigned entity id of the resource node.
	};

	/// Server → client: harvest cast bar started; client must show progress bar (M36.1).
	struct HarvestStartMessage
	{
		uint64_t nodeEntityId        = 0;
		uint32_t harvestDurationTicks = 0; ///< Total server ticks until HarvestComplete fires.
	};

	/// Server → client: harvest completed; InventoryDelta follows (M36.1).
	struct HarvestCompleteMessage
	{
		uint64_t nodeEntityId = 0;
	};

	/// Client → server: player explicitly cancels their current harvest (M36.1).
	struct HarvestCancelRequestMessage
	{
		uint32_t clientId = 0;
	};

	/// Reason code for a server-initiated harvest cancellation (M36.1).
	enum class HarvestCancelReason : uint8_t
	{
		PlayerRequested = 0, ///< Player pressed cancel.
		PlayerMoved     = 1, ///< Player moved outside the harvest range.
		PlayerDamaged   = 2, ///< Player received damage.
		NodeGone        = 3, ///< Node was harvested by another player.
	};

	/// Server → client: harvest cancelled — discard cast bar (M36.1).
	struct HarvestCancelledMessage
	{
		uint64_t nodeEntityId = 0;
		HarvestCancelReason reason = HarvestCancelReason::PlayerRequested;
	};

	std::vector<std::byte> EncodeHarvestRequest(const HarvestRequestMessage& message);
	bool DecodeHarvestRequest(std::span<const std::byte> packet, HarvestRequestMessage& outMessage);

	std::vector<std::byte> EncodeHarvestStart(const HarvestStartMessage& message);
	bool DecodeHarvestStart(std::span<const std::byte> packet, HarvestStartMessage& outMessage);

	std::vector<std::byte> EncodeHarvestComplete(const HarvestCompleteMessage& message);
	bool DecodeHarvestComplete(std::span<const std::byte> packet, HarvestCompleteMessage& outMessage);

	std::vector<std::byte> EncodeHarvestCancelRequest(const HarvestCancelRequestMessage& message);
	bool DecodeHarvestCancelRequest(std::span<const std::byte> packet, HarvestCancelRequestMessage& outMessage);

	std::vector<std::byte> EncodeHarvestCancelled(const HarvestCancelledMessage& message);
	bool DecodeHarvestCancelled(std::span<const std::byte> packet, HarvestCancelledMessage& outMessage);

	// -------------------------------------------------------------------------
	// M36.2 — Crafting / profession skill system messages
	// -------------------------------------------------------------------------

	/// Maximum skill level per profession (M36.2).
	inline constexpr uint32_t kMaxProfessionSkillLevel = 300u;
	/// Maximum number of primary professions per character (M36.2).
	inline constexpr uint32_t kMaxPrimaryProfessions = 2u;
	/// Maximum number of recipe entries in one CraftRecipeListResult packet (M36.2).
	inline constexpr uint16_t kMaxCraftRecipeListRows = 64u;

	/// One profession entry replicated to the client (M36.2).
	struct ProfessionWireEntry
	{
		std::string professionKey; ///< E.g. "blacksmithing", "alchemy".
		uint32_t    skillLevel  = 1;
		uint8_t     isPrimary   = 0; ///< 1 when this is a primary profession slot.
	};

	/// Client → server: request to learn a profession by key (M36.2).
	struct LearnProfessionRequestMessage
	{
		uint32_t    clientId       = 0;
		std::string professionKey;
		uint8_t     asPrimary      = 0; ///< 1 when the player wants it as a primary slot.
	};

	/// Server → client: full profession state for the owning client (M36.2).
	struct ProfessionUpdateMessage
	{
		uint32_t clientId = 0;
		std::vector<ProfessionWireEntry> professions;
	};

	/// Client → server: request the recipe list for one profession (M36.2).
	struct CraftRecipeListRequestMessage
	{
		uint32_t    clientId       = 0;
		std::string professionKey;
	};

	/// One recipe summary row sent in CraftRecipeListResult (M36.2).
	struct CraftRecipeWireRow
	{
		std::string recipeId;
		uint32_t    skillRequired  = 0;
		uint32_t    outputItemId   = 0;
		uint32_t    outputQuantity = 1;
	};

	/// Server → client: list of recipes for the requested profession (M36.2).
	struct CraftRecipeListResultMessage
	{
		uint32_t    clientId = 0;
		std::string professionKey;
		std::vector<CraftRecipeWireRow> recipes;
	};

	/// Client → server: start crafting the given recipe (M36.2).
	struct CraftRequestMessage
	{
		uint32_t    clientId  = 0;
		std::string recipeId;
	};

	/// Server → client: crafting cast bar started (M36.2).
	struct CraftStartMessage
	{
		std::string recipeId;
		uint32_t    durationTicks = 0;
	};

	/// Server → client: crafting completed — output items granted via InventoryDelta (M36.2/M36.3).
	struct CraftCompleteMessage
	{
		std::string recipeId;
		uint8_t     skillGained   = 0; ///< 1 when the player gained a skill point this craft.
		uint32_t    newSkillLevel = 0;
		/// M36.3 — quality tier rolled for this craft (0=Normal, 1=Uncommon, 2=Rare, 3=Epic).
		uint8_t     qualityTier  = 0;
	};

	/// Client → server: cancel the current crafting session (M36.2).
	struct CraftCancelRequestMessage
	{
		uint32_t clientId = 0;
	};

	/// Server → client: crafting session was cancelled (M36.2).
	struct CraftCancelledMessage
	{
		std::string recipeId;
	};

	std::vector<std::byte> EncodeLearnProfessionRequest(const LearnProfessionRequestMessage& message);
	bool DecodeLearnProfessionRequest(std::span<const std::byte> packet, LearnProfessionRequestMessage& outMessage);

	std::vector<std::byte> EncodeProfessionUpdate(const ProfessionUpdateMessage& message);
	bool DecodeProfessionUpdate(std::span<const std::byte> packet, ProfessionUpdateMessage& outMessage);

	std::vector<std::byte> EncodeCraftRecipeListRequest(const CraftRecipeListRequestMessage& message);
	bool DecodeCraftRecipeListRequest(std::span<const std::byte> packet, CraftRecipeListRequestMessage& outMessage);

	std::vector<std::byte> EncodeCraftRecipeListResult(const CraftRecipeListResultMessage& message);
	bool DecodeCraftRecipeListResult(std::span<const std::byte> packet, CraftRecipeListResultMessage& outMessage);

	std::vector<std::byte> EncodeCraftRequest(const CraftRequestMessage& message);
	bool DecodeCraftRequest(std::span<const std::byte> packet, CraftRequestMessage& outMessage);

	std::vector<std::byte> EncodeCraftStart(const CraftStartMessage& message);
	bool DecodeCraftStart(std::span<const std::byte> packet, CraftStartMessage& outMessage);

	std::vector<std::byte> EncodeCraftComplete(const CraftCompleteMessage& message);
	bool DecodeCraftComplete(std::span<const std::byte> packet, CraftCompleteMessage& outMessage);

	std::vector<std::byte> EncodeCraftCancelRequest(const CraftCancelRequestMessage& message);
	bool DecodeCraftCancelRequest(std::span<const std::byte> packet, CraftCancelRequestMessage& outMessage);

	std::vector<std::byte> EncodeCraftCancelled(const CraftCancelledMessage& message);
	bool DecodeCraftCancelled(std::span<const std::byte> packet, CraftCancelledMessage& outMessage);
}
