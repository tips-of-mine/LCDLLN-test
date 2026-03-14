#pragma once

#include "engine/server/ReplicationTypes.h"

#include <cstddef>
#include <cstdint>
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
		CombatEvent = 9
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

	/// Decode a hello packet and validate the protocol header.
	bool DecodeHello(std::span<const std::byte> packet, HelloMessage& outMessage);

	/// Decode an input packet and validate the protocol header.
	bool DecodeInput(std::span<const std::byte> packet, InputMessage& outMessage);

	/// Encode a welcome packet with the protocol header.
	std::vector<std::byte> EncodeWelcome(const WelcomeMessage& message);

	/// Encode a snapshot packet with the protocol header.
	std::vector<std::byte> EncodeSnapshot(const SnapshotMessage& message, std::span<const SnapshotEntity> entities);

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
}
