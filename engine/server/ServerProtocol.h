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
		FriendStatusUpdate = 24
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
}
