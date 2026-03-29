#include "engine/server/ServerProtocol.h"

#include "engine/net/ChatSystem.h"

#include <bit>

namespace engine::server
{
	namespace
	{
		/// Wire magic used to reject packets from unrelated clients.
		inline constexpr uint32_t kWireMagic = 0x31525653u;

		/// Fixed packet header size shared by all protocol messages.
		inline constexpr size_t kHeaderSize = 8;

		/// Read one raw byte from a packet buffer.
		uint8_t ReadU8(std::span<const std::byte> bytes, size_t offset)
		{
			return static_cast<uint8_t>(bytes[offset]);
		}

		/// Append one raw byte to a packet buffer.
		void WriteU8(std::vector<std::byte>& outBytes, uint8_t value)
		{
			outBytes.push_back(static_cast<std::byte>(value));
		}

		/// Read a little-endian 16-bit value from a packet buffer.
		uint16_t ReadU16(std::span<const std::byte> bytes, size_t offset)
		{
			return static_cast<uint16_t>(static_cast<uint8_t>(bytes[offset + 0]))
				| (static_cast<uint16_t>(static_cast<uint8_t>(bytes[offset + 1])) << 8);
		}

		/// Read a little-endian 32-bit value from a packet buffer.
		uint32_t ReadU32(std::span<const std::byte> bytes, size_t offset)
		{
			return static_cast<uint32_t>(static_cast<uint8_t>(bytes[offset + 0]))
				| (static_cast<uint32_t>(static_cast<uint8_t>(bytes[offset + 1])) << 8)
				| (static_cast<uint32_t>(static_cast<uint8_t>(bytes[offset + 2])) << 16)
				| (static_cast<uint32_t>(static_cast<uint8_t>(bytes[offset + 3])) << 24);
		}

		/// Read a little-endian 64-bit value from a packet buffer.
		uint64_t ReadU64(std::span<const std::byte> bytes, size_t offset)
		{
			return static_cast<uint64_t>(ReadU32(bytes, offset))
				| (static_cast<uint64_t>(ReadU32(bytes, offset + 4)) << 32);
		}

		/// Read one IEEE-754 float from a packet buffer.
		float ReadF32(std::span<const std::byte> bytes, size_t offset)
		{
			return std::bit_cast<float>(ReadU32(bytes, offset));
		}

		/// Append a little-endian 16-bit value to an output buffer.
		void WriteU16(std::vector<std::byte>& outBytes, uint16_t value)
		{
			outBytes.push_back(static_cast<std::byte>(value & 0xFFu));
			outBytes.push_back(static_cast<std::byte>((value >> 8) & 0xFFu));
		}

		/// Append a little-endian 32-bit value to an output buffer.
		void WriteU32(std::vector<std::byte>& outBytes, uint32_t value)
		{
			outBytes.push_back(static_cast<std::byte>(value & 0xFFu));
			outBytes.push_back(static_cast<std::byte>((value >> 8) & 0xFFu));
			outBytes.push_back(static_cast<std::byte>((value >> 16) & 0xFFu));
			outBytes.push_back(static_cast<std::byte>((value >> 24) & 0xFFu));
		}

		/// Append a little-endian 64-bit value to an output buffer.
		void WriteU64(std::vector<std::byte>& outBytes, uint64_t value)
		{
			WriteU32(outBytes, static_cast<uint32_t>(value & 0xFFFFFFFFull));
			WriteU32(outBytes, static_cast<uint32_t>((value >> 32) & 0xFFFFFFFFull));
		}

		/// Read a sized UTF-8 string from a packet buffer.
		bool ReadSizedString(std::span<const std::byte> bytes, size_t& offset, std::string& outValue)
		{
			if ((offset + 2) > bytes.size())
			{
				return false;
			}

			const uint16_t length = ReadU16(bytes, offset);
			offset += 2;
			if ((offset + length) > bytes.size())
			{
				return false;
			}

			outValue.assign(reinterpret_cast<const char*>(bytes.data() + offset), length);
			offset += length;
			return true;
		}

		/// Append a float to an output buffer using IEEE-754 bit representation.
		void WriteF32(std::vector<std::byte>& outBytes, float value)
		{
			WriteU32(outBytes, std::bit_cast<uint32_t>(value));
		}

		/// Append one sized UTF-8 string to a packet buffer.
		void WriteSizedString(std::vector<std::byte>& outBytes, std::string_view value)
		{
			WriteU16(outBytes, static_cast<uint16_t>(value.size()));
			for (const char ch : value)
			{
				outBytes.push_back(static_cast<std::byte>(static_cast<uint8_t>(ch)));
			}
		}

		/// Append the minimal entity state fields to a packet buffer.
		void WriteEntityState(std::vector<std::byte>& outBytes, const EntityState& state)
		{
			WriteF32(outBytes, state.positionX);
			WriteF32(outBytes, state.positionY);
			WriteF32(outBytes, state.positionZ);
			WriteF32(outBytes, state.yawRadians);
			WriteF32(outBytes, state.velocityX);
			WriteF32(outBytes, state.velocityY);
			WriteF32(outBytes, state.velocityZ);
			WriteU32(outBytes, state.currentHealth);
			WriteU32(outBytes, state.maxHealth);
			WriteU32(outBytes, state.stateFlags);
		}

		/// Read one entity state payload from the packet buffer.
		bool ReadEntityState(std::span<const std::byte> bytes, size_t& offset, EntityState& outState)
		{
			if ((offset + 40) > bytes.size())
			{
				return false;
			}

			outState.positionX = ReadF32(bytes, offset + 0);
			outState.positionY = ReadF32(bytes, offset + 4);
			outState.positionZ = ReadF32(bytes, offset + 8);
			outState.yawRadians = ReadF32(bytes, offset + 12);
			outState.velocityX = ReadF32(bytes, offset + 16);
			outState.velocityY = ReadF32(bytes, offset + 20);
			outState.velocityZ = ReadF32(bytes, offset + 24);
			outState.currentHealth = ReadU32(bytes, offset + 28);
			outState.maxHealth = ReadU32(bytes, offset + 32);
			outState.stateFlags = ReadU32(bytes, offset + 36);
			offset += 40;
			return true;
		}

