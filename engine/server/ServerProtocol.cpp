#include "engine/server/ServerProtocol.h"

#include <bit>

namespace engine::server
{
	namespace
	{
		/// Wire magic used to reject packets from unrelated clients.
		inline constexpr uint32_t kWireMagic = 0x31525653u;

		/// Fixed packet header size shared by all protocol messages.
		inline constexpr size_t kHeaderSize = 8;

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

		/// Append a float to an output buffer using IEEE-754 bit representation.
		void WriteF32(std::vector<std::byte>& outBytes, float value)
		{
			WriteU32(outBytes, std::bit_cast<uint32_t>(value));
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
			WriteU32(outBytes, state.stateFlags);
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
		std::vector<std::byte> packet = BeginPacket(MessageKind::Snapshot, 20 + (entities.size() * 40));
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
		std::vector<std::byte> packet = BeginPacket(MessageKind::Spawn, 44);
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
}
