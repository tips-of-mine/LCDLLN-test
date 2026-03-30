#pragma once

#include "engine/server/ReplicationTypes.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <span>
#include <vector>

namespace engine::server
{
	/// Protocol version accepted by the server skeleton.
	inline constexpr uint16_t kProtocolVersion = 1;

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

		// M35.2 — Vendor shop messages ----------------------------------------

		/// Client requests to open a vendor's shop (M35.2).
		VendorOpen = 44,
		/// Server pushes vendor catalog + prices to the requesting client (M35.2).
		VendorShopSync = 45,
		/// Client requests to buy one item from a vendor (M35.2).
		VendorBuy = 46,
		/// Client requests to sell one item to a vendor (M35.2).
		VendorSell = 47,
		/// Server acknowledges a buy/sell transaction with result + new gold balance (M35.2).
		VendorTransactionResult = 48,

		// M35.3 — Player direct trade messages ----------------------------------------

		/// Client sends /trade <name> to initiate a direct trade (M35.3).
		TradeRequest = 49,
		/// Server pushes an incoming trade request notification to the target (M35.3).
		TradeRequestNotify = 50,
		/// Target accepts the pending trade request (M35.3).
		TradeAccept = 51,
		/// Target declines the pending trade request (M35.3).
		TradeDecline = 52,
		/// Client adds one item stack to its own trade offer (M35.3).
		TradeAddItem = 53,
		/// Client removes one item from its own trade offer (M35.3).
		TradeRemoveItem = 54,
		/// Client sets the gold amount in its own trade offer (M35.3).
		TradeSetGold = 55,
		/// Client locks its offer, entering the review phase (M35.3).
		TradeLock = 56,
		/// Client confirms the final trade after both sides are locked (M35.3).
		TradeConfirm = 57,
		/// Client cancels the trade at any open/reviewing stage (M35.3).
		TradeCancel = 58,
		/// Server pushes the full trade window state to both clients (M35.3).
		TradeWindowSync = 59,
		/// Server sends the final trade outcome to both clients (M35.3).
		TradeResult = 60,

		// M35.4 — Auction house messages ----------------------------------------