		/// Validate the common header and return the remaining payload span.
		bool DecodeHeader(std::span<const std::byte> packet, MessageKind expectedKind, std::span<const std::byte>& outPayload)
		{
			if (packet.size() < kHeaderSize)
			{
				return false;
			}

			const uint32_t magic = ReadU32(packet, 0);
			const uint16_t version = ReadU16(packet, 4);
			const MessageKind kind = static_cast<MessageKind>(ReadU16(packet, 6));
			if (magic != kWireMagic || version != kProtocolVersion || kind != expectedKind)
			{
				return false;
			}

			outPayload = packet.subspan(kHeaderSize);
			return true;
		}

		/// Build a packet with the shared protocol header and payload bytes.
		std::vector<std::byte> BeginPacket(MessageKind kind, size_t payloadSize)
		{
			std::vector<std::byte> packet;
			packet.reserve(kHeaderSize + payloadSize);
			WriteU32(packet, kWireMagic);
			WriteU16(packet, kProtocolVersion);
			WriteU16(packet, static_cast<uint16_t>(kind));
			return packet;
		}
	}

	bool DecodeHello(std::span<const std::byte> packet, HelloMessage& outMessage)
	{
		std::span<const std::byte> payload;
		if (!DecodeHeader(packet, MessageKind::Hello, payload) || payload.size() != 8)
		{
			return false;
		}

		outMessage.requestedTickHz = ReadU16(payload, 0);
		outMessage.requestedSnapshotHz = ReadU16(payload, 2);
		outMessage.clientNonce = ReadU32(payload, 4);
		return true;
	}

	bool DecodeInput(std::span<const std::byte> packet, InputMessage& outMessage)
	{
		std::span<const std::byte> payload;
		if (!DecodeHeader(packet, MessageKind::Input, payload) || payload.size() != 16)
		{
			return false;
		}

		outMessage.clientId = ReadU32(payload, 0);
		outMessage.inputSequence = ReadU32(payload, 4);
		outMessage.positionMetersX = std::bit_cast<float>(ReadU32(payload, 8));
		outMessage.positionMetersZ = std::bit_cast<float>(ReadU32(payload, 12));
		return true;
	}

	bool PeekMessageKind(std::span<const std::byte> packet, MessageKind& outKind)
	{
		if (packet.size() < kHeaderSize)
		{
			return false;
		}

		const uint32_t magic = ReadU32(packet, 0);
		const uint16_t version = ReadU16(packet, 4);
		if (magic != kWireMagic || version != kProtocolVersion)
		{
			return false;
		}

		outKind = static_cast<MessageKind>(ReadU16(packet, 6));
		return true;
	}

	bool DecodeWelcome(std::span<const std::byte> packet, WelcomeMessage& outMessage)
	{
		std::span<const std::byte> payload;
		if (!DecodeHeader(packet, MessageKind::Welcome, payload) || payload.size() != 8)
		{
			return false;
		}

		outMessage.clientId = ReadU32(payload, 0);
		outMessage.tickHz = ReadU16(payload, 4);
		outMessage.snapshotHz = ReadU16(payload, 6);
		return true;
	}

	bool DecodeSnapshot(std::span<const std::byte> packet, SnapshotMessage& outMessage, std::vector<SnapshotEntity>& outEntities)
	{
		std::span<const std::byte> payload;
		if (!DecodeHeader(packet, MessageKind::Snapshot, payload) || payload.size() < 20)
		{
			return false;
		}

		outMessage.clientId = ReadU32(payload, 0);
		outMessage.serverTick = ReadU32(payload, 4);
		outMessage.connectedClients = ReadU16(payload, 8);
		outMessage.entityCount = ReadU16(payload, 10);
		outMessage.receivedPackets = ReadU32(payload, 12);
		outMessage.sentPackets = ReadU32(payload, 16);

		const size_t entityCount = static_cast<size_t>(outMessage.entityCount);
		const size_t expectedPayloadSize = 20 + (entityCount * 48);
		if (payload.size() != expectedPayloadSize)
		{
			return false;
		}

		outEntities.clear();
		outEntities.resize(entityCount);
		size_t offset = 20;
		for (size_t index = 0; index < entityCount; ++index)
		{
			SnapshotEntity& entity = outEntities[index];
			entity.entityId = ReadU64(payload, offset);
			offset += 8;
			if (!ReadEntityState(payload, offset, entity.state))
			{
				outEntities.clear();
				return false;
			}
		}

		return true;
	}

	bool DecodeZoneChange(std::span<const std::byte> packet, ZoneChangeMessage& outMessage)
	{
		std::span<const std::byte> payload;
		if (!DecodeHeader(packet, MessageKind::ZoneChange, payload) || payload.size() != 16)
		{
			return false;
		}

		outMessage.zoneId = ReadU32(payload, 0);
		outMessage.spawnPositionX = std::bit_cast<float>(ReadU32(payload, 4));
		outMessage.spawnPositionY = std::bit_cast<float>(ReadU32(payload, 8));
		outMessage.spawnPositionZ = std::bit_cast<float>(ReadU32(payload, 12));
		return true;
	}

	bool DecodeAttackRequest(std::span<const std::byte> packet, AttackRequestMessage& outMessage)
	{
		std::span<const std::byte> payload;
		if (!DecodeHeader(packet, MessageKind::AttackRequest, payload) || payload.size() != 12)
		{
			return false;
		}

		outMessage.clientId = ReadU32(payload, 0);
		outMessage.targetEntityId = ReadU64(payload, 4);
		return true;
	}

