#include "src/shared/network/ServerProtocol.h"

#include "src/shared/net/ChatSystem.h"

#include <algorithm>
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
		// Phase 3.7.5 — payload = 12 octets (uint16 tick, uint16 snap, uint64 nonce).
		std::span<const std::byte> payload;
		if (!DecodeHeader(packet, MessageKind::Hello, payload) || payload.size() != 12)
		{
			return false;
		}

		outMessage.requestedTickHz = ReadU16(payload, 0);
		outMessage.requestedSnapshotHz = ReadU16(payload, 2);
		outMessage.clientNonce = ReadU64(payload, 4);
		return true;
	}

	std::vector<std::byte> EncodeHello(const HelloMessage& message)
	{
		// Phase 3.7.5 — payload = 12 octets (uint16 + uint16 + uint64).
		std::vector<std::byte> packet = BeginPacket(MessageKind::Hello, 12);
		WriteU16(packet, message.requestedTickHz);
		WriteU16(packet, message.requestedSnapshotHz);
		WriteU64(packet, message.clientNonce);
		return packet;
	}

	bool DecodeInput(std::span<const std::byte> packet, InputMessage& outMessage)
	{
		std::span<const std::byte> payload;
		// TD.8 — payload 25 octets : 24 (TC.1) + 1 (animationState).
		if (!DecodeHeader(packet, MessageKind::Input, payload) || payload.size() != 25)
		{
			return false;
		}

		outMessage.clientId = ReadU32(payload, 0);
		outMessage.inputSequence = ReadU32(payload, 4);
		outMessage.positionMetersX = std::bit_cast<float>(ReadU32(payload, 8));
		outMessage.positionMetersY = std::bit_cast<float>(ReadU32(payload, 12));
		outMessage.positionMetersZ = std::bit_cast<float>(ReadU32(payload, 16));
		outMessage.yawRadians = std::bit_cast<float>(ReadU32(payload, 20));
		outMessage.animationState = ReadU8(payload, 24);
		return true;
	}

	std::vector<std::byte> EncodeInput(const InputMessage& message)
	{
		// TC.1/TD.8 — payload 25 octets : clientId, inputSequence, posX, posY, posZ, yaw,
		// animationState (1 octet).
		std::vector<std::byte> packet = BeginPacket(MessageKind::Input, 25);
		WriteU32(packet, message.clientId);
		WriteU32(packet, message.inputSequence);
		WriteU32(packet, std::bit_cast<uint32_t>(message.positionMetersX));
		WriteU32(packet, std::bit_cast<uint32_t>(message.positionMetersY));
		WriteU32(packet, std::bit_cast<uint32_t>(message.positionMetersZ));
		WriteU32(packet, std::bit_cast<uint32_t>(message.yawRadians));
		WriteU8(packet, message.animationState);
		return packet;
	}

	std::vector<std::byte> EncodeGoodbye(const GoodbyeMessage& message)
	{
		// Départ propre : payload 4 octets (clientId). Pas de bump de version — un shard
		// qui ne gère pas cet opcode l'ignore simplement (fallback timeout d'inactivité).
		std::vector<std::byte> packet = BeginPacket(MessageKind::Goodbye, 4);
		WriteU32(packet, message.clientId);
		return packet;
	}

	bool DecodeGoodbye(std::span<const std::byte> packet, GoodbyeMessage& outMessage)
	{
		std::span<const std::byte> payload;
		if (!DecodeHeader(packet, MessageKind::Goodbye, payload) || payload.size() != 4)
		{
			return false;
		}
		outMessage.clientId = ReadU32(payload, 0);
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
		// TG.1 — header passe de 20 → 24 octets (ajout chunkIndex + chunkCount).
		if (!DecodeHeader(packet, MessageKind::Snapshot, payload) || payload.size() < 24)
		{
			return false;
		}

		outMessage.clientId = ReadU32(payload, 0);
		outMessage.serverTick = ReadU32(payload, 4);
		outMessage.connectedClients = ReadU16(payload, 8);
		outMessage.entityCount = ReadU16(payload, 10);
		outMessage.receivedPackets = ReadU32(payload, 12);
		outMessage.sentPackets = ReadU32(payload, 16);
		// TG.1 — chunkIndex / chunkCount. Le client utilise ces 2 valeurs pour reassembler
		// un snapshot scinde sur plusieurs datagrammes (meme serverTick).
		outMessage.chunkIndex = ReadU16(payload, 20);
		outMessage.chunkCount = ReadU16(payload, 22);

		const size_t entityCount = static_cast<size_t>(outMessage.entityCount);
		// TD.6/TD.8/SP1 — taille variable par entite : 8 (entityId) + 40 (EntityState) + 4 (playerClientId)
		// + 2 (nameLen) + N (name) + 2 (genderLen) + M (gender) + 1 (animationState) + 4 (archetypeId)
		// octets. On vérifie un minimum (61 par entité, nom et genre vides), puis on parse
		// sequentiellement et on rejette si on dépasse la fin du payload en cours de route.
		// TG.1 — header a 24 octets.
		const size_t minimumPayloadSize = 24 + (entityCount * 61);
		if (payload.size() < minimumPayloadSize)
		{
			return false;
		}

		outEntities.clear();
		outEntities.resize(entityCount);
		size_t offset = 24;
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
			// TD.4 : id client decode apres EntityState. ReadEntityState a deja avance offset de 40.
			entity.playerClientId = ReadU32(payload, offset);
			offset += 4;
			// TD.5 : nom du personnage (chaîne préfixée u16). Borne dure : 64 octets (cf.
			// kMaxChatLocalSenderNameBytes), au-delà on rejette le paquet pour défense en profondeur.
			if (!ReadSizedString(payload, offset, entity.characterName)
				|| entity.characterName.size() > 64u)
			{
				outEntities.clear();
				return false;
			}
			// TD.6 : genre du personnage (chaîne préfixée u16). Borne dure : 8 octets (cf.
			// VARCHAR(8) en DB, cf. migration 0067). Vide pour les mobs/lootbags.
			if (!ReadSizedString(payload, offset, entity.gender)
				|| entity.gender.size() > 8u)
			{
				outEntities.clear();
				return false;
			}
			// TD.8 : état d'animation (1 octet) après le genre. ReadSizedString a avancé offset ;
			// le minimum (61/entité, chaînes vides) garantit qu'il reste au moins 5 octets, mais
			// avec des chaînes non vides on peut dépasser : on vérifie explicitement la borne.
			if (offset + 1u > payload.size())
			{
				outEntities.clear();
				return false;
			}
			entity.animationState = static_cast<AvatarAnimState>(ReadU8(payload, offset));
			offset += 1u;
			// Combat SP1 (wire v9) : archétype de créature (u32) après animationState.
			// 0 = joueur / loot bag ; ≠ 0 = mob (résolu par le CreatureCatalog client).
			if (offset + 4u > payload.size())
			{
				outEntities.clear();
				return false;
			}
			entity.archetypeId = ReadU32(payload, offset);
			offset += 4u;
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

	std::vector<std::byte> EncodeTalkRequest(const TalkRequestMessage& message)
	{
		const size_t payloadSize = 4u + 2u + message.targetId.size();
		std::vector<std::byte> packet = BeginPacket(MessageKind::TalkRequest, payloadSize);
		WriteU32(packet, message.clientId);
		WriteSizedString(packet, message.targetId);
		return packet;
	}

	std::vector<std::byte> EncodeWelcome(const WelcomeMessage& message)
	{
		std::vector<std::byte> packet = BeginPacket(MessageKind::Welcome, 8);
		WriteU32(packet, message.clientId);
		WriteU16(packet, message.tickHz);
		WriteU16(packet, message.snapshotHz);
		return packet;
	}

	std::vector<std::byte> EncodePlayerStats(const PlayerStatsMessage& message)
	{
		// R1-B — payload : 5 u32 (clientId, maxHealth, resource, stamina, damage) + 9 f32
		// (accuracy, range, critRate, critMult, speedWalk, speedRun, speedSprint, perception,
		// stealth) + 1 chaîne taillée (resourceKey, préfixe u16). Estimation pour reserve.
		std::vector<std::byte> packet = BeginPacket(
			MessageKind::PlayerStats, 5 * 4 + 9 * 4 + 2 + message.resourceKey.size());
		WriteU32(packet, message.clientId);
		WriteU32(packet, message.maxHealth);
		WriteU32(packet, message.resource);
		WriteU32(packet, message.stamina);
		WriteU32(packet, message.damage);
		WriteF32(packet, message.accuracy);
		WriteF32(packet, message.range);
		WriteF32(packet, message.critRate);
		WriteF32(packet, message.critMult);
		WriteF32(packet, message.speedWalk);
		WriteF32(packet, message.speedRun);
		WriteF32(packet, message.speedSprint);
		WriteF32(packet, message.perception);
		WriteF32(packet, message.stealth);
		WriteSizedString(packet, message.resourceKey);
		return packet;
	}

	bool DecodePlayerStats(std::span<const std::byte> packet, PlayerStatsMessage& outMessage)
	{
		std::span<const std::byte> payload;
		// Taille fixe minimale : 5 u32 + 9 f32 + 2 octets de préfixe de chaîne = 58 octets.
		if (!DecodeHeader(packet, MessageKind::PlayerStats, payload) || payload.size() < 58)
		{
			return false;
		}

		outMessage.clientId = ReadU32(payload, 0);
		outMessage.maxHealth = ReadU32(payload, 4);
		outMessage.resource = ReadU32(payload, 8);
		outMessage.stamina = ReadU32(payload, 12);
		outMessage.damage = ReadU32(payload, 16);
		outMessage.accuracy = ReadF32(payload, 20);
		outMessage.range = ReadF32(payload, 24);
		outMessage.critRate = ReadF32(payload, 28);
		outMessage.critMult = ReadF32(payload, 32);
		outMessage.speedWalk = ReadF32(payload, 36);
		outMessage.speedRun = ReadF32(payload, 40);
		outMessage.speedSprint = ReadF32(payload, 44);
		outMessage.perception = ReadF32(payload, 48);
		outMessage.stealth = ReadF32(payload, 52);
		size_t offset = 56;
		if (!ReadSizedString(payload, offset, outMessage.resourceKey))
		{
			return false;
		}
		if (offset != payload.size())
		{
			return false;
		}
		return true;
	}

	std::vector<std::byte> EncodeSnapshot(const SnapshotMessage& message, std::span<const SnapshotEntity> entities)
	{
		// TD.6/TD.8/SP1 — taille par entite : 8 (entityId) + 40 (EntityState) + 4 (playerClientId)
		// + 2 (nameLen) + N (name bytes) + 2 (genderLen) + M (gender bytes) + 1 (animationState)
		// + 4 (archetypeId) octets. Wire-bump v8→v9 : ajout de l'archétype (u32) après l'état
		// d'animation. Pour le sizing : estimation à 8 + 40 + 4 + 2 + 2 + 1 + 4 = 61 par entité
		// (nom et genre vides). BeginPacket.reserve est juste un hint, pas une borne stricte
		// (le vector grandit à l'append).
		// TG.1 — header passe de 20 → 24 octets (ajout chunkIndex + chunkCount uint16 × 2).
		std::vector<std::byte> packet = BeginPacket(MessageKind::Snapshot, 24 + (entities.size() * 61));
		WriteU32(packet, message.clientId);
		WriteU32(packet, message.serverTick);
		WriteU16(packet, message.connectedClients);
		WriteU16(packet, message.entityCount);
		WriteU32(packet, message.receivedPackets);
		WriteU32(packet, message.sentPackets);
		// TG.1 — chunking : chunkCount = 1 (defaut) = mono-paquet ; > 1 = chunk d'un snapshot
		// scinde pour rester sous le budget MTU UDP. Le client reassemble par serverTick.
		WriteU16(packet, message.chunkIndex);
		WriteU16(packet, message.chunkCount);
		for (const SnapshotEntity& entity : entities)
		{
			WriteU64(packet, entity.entityId);
			WriteEntityState(packet, entity.state);
			// TD.4 : id client (≠ entityId) pour qu'un client distant puisse afficher la plaque.
			WriteU32(packet, entity.playerClientId);
			// TD.5 : nom du personnage (préfixé u16). Vide pour les mobs / lootbags.
			WriteSizedString(packet, entity.characterName);
			// TD.6 : genre du personnage (préfixé u16). Vide pour les mobs / lootbags.
			WriteSizedString(packet, entity.gender);
			// TD.8 : état d'animation (1 octet). Idle (0) pour les mobs / lootbags.
			WriteU8(packet, static_cast<uint8_t>(entity.animationState));
			// Combat SP1 (wire v9) : archétype de créature (0 = joueur / loot bag).
			WriteU32(packet, entity.archetypeId);
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

	std::vector<std::byte> EncodeShopOpen(const ShopOpenMessage& message)
	{
		const uint16_t offerCount = static_cast<uint16_t>(
			std::min<size_t>(message.offers.size(), static_cast<size_t>(kMaxShopOffersPerPacket)));
		const size_t payloadSize = 4 + 2 + message.displayName.size() + 2 + (static_cast<size_t>(offerCount) * 12u);
		std::vector<std::byte> packet = BeginPacket(MessageKind::ShopOpen, payloadSize);
		WriteU32(packet, message.vendorId);
		WriteSizedString(packet, message.displayName);
		WriteU16(packet, offerCount);
		for (uint16_t i = 0; i < offerCount; ++i)
		{
			WriteU32(packet, message.offers[i].itemId);
			WriteU32(packet, message.offers[i].buyPrice);
			WriteU32(packet, message.offers[i].stock);
		}
		return packet;
	}

	bool DecodeShopOpen(std::span<const std::byte> packet, ShopOpenMessage& outMessage)
	{
		std::span<const std::byte> payload;
		if (!DecodeHeader(packet, MessageKind::ShopOpen, payload) || payload.size() < 8)
		{
			return false;
		}
		outMessage.vendorId = ReadU32(payload, 0);
		size_t offset = 4;
		if (!ReadSizedString(payload, offset, outMessage.displayName))
		{
			return false;
		}
		if ((offset + 2) > payload.size())
		{
			return false;
		}
		const uint16_t offerCount = ReadU16(payload, offset);
		offset += 2;
		if (offerCount > kMaxShopOffersPerPacket)
		{
			return false;
		}
		const size_t need = offset + (static_cast<size_t>(offerCount) * 12u);
		if (payload.size() != need)
		{
			return false;
		}
		outMessage.offers.clear();
		outMessage.offers.reserve(offerCount);
		for (uint16_t i = 0; i < offerCount; ++i)
		{
			ShopOfferWire row{};
			row.itemId = ReadU32(payload, offset);
			offset += 4;
			row.buyPrice = ReadU32(payload, offset);
			offset += 4;
			row.stock = ReadU32(payload, offset);
			offset += 4;
			outMessage.offers.push_back(row);
		}
		return true;
	}

	std::vector<std::byte> EncodeShopBuyRequest(const ShopBuyRequestMessage& message)
	{
		std::vector<std::byte> packet = BeginPacket(MessageKind::ShopBuyRequest, 16);
		WriteU32(packet, message.clientId);
		WriteU32(packet, message.vendorId);
		WriteU32(packet, message.itemId);
		WriteU32(packet, message.quantity);
		return packet;
	}

	bool DecodeShopBuyRequest(std::span<const std::byte> packet, ShopBuyRequestMessage& outMessage)
	{
		std::span<const std::byte> payload;
		if (!DecodeHeader(packet, MessageKind::ShopBuyRequest, payload) || payload.size() != 16)
		{
			return false;
		}
		outMessage.clientId = ReadU32(payload, 0);
		outMessage.vendorId = ReadU32(payload, 4);
		outMessage.itemId = ReadU32(payload, 8);
		outMessage.quantity = ReadU32(payload, 12);
		return true;
	}

	std::vector<std::byte> EncodeShopSellRequest(const ShopSellRequestMessage& message)
	{
		std::vector<std::byte> packet = BeginPacket(MessageKind::ShopSellRequest, 16);
		WriteU32(packet, message.clientId);
		WriteU32(packet, message.vendorId);
		WriteU32(packet, message.itemId);
		WriteU32(packet, message.quantity);
		return packet;
	}

	bool DecodeShopSellRequest(std::span<const std::byte> packet, ShopSellRequestMessage& outMessage)
	{
		std::span<const std::byte> payload;
		if (!DecodeHeader(packet, MessageKind::ShopSellRequest, payload) || payload.size() != 16)
		{
			return false;
		}
		outMessage.clientId = ReadU32(payload, 0);
		outMessage.vendorId = ReadU32(payload, 4);
		outMessage.itemId = ReadU32(payload, 8);
		outMessage.quantity = ReadU32(payload, 12);
		return true;
	}

	std::vector<std::byte> EncodeAuctionBrowseRequest(const AuctionBrowseRequestMessage& message)
	{
		std::vector<std::byte> packet = BeginPacket(MessageKind::AuctionBrowseRequest, 24);
		WriteU32(packet, message.clientId);
		WriteU32(packet, message.minPrice);
		WriteU32(packet, message.maxPrice);
		WriteU32(packet, message.itemIdFilter);
		WriteU32(packet, message.sortMode);
		WriteU32(packet, message.maxRows);
		return packet;
	}

	bool DecodeAuctionBrowseRequest(std::span<const std::byte> packet, AuctionBrowseRequestMessage& outMessage)
	{
		std::span<const std::byte> payload;
		if (!DecodeHeader(packet, MessageKind::AuctionBrowseRequest, payload) || payload.size() != 24)
		{
			return false;
		}
		outMessage.clientId = ReadU32(payload, 0);
		outMessage.minPrice = ReadU32(payload, 4);
		outMessage.maxPrice = ReadU32(payload, 8);
		outMessage.itemIdFilter = ReadU32(payload, 12);
		outMessage.sortMode = ReadU32(payload, 16);
		outMessage.maxRows = ReadU32(payload, 20);
		return true;
	}

	std::vector<std::byte> EncodeAuctionBrowseResult(const AuctionBrowseResultMessage& message)
	{
		const uint32_t rowCount = static_cast<uint32_t>(
			std::min<size_t>(message.rows.size(), static_cast<size_t>(kMaxAuctionBrowseRowsWire)));
		const size_t payloadSize = 8u + (static_cast<size_t>(rowCount) * 28u);
		std::vector<std::byte> packet = BeginPacket(MessageKind::AuctionBrowseResult, payloadSize);
		WriteU32(packet, message.clientId);
		WriteU32(packet, rowCount);
		for (uint32_t i = 0; i < rowCount; ++i)
		{
			const AuctionListingWireRow& r = message.rows[i];
			WriteU32(packet, r.listingId);
			WriteU32(packet, r.itemId);
			WriteU32(packet, r.quantity);
			WriteU32(packet, r.startBid);
			WriteU32(packet, r.buyoutPrice);
			WriteU32(packet, r.currentBid);
			WriteU32(packet, r.expiresAtTick);
		}
		return packet;
	}

	bool DecodeAuctionBrowseResult(std::span<const std::byte> packet, AuctionBrowseResultMessage& outMessage)
	{
		std::span<const std::byte> payload;
		if (!DecodeHeader(packet, MessageKind::AuctionBrowseResult, payload) || payload.size() < 8)
		{
			return false;
		}
		outMessage.clientId = ReadU32(payload, 0);
		const uint32_t rowCount = ReadU32(payload, 4);
		if (rowCount > kMaxAuctionBrowseRowsWire)
		{
			return false;
		}
		const size_t need = 8u + (static_cast<size_t>(rowCount) * 28u);
		if (payload.size() != need)
		{
			return false;
		}
		outMessage.rows.clear();
		outMessage.rows.reserve(rowCount);
		size_t offset = 8;
		for (uint32_t i = 0; i < rowCount; ++i)
		{
			AuctionListingWireRow r{};
			r.listingId = ReadU32(payload, offset);
			offset += 4;
			r.itemId = ReadU32(payload, offset);
			offset += 4;
			r.quantity = ReadU32(payload, offset);
			offset += 4;
			r.startBid = ReadU32(payload, offset);
			offset += 4;
			r.buyoutPrice = ReadU32(payload, offset);
			offset += 4;
			r.currentBid = ReadU32(payload, offset);
			offset += 4;
			r.expiresAtTick = ReadU32(payload, offset);
			offset += 4;
			outMessage.rows.push_back(r);
		}
		return true;
	}

	std::vector<std::byte> EncodeAuctionListItemRequest(const AuctionListItemRequestMessage& message)
	{
		std::vector<std::byte> packet = BeginPacket(MessageKind::AuctionListItemRequest, 24);
		WriteU32(packet, message.clientId);
		WriteU32(packet, message.itemId);
		WriteU32(packet, message.quantity);
		WriteU32(packet, message.startBid);
		WriteU32(packet, message.buyoutPrice);
		WriteU32(packet, message.durationHours);
		return packet;
	}

	bool DecodeAuctionListItemRequest(std::span<const std::byte> packet, AuctionListItemRequestMessage& outMessage)
	{
		std::span<const std::byte> payload;
		if (!DecodeHeader(packet, MessageKind::AuctionListItemRequest, payload) || payload.size() != 24)
		{
			return false;
		}
		outMessage.clientId = ReadU32(payload, 0);
		outMessage.itemId = ReadU32(payload, 4);
		outMessage.quantity = ReadU32(payload, 8);
		outMessage.startBid = ReadU32(payload, 12);
		outMessage.buyoutPrice = ReadU32(payload, 16);
		outMessage.durationHours = ReadU32(payload, 20);
		return true;
	}

	std::vector<std::byte> EncodeAuctionBidRequest(const AuctionBidRequestMessage& message)
	{
		std::vector<std::byte> packet = BeginPacket(MessageKind::AuctionBidRequest, 12);
		WriteU32(packet, message.clientId);
		WriteU32(packet, message.listingId);
		WriteU32(packet, message.bidAmount);
		return packet;
	}

	bool DecodeAuctionBidRequest(std::span<const std::byte> packet, AuctionBidRequestMessage& outMessage)
	{
		std::span<const std::byte> payload;
		if (!DecodeHeader(packet, MessageKind::AuctionBidRequest, payload) || payload.size() != 12)
		{
			return false;
		}
		outMessage.clientId = ReadU32(payload, 0);
		outMessage.listingId = ReadU32(payload, 4);
		outMessage.bidAmount = ReadU32(payload, 8);
		return true;
	}

	std::vector<std::byte> EncodeAuctionBuyoutRequest(const AuctionBuyoutRequestMessage& message)
	{
		std::vector<std::byte> packet = BeginPacket(MessageKind::AuctionBuyoutRequest, 8);
		WriteU32(packet, message.clientId);
		WriteU32(packet, message.listingId);
		return packet;
	}

	bool DecodeAuctionBuyoutRequest(std::span<const std::byte> packet, AuctionBuyoutRequestMessage& outMessage)
	{
		std::span<const std::byte> payload;
		if (!DecodeHeader(packet, MessageKind::AuctionBuyoutRequest, payload) || payload.size() != 8)
		{
			return false;
		}
		outMessage.clientId = ReadU32(payload, 0);
		outMessage.listingId = ReadU32(payload, 4);
		return true;
	}

	// -------------------------------------------------------------------------
	// M35.3 — Direct player-to-player trade codec
	// -------------------------------------------------------------------------

	namespace
	{
		/// Encode one TradeSideWire into an output buffer (variable-length).
		void WriteTradeSide(std::vector<std::byte>& out, const TradeSideWire& side)
		{
			const uint8_t count = static_cast<uint8_t>(
				std::min(side.items.size(), static_cast<size_t>(kMaxTradeItemSlots)));
			WriteU32(out, side.clientId);
			WriteU32(out, side.goldAmount);
			WriteU8(out, side.locked);
			WriteU8(out, side.confirmed);
			WriteU8(out, count);
			for (uint8_t i = 0; i < count; ++i)
			{
				WriteU32(out, side.items[i].itemId);
				WriteU32(out, side.items[i].quantity);
			}
		}

		/// Decode one TradeSideWire from an offset into a payload span (variable-length).
		bool ReadTradeSide(std::span<const std::byte> payload, size_t& offset, TradeSideWire& outSide)
		{
			if ((offset + 11) > payload.size())
			{
				return false;
			}
			outSide.clientId   = ReadU32(payload, offset);     offset += 4;
			outSide.goldAmount = ReadU32(payload, offset);     offset += 4;
			outSide.locked     = ReadU8(payload, offset);      offset += 1;
			outSide.confirmed  = ReadU8(payload, offset);      offset += 1;
			const uint8_t count = ReadU8(payload, offset);     offset += 1;
			if (count > kMaxTradeItemSlots)
			{
				return false;
			}
			if ((offset + static_cast<size_t>(count) * 8) > payload.size())
			{
				return false;
			}
			outSide.items.resize(count);
			for (uint8_t i = 0; i < count; ++i)
			{
				outSide.items[i].itemId   = ReadU32(payload, offset); offset += 4;
				outSide.items[i].quantity = ReadU32(payload, offset); offset += 4;
			}
			return true;
		}
	} // anonymous namespace

	std::vector<std::byte> EncodeTradeRequest(const TradeRequestMessage& message)
	{
		const size_t nameLen = message.targetName.size();
		std::vector<std::byte> packet = BeginPacket(MessageKind::TradeRequest, 4 + 2 + nameLen);
		WriteU32(packet, message.clientId);
		WriteSizedString(packet, message.targetName);
		return packet;
	}

	bool DecodeTradeRequest(std::span<const std::byte> packet, TradeRequestMessage& outMessage)
	{
		std::span<const std::byte> payload;
		if (!DecodeHeader(packet, MessageKind::TradeRequest, payload) || payload.size() < 6)
		{
			return false;
		}
		outMessage.clientId = ReadU32(payload, 0);
		size_t offset = 4;
		return ReadSizedString(payload, offset, outMessage.targetName);
	}

	std::vector<std::byte> EncodeTradeRequestNotify(const TradeRequestNotifyMessage& message)
	{
		const size_t nameLen = message.initiatorName.size();
		std::vector<std::byte> packet = BeginPacket(MessageKind::TradeRequestNotify, 2 + nameLen);
		WriteSizedString(packet, message.initiatorName);
		return packet;
	}

	bool DecodeTradeRequestNotify(std::span<const std::byte> packet, TradeRequestNotifyMessage& outMessage)
	{
		std::span<const std::byte> payload;
		if (!DecodeHeader(packet, MessageKind::TradeRequestNotify, payload) || payload.size() < 2)
		{
			return false;
		}
		size_t offset = 0;
		return ReadSizedString(payload, offset, outMessage.initiatorName);
	}

	std::vector<std::byte> EncodeTradeAccept(const TradeAcceptMessage& message)
	{
		std::vector<std::byte> packet = BeginPacket(MessageKind::TradeAccept, 4);
		WriteU32(packet, message.clientId);
		return packet;
	}

	bool DecodeTradeAccept(std::span<const std::byte> packet, TradeAcceptMessage& outMessage)
	{
		std::span<const std::byte> payload;
		if (!DecodeHeader(packet, MessageKind::TradeAccept, payload) || payload.size() != 4)
		{
			return false;
		}
		outMessage.clientId = ReadU32(payload, 0);
		return true;
	}

	std::vector<std::byte> EncodeTradeDecline(const TradeDeclineMessage& message)
	{
		std::vector<std::byte> packet = BeginPacket(MessageKind::TradeDecline, 4);
		WriteU32(packet, message.clientId);
		return packet;
	}

	bool DecodeTradeDecline(std::span<const std::byte> packet, TradeDeclineMessage& outMessage)
	{
		std::span<const std::byte> payload;
		if (!DecodeHeader(packet, MessageKind::TradeDecline, payload) || payload.size() != 4)
		{
			return false;
		}
		outMessage.clientId = ReadU32(payload, 0);
		return true;
	}

	std::vector<std::byte> EncodeTradeAddItem(const TradeAddItemMessage& message)
	{
		std::vector<std::byte> packet = BeginPacket(MessageKind::TradeAddItem, 12);
		WriteU32(packet, message.clientId);
		WriteU32(packet, message.itemId);
		WriteU32(packet, message.quantity);
		return packet;
	}

	bool DecodeTradeAddItem(std::span<const std::byte> packet, TradeAddItemMessage& outMessage)
	{
		std::span<const std::byte> payload;
		if (!DecodeHeader(packet, MessageKind::TradeAddItem, payload) || payload.size() != 12)
		{
			return false;
		}
		outMessage.clientId = ReadU32(payload, 0);
		outMessage.itemId   = ReadU32(payload, 4);
		outMessage.quantity = ReadU32(payload, 8);
		return true;
	}

	std::vector<std::byte> EncodeTradeSetGold(const TradeSetGoldMessage& message)
	{
		std::vector<std::byte> packet = BeginPacket(MessageKind::TradeSetGold, 8);
		WriteU32(packet, message.clientId);
		WriteU32(packet, message.goldAmount);
		return packet;
	}

	bool DecodeTradeSetGold(std::span<const std::byte> packet, TradeSetGoldMessage& outMessage)
	{
		std::span<const std::byte> payload;
		if (!DecodeHeader(packet, MessageKind::TradeSetGold, payload) || payload.size() != 8)
		{
			return false;
		}
		outMessage.clientId   = ReadU32(payload, 0);
		outMessage.goldAmount = ReadU32(payload, 4);
		return true;
	}

	std::vector<std::byte> EncodeTradeLock(const TradeLockMessage& message)
	{
		std::vector<std::byte> packet = BeginPacket(MessageKind::TradeLock, 4);
		WriteU32(packet, message.clientId);
		return packet;
	}

	bool DecodeTradeLock(std::span<const std::byte> packet, TradeLockMessage& outMessage)
	{
		std::span<const std::byte> payload;
		if (!DecodeHeader(packet, MessageKind::TradeLock, payload) || payload.size() != 4)
		{
			return false;
		}
		outMessage.clientId = ReadU32(payload, 0);
		return true;
	}

	std::vector<std::byte> EncodeTradeConfirm(const TradeConfirmMessage& message)
	{
		std::vector<std::byte> packet = BeginPacket(MessageKind::TradeConfirm, 4);
		WriteU32(packet, message.clientId);
		return packet;
	}

	bool DecodeTradeConfirm(std::span<const std::byte> packet, TradeConfirmMessage& outMessage)
	{
		std::span<const std::byte> payload;
		if (!DecodeHeader(packet, MessageKind::TradeConfirm, payload) || payload.size() != 4)
		{
			return false;
		}
		outMessage.clientId = ReadU32(payload, 0);
		return true;
	}

	std::vector<std::byte> EncodeTradeCancel(const TradeCancelMessage& message)
	{
		std::vector<std::byte> packet = BeginPacket(MessageKind::TradeCancel, 4);
		WriteU32(packet, message.clientId);
		return packet;
	}

	bool DecodeTradeCancel(std::span<const std::byte> packet, TradeCancelMessage& outMessage)
	{
		std::span<const std::byte> payload;
		if (!DecodeHeader(packet, MessageKind::TradeCancel, payload) || payload.size() != 4)
		{
			return false;
		}
		outMessage.clientId = ReadU32(payload, 0);
		return true;
	}

	std::vector<std::byte> EncodeTradeWindowUpdate(const TradeWindowUpdateMessage& message)
	{
		/// Layout: [self side][other side][reviewTicksRemaining:4]
		const uint8_t selfCount  = static_cast<uint8_t>(
			std::min(message.self.items.size(),  static_cast<size_t>(kMaxTradeItemSlots)));
		const uint8_t otherCount = static_cast<uint8_t>(
			std::min(message.other.items.size(), static_cast<size_t>(kMaxTradeItemSlots)));
		/// Side wire size: clientId(4) + goldAmount(4) + locked(1) + confirmed(1) + count(1) + items(8*n)
		const size_t selfSize  = 11 + static_cast<size_t>(selfCount)  * 8;
		const size_t otherSize = 11 + static_cast<size_t>(otherCount) * 8;
		std::vector<std::byte> packet = BeginPacket(MessageKind::TradeWindowUpdate, selfSize + otherSize + 4);
		WriteTradeSide(packet, message.self);
		WriteTradeSide(packet, message.other);
		WriteU32(packet, message.reviewTicksRemaining);
		return packet;
	}

	bool DecodeTradeWindowUpdate(std::span<const std::byte> packet, TradeWindowUpdateMessage& outMessage)
	{
		std::span<const std::byte> payload;
		if (!DecodeHeader(packet, MessageKind::TradeWindowUpdate, payload) || payload.size() < 26)
		{
			return false;
		}
		size_t offset = 0;
		if (!ReadTradeSide(payload, offset, outMessage.self))
		{
			return false;
		}
		if (!ReadTradeSide(payload, offset, outMessage.other))
		{
			return false;
		}
		if ((offset + 4) > payload.size())
		{
			return false;
		}
		outMessage.reviewTicksRemaining = ReadU32(payload, offset);
		return true;
	}

	std::vector<std::byte> EncodeTradeComplete(const TradeCompleteMessage& message)
	{
		std::vector<std::byte> packet = BeginPacket(MessageKind::TradeComplete, 4);
		WriteU32(packet, message.clientId);
		return packet;
	}

	bool DecodeTradeComplete(std::span<const std::byte> packet, TradeCompleteMessage& outMessage)
	{
		std::span<const std::byte> payload;
		if (!DecodeHeader(packet, MessageKind::TradeComplete, payload) || payload.size() != 4)
		{
			return false;
		}
		outMessage.clientId = ReadU32(payload, 0);
		return true;
	}

	std::vector<std::byte> EncodeTradeCancelled(const TradeCancelledMessage& message)
	{
		const size_t reasonLen = message.reason.size();
		std::vector<std::byte> packet = BeginPacket(MessageKind::TradeCancelled, 2 + reasonLen);
		WriteSizedString(packet, message.reason);
		return packet;
	}

	bool DecodeTradeCancelled(std::span<const std::byte> packet, TradeCancelledMessage& outMessage)
	{
		std::span<const std::byte> payload;
		if (!DecodeHeader(packet, MessageKind::TradeCancelled, payload) || payload.size() < 2)
		{
			return false;
		}
		size_t offset = 0;
		return ReadSizedString(payload, offset, outMessage.reason);
	}

	// -------------------------------------------------------------------------
	// M36.1 — Gathering / harvesting resource nodes codec
	// -------------------------------------------------------------------------

	std::vector<std::byte> EncodeHarvestRequest(const HarvestRequestMessage& message)
	{
		/// Layout: clientId(4) + nodeEntityId(8)
		std::vector<std::byte> packet = BeginPacket(MessageKind::HarvestRequest, 12);
		WriteU32(packet, message.clientId);
		WriteU64(packet, message.nodeEntityId);
		return packet;
	}

	bool DecodeHarvestRequest(std::span<const std::byte> packet, HarvestRequestMessage& outMessage)
	{
		std::span<const std::byte> payload;
		if (!DecodeHeader(packet, MessageKind::HarvestRequest, payload) || payload.size() != 12)
		{
			return false;
		}
		outMessage.clientId     = ReadU32(payload, 0);
		outMessage.nodeEntityId = ReadU64(payload, 4);
		return true;
	}

	std::vector<std::byte> EncodeHarvestStart(const HarvestStartMessage& message)
	{
		/// Layout: nodeEntityId(8) + harvestDurationTicks(4)
		std::vector<std::byte> packet = BeginPacket(MessageKind::HarvestStart, 12);
		WriteU64(packet, message.nodeEntityId);
		WriteU32(packet, message.harvestDurationTicks);
		return packet;
	}

	bool DecodeHarvestStart(std::span<const std::byte> packet, HarvestStartMessage& outMessage)
	{
		std::span<const std::byte> payload;
		if (!DecodeHeader(packet, MessageKind::HarvestStart, payload) || payload.size() != 12)
		{
			return false;
		}
		outMessage.nodeEntityId         = ReadU64(payload, 0);
		outMessage.harvestDurationTicks = ReadU32(payload, 8);
		return true;
	}

	std::vector<std::byte> EncodeHarvestComplete(const HarvestCompleteMessage& message)
	{
		/// Layout: nodeEntityId(8)
		std::vector<std::byte> packet = BeginPacket(MessageKind::HarvestComplete, 8);
		WriteU64(packet, message.nodeEntityId);
		return packet;
	}

	bool DecodeHarvestComplete(std::span<const std::byte> packet, HarvestCompleteMessage& outMessage)
	{
		std::span<const std::byte> payload;
		if (!DecodeHeader(packet, MessageKind::HarvestComplete, payload) || payload.size() != 8)
		{
			return false;
		}
		outMessage.nodeEntityId = ReadU64(payload, 0);
		return true;
	}

	std::vector<std::byte> EncodeHarvestCancelRequest(const HarvestCancelRequestMessage& message)
	{
		/// Layout: clientId(4)
		std::vector<std::byte> packet = BeginPacket(MessageKind::HarvestCancelRequest, 4);
		WriteU32(packet, message.clientId);
		return packet;
	}

	bool DecodeHarvestCancelRequest(std::span<const std::byte> packet, HarvestCancelRequestMessage& outMessage)
	{
		std::span<const std::byte> payload;
		if (!DecodeHeader(packet, MessageKind::HarvestCancelRequest, payload) || payload.size() != 4)
		{
			return false;
		}
		outMessage.clientId = ReadU32(payload, 0);
		return true;
	}

	std::vector<std::byte> EncodeHarvestCancelled(const HarvestCancelledMessage& message)
	{
		/// Layout: nodeEntityId(8) + reason(1)
		std::vector<std::byte> packet = BeginPacket(MessageKind::HarvestCancelled, 9);
		WriteU64(packet, message.nodeEntityId);
		WriteU8(packet, static_cast<uint8_t>(message.reason));
		return packet;
	}

	bool DecodeHarvestCancelled(std::span<const std::byte> packet, HarvestCancelledMessage& outMessage)
	{
		std::span<const std::byte> payload;
		if (!DecodeHeader(packet, MessageKind::HarvestCancelled, payload) || payload.size() != 9)
		{
			return false;
		}
		outMessage.nodeEntityId = ReadU64(payload, 0);
		outMessage.reason       = static_cast<HarvestCancelReason>(ReadU8(payload, 8));
		return true;
	}

	// -------------------------------------------------------------------------
	// M36.2 — Crafting / profession skill system codec
	// -------------------------------------------------------------------------

	namespace
	{
		/// Encode one ProfessionWireEntry into an output buffer.
		void WriteProfessionEntry(std::vector<std::byte>& out, const ProfessionWireEntry& e)
		{
			WriteSizedString(out, e.professionKey);
			WriteU32(out, e.skillLevel);
			WriteU8(out, e.isPrimary);
		}

		/// Read one ProfessionWireEntry from \p payload at \p offset.
		bool ReadProfessionEntry(std::span<const std::byte> payload, size_t& offset, ProfessionWireEntry& out)
		{
			if (!ReadSizedString(payload, offset, out.professionKey)) return false;
			if ((offset + 5) > payload.size()) return false;
			out.skillLevel = ReadU32(payload, offset); offset += 4;
			out.isPrimary  = ReadU8(payload, offset);  offset += 1;
			return true;
		}

		/// Encoded size (bytes) of one ProfessionWireEntry.
		size_t ProfessionEntrySize(const ProfessionWireEntry& e)
		{
			return 2 + e.professionKey.size() + 4 + 1; // len(2) + key + skill(4) + isPrimary(1)
		}

		/// Encode one CraftRecipeWireRow into an output buffer.
		void WriteRecipeWireRow(std::vector<std::byte>& out, const CraftRecipeWireRow& r)
		{
			WriteSizedString(out, r.recipeId);
			WriteU32(out, r.skillRequired);
			WriteU32(out, r.outputItemId);
			WriteU32(out, r.outputQuantity);
		}

		/// Read one CraftRecipeWireRow from \p payload at \p offset.
		bool ReadRecipeWireRow(std::span<const std::byte> payload, size_t& offset, CraftRecipeWireRow& out)
		{
			if (!ReadSizedString(payload, offset, out.recipeId)) return false;
			if ((offset + 12) > payload.size()) return false;
			out.skillRequired  = ReadU32(payload, offset); offset += 4;
			out.outputItemId   = ReadU32(payload, offset); offset += 4;
			out.outputQuantity = ReadU32(payload, offset); offset += 4;
			return true;
		}
	} // anonymous namespace

	std::vector<std::byte> EncodeLearnProfessionRequest(const LearnProfessionRequestMessage& message)
	{
		/// Layout: clientId(4) + professionKey(2+n) + asPrimary(1)
		const size_t keyLen = message.professionKey.size();
		std::vector<std::byte> packet = BeginPacket(MessageKind::LearnProfessionRequest, 4 + 2 + keyLen + 1);
		WriteU32(packet, message.clientId);
		WriteSizedString(packet, message.professionKey);
		WriteU8(packet, message.asPrimary);
		return packet;
	}

	bool DecodeLearnProfessionRequest(std::span<const std::byte> packet, LearnProfessionRequestMessage& outMessage)
	{
		std::span<const std::byte> payload;
		if (!DecodeHeader(packet, MessageKind::LearnProfessionRequest, payload) || payload.size() < 7)
			return false;
		outMessage.clientId = ReadU32(payload, 0);
		size_t offset = 4;
		if (!ReadSizedString(payload, offset, outMessage.professionKey)) return false;
		if (offset >= payload.size()) return false;
		outMessage.asPrimary = ReadU8(payload, offset);
		return true;
	}

	std::vector<std::byte> EncodeProfessionUpdate(const ProfessionUpdateMessage& message)
	{
		/// Layout: clientId(4) + count(1) + entries(variable)
		size_t payloadSize = 4 + 1;
		for (const ProfessionWireEntry& e : message.professions)
			payloadSize += ProfessionEntrySize(e);
		std::vector<std::byte> packet = BeginPacket(MessageKind::ProfessionUpdate, payloadSize);
		WriteU32(packet, message.clientId);
		WriteU8(packet, static_cast<uint8_t>(std::min(message.professions.size(), static_cast<size_t>(255u))));
		for (const ProfessionWireEntry& e : message.professions)
			WriteProfessionEntry(packet, e);
		return packet;
	}

	bool DecodeProfessionUpdate(std::span<const std::byte> packet, ProfessionUpdateMessage& outMessage)
	{
		std::span<const std::byte> payload;
		if (!DecodeHeader(packet, MessageKind::ProfessionUpdate, payload) || payload.size() < 5)
			return false;
		outMessage.clientId = ReadU32(payload, 0);
		const uint8_t count = ReadU8(payload, 4);
		size_t offset = 5;
		outMessage.professions.clear();
		outMessage.professions.reserve(count);
		for (uint8_t i = 0; i < count; ++i)
		{
			ProfessionWireEntry e;
			if (!ReadProfessionEntry(payload, offset, e)) return false;
			outMessage.professions.push_back(std::move(e));
		}
		return true;
	}

	std::vector<std::byte> EncodeCraftRecipeListRequest(const CraftRecipeListRequestMessage& message)
	{
		/// Layout: clientId(4) + professionKey(2+n)
		const size_t keyLen = message.professionKey.size();
		std::vector<std::byte> packet = BeginPacket(MessageKind::CraftRecipeListRequest, 4 + 2 + keyLen);
		WriteU32(packet, message.clientId);
		WriteSizedString(packet, message.professionKey);
		return packet;
	}

	bool DecodeCraftRecipeListRequest(std::span<const std::byte> packet, CraftRecipeListRequestMessage& outMessage)
	{
		std::span<const std::byte> payload;
		if (!DecodeHeader(packet, MessageKind::CraftRecipeListRequest, payload) || payload.size() < 6)
			return false;
		outMessage.clientId = ReadU32(payload, 0);
		size_t offset = 4;
		return ReadSizedString(payload, offset, outMessage.professionKey);
	}

	std::vector<std::byte> EncodeCraftRecipeListResult(const CraftRecipeListResultMessage& message)
	{
		/// Layout: clientId(4) + professionKey(2+n) + rowCount(2) + rows(variable)
		size_t payloadSize = 4 + 2 + message.professionKey.size() + 2;
		for (const CraftRecipeWireRow& r : message.recipes)
			payloadSize += 2 + r.recipeId.size() + 12;
		std::vector<std::byte> packet = BeginPacket(MessageKind::CraftRecipeListResult, payloadSize);
		WriteU32(packet, message.clientId);
		WriteSizedString(packet, message.professionKey);
		const uint16_t count = static_cast<uint16_t>(std::min(message.recipes.size(), static_cast<size_t>(kMaxCraftRecipeListRows)));
		WriteU16(packet, count);
		for (uint16_t i = 0; i < count; ++i)
			WriteRecipeWireRow(packet, message.recipes[i]);
		return packet;
	}

	bool DecodeCraftRecipeListResult(std::span<const std::byte> packet, CraftRecipeListResultMessage& outMessage)
	{
		std::span<const std::byte> payload;
		if (!DecodeHeader(packet, MessageKind::CraftRecipeListResult, payload) || payload.size() < 8)
			return false;
		outMessage.clientId = ReadU32(payload, 0);
		size_t offset = 4;
		if (!ReadSizedString(payload, offset, outMessage.professionKey)) return false;
		if ((offset + 2) > payload.size()) return false;
		const uint16_t count = ReadU16(payload, offset); offset += 2;
		if (count > kMaxCraftRecipeListRows) return false;
		outMessage.recipes.clear();
		outMessage.recipes.reserve(count);
		for (uint16_t i = 0; i < count; ++i)
		{
			CraftRecipeWireRow row;
			if (!ReadRecipeWireRow(payload, offset, row)) return false;
			outMessage.recipes.push_back(std::move(row));
		}
		return true;
	}

	std::vector<std::byte> EncodeCraftRequest(const CraftRequestMessage& message)
	{
		/// Layout: clientId(4) + recipeId(2+n)
		const size_t idLen = message.recipeId.size();
		std::vector<std::byte> packet = BeginPacket(MessageKind::CraftRequest, 4 + 2 + idLen);
		WriteU32(packet, message.clientId);
		WriteSizedString(packet, message.recipeId);
		return packet;
	}

	bool DecodeCraftRequest(std::span<const std::byte> packet, CraftRequestMessage& outMessage)
	{
		std::span<const std::byte> payload;
		if (!DecodeHeader(packet, MessageKind::CraftRequest, payload) || payload.size() < 6)
			return false;
		outMessage.clientId = ReadU32(payload, 0);
		size_t offset = 4;
		return ReadSizedString(payload, offset, outMessage.recipeId);
	}

	std::vector<std::byte> EncodeCraftStart(const CraftStartMessage& message)
	{
		/// Layout: recipeId(2+n) + durationTicks(4)
		const size_t idLen = message.recipeId.size();
		std::vector<std::byte> packet = BeginPacket(MessageKind::CraftStart, 2 + idLen + 4);
		WriteSizedString(packet, message.recipeId);
		WriteU32(packet, message.durationTicks);
		return packet;
	}

	bool DecodeCraftStart(std::span<const std::byte> packet, CraftStartMessage& outMessage)
	{
		std::span<const std::byte> payload;
		if (!DecodeHeader(packet, MessageKind::CraftStart, payload) || payload.size() < 6)
			return false;
		size_t offset = 0;
		if (!ReadSizedString(payload, offset, outMessage.recipeId)) return false;
		if ((offset + 4) > payload.size()) return false;
		outMessage.durationTicks = ReadU32(payload, offset);
		return true;
	}

	std::vector<std::byte> EncodeCraftComplete(const CraftCompleteMessage& message)
	{
		/// Layout: recipeId(2+n) + skillGained(1) + newSkillLevel(4) + qualityTier(1) [M36.3]
		const size_t idLen = message.recipeId.size();
		std::vector<std::byte> packet = BeginPacket(MessageKind::CraftComplete, 2 + idLen + 1 + 4 + 1);
		WriteSizedString(packet, message.recipeId);
		WriteU8(packet, message.skillGained);
		WriteU32(packet, message.newSkillLevel);
		WriteU8(packet, message.qualityTier);
		return packet;
	}

	bool DecodeCraftComplete(std::span<const std::byte> packet, CraftCompleteMessage& outMessage)
	{
		std::span<const std::byte> payload;
		if (!DecodeHeader(packet, MessageKind::CraftComplete, payload) || payload.size() < 8)
			return false;
		size_t offset = 0;
		if (!ReadSizedString(payload, offset, outMessage.recipeId)) return false;
		if ((offset + 6) > payload.size()) return false;
		outMessage.skillGained   = ReadU8(payload, offset);  offset += 1;
		outMessage.newSkillLevel = ReadU32(payload, offset);  offset += 4;
		outMessage.qualityTier   = ReadU8(payload, offset);
		return true;
	}

	std::vector<std::byte> EncodeCraftCancelRequest(const CraftCancelRequestMessage& message)
	{
		/// Layout: clientId(4)
		std::vector<std::byte> packet = BeginPacket(MessageKind::CraftCancelRequest, 4);
		WriteU32(packet, message.clientId);
		return packet;
	}

	bool DecodeCraftCancelRequest(std::span<const std::byte> packet, CraftCancelRequestMessage& outMessage)
	{
		std::span<const std::byte> payload;
		if (!DecodeHeader(packet, MessageKind::CraftCancelRequest, payload) || payload.size() != 4)
			return false;
		outMessage.clientId = ReadU32(payload, 0);
		return true;
	}

	std::vector<std::byte> EncodeCraftCancelled(const CraftCancelledMessage& message)
	{
		/// Layout: recipeId(2+n)
		const size_t idLen = message.recipeId.size();
		std::vector<std::byte> packet = BeginPacket(MessageKind::CraftCancelled, 2 + idLen);
		WriteSizedString(packet, message.recipeId);
		return packet;
	}

	bool DecodeCraftCancelled(std::span<const std::byte> packet, CraftCancelledMessage& outMessage)
	{
		std::span<const std::byte> payload;
		if (!DecodeHeader(packet, MessageKind::CraftCancelled, payload) || payload.size() < 2)
			return false;
		size_t offset = 0;
		return ReadSizedString(payload, offset, outMessage.recipeId);
	}
}
