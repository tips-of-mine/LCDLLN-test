// ETAPE3.2 (chantiers 4.2, 4.5, 4.6) — snapshots en delta sur base acquittée.
// Voir SnapshotDelta.h pour le principe.
//
// Charge utile d'un morceau (petit-boutiste) :
//   en-tête (34 o) : clientId u32 | serverTick u32 | snapshotSeq u32 | baselineSeq u32 |
//                    connectedClients u16 | chunkIndex u16 | chunkCount u16 |
//                    receivedPackets u32 | sentPackets u32 | removedCount u16 | entryCount u16
//   retraits       : removedCount × entityId (varint)
//   entrées        : entryCount × { entityId varint | masque u8 | champs du masque }
//     position  : 3 × entier zigzag varint (cm) — delta si l'entité est dans la base,
//                 valeur absolue sinon
//     yaw       : u16
//     vitesse   : 3 × entier zigzag varint (cm/s), valeur absolue
//     PV, PV max, drapeaux : varint ; animation : u8
//     identité  : playerClientId varint | archetypeId varint | nom (u8 + octets) |
//                 genre (u8 + octets)
// Une entité absente de la base doit arriver avec le masque complet.

#include "src/shared/network/SnapshotDelta.h"

#include "src/shared/network/ServerProtocol.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace engine::network::snapshot
{
	namespace
	{
		constexpr double kTwoPi = 6.283185307179586;
		constexpr size_t kMaxNameBytes = 64u;
		constexpr size_t kMaxGenderBytes = 8u;

		// --- Écriture -----------------------------------------------------------

		void PutU8(std::vector<std::byte>& out, uint8_t v)
		{
			out.push_back(static_cast<std::byte>(v));
		}

		void PutU16(std::vector<std::byte>& out, uint16_t v)
		{
			PutU8(out, static_cast<uint8_t>(v & 0xFFu));
			PutU8(out, static_cast<uint8_t>((v >> 8) & 0xFFu));
		}

		void PutU32(std::vector<std::byte>& out, uint32_t v)
		{
			for (int i = 0; i < 4; ++i)
				PutU8(out, static_cast<uint8_t>((v >> (8 * i)) & 0xFFu));
		}

		void PutVarU64(std::vector<std::byte>& out, uint64_t v)
		{
			while (v >= 0x80u)
			{
				PutU8(out, static_cast<uint8_t>((v & 0x7Fu) | 0x80u));
				v >>= 7;
			}
			PutU8(out, static_cast<uint8_t>(v));
		}

		void PutZigZag(std::vector<std::byte>& out, int64_t v)
		{
			const uint64_t zz = (static_cast<uint64_t>(v) << 1) ^ static_cast<uint64_t>(v >> 63);
			PutVarU64(out, zz);
		}

		void PutShortString(std::vector<std::byte>& out, const std::string& s, size_t maxBytes)
		{
			const size_t n = (std::min)(s.size(), maxBytes);
			PutU8(out, static_cast<uint8_t>(n));
			for (size_t i = 0; i < n; ++i)
				PutU8(out, static_cast<uint8_t>(s[i]));
		}

		// --- Lecture ------------------------------------------------------------

		struct Reader
		{
			std::span<const std::byte> bytes;
			size_t offset = 0;

			bool U8(uint8_t& v)
			{
				if (offset + 1u > bytes.size())
					return false;
				v = static_cast<uint8_t>(bytes[offset++]);
				return true;
			}

			bool U16(uint16_t& v)
			{
				uint8_t a = 0, b = 0;
				if (!U8(a) || !U8(b))
					return false;
				v = static_cast<uint16_t>(a | (static_cast<uint16_t>(b) << 8));
				return true;
			}

			bool U32(uint32_t& v)
			{
				v = 0;
				for (int i = 0; i < 4; ++i)
				{
					uint8_t b = 0;
					if (!U8(b))
						return false;
					v |= static_cast<uint32_t>(b) << (8 * i);
				}
				return true;
			}

			bool VarU64(uint64_t& v)
			{
				v = 0;
				for (int shift = 0; shift < 64; shift += 7)
				{
					uint8_t b = 0;
					if (!U8(b))
						return false;
					v |= static_cast<uint64_t>(b & 0x7Fu) << shift;
					if ((b & 0x80u) == 0u)
						return true;
				}
				return false; // plus de 10 octets : mal formé
			}

			bool VarU32(uint32_t& v)
			{
				uint64_t w = 0;
				if (!VarU64(w) || w > 0xFFFFFFFFull)
					return false;
				v = static_cast<uint32_t>(w);
				return true;
			}

			bool ZigZag(int64_t& v)
			{
				uint64_t zz = 0;
				if (!VarU64(zz))
					return false;
				v = static_cast<int64_t>(zz >> 1) ^ -static_cast<int64_t>(zz & 1u);
				return true;
			}

			bool ZigZag32(int32_t& v)
			{
				int64_t w = 0;
				if (!ZigZag(w) || w < (std::numeric_limits<int32_t>::min)() || w > (std::numeric_limits<int32_t>::max)())
					return false;
				v = static_cast<int32_t>(w);
				return true;
			}

			bool ShortString(std::string& s, size_t maxBytes)
			{
				uint8_t n = 0;
				if (!U8(n) || n > maxBytes || offset + n > bytes.size())
					return false;
				s.assign(reinterpret_cast<const char*>(bytes.data() + offset), n);
				offset += n;
				return true;
			}
		};

		int32_t QuantizeMeters(float meters, float scale)
		{
			if (!std::isfinite(meters))
				return 0;
			const double v = std::round(static_cast<double>(meters) * scale);
			constexpr double lo = static_cast<double>((std::numeric_limits<int32_t>::min)());
			constexpr double hi = static_cast<double>((std::numeric_limits<int32_t>::max)());
			return static_cast<int32_t>(std::clamp(v, lo, hi));
		}

		uint16_t QuantizeYaw(float radians)
		{
			if (!std::isfinite(radians))
				return 0;
			double turns = static_cast<double>(radians) / kTwoPi;
			turns -= std::floor(turns); // [0, 1)
			const uint32_t step = static_cast<uint32_t>(std::lround(turns * 65536.0)) & 0xFFFFu;
			return static_cast<uint16_t>(step);
		}

		float DequantizeYaw(uint16_t yaw)
		{
			double radians = static_cast<double>(yaw) * (kTwoPi / 65536.0);
			if (radians > kTwoPi / 2.0)
				radians -= kTwoPi;
			return static_cast<float>(radians);
		}

		/// Masque des champs qui diffèrent entre \p cur et \p base.
		uint8_t ChangedFields(const WireEntity& cur, const WireEntity& base)
		{
			uint8_t mask = 0;
			if (cur.positionCmX != base.positionCmX || cur.positionCmY != base.positionCmY
				|| cur.positionCmZ != base.positionCmZ)
				mask |= kFieldPosition;
			if (cur.yaw != base.yaw)
				mask |= kFieldYaw;
			if (cur.velocityCmX != base.velocityCmX || cur.velocityCmY != base.velocityCmY
				|| cur.velocityCmZ != base.velocityCmZ)
				mask |= kFieldVelocity;
			if (cur.currentHealth != base.currentHealth)
				mask |= kFieldHealth;
			if (cur.maxHealth != base.maxHealth)
				mask |= kFieldMaxHealth;
			if (cur.stateFlags != base.stateFlags)
				mask |= kFieldStateFlags;
			if (cur.animationState != base.animationState)
				mask |= kFieldAnimation;
			if (cur.playerClientId != base.playerClientId || cur.archetypeId != base.archetypeId
				|| cur.characterName != base.characterName || cur.gender != base.gender)
				mask |= kFieldIdentity;
			return mask;
		}

		/// Écrit une entrée. \p base = état dans la base (nullptr = entité nouvelle,
		/// positions absolues).
		void WriteEntry(std::vector<std::byte>& out, const WireEntity& cur, const WireEntity* base, uint8_t mask)
		{
			PutVarU64(out, cur.entityId);
			PutU8(out, mask);
			if ((mask & kFieldPosition) != 0u)
			{
				const int64_t bx = base ? base->positionCmX : 0;
				const int64_t by = base ? base->positionCmY : 0;
				const int64_t bz = base ? base->positionCmZ : 0;
				PutZigZag(out, static_cast<int64_t>(cur.positionCmX) - bx);
				PutZigZag(out, static_cast<int64_t>(cur.positionCmY) - by);
				PutZigZag(out, static_cast<int64_t>(cur.positionCmZ) - bz);
			}
			if ((mask & kFieldYaw) != 0u)
				PutU16(out, cur.yaw);
			if ((mask & kFieldVelocity) != 0u)
			{
				PutZigZag(out, cur.velocityCmX);
				PutZigZag(out, cur.velocityCmY);
				PutZigZag(out, cur.velocityCmZ);
			}
			if ((mask & kFieldHealth) != 0u)
				PutVarU64(out, cur.currentHealth);
			if ((mask & kFieldMaxHealth) != 0u)
				PutVarU64(out, cur.maxHealth);
			if ((mask & kFieldStateFlags) != 0u)
				PutVarU64(out, cur.stateFlags);
			if ((mask & kFieldAnimation) != 0u)
				PutU8(out, cur.animationState);
			if ((mask & kFieldIdentity) != 0u)
			{
				PutVarU64(out, cur.playerClientId);
				PutVarU64(out, cur.archetypeId);
				PutShortString(out, cur.characterName, kMaxNameBytes);
				PutShortString(out, cur.gender, kMaxGenderBytes);
			}
		}

		bool ReadEntry(Reader& r, EntityUpdate& u)
		{
			if (!r.VarU64(u.values.entityId) || !r.U8(u.mask))
				return false;
			if ((u.mask & kFieldPosition) != 0u)
			{
				if (!r.ZigZag(u.position[0]) || !r.ZigZag(u.position[1]) || !r.ZigZag(u.position[2]))
					return false;
			}
			if ((u.mask & kFieldYaw) != 0u && !r.U16(u.values.yaw))
				return false;
			if ((u.mask & kFieldVelocity) != 0u)
			{
				if (!r.ZigZag32(u.values.velocityCmX) || !r.ZigZag32(u.values.velocityCmY)
					|| !r.ZigZag32(u.values.velocityCmZ))
					return false;
			}
			if ((u.mask & kFieldHealth) != 0u && !r.VarU32(u.values.currentHealth))
				return false;
			if ((u.mask & kFieldMaxHealth) != 0u && !r.VarU32(u.values.maxHealth))
				return false;
			if ((u.mask & kFieldStateFlags) != 0u && !r.VarU32(u.values.stateFlags))
				return false;
			if ((u.mask & kFieldAnimation) != 0u && !r.U8(u.values.animationState))
				return false;
			if ((u.mask & kFieldIdentity) != 0u)
			{
				if (!r.VarU32(u.values.playerClientId) || !r.VarU32(u.values.archetypeId)
					|| !r.ShortString(u.values.characterName, kMaxNameBytes)
					|| !r.ShortString(u.values.gender, kMaxGenderBytes))
					return false;
			}
			return true;
		}

		bool ById(const WireEntity& a, const WireEntity& b)
		{
			return a.entityId < b.entityId;
		}

		const WireEntity* FindInFrame(const Frame& frame, engine::server::EntityId id)
		{
			const auto it = std::lower_bound(frame.entities.begin(), frame.entities.end(), id,
				[](const WireEntity& e, engine::server::EntityId v) { return e.entityId < v; });
			return (it != frame.entities.end() && it->entityId == id) ? &*it : nullptr;
		}
	}

	bool WireEntity::SameWireState(const WireEntity& other) const
	{
		return entityId == other.entityId && ChangedFields(*this, other) == 0u;
	}

	WireEntity Quantize(const engine::server::SnapshotEntity& entity)
	{
		WireEntity w{};
		w.entityId = entity.entityId;
		w.positionCmX = QuantizeMeters(entity.state.positionX, 100.0f);
		w.positionCmY = QuantizeMeters(entity.state.positionY, 100.0f);
		w.positionCmZ = QuantizeMeters(entity.state.positionZ, 100.0f);
		w.yaw = QuantizeYaw(entity.state.yawRadians);
		w.velocityCmX = QuantizeMeters(entity.state.velocityX, 100.0f);
		w.velocityCmY = QuantizeMeters(entity.state.velocityY, 100.0f);
		w.velocityCmZ = QuantizeMeters(entity.state.velocityZ, 100.0f);
		w.currentHealth = entity.state.currentHealth;
		w.maxHealth = entity.state.maxHealth;
		w.stateFlags = entity.state.stateFlags;
		w.animationState = static_cast<uint8_t>(entity.animationState);
		w.playerClientId = entity.playerClientId;
		w.archetypeId = entity.archetypeId;
		w.characterName = entity.characterName.substr(0, kMaxNameBytes);
		w.gender = entity.gender.substr(0, kMaxGenderBytes);
		return w;
	}

	engine::server::SnapshotEntity Dequantize(const WireEntity& w)
	{
		engine::server::SnapshotEntity e{};
		e.entityId = w.entityId;
		e.state.positionX = static_cast<float>(static_cast<double>(w.positionCmX) / 100.0);
		e.state.positionY = static_cast<float>(static_cast<double>(w.positionCmY) / 100.0);
		e.state.positionZ = static_cast<float>(static_cast<double>(w.positionCmZ) / 100.0);
		e.state.yawRadians = DequantizeYaw(w.yaw);
		e.state.velocityX = static_cast<float>(static_cast<double>(w.velocityCmX) / 100.0);
		e.state.velocityY = static_cast<float>(static_cast<double>(w.velocityCmY) / 100.0);
		e.state.velocityZ = static_cast<float>(static_cast<double>(w.velocityCmZ) / 100.0);
		e.state.currentHealth = w.currentHealth;
		e.state.maxHealth = w.maxHealth;
		e.state.stateFlags = w.stateFlags;
		e.animationState = static_cast<engine::server::AvatarAnimState>(w.animationState);
		e.playerClientId = w.playerClientId;
		e.archetypeId = w.archetypeId;
		e.characterName = w.characterName;
		e.gender = w.gender;
		return e;
	}

	std::vector<std::vector<std::byte>> EncodeSnapshotDelta(const ChunkHeader& headerTemplate,
		const std::vector<WireEntity>& current, const Frame* baseline, size_t chunkPayloadBudget)
	{
		// Un morceau doit pouvoir contenir au moins l'en-tête et une grosse entrée.
		const size_t budget = (std::max)(chunkPayloadBudget, kChunkHeaderSize + 256u);

		struct ChunkBody
		{
			std::vector<std::byte> removed;
			std::vector<std::byte> entries;
			uint16_t removedCount = 0;
			uint16_t entryCount = 0;
			size_t Size() const { return kChunkHeaderSize + removed.size() + entries.size(); }
		};
		std::vector<ChunkBody> chunks(1);
		std::vector<std::byte> item;

		auto place = [&](bool isRemoval) {
			ChunkBody* body = &chunks.back();
			const bool full = body->Size() + item.size() > budget;
			const bool hasItems = body->removedCount != 0u || body->entryCount != 0u;
			const bool countFull = isRemoval ? body->removedCount == 0xFFFFu : body->entryCount == 0xFFFFu;
			if ((full && hasItems) || countFull)
			{
				chunks.emplace_back();
				body = &chunks.back();
			}
			auto& dst = isRemoval ? body->removed : body->entries;
			dst.insert(dst.end(), item.begin(), item.end());
			if (isRemoval)
				++body->removedCount;
			else
				++body->entryCount;
		};

		// Retraits : entités de la base absentes du snapshot courant (deux listes triées).
		if (baseline != nullptr)
		{
			size_t ci = 0;
			for (const WireEntity& b : baseline->entities)
			{
				while (ci < current.size() && current[ci].entityId < b.entityId)
					++ci;
				if (ci < current.size() && current[ci].entityId == b.entityId)
					continue;
				item.clear();
				PutVarU64(item, b.entityId);
				place(true);
			}
		}

		// Entrées : nouvelles entités (masque complet) ou champs modifiés.
		for (const WireEntity& cur : current)
		{
			const WireEntity* base = baseline ? FindInFrame(*baseline, cur.entityId) : nullptr;
			const uint8_t mask = base ? ChangedFields(cur, *base) : kFieldAll;
			if (mask == 0u)
				continue;
			item.clear();
			WriteEntry(item, cur, base, mask);
			place(false);
		}

		// Trop gros pour le décodeur : on n'envoie RIEN plutôt qu'un snapshot tronqué
		// (le client reconstruirait un état différent de celui gardé par le shard).
		if (chunks.size() > kMaxChunksPerSnapshot)
			return {};
		const size_t count = chunks.size();
		std::vector<std::vector<std::byte>> packets;
		packets.reserve(count);
		for (size_t i = 0; i < count; ++i)
		{
			const ChunkBody& body = chunks[i];
			std::vector<std::byte> packet = engine::server::BeginProtocolMessage(
				engine::server::MessageKind::Snapshot, body.Size());
			PutU32(packet, headerTemplate.clientId);
			PutU32(packet, headerTemplate.serverTick);
			PutU32(packet, headerTemplate.snapshotSeq);
			PutU32(packet, baseline ? baseline->seq : 0u);
			PutU16(packet, headerTemplate.connectedClients);
			PutU16(packet, static_cast<uint16_t>(i));
			PutU16(packet, static_cast<uint16_t>(count));
			PutU32(packet, headerTemplate.receivedPackets);
			PutU32(packet, headerTemplate.sentPackets);
			PutU16(packet, body.removedCount);
			PutU16(packet, body.entryCount);
			packet.insert(packet.end(), body.removed.begin(), body.removed.end());
			packet.insert(packet.end(), body.entries.begin(), body.entries.end());
			packets.push_back(std::move(packet));
		}
		return packets;
	}

	bool DecodeSnapshotChunk(std::span<const std::byte> packet, DecodedChunk& outChunk)
	{
		std::span<const std::byte> payload;
		if (!engine::server::DecodeProtocolPayload(packet, engine::server::MessageKind::Snapshot, payload)
			|| payload.size() < kChunkHeaderSize)
		{
			return false;
		}
		Reader r{ payload, 0 };
		ChunkHeader& h = outChunk.header;
		uint16_t removedCount = 0;
		uint16_t entryCount = 0;
		if (!r.U32(h.clientId) || !r.U32(h.serverTick) || !r.U32(h.snapshotSeq) || !r.U32(h.baselineSeq)
			|| !r.U16(h.connectedClients) || !r.U16(h.chunkIndex) || !r.U16(h.chunkCount)
			|| !r.U32(h.receivedPackets) || !r.U32(h.sentPackets) || !r.U16(removedCount) || !r.U16(entryCount))
		{
			return false;
		}
		if (h.snapshotSeq == 0u || h.chunkCount == 0u || h.chunkCount > kMaxChunksPerSnapshot
			|| h.chunkIndex >= h.chunkCount || h.baselineSeq >= h.snapshotSeq)
		{
			return false;
		}
		// Chaque retrait fait au moins 1 octet, chaque entrée au moins 2 : borne avant
		// de réserver (un compteur mensonger ne fait pas allouer).
		const size_t remaining = payload.size() - r.offset;
		if (static_cast<size_t>(removedCount) + 2u * static_cast<size_t>(entryCount) > remaining)
			return false;
		outChunk.removed.clear();
		outChunk.removed.reserve(removedCount);
		for (uint16_t i = 0; i < removedCount; ++i)
		{
			uint64_t id = 0;
			if (!r.VarU64(id))
				return false;
			outChunk.removed.push_back(id);
		}
		outChunk.updates.clear();
		outChunk.updates.resize(entryCount);
		for (uint16_t i = 0; i < entryCount; ++i)
		{
			if (!ReadEntry(r, outChunk.updates[i]))
				return false;
		}
		return r.offset == payload.size();
	}

	// --- SnapshotSender -----------------------------------------------------

	const Frame* SnapshotSender::FindFrame(uint32_t seq) const
	{
		if (seq == 0u)
			return nullptr;
		const Frame& f = m_history[seq % kHistorySize];
		return f.seq == seq ? &f : nullptr;
	}

	void SnapshotSender::OnAck(uint32_t seq)
	{
		if (seq > m_lastAcked && seq < m_nextSeq)
			m_lastAcked = seq;
	}

	std::vector<std::vector<std::byte>> SnapshotSender::BuildPackets(const ChunkHeader& headerTemplate,
		std::vector<WireEntity> current, size_t chunkPayloadBudget)
	{
		std::sort(current.begin(), current.end(), ById);
		// Doublons d'identifiant (ne devrait pas arriver) : on garde le premier.
		current.erase(std::unique(current.begin(), current.end(),
			[](const WireEntity& a, const WireEntity& b) { return a.entityId == b.entityId; }), current.end());
		if (current.size() > kMaxEntitiesPerSnapshot)
			current.resize(kMaxEntitiesPerSnapshot);

		const uint32_t seq = m_nextSeq;
		const Frame* baseline = FindFrame(m_lastAcked);

		// Chantier 4.5 — les entités de faible priorité ne changent qu'un snapshot sur
		// deux : les snapshots impairs reprennent leur état de la base (donc rien à
		// envoyer pour elles).
		if (baseline != nullptr && (seq % 2u) == 1u)
		{
			for (WireEntity& e : current)
			{
				if (!e.halfRate)
					continue;
				if (const WireEntity* base = FindInFrame(*baseline, e.entityId))
				{
					e = *base;
					e.halfRate = true;
				}
			}
		}

		ChunkHeader header = headerTemplate;
		header.snapshotSeq = seq;
		std::vector<std::vector<std::byte>> packets = EncodeSnapshotDelta(header, current, baseline, chunkPayloadBudget);
		if (packets.empty())
			return packets; // rien envoyé : le numéro n'est pas consommé, la base reste
		m_lastBaseline = baseline ? baseline->seq : 0u;

		Frame& slot = m_history[seq % kHistorySize];
		slot.seq = seq;
		slot.entities = std::move(current);
		++m_nextSeq;
		if (m_nextSeq == 0u) // 2^32 snapshots : repartir proprement (jamais en pratique)
			Reset();
		return packets;
	}

	void SnapshotSender::Reset()
	{
		for (Frame& f : m_history)
		{
			f.seq = 0;
			f.entities.clear();
		}
		m_nextSeq = 1;
		m_lastAcked = 0;
		m_lastBaseline = 0;
	}

	// --- SnapshotReceiver ---------------------------------------------------

	const Frame* SnapshotReceiver::FindFrame(uint32_t seq) const
	{
		if (seq == 0u)
			return nullptr;
		const Frame& f = m_history[seq % kHistorySize];
		return f.seq == seq ? &f : nullptr;
	}

	void SnapshotReceiver::Reset()
	{
		for (Frame& f : m_history)
		{
			f.seq = 0;
			f.entities.clear();
		}
		m_latestCommitted = 0;
		m_assembling = false;
		m_chunksSeen.clear();
		m_chunksReceived = 0;
		m_assemblyChunks.clear();
	}

	SnapshotReceiver::Result SnapshotReceiver::Apply(std::span<const std::byte> packet,
		std::vector<engine::server::SnapshotEntity>& outEntities, ChunkHeader& outHeader)
	{
		DecodedChunk chunk;
		if (!DecodeSnapshotChunk(packet, chunk))
		{
			++m_dropped;
			return Result::Dropped;
		}
		const ChunkHeader& h = chunk.header;
		if (h.snapshotSeq <= m_latestCommitted)
			return Result::Dropped; // plus ancien que ce qu'on a déjà : inutile

		if (m_assembling && h.snapshotSeq < m_assemblyHeader.snapshotSeq)
			return Result::Dropped; // morceau en retard d'un snapshot déjà remplacé
		if (!m_assembling || h.snapshotSeq != m_assemblyHeader.snapshotSeq)
		{
			if (m_assembling)
				++m_dropped; // snapshot précédent incomplet : remplacé par le plus récent
			m_assembling = true;
			m_assemblyHeader = h;
			m_chunksSeen.assign(h.chunkCount, false);
			m_chunksReceived = 0;
			m_assemblyChunks.clear();
		}
		// Les morceaux d'un même snapshot doivent s'accorder.
		if (h.chunkCount != m_assemblyHeader.chunkCount || h.baselineSeq != m_assemblyHeader.baselineSeq)
		{
			++m_dropped;
			m_assembling = false;
			return Result::Dropped;
		}
		if (m_chunksSeen[h.chunkIndex])
			return Result::Pending; // doublon
		m_chunksSeen[h.chunkIndex] = true;
		++m_chunksReceived;
		m_assemblyChunks.push_back(std::move(chunk));
		if (m_chunksReceived < m_assemblyHeader.chunkCount)
			return Result::Pending;

		m_assembling = false;
		if (!Commit(outEntities, outHeader))
		{
			++m_dropped;
			m_assemblyChunks.clear();
			return Result::Dropped;
		}
		m_assemblyChunks.clear();
		return Result::Committed;
	}

	bool SnapshotReceiver::Commit(std::vector<engine::server::SnapshotEntity>& outEntities, ChunkHeader& outHeader)
	{
		const ChunkHeader& h = m_assemblyHeader;
		const Frame* baseline = nullptr;
		if (h.baselineSeq != 0u)
		{
			baseline = FindFrame(h.baselineSeq);
			if (baseline == nullptr)
				return false; // base inconnue (trop ancienne) : le shard enverra mieux
		}

		Frame next;
		next.seq = h.snapshotSeq;
		if (baseline != nullptr)
			next.entities = baseline->entities;

		// Retraits puis mises à jour, par identifiant (listes courtes : recherche
		// binaire dans la copie triée de la base).
		for (const DecodedChunk& c : m_assemblyChunks)
		{
			for (engine::server::EntityId id : c.removed)
			{
				const auto it = std::lower_bound(next.entities.begin(), next.entities.end(), id,
					[](const WireEntity& e, engine::server::EntityId v) { return e.entityId < v; });
				if (it != next.entities.end() && it->entityId == id)
					next.entities.erase(it);
			}
		}
		std::vector<WireEntity> added;
		for (const DecodedChunk& c : m_assemblyChunks)
		{
			for (const EntityUpdate& u : c.updates)
			{
				const engine::server::EntityId id = u.values.entityId;
				const WireEntity* inBase = baseline ? FindInFrame(*baseline, id) : nullptr;
				WireEntity* target = nullptr;
				const auto it = std::lower_bound(next.entities.begin(), next.entities.end(), id,
					[](const WireEntity& e, engine::server::EntityId v) { return e.entityId < v; });
				if (it != next.entities.end() && it->entityId == id)
					target = &*it;
				if (inBase == nullptr)
				{
					// Entité nouvelle : tous les champs, positions absolues.
					if (u.mask != kFieldAll)
						return false;
					WireEntity e = u.values;
					if (u.position[0] < (std::numeric_limits<int32_t>::min)() || u.position[0] > (std::numeric_limits<int32_t>::max)()
						|| u.position[1] < (std::numeric_limits<int32_t>::min)() || u.position[1] > (std::numeric_limits<int32_t>::max)()
						|| u.position[2] < (std::numeric_limits<int32_t>::min)() || u.position[2] > (std::numeric_limits<int32_t>::max)())
						return false;
					e.positionCmX = static_cast<int32_t>(u.position[0]);
					e.positionCmY = static_cast<int32_t>(u.position[1]);
					e.positionCmZ = static_cast<int32_t>(u.position[2]);
					added.push_back(std::move(e));
					continue;
				}
				if (target == nullptr)
					return false; // listée retirée ET mise à jour : incohérent
				WireEntity& e = *target;
				if ((u.mask & kFieldPosition) != 0u)
				{
					const int64_t x = static_cast<int64_t>(inBase->positionCmX) + u.position[0];
					const int64_t y = static_cast<int64_t>(inBase->positionCmY) + u.position[1];
					const int64_t z = static_cast<int64_t>(inBase->positionCmZ) + u.position[2];
					constexpr int64_t lo = (std::numeric_limits<int32_t>::min)();
					constexpr int64_t hi = (std::numeric_limits<int32_t>::max)();
					if (x < lo || x > hi || y < lo || y > hi || z < lo || z > hi)
						return false;
					e.positionCmX = static_cast<int32_t>(x);
					e.positionCmY = static_cast<int32_t>(y);
					e.positionCmZ = static_cast<int32_t>(z);
				}
				if ((u.mask & kFieldYaw) != 0u)
					e.yaw = u.values.yaw;
				if ((u.mask & kFieldVelocity) != 0u)
				{
					e.velocityCmX = u.values.velocityCmX;
					e.velocityCmY = u.values.velocityCmY;
					e.velocityCmZ = u.values.velocityCmZ;
				}
				if ((u.mask & kFieldHealth) != 0u)
					e.currentHealth = u.values.currentHealth;
				if ((u.mask & kFieldMaxHealth) != 0u)
					e.maxHealth = u.values.maxHealth;
				if ((u.mask & kFieldStateFlags) != 0u)
					e.stateFlags = u.values.stateFlags;
				if ((u.mask & kFieldAnimation) != 0u)
					e.animationState = u.values.animationState;
				if ((u.mask & kFieldIdentity) != 0u)
				{
					e.playerClientId = u.values.playerClientId;
					e.archetypeId = u.values.archetypeId;
					e.characterName = u.values.characterName;
					e.gender = u.values.gender;
				}
			}
		}
		if (!added.empty())
		{
			next.entities.insert(next.entities.end(), std::make_move_iterator(added.begin()),
				std::make_move_iterator(added.end()));
			std::sort(next.entities.begin(), next.entities.end(), ById);
			const auto dup = std::adjacent_find(next.entities.begin(), next.entities.end(),
				[](const WireEntity& a, const WireEntity& b) { return a.entityId == b.entityId; });
			if (dup != next.entities.end())
				return false; // même entité ajoutée deux fois
		}
		if (next.entities.size() > kMaxEntitiesPerSnapshot)
			return false;

		outEntities.clear();
		outEntities.reserve(next.entities.size());
		for (const WireEntity& e : next.entities)
			outEntities.push_back(Dequantize(e));
		outHeader = h;
		outHeader.chunkIndex = 0;
		outHeader.chunkCount = 1;

		m_latestCommitted = next.seq;
		Frame& slot = m_history[next.seq % kHistorySize];
		slot = std::move(next);
		return true;
	}
}