	bool DecodePickupRequest(std::span<const std::byte> packet, PickupRequestMessage& outMessage)
	{
		std::span<const std::byte> payload;
		if (!DecodeHeader(packet, MessageKind::PickupRequest, payload) || payload.size() != 12)
		{
			return false;
		}

		outMessage.clientId = ReadU32(payload, 0);
		outMessage.lootBagEntityId = ReadU64(payload, 4);
		return true;
	}

	bool DecodeTalkRequest(std::span<const std::byte> packet, TalkRequestMessage& outMessage)
	{
		std::span<const std::byte> payload;
		if (!DecodeHeader(packet, MessageKind::TalkRequest, payload) || payload.size() < 6)
		{
			return false;
		}

		outMessage.clientId = ReadU32(payload, 0);
		size_t offset = 4;
		if (!ReadSizedString(payload, offset, outMessage.targetId) || offset != payload.size())
		{
			return false;
		}

		return !outMessage.targetId.empty();
	}

	std::vector<std::byte> EncodeWelcome(const WelcomeMessage& message)
	{
		std::vector<std::byte> packet = BeginPacket(MessageKind::Welcome, 8);
		WriteU32(packet, message.clientId);
		WriteU16(packet, message.tickHz);
		WriteU16(packet, message.snapshotHz);
		return packet;
	}

	std::vector<std::byte> EncodeSnapshot(const SnapshotMessage& message, std::span<const SnapshotEntity> entities)
	{
		std::vector<std::byte> packet = BeginPacket(MessageKind::Snapshot, 20 + (entities.size() * 48));
		WriteU32(packet, message.clientId);
		WriteU32(packet, message.serverTick);
		WriteU16(packet, message.connectedClients);
		WriteU16(packet, message.entityCount);
		WriteU32(packet, message.receivedPackets);
		WriteU32(packet, message.sentPackets);
		for (const SnapshotEntity& entity : entities)
		{
			WriteU64(packet, entity.entityId);
			WriteEntityState(packet, entity.state);
		}
		return packet;
	}

	std::vector<std::byte> EncodeSpawn(const SpawnEntity& entity)
	{
		std::vector<std::byte> packet = BeginPacket(MessageKind::Spawn, 52);
		WriteU64(packet, entity.entityId);
		WriteU32(packet, entity.archetypeId);
		WriteEntityState(packet, entity.state);
		return packet;
	}

	std::vector<std::byte> EncodeDespawn(const DespawnEntity& entity)
	{
		std::vector<std::byte> packet = BeginPacket(MessageKind::Despawn, 8);
		WriteU64(packet, entity.entityId);
		return packet;
	}

	std::vector<std::byte> EncodeZoneChange(const ZoneChangeMessage& message)
	{
		std::vector<std::byte> packet = BeginPacket(MessageKind::ZoneChange, 16);
		WriteU32(packet, message.zoneId);
		WriteF32(packet, message.spawnPositionX);
		WriteF32(packet, message.spawnPositionY);
		WriteF32(packet, message.spawnPositionZ);
		return packet;
	}

	std::vector<std::byte> EncodeCombatEvent(const CombatEventMessage& message)
	{
		std::vector<std::byte> packet = BeginPacket(MessageKind::CombatEvent, 32);
		WriteU64(packet, message.attackerEntityId);
		WriteU64(packet, message.targetEntityId);
		WriteU32(packet, message.damage);
		WriteU32(packet, message.targetCurrentHealth);
		WriteU32(packet, message.targetMaxHealth);
		WriteU32(packet, message.targetStateFlags);
		return packet;
	}

	bool DecodeCombatEvent(std::span<const std::byte> packet, CombatEventMessage& outMessage)
	{
		std::span<const std::byte> payload;
		if (!DecodeHeader(packet, MessageKind::CombatEvent, payload) || payload.size() != 32)
		{
			return false;
		}

		outMessage.attackerEntityId = ReadU64(payload, 0);
		outMessage.targetEntityId = ReadU64(payload, 8);
		outMessage.damage = ReadU32(payload, 16);
		outMessage.targetCurrentHealth = ReadU32(payload, 20);
		outMessage.targetMaxHealth = ReadU32(payload, 24);
		outMessage.targetStateFlags = ReadU32(payload, 28);
		return true;
	}

	std::vector<std::byte> EncodeInventoryDelta(const InventoryDeltaMessage& message, std::span<const ItemStack> items)
	{
		std::vector<std::byte> packet = BeginPacket(MessageKind::InventoryDelta, 6 + (items.size() * 8));
		WriteU32(packet, message.clientId);
		WriteU16(packet, static_cast<uint16_t>(items.size()));
		for (const ItemStack& item : items)
		{
			WriteU32(packet, item.itemId);
			WriteU32(packet, item.quantity);
		}
		return packet;
	}

	bool DecodeInventoryDelta(std::span<const std::byte> packet, InventoryDeltaMessage& outMessage, std::vector<ItemStack>& outItems)
	{
		std::span<const std::byte> payload;
		if (!DecodeHeader(packet, MessageKind::InventoryDelta, payload) || payload.size() < 6)
		{
			return false;
		}

		outMessage.clientId = ReadU32(payload, 0);
		const size_t itemCount = static_cast<size_t>(ReadU16(payload, 4));
		const size_t expectedPayloadSize = 6 + (itemCount * 8);
		if (payload.size() != expectedPayloadSize)
		{
			return false;
		}

		outItems.clear();
		outItems.resize(itemCount);
		size_t offset = 6;
		for (size_t index = 0; index < itemCount; ++index)
		{
			ItemStack& item = outItems[index];
			item.itemId = ReadU32(payload, offset + 0);
			item.quantity = ReadU32(payload, offset + 4);
			offset += 8;
		}

		return true;
	}