		/// Client requests to post a new auction listing (M35.4).
		AHPostListing = 61,
		/// Server sends the result of an auction post attempt (M35.4).
		AHPostListingResult = 62,
		/// Client requests a search of active listings with optional filters (M35.4).
		AHSearchRequest = 63,
		/// Server sends the search result page to the requesting client (M35.4).
		AHSearchResult = 64,
		/// Client places a bid on an active auction listing (M35.4).
		AHBid = 65,
		/// Server sends the bid outcome to the bidder (M35.4).
		AHBidResult = 66,
		/// Client requests an instant buyout of an active listing (M35.4).
		AHBuyout = 67,
		/// Server sends the buyout outcome to the buyer (M35.4).
		AHBuyoutResult = 68,
		/// Client requests the list of its own active auction listings (M35.4).
		AHMyListings = 69,
		/// Server sends the requesting client's active listings (M35.4).
		AHMyListingsResult = 70,
		/// Client requests to cancel one of its own active listings (M35.4).
		AHCancelListing = 71,
		/// Server sends the cancel outcome to the seller (M35.4).
		AHCancelResult = 72,
		/// Server pushes pending AH deliveries (items/gold) to a client on login (M35.4).
		AHDeliverySync = 73
	};

	/// Initial client handshake sent before any other message.
	struct HelloMessage
	{
		uint16_t requestedTickHz = 0;
		uint16_t requestedSnapshotHz = 0;
		uint32_t clientNonce = 0;
	};

	/// Minimal input envelope accepted by the server authoritative loop.
	struct InputMessage
	{
		uint32_t clientId = 0;
		uint32_t inputSequence = 0;
		float positionMetersX = 0.0f;
		float positionMetersZ = 0.0f;
	};

	/// Handshake acknowledgement emitted after a client is accepted.
	struct WelcomeMessage
	{
		uint32_t clientId = 0;
		uint16_t tickHz = 0;
		uint16_t snapshotHz = 0;
	};

	/// Snapshot envelope carrying timing, connection stats and entity state count.
	struct SnapshotMessage
	{
		uint32_t clientId = 0;
		uint32_t serverTick = 0;
		uint16_t connectedClients = 0;
		uint16_t entityCount = 0;
		uint32_t receivedPackets = 0;
		uint32_t sentPackets = 0;
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

	/// Decode an input packet and validate the protocol header.
	bool DecodeInput(std::span<const std::byte> packet, InputMessage& outMessage);

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
	// M35.2 — Vendor shop messages
	// -------------------------------------------------------------------------

	/// Client request to open a vendor shop by vendor id.
	struct VendorOpenMessage
	{
		uint32_t    clientId = 0;
		std::string vendorId; ///< Stable vendor identifier matching vendor_definitions.json.
	};

	/// One item entry replicated inside a VendorShopSync packet.
	struct VendorShopItemEntry
	{
		uint32_t itemId    = 0;
		uint32_t buyPrice  = 0; ///< Gold cost to buy from the vendor.
		uint32_t sellPrice = 0; ///< Gold received when selling back (25% of buyPrice).
		int32_t  stock     = -1; ///< Remaining stock; -1 means infinite.
	};

	/// Server → client snapshot of a vendor's catalog after VendorOpen is validated.
	struct VendorShopSyncMessage
	{
		uint32_t                       clientId = 0;
		std::string                    vendorId;
		std::vector<VendorShopItemEntry> items;
	};

	/// Client request to buy one item stack from a vendor.
	struct VendorBuyMessage
	{
		uint32_t    clientId = 0;
		std::string vendorId;
		uint32_t    itemId   = 0;
		uint32_t    quantity = 1;
	};

	/// Client request to sell one item stack to a vendor.
	struct VendorSellMessage
	{
		uint32_t    clientId = 0;
		std::string vendorId;
		uint32_t    itemId   = 0;
		uint32_t    quantity = 1;
	};

	/// Server acknowledgement of a vendor buy or sell transaction.
	struct VendorTransactionResultMessage
	{
		uint32_t    clientId    = 0;
		uint8_t     success     = 0; ///< 1 on success, 0 on failure.
		uint32_t    newGold     = 0; ///< Player gold balance after the transaction.
		std::string errorReason;     ///< Human-readable reason on failure.
	};

	/// Encode a client vendor-open request packet.
	std::vector<std::byte> EncodeVendorOpen(const VendorOpenMessage& message);

	/// Decode a client vendor-open request packet.
	bool DecodeVendorOpen(std::span<const std::byte> packet, VendorOpenMessage& outMessage);

	/// Encode a server vendor shop sync packet.
	std::vector<std::byte> EncodeVendorShopSync(const VendorShopSyncMessage& message);

	/// Decode a server vendor shop sync packet.
	bool DecodeVendorShopSync(std::span<const std::byte> packet, VendorShopSyncMessage& outMessage);

	/// Encode a client vendor buy request packet.
	std::vector<std::byte> EncodeVendorBuy(const VendorBuyMessage& message);

	/// Decode a client vendor buy request packet.
	bool DecodeVendorBuy(std::span<const std::byte> packet, VendorBuyMessage& outMessage);

	/// Encode a client vendor sell request packet.
	std::vector<std::byte> EncodeVendorSell(const VendorSellMessage& message);

	/// Decode a client vendor sell request packet.
	bool DecodeVendorSell(std::span<const std::byte> packet, VendorSellMessage& outMessage);

	/// Encode a server vendor transaction result packet.
	std::vector<std::byte> EncodeVendorTransactionResult(const VendorTransactionResultMessage& message);

	/// Decode a server vendor transaction result packet.
	bool DecodeVendorTransactionResult(std::span<const std::byte> packet, VendorTransactionResultMessage& outMessage);

	// -------------------------------------------------------------------------
	// M35.3 — Player direct trade messages
	// -------------------------------------------------------------------------

	/// Client initiates a trade with a named player via /trade <name>.
	struct TradeRequestMessage
	{
		uint32_t    clientId   = 0;
		std::string targetName; ///< Display name of the player to trade with (e.g. "P12").
	};

	/// Server notification pushed to the trade target (M35.3).
	struct TradeRequestNotifyMessage
	{
		std::string requesterName; ///< Display name of the player requesting the trade.
	};

	/// Target accepts the pending trade request (M35.3).
	struct TradeAcceptMessage
	{
		uint32_t clientId = 0;
	};

	/// Target declines the pending trade request (M35.3).
	struct TradeDeclineMessage
	{
		uint32_t clientId = 0;
	};

	/// Client adds one item stack to its own trade offer (M35.3).
	struct TradeAddItemMessage
	{
		uint32_t clientId = 0;
		uint32_t itemId   = 0;
		uint32_t quantity = 1;
	};

	/// Client removes one item from its own trade offer (M35.3).
	struct TradeRemoveItemMessage
	{
		uint32_t clientId = 0;
		uint32_t itemId   = 0;
	};

	/// Client sets the gold amount offered in the trade (M35.3).
	struct TradeSetGoldMessage
	{
		uint32_t clientId = 0;
		uint32_t gold     = 0;
	};

	/// Client locks its offer, signalling readiness for review (M35.3).
	struct TradeLockMessage
	{
		uint32_t clientId = 0;
	};

	/// Client confirms the final trade after both sides locked and reviewed (M35.3).
	struct TradeConfirmMessage
	{
		uint32_t clientId = 0;
	};

	/// Client cancels the active trade (M35.3).
	struct TradeCancelMessage
	{
		uint32_t clientId = 0;
	};

	/// Full trade window snapshot pushed by the server to both clients after any change (M35.3).
	struct TradeWindowSyncMessage
	{
		uint32_t clientId = 0;
		uint8_t  tradeState = 0;        ///< Wire value of engine::server::TradeState.
		/// Receiving client's own offer.
		std::vector<ItemStack> myItems;
		uint32_t myGold   = 0;
		bool     myLocked = false;
		/// Other side's offer.
		std::string            otherPlayerName;
		std::vector<ItemStack> otherItems;
		uint32_t otherGold   = 0;
		bool     otherLocked = false;
		/// Ticks remaining in 5-second review phase; 0 outside the review state.
		uint32_t reviewTicksRemaining = 0;
	};

	/// Server trade outcome sent to both clients after completion or failure (M35.3).
	struct TradeResultMessage
	{
		uint32_t    clientId    = 0;
		uint8_t     success     = 0;   ///< 1 on success, 0 on failure.
		std::string errorReason;       ///< Human-readable reason on failure.
	};

	/// Encode a client trade request packet.
	std::vector<std::byte> EncodeTradeRequest(const TradeRequestMessage& message);

	/// Decode a client trade request packet.
	bool DecodeTradeRequest(std::span<const std::byte> packet, TradeRequestMessage& outMessage);

	/// Encode a server trade request notification packet.
	std::vector<std::byte> EncodeTradeRequestNotify(const TradeRequestNotifyMessage& message);

	/// Decode a server trade request notification packet.
	bool DecodeTradeRequestNotify(std::span<const std::byte> packet, TradeRequestNotifyMessage& outMessage);

	/// Encode a client trade accept packet.
	std::vector<std::byte> EncodeTradeAccept(const TradeAcceptMessage& message);

	/// Decode a client trade accept packet.
	bool DecodeTradeAccept(std::span<const std::byte> packet, TradeAcceptMessage& outMessage);

	/// Encode a client trade decline packet.
	std::vector<std::byte> EncodeTradeDecline(const TradeDeclineMessage& message);

	/// Decode a client trade decline packet.
	bool DecodeTradeDecline(std::span<const std::byte> packet, TradeDeclineMessage& outMessage);

	/// Encode a client trade add-item packet.
	std::vector<std::byte> EncodeTradeAddItem(const TradeAddItemMessage& message);

	/// Decode a client trade add-item packet.
	bool DecodeTradeAddItem(std::span<const std::byte> packet, TradeAddItemMessage& outMessage);

	/// Encode a client trade remove-item packet.
	std::vector<std::byte> EncodeTradeRemoveItem(const TradeRemoveItemMessage& message);

	/// Decode a client trade remove-item packet.
	bool DecodeTradeRemoveItem(std::span<const std::byte> packet, TradeRemoveItemMessage& outMessage);

	/// Encode a client trade set-gold packet.
	std::vector<std::byte> EncodeTradeSetGold(const TradeSetGoldMessage& message);

	/// Decode a client trade set-gold packet.
	bool DecodeTradeSetGold(std::span<const std::byte> packet, TradeSetGoldMessage& outMessage);

	/// Encode a client trade lock packet.
	std::vector<std::byte> EncodeTradeLock(const TradeLockMessage& message);

	/// Decode a client trade lock packet.
	bool DecodeTradeLock(std::span<const std::byte> packet, TradeLockMessage& outMessage);

	/// Encode a client trade confirm packet.
	std::vector<std::byte> EncodeTradeConfirm(const TradeConfirmMessage& message);

	/// Decode a client trade confirm packet.
	bool DecodeTradeConfirm(std::span<const std::byte> packet, TradeConfirmMessage& outMessage);

	/// Encode a client trade cancel packet.
	std::vector<std::byte> EncodeTradeCancel(const TradeCancelMessage& message);

	/// Decode a client trade cancel packet.
	bool DecodeTradeCancel(std::span<const std::byte> packet, TradeCancelMessage& outMessage);

	/// Encode a server trade window sync packet pushed to both clients after any change.
	std::vector<std::byte> EncodeTradeWindowSync(const TradeWindowSyncMessage& message);

	/// Decode a server trade window sync packet.
	bool DecodeTradeWindowSync(std::span<const std::byte> packet, TradeWindowSyncMessage& outMessage);

	/// Encode a server trade result packet.
	std::vector<std::byte> EncodeTradeResult(const TradeResultMessage& message);

	/// Decode a server trade result packet.
	bool DecodeTradeResult(std::span<const std::byte> packet, TradeResultMessage& outMessage);

	// -------------------------------------------------------------------------
	// M35.4 — Auction house messages
	// -------------------------------------------------------------------------

	/// Auction listing duration choices (wire-stable, hours).
	enum class AHDuration : uint8_t
	{
		Hours12 = 12,
		Hours24 = 24,
		Hours48 = 48
	};

	/// Sort order applied to AH search results (wire-stable).
	enum class AHSortOrder : uint8_t
	{
		PriceAsc  = 0, ///< Current bid / buyout ascending.
		PriceDesc = 1, ///< Current bid / buyout descending.
		TimeAsc   = 2, ///< Soonest expiry first.
		TimeDesc  = 3  ///< Latest expiry first.
	};

	/// Client request to post a new auction listing (M35.4).
	struct AHPostListingMessage
	{
		uint32_t   clientId     = 0;
		uint32_t   itemId       = 0;
		uint32_t   itemQuantity = 1;
		uint64_t   startBid     = 0;   ///< Minimum starting bid (gold).
		uint64_t   buyout       = 0;   ///< 0 = no buyout price.
		AHDuration duration     = AHDuration::Hours24;
	};

	/// Server result of a post-listing attempt (M35.4).
	struct AHPostListingResultMessage
	{
		uint32_t    clientId  = 0;
		uint8_t     success   = 0;    ///< 1 on success, 0 on failure.
		uint64_t    listingId = 0;    ///< Assigned listing id on success; 0 on failure.
		std::string errorReason;      ///< Human-readable reason on failure.
	};

	/// Client search request with optional filters and sort (M35.4).
	struct AHSearchRequestMessage
	{
		uint32_t    clientId    = 0;
		uint32_t    itemId      = 0;    ///< 0 = any item.
		uint64_t    maxPrice    = 0;    ///< 0 = no upper price limit.
		uint32_t    pageIndex   = 0;    ///< 0-based page index.
		AHSortOrder sortOrder   = AHSortOrder::PriceAsc;
	};

	/// One listing entry inside an AHSearchResult packet (M35.4).
	struct AHListingEntry
	{
		uint64_t listingId    = 0;
		uint32_t sellerItemId = 0;
		uint32_t itemQuantity = 1;
		uint64_t startBid     = 0;
		uint64_t buyout       = 0;    ///< 0 = no buyout.
		uint64_t currentBid   = 0;
		uint32_t expiresInSec = 0;    ///< Seconds until auction expires (clamped to 0).
		uint8_t  hasBid       = 0;    ///< 1 when at least one bid exists.
	};

	/// Server search result page pushed to the requesting client (M35.4).
	struct AHSearchResultMessage
	{
		uint32_t                   clientId    = 0;
		uint32_t                   totalCount  = 0; ///< Total matching listings (all pages).
		uint32_t                   pageIndex   = 0;
		std::vector<AHListingEntry> listings;
	};

	/// Client bid request on one active listing (M35.4).
	struct AHBidMessage
	{
		uint32_t clientId   = 0;
		uint64_t listingId  = 0;
		uint64_t bidAmount  = 0; ///< Must be > current_bid and >= current_bid * 1.05.
	};

	/// Server bid outcome pushed to the bidder (M35.4).
	struct AHBidResultMessage
	{
		uint32_t    clientId    = 0;
		uint64_t    listingId   = 0;
		uint8_t     success     = 0;  ///< 1 on success.
		uint64_t    newBid      = 0;  ///< Accepted bid amount on success.
		std::string errorReason;      ///< Human-readable reason on failure.
	};

	/// Client buyout request for one active listing (M35.4).
	struct AHBuyoutMessage
	{
		uint32_t clientId  = 0;
		uint64_t listingId = 0;
	};

	/// Server buyout outcome pushed to the buyer (M35.4).
	struct AHBuyoutResultMessage
	{
		uint32_t    clientId    = 0;
		uint64_t    listingId   = 0;
		uint8_t     success     = 0;  ///< 1 on success.
		std::string errorReason;      ///< Human-readable reason on failure.
	};

	/// Client request to list its own active auctions (M35.4).
	struct AHMyListingsMessage
	{
		uint32_t clientId = 0;
	};

	/// Server response to AHMyListings (M35.4).
	struct AHMyListingsResultMessage
	{
		uint32_t                    clientId = 0;
		std::vector<AHListingEntry> listings;
	};

	/// Client request to cancel one of its own active listings (M35.4).
	struct AHCancelListingMessage
	{
		uint32_t clientId  = 0;
		uint64_t listingId = 0;
	};

	/// Server cancel outcome pushed to the seller (M35.4).
	struct AHCancelResultMessage
	{
		uint32_t    clientId    = 0;
		uint64_t    listingId   = 0;
		uint8_t     success     = 0;
		std::string errorReason;
	};

	/// One pending item/gold delivery pushed to a client on login (M35.4).
	struct AHDeliveryEntry
	{
		uint64_t deliveryId   = 0;
		uint64_t goldAmount   = 0;  ///< Gold credited; 0 when item-only.
		uint32_t itemId       = 0;  ///< Item delivered; 0 when gold-only.
		uint32_t itemQuantity = 0;
		std::string reason;         ///< "sold", "outbid", "expired_no_bid", "cancelled"
	};

	/// Server pushes all pending AH deliveries to the client on login (M35.4).
	struct AHDeliverySyncMessage
	{
		uint32_t                     clientId = 0;
		std::vector<AHDeliveryEntry> deliveries;
	};

	// M35.4 — encode / decode declarations ------------------------------------

	/// Encode a client AH post-listing request packet.
	std::vector<std::byte> EncodeAHPostListing(const AHPostListingMessage& message);

	/// Decode a client AH post-listing request packet.
	bool DecodeAHPostListing(std::span<const std::byte> packet, AHPostListingMessage& outMessage);

	/// Encode a server AH post-listing result packet.
	std::vector<std::byte> EncodeAHPostListingResult(const AHPostListingResultMessage& message);

	/// Decode a server AH post-listing result packet.
	bool DecodeAHPostListingResult(std::span<const std::byte> packet, AHPostListingResultMessage& outMessage);

	/// Encode a client AH search request packet.
	std::vector<std::byte> EncodeAHSearchRequest(const AHSearchRequestMessage& message);

	/// Decode a client AH search request packet.
	bool DecodeAHSearchRequest(std::span<const std::byte> packet, AHSearchRequestMessage& outMessage);

	/// Encode a server AH search result packet.
	std::vector<std::byte> EncodeAHSearchResult(const AHSearchResultMessage& message);

	/// Decode a server AH search result packet.
	bool DecodeAHSearchResult(std::span<const std::byte> packet, AHSearchResultMessage& outMessage);

	/// Encode a client AH bid request packet.
	std::vector<std::byte> EncodeAHBid(const AHBidMessage& message);

	/// Decode a client AH bid request packet.
	bool DecodeAHBid(std::span<const std::byte> packet, AHBidMessage& outMessage);

	/// Encode a server AH bid result packet.
	std::vector<std::byte> EncodeAHBidResult(const AHBidResultMessage& message);

	/// Decode a server AH bid result packet.
	bool DecodeAHBidResult(std::span<const std::byte> packet, AHBidResultMessage& outMessage);

	/// Encode a client AH buyout request packet.
	std::vector<std::byte> EncodeAHBuyout(const AHBuyoutMessage& message);

	/// Decode a client AH buyout request packet.
	bool DecodeAHBuyout(std::span<const std::byte> packet, AHBuyoutMessage& outMessage);

	/// Encode a server AH buyout result packet.
	std::vector<std::byte> EncodeAHBuyoutResult(const AHBuyoutResultMessage& message);

	/// Decode a server AH buyout result packet.
	bool DecodeAHBuyoutResult(std::span<const std::byte> packet, AHBuyoutResultMessage& outMessage);

	/// Encode a client AH my-listings request packet.
	std::vector<std::byte> EncodeAHMyListings(const AHMyListingsMessage& message);

	/// Decode a client AH my-listings request packet.
	bool DecodeAHMyListings(std::span<const std::byte> packet, AHMyListingsMessage& outMessage);

	/// Encode a server AH my-listings result packet.
	std::vector<std::byte> EncodeAHMyListingsResult(const AHMyListingsResultMessage& message);

	/// Decode a server AH my-listings result packet.
	bool DecodeAHMyListingsResult(std::span<const std::byte> packet, AHMyListingsResultMessage& outMessage);

	/// Encode a client AH cancel-listing request packet.
	std::vector<std::byte> EncodeAHCancelListing(const AHCancelListingMessage& message);

	/// Decode a client AH cancel-listing request packet.
	bool DecodeAHCancelListing(std::span<const std::byte> packet, AHCancelListingMessage& outMessage);

	/// Encode a server AH cancel result packet.
	std::vector<std::byte> EncodeAHCancelResult(const AHCancelResultMessage& message);

	/// Decode a server AH cancel result packet.
	bool DecodeAHCancelResult(std::span<const std::byte> packet, AHCancelResultMessage& outMessage);

	/// Encode a server AH delivery sync packet pushed to a client on login.
	std::vector<std::byte> EncodeAHDeliverySync(const AHDeliverySyncMessage& message);

	/// Decode a server AH delivery sync packet.
	bool DecodeAHDeliverySync(std::span<const std::byte> packet, AHDeliverySyncMessage& outMessage);
}