	std::vector<std::byte> EncodeQuestDelta(const QuestDeltaMessage& message)
	{
		size_t payloadSize = 4 + 1 + 2 + message.questId.size() + 4 + 4 + 1 + 2;
		for (const QuestDeltaStep& step : message.steps)
		{
			payloadSize += 1 + 2 + step.targetId.size() + 4 + 4;
		}
		payloadSize += message.rewardItems.size() * 8;

		std::vector<std::byte> packet = BeginPacket(MessageKind::QuestDelta, payloadSize);
		WriteU32(packet, message.clientId);
		packet.push_back(static_cast<std::byte>(message.status));
		WriteSizedString(packet, message.questId);
		WriteU32(packet, message.rewardExperience);
		WriteU32(packet, message.rewardGold);
		packet.push_back(static_cast<std::byte>(static_cast<uint8_t>(message.steps.size())));
		for (const QuestDeltaStep& step : message.steps)
		{
			packet.push_back(static_cast<std::byte>(step.stepType));
			WriteSizedString(packet, step.targetId);
			WriteU32(packet, step.currentCount);
			WriteU32(packet, step.requiredCount);
		}
		WriteU16(packet, static_cast<uint16_t>(message.rewardItems.size()));
		for (const ItemStack& item : message.rewardItems)
		{
			WriteU32(packet, item.itemId);
			WriteU32(packet, item.quantity);
		}
		return packet;
	}

	bool DecodeQuestDelta(std::span<const std::byte> packet, QuestDeltaMessage& outMessage)
	{
		std::span<const std::byte> payload;
		if (!DecodeHeader(packet, MessageKind::QuestDelta, payload) || payload.size() < 16)
		{
			return false;
		}

		size_t offset = 0;
		outMessage.clientId = ReadU32(payload, offset);
		offset += 4;
		outMessage.status = static_cast<uint8_t>(payload[offset]);
		offset += 1;
		if (!ReadSizedString(payload, offset, outMessage.questId))
		{
			return false;
		}

		if ((offset + 9) > payload.size())
		{
			return false;
		}

		outMessage.rewardExperience = ReadU32(payload, offset);
		offset += 4;
		outMessage.rewardGold = ReadU32(payload, offset);
		offset += 4;

		const size_t stepCount = static_cast<size_t>(static_cast<uint8_t>(payload[offset]));
		offset += 1;
		outMessage.steps.clear();
		outMessage.steps.reserve(stepCount);
		for (size_t index = 0; index < stepCount; ++index)
		{
			if (offset >= payload.size())
			{
				outMessage.steps.clear();
				return false;
			}

			QuestDeltaStep step{};
			step.stepType = static_cast<uint8_t>(payload[offset]);
			offset += 1;
			if (!ReadSizedString(payload, offset, step.targetId) || (offset + 8) > payload.size())
			{
				outMessage.steps.clear();
				return false;
			}

			step.currentCount = ReadU32(payload, offset);
			offset += 4;
			step.requiredCount = ReadU32(payload, offset);
			offset += 4;
			outMessage.steps.push_back(std::move(step));
		}

		if ((offset + 2) > payload.size())
		{
			outMessage.steps.clear();
			return false;
		}

		const size_t rewardItemCount = static_cast<size_t>(ReadU16(payload, offset));
		offset += 2;
		if ((offset + (rewardItemCount * 8)) != payload.size())
		{
			outMessage.steps.clear();
			return false;
		}

		outMessage.rewardItems.clear();
		outMessage.rewardItems.resize(rewardItemCount);
		for (size_t index = 0; index < rewardItemCount; ++index)
		{
			ItemStack& item = outMessage.rewardItems[index];
			item.itemId = ReadU32(payload, offset + 0);
			item.quantity = ReadU32(payload, offset + 4);
			offset += 8;
		}

		return true;
	}

	std::vector<std::byte> EncodeEventState(const EventStateMessage& message)
	{
		size_t payloadSize = 4 + 1 + 2 + 2 + 4 + 4 + 2 + message.eventId.size() + 2 + message.notificationText.size() + 4 + 4 + 2;
		payloadSize += message.rewardItems.size() * 8;

		std::vector<std::byte> packet = BeginPacket(MessageKind::EventState, payloadSize);
		WriteU32(packet, message.zoneId);
		packet.push_back(static_cast<std::byte>(message.status));
		WriteU16(packet, message.phaseIndex);
		WriteU16(packet, message.phaseCount);
		WriteU32(packet, message.progressCurrent);
		WriteU32(packet, message.progressRequired);
		WriteSizedString(packet, message.eventId);
		WriteSizedString(packet, message.notificationText);
		WriteU32(packet, message.rewardExperience);
		WriteU32(packet, message.rewardGold);
		WriteU16(packet, static_cast<uint16_t>(message.rewardItems.size()));
		for (const ItemStack& item : message.rewardItems)
		{
			WriteU32(packet, item.itemId);
			WriteU32(packet, item.quantity);
		}
		return packet;
	}

	bool DecodeEventState(std::span<const std::byte> packet, EventStateMessage& outMessage)
	{
		std::span<const std::byte> payload;
		if (!DecodeHeader(packet, MessageKind::EventState, payload) || payload.size() < 25)
		{
			return false;
		}

		size_t offset = 0;
		outMessage.zoneId = ReadU32(payload, offset);
		offset += 4;
		outMessage.status = static_cast<uint8_t>(payload[offset]);
		offset += 1;
		if ((offset + 12) > payload.size())
		{
			return false;
		}

		outMessage.phaseIndex = ReadU16(payload, offset);
		offset += 2;
		outMessage.phaseCount = ReadU16(payload, offset);
		offset += 2;
		outMessage.progressCurrent = ReadU32(payload, offset);
		offset += 4;
		outMessage.progressRequired = ReadU32(payload, offset);
		offset += 4;
		if (!ReadSizedString(payload, offset, outMessage.eventId)
			|| !ReadSizedString(payload, offset, outMessage.notificationText)
			|| (offset + 10) > payload.size())
		{
			return false;
		}

		outMessage.rewardExperience = ReadU32(payload, offset);
		offset += 4;
		outMessage.rewardGold = ReadU32(payload, offset);
		offset += 4;
		const size_t rewardItemCount = static_cast<size_t>(ReadU16(payload, offset));
		offset += 2;
		if ((offset + (rewardItemCount * 8)) != payload.size())
		{
			return false;
		}

		outMessage.rewardItems.clear();
		outMessage.rewardItems.resize(rewardItemCount);
		for (size_t index = 0; index < rewardItemCount; ++index)
		{
			ItemStack& item = outMessage.rewardItems[index];
			item.itemId = ReadU32(payload, offset + 0);
			item.quantity = ReadU32(payload, offset + 4);
			offset += 8;
		}

		return true;
	}

	inline constexpr uint16_t kMaxChatPayloadUtf8Bytes = 256;
	inline constexpr uint16_t kMaxChatSenderDisplayUtf8Bytes = 48;

	std::vector<std::byte> EncodeChatSend(const ChatSendRequestMessage& message)
	{
		const uint16_t textLength = static_cast<uint16_t>(message.text.size());
		std::vector<std::byte> packet = BeginPacket(MessageKind::ChatSend, 4 + 1 + 8 + 2 + textLength);
		WriteU32(packet, message.clientId);
		WriteU8(packet, message.channel);
		WriteU64(packet, message.whisperTargetEntityId);
		WriteSizedString(packet, message.text);
		return packet;
	}

	bool DecodeChatSend(std::span<const std::byte> packet, ChatSendRequestMessage& outMessage)
	{
		std::span<const std::byte> payload;
		if (!DecodeHeader(packet, MessageKind::ChatSend, payload) || payload.size() < 15)
		{
			return false;
		}

		size_t offset = 0;
		outMessage.clientId = ReadU32(payload, offset);
		offset += 4;
		outMessage.channel = ReadU8(payload, offset);
		offset += 1;
		outMessage.whisperTargetEntityId = ReadU64(payload, offset);
		offset += 8;
		if (!ReadSizedString(payload, offset, outMessage.text))
		{
			return false;
		}

		if (outMessage.text.size() > kMaxChatPayloadUtf8Bytes)
		{
			return false;
		}

		if (offset != payload.size())
		{
			return false;
		}

		return true;
	}

	std::vector<std::byte> EncodeChatRelay(const ChatRelayMessage& message)
	{
		const uint16_t senderLength = static_cast<uint16_t>(message.senderDisplay.size());
		const uint16_t textLength = static_cast<uint16_t>(message.text.size());
		std::vector<std::byte> packet = BeginPacket(MessageKind::ChatRelay, 1 + 8 + 8 + 2 + senderLength + 2 + textLength);
		WriteU8(packet, message.channel);
		WriteU64(packet, message.senderEntityId);
		WriteU64(packet, message.timestampUnixMs);
		WriteSizedString(packet, message.senderDisplay);
		WriteSizedString(packet, message.text);
		return packet;
	}

	std::vector<std::byte> EncodeServerNotify(const std::string& text, uint64_t timestampUnixMs)
	{
		ChatRelayMessage msg{};
		msg.channel = engine::net::ToWire(engine::net::ChatChannel::Server);
		msg.senderEntityId = 0;
		msg.timestampUnixMs = timestampUnixMs;
		msg.senderDisplay = "[Serveur]";
		msg.text = text;
		return EncodeChatRelay(msg);
	}

	bool DecodeChatRelay(std::span<const std::byte> packet, ChatRelayMessage& outMessage)
	{
		std::span<const std::byte> payload;
		if (!DecodeHeader(packet, MessageKind::ChatRelay, payload) || payload.size() < 18)
		{
			return false;
		}

		size_t offset = 0;
		outMessage.channel = ReadU8(payload, offset);
		offset += 1;
		outMessage.senderEntityId = ReadU64(payload, offset);
		offset += 8;
		outMessage.timestampUnixMs = ReadU64(payload, offset);
		offset += 8;
		if (!ReadSizedString(payload, offset, outMessage.senderDisplay))
		{
			return false;
		}

		if (outMessage.senderDisplay.size() > kMaxChatSenderDisplayUtf8Bytes)
		{
			return false;
		}

		if (!ReadSizedString(payload, offset, outMessage.text))
		{
			return false;
		}

		if (outMessage.text.size() > kMaxChatPayloadUtf8Bytes)
		{
			return false;
		}

		if (offset != payload.size())
		{
			return false;
		}

		return true;
	}

	inline constexpr uint8_t kMinEmoteWireId = 1;
	inline constexpr uint8_t kMaxEmoteWireId = 8;

	std::vector<std::byte> EncodeEmoteRelay(const EmoteRelayMessage& message)
	{
		std::vector<std::byte> packet = BeginPacket(MessageKind::EmoteRelay, 14);
		WriteU64(packet, message.actorEntityId);
		WriteU8(packet, message.emoteId);
		WriteU8(packet, message.flags);
		WriteU32(packet, message.serverTick);
		return packet;
	}

	bool DecodeEmoteRelay(std::span<const std::byte> packet, EmoteRelayMessage& outMessage)
	{
		std::span<const std::byte> payload;
		if (!DecodeHeader(packet, MessageKind::EmoteRelay, payload) || payload.size() != 14)
		{
			return false;
		}

		outMessage.actorEntityId = ReadU64(payload, 0);
		outMessage.emoteId = ReadU8(payload, 8);
		outMessage.flags = ReadU8(payload, 9);
		outMessage.serverTick = ReadU32(payload, 10);
		if (outMessage.emoteId < kMinEmoteWireId || outMessage.emoteId > kMaxEmoteWireId)
		{
			return false;
		}

		return true;
	}

	// -------------------------------------------------------------------------
	// M32.1 — Friend system encode / decode
	// -------------------------------------------------------------------------

	/// Maximum byte length accepted for a player name field in friend packets.
	inline constexpr size_t kMaxFriendNameUtf8Bytes = 64u;

	std::vector<std::byte> EncodeFriendRequest(const FriendRequestMessage& message)
	{
		std::vector<std::byte> packet = BeginPacket(MessageKind::FriendRequest, 4 + 2 + message.targetName.size());
		WriteU32(packet, message.clientId);
		WriteSizedString(packet, message.targetName);
		return packet;
	}

	bool DecodeFriendRequest(std::span<const std::byte> packet, FriendRequestMessage& outMessage)
	{
		std::span<const std::byte> payload;
		if (!DecodeHeader(packet, MessageKind::FriendRequest, payload) || payload.size() < 6)
			return false;

		outMessage.clientId = ReadU32(payload, 0);
		size_t offset = 4;
		if (!ReadSizedString(payload, offset, outMessage.targetName))
			return false;
		if (outMessage.targetName.empty() || outMessage.targetName.size() > kMaxFriendNameUtf8Bytes)
			return false;
		if (offset != payload.size())
			return false;
		return true;
	}

	std::vector<std::byte> EncodeFriendRequestNotify(const FriendRequestNotifyMessage& message)
	{
		std::vector<std::byte> packet = BeginPacket(MessageKind::FriendRequestNotify, 2 + message.requesterName.size());
		WriteSizedString(packet, message.requesterName);
		return packet;
	}

	bool DecodeFriendRequestNotify(std::span<const std::byte> packet, FriendRequestNotifyMessage& outMessage)
	{
		std::span<const std::byte> payload;
		if (!DecodeHeader(packet, MessageKind::FriendRequestNotify, payload) || payload.size() < 2)
			return false;

		size_t offset = 0;
		if (!ReadSizedString(payload, offset, outMessage.requesterName))
			return false;
		if (outMessage.requesterName.empty() || outMessage.requesterName.size() > kMaxFriendNameUtf8Bytes)
			return false;
		if (offset != payload.size())
			return false;
		return true;
	}

	std::vector<std::byte> EncodeFriendAccept(const FriendAcceptMessage& message)
	{
		std::vector<std::byte> packet = BeginPacket(MessageKind::FriendAccept, 4 + 2 + message.requesterName.size());
		WriteU32(packet, message.clientId);
		WriteSizedString(packet, message.requesterName);
		return packet;
	}

	bool DecodeFriendAccept(std::span<const std::byte> packet, FriendAcceptMessage& outMessage)
	{
		std::span<const std::byte> payload;
		if (!DecodeHeader(packet, MessageKind::FriendAccept, payload) || payload.size() < 6)
			return false;

		outMessage.clientId = ReadU32(payload, 0);
		size_t offset = 4;
		if (!ReadSizedString(payload, offset, outMessage.requesterName))
			return false;
		if (outMessage.requesterName.empty() || outMessage.requesterName.size() > kMaxFriendNameUtf8Bytes)
			return false;
		if (offset != payload.size())
			return false;
		return true;
	}

	std::vector<std::byte> EncodeFriendDecline(const FriendDeclineMessage& message)
	{
		std::vector<std::byte> packet = BeginPacket(MessageKind::FriendDecline, 4 + 2 + message.requesterName.size());
		WriteU32(packet, message.clientId);
		WriteSizedString(packet, message.requesterName);
		return packet;
	}

	bool DecodeFriendDecline(std::span<const std::byte> packet, FriendDeclineMessage& outMessage)
	{
		std::span<const std::byte> payload;
		if (!DecodeHeader(packet, MessageKind::FriendDecline, payload) || payload.size() < 6)
			return false;

		outMessage.clientId = ReadU32(payload, 0);
		size_t offset = 4;
		if (!ReadSizedString(payload, offset, outMessage.requesterName))
			return false;
		if (outMessage.requesterName.empty() || outMessage.requesterName.size() > kMaxFriendNameUtf8Bytes)
			return false;
		if (offset != payload.size())
			return false;
		return true;
	}

	std::vector<std::byte> EncodeFriendRemove(const FriendRemoveMessage& message)
	{
		std::vector<std::byte> packet = BeginPacket(MessageKind::FriendRemove, 4 + 2 + message.friendName.size());
		WriteU32(packet, message.clientId);
		WriteSizedString(packet, message.friendName);
		return packet;
	}

	bool DecodeFriendRemove(std::span<const std::byte> packet, FriendRemoveMessage& outMessage)
	{
		std::span<const std::byte> payload;
		if (!DecodeHeader(packet, MessageKind::FriendRemove, payload) || payload.size() < 6)
			return false;

		outMessage.clientId = ReadU32(payload, 0);
		size_t offset = 4;
		if (!ReadSizedString(payload, offset, outMessage.friendName))
			return false;
		if (outMessage.friendName.empty() || outMessage.friendName.size() > kMaxFriendNameUtf8Bytes)
			return false;
		if (offset != payload.size())
			return false;
		return true;
	}

	std::vector<std::byte> EncodeFriendListSync(const FriendListSyncMessage& message)
	{
		// Per entry: u16 name length + name bytes + u8 presenceStatus + u8 isPendingInbound
		size_t payloadSize = 2; // u16 entry count
		for (const auto& e : message.friends)
			payloadSize += 2 + e.name.size() + 1 + 1;

		std::vector<std::byte> packet = BeginPacket(MessageKind::FriendListSync, payloadSize);
		WriteU16(packet, static_cast<uint16_t>(message.friends.size()));
		for (const auto& e : message.friends)
		{
			WriteSizedString(packet, e.name);
			WriteU8(packet, static_cast<uint8_t>(e.presenceStatus));
			WriteU8(packet, e.isPendingInbound ? 1u : 0u);
		}
		return packet;
	}

	bool DecodeFriendListSync(std::span<const std::byte> packet, FriendListSyncMessage& outMessage)
	{
		std::span<const std::byte> payload;
		if (!DecodeHeader(packet, MessageKind::FriendListSync, payload) || payload.size() < 2)
			return false;

		const uint16_t count = ReadU16(payload, 0);
		size_t offset = 2;
		outMessage.friends.clear();
		outMessage.friends.reserve(count);
		for (uint16_t i = 0; i < count; ++i)
		{
			FriendListEntry entry;
			if (!ReadSizedString(payload, offset, entry.name))
				return false;
			if (entry.name.empty() || entry.name.size() > kMaxFriendNameUtf8Bytes)
				return false;
			if ((offset + 2) > payload.size())
				return false;
			entry.presenceStatus     = static_cast<PresenceStatus>(ReadU8(payload, offset));
			entry.isPendingInbound   = ReadU8(payload, offset + 1) != 0;
			offset += 2;
			outMessage.friends.push_back(std::move(entry));
		}
		if (offset != payload.size())
			return false;
		return true;
	}

	std::vector<std::byte> EncodeFriendStatusUpdate(const FriendStatusUpdateMessage& message)
	{
		std::vector<std::byte> packet = BeginPacket(MessageKind::FriendStatusUpdate, 2 + message.friendName.size() + 1);
		WriteSizedString(packet, message.friendName);
		WriteU8(packet, static_cast<uint8_t>(message.presenceStatus));
		return packet;
	}

	bool DecodeFriendStatusUpdate(std::span<const std::byte> packet, FriendStatusUpdateMessage& outMessage)
	{
		std::span<const std::byte> payload;
		if (!DecodeHeader(packet, MessageKind::FriendStatusUpdate, payload) || payload.size() < 3)
			return false;

		size_t offset = 0;
		if (!ReadSizedString(payload, offset, outMessage.friendName))
			return false;
		if (outMessage.friendName.empty() || outMessage.friendName.size() > kMaxFriendNameUtf8Bytes)
			return false;
		if (offset >= payload.size())
			return false;
		outMessage.presenceStatus = static_cast<PresenceStatus>(ReadU8(payload, offset));
		++offset;
		if (offset != payload.size())
			return false;
		return true;
	}

	// -------------------------------------------------------------------------
	// M32.2 — Party system encode / decode
	// -------------------------------------------------------------------------

	namespace
	{
		/// Maximum byte length of a party member display name (e.g. "P65535\0" safety cap).
		inline constexpr size_t kMaxPartyNameUtf8Bytes = 64;
	}

	std::vector<std::byte> EncodePartyInvite(const PartyInviteMessage& message)
	{
		std::vector<std::byte> packet = BeginPacket(MessageKind::PartyInvite,
		    4 + 2 + message.targetName.size());
		WriteU32(packet, message.clientId);
		WriteSizedString(packet, message.targetName);
		return packet;
	}

	bool DecodePartyInvite(std::span<const std::byte> packet, PartyInviteMessage& outMessage)
	{
		std::span<const std::byte> payload;
		if (!DecodeHeader(packet, MessageKind::PartyInvite, payload) || payload.size() < 6)
			return false;
		outMessage.clientId = ReadU32(payload, 0);
		size_t offset = 4;
		if (!ReadSizedString(payload, offset, outMessage.targetName))
			return false;
		if (outMessage.targetName.empty() || outMessage.targetName.size() > kMaxPartyNameUtf8Bytes)
			return false;
		if (offset != payload.size())
			return false;
		return true;
	}

	std::vector<std::byte> EncodePartyInviteNotify(const PartyInviteNotifyMessage& message)
	{
		std::vector<std::byte> packet = BeginPacket(MessageKind::PartyInviteNotify,
		    2 + message.inviterName.size());
		WriteSizedString(packet, message.inviterName);
		return packet;
	}

	bool DecodePartyInviteNotify(std::span<const std::byte> packet, PartyInviteNotifyMessage& outMessage)
	{
		std::span<const std::byte> payload;
		if (!DecodeHeader(packet, MessageKind::PartyInviteNotify, payload) || payload.size() < 2)
			return false;
		size_t offset = 0;
		if (!ReadSizedString(payload, offset, outMessage.inviterName))
			return false;
		if (outMessage.inviterName.empty() || outMessage.inviterName.size() > kMaxPartyNameUtf8Bytes)
			return false;
		if (offset != payload.size())
			return false;
		return true;
	}

	std::vector<std::byte> EncodePartyAccept(const PartyAcceptMessage& message)
	{
		std::vector<std::byte> packet = BeginPacket(MessageKind::PartyAccept, 4);
		WriteU32(packet, message.clientId);
		return packet;
	}

	bool DecodePartyAccept(std::span<const std::byte> packet, PartyAcceptMessage& outMessage)
	{
		std::span<const std::byte> payload;
		if (!DecodeHeader(packet, MessageKind::PartyAccept, payload) || payload.size() != 4)
			return false;
		outMessage.clientId = ReadU32(payload, 0);
		return true;
	}

	std::vector<std::byte> EncodePartyDecline(const PartyDeclineMessage& message)
	{
		std::vector<std::byte> packet = BeginPacket(MessageKind::PartyDecline, 4);
		WriteU32(packet, message.clientId);
		return packet;
	}

	bool DecodePartyDecline(std::span<const std::byte> packet, PartyDeclineMessage& outMessage)
	{
		std::span<const std::byte> payload;
		if (!DecodeHeader(packet, MessageKind::PartyDecline, payload) || payload.size() != 4)
			return false;
		outMessage.clientId = ReadU32(payload, 0);
		return true;
	}

	std::vector<std::byte> EncodePartyKick(const PartyKickMessage& message)
	{
		std::vector<std::byte> packet = BeginPacket(MessageKind::PartyKick,
		    4 + 2 + message.targetName.size());
		WriteU32(packet, message.clientId);
		WriteSizedString(packet, message.targetName);
		return packet;
	}

	bool DecodePartyKick(std::span<const std::byte> packet, PartyKickMessage& outMessage)
	{
		std::span<const std::byte> payload;
		if (!DecodeHeader(packet, MessageKind::PartyKick, payload) || payload.size() < 6)
			return false;
		outMessage.clientId = ReadU32(payload, 0);
		size_t offset = 4;
		if (!ReadSizedString(payload, offset, outMessage.targetName))
			return false;
		if (outMessage.targetName.empty() || outMessage.targetName.size() > kMaxPartyNameUtf8Bytes)
			return false;
		if (offset != payload.size())
			return false;
		return true;
	}

	std::vector<std::byte> EncodePartyUpdate(const PartyUpdateMessage& message)
	{
		// Layout: u32 partyId + u32 leaderId + u8 lootMode + u8 memberCount
		//         + for each member: u32 clientId + u32 hp + u32 maxHp + u32 mana + u32 maxMana
		//                            + u16 nameLen + name
		size_t payloadSize = 4 + 4 + 1 + 1;
		for (const auto& m : message.members)
			payloadSize += 4 + 4 + 4 + 4 + 4 + 2 + m.displayName.size();

		std::vector<std::byte> packet = BeginPacket(MessageKind::PartyUpdate, payloadSize);
		WriteU32(packet, message.partyId);
		WriteU32(packet, message.leaderId);
		WriteU8(packet, static_cast<uint8_t>(message.lootMode));
		WriteU8(packet, static_cast<uint8_t>(message.members.size()));
		for (const auto& m : message.members)
		{
			WriteU32(packet, m.clientId);
			WriteU32(packet, m.currentHealth);
			WriteU32(packet, m.maxHealth);
			WriteU32(packet, m.currentMana);
			WriteU32(packet, m.maxMana);
			WriteSizedString(packet, m.displayName);
		}
		return packet;
	}

	bool DecodePartyUpdate(std::span<const std::byte> packet, PartyUpdateMessage& outMessage)
	{
		std::span<const std::byte> payload;
		if (!DecodeHeader(packet, MessageKind::PartyUpdate, payload) || payload.size() < 10)
			return false;

		outMessage.partyId  = ReadU32(payload, 0);
		outMessage.leaderId = ReadU32(payload, 4);
		outMessage.lootMode = static_cast<WireLootMode>(ReadU8(payload, 8));
		const uint8_t memberCount = ReadU8(payload, 9);
		size_t offset = 10;
		outMessage.members.clear();
		outMessage.members.reserve(memberCount);
		for (uint8_t i = 0; i < memberCount; ++i)
		{
			if (offset + 4 + 4 + 4 + 4 + 4 + 2 > payload.size())
				return false;
			PartyMemberEntry e{};
			e.clientId      = ReadU32(payload, offset);      offset += 4;
			e.currentHealth = ReadU32(payload, offset);      offset += 4;
			e.maxHealth     = ReadU32(payload, offset);      offset += 4;
			e.currentMana   = ReadU32(payload, offset);      offset += 4;
			e.maxMana       = ReadU32(payload, offset);      offset += 4;
			if (!ReadSizedString(payload, offset, e.displayName))
				return false;
			if (e.displayName.size() > kMaxPartyNameUtf8Bytes)
				return false;
			outMessage.members.push_back(std::move(e));
		}
		if (offset != payload.size())
			return false;
		return true;
	}

	std::vector<std::byte> EncodePartyLootMode(const PartyLootModeMessage& message)
	{
		std::vector<std::byte> packet = BeginPacket(MessageKind::PartyLootMode, 4 + 1);
		WriteU32(packet, message.clientId);
		WriteU8(packet, static_cast<uint8_t>(message.lootMode));
		return packet;
	}

	bool DecodePartyLootMode(std::span<const std::byte> packet, PartyLootModeMessage& outMessage)
	{
		std::span<const std::byte> payload;
		if (!DecodeHeader(packet, MessageKind::PartyLootMode, payload) || payload.size() != 5)
			return false;
		outMessage.clientId = ReadU32(payload, 0);
		outMessage.lootMode = static_cast<WireLootMode>(ReadU8(payload, 4));
		return true;
	}

	std::vector<std::byte> EncodePartyLeave(const PartyLeaveMessage& message)
	{
		std::vector<std::byte> packet = BeginPacket(MessageKind::PartyLeave, 4);
		WriteU32(packet, message.clientId);
		return packet;
	}

	bool DecodePartyLeave(std::span<const std::byte> packet, PartyLeaveMessage& outMessage)
	{
		std::span<const std::byte> payload;
		if (!DecodeHeader(packet, MessageKind::PartyLeave, payload) || payload.size() != 4)
			return false;
		outMessage.clientId = ReadU32(payload, 0);
		return true;
	}

	std::vector<std::byte> EncodeWalletUpdate(const WalletUpdateMessage& message)
	{
		std::vector<std::byte> packet = BeginPacket(MessageKind::WalletUpdate, 20);
		WriteU32(packet, message.clientId);
		WriteU32(packet, message.gold);
		WriteU32(packet, message.honor);
		WriteU32(packet, message.badges);
		WriteU32(packet, message.premiumCurrency);
		return packet;
	}

	bool DecodeWalletUpdate(std::span<const std::byte> packet, WalletUpdateMessage& outMessage)
	{
		std::span<const std::byte> payload;
		if (!DecodeHeader(packet, MessageKind::WalletUpdate, payload) || payload.size() != 20)
		{
			return false;
		}
		outMessage.clientId = ReadU32(payload, 0);
		outMessage.gold = ReadU32(payload, 4);
		outMessage.honor = ReadU32(payload, 8);
		outMessage.badges = ReadU32(payload, 12);
		outMessage.premiumCurrency = ReadU32(payload, 16);
		return true;
	}
}
