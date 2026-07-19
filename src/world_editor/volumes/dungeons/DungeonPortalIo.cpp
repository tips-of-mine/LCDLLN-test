#include "src/world_editor/volumes/dungeons/DungeonPortalIo.h"

#include <algorithm> // std::min — plafond du reserve (P0 audit 3.1)
#include <cstring>

namespace engine::editor::world::volumes::dungeons
{
	namespace
	{
		void WriteU8(std::vector<uint8_t>& buf, uint8_t v) { buf.push_back(v); }
		void WriteU16(std::vector<uint8_t>& buf, uint16_t v)
		{
			buf.push_back(static_cast<uint8_t>(v & 0xFFu));
			buf.push_back(static_cast<uint8_t>((v >> 8) & 0xFFu));
		}
		void WriteU32(std::vector<uint8_t>& buf, uint32_t v)
		{
			for (int i = 0; i < 4; ++i)
				buf.push_back(static_cast<uint8_t>((v >> (i * 8)) & 0xFFu));
		}
		void WriteU64(std::vector<uint8_t>& buf, uint64_t v)
		{
			for (int i = 0; i < 8; ++i)
				buf.push_back(static_cast<uint8_t>((v >> (i * 8)) & 0xFFu));
		}
		void WriteF32(std::vector<uint8_t>& buf, float v)
		{
			uint32_t bits;
			std::memcpy(&bits, &v, 4);
			WriteU32(buf, bits);
		}
		void WriteVec3(std::vector<uint8_t>& buf, const engine::math::Vec3& v)
		{
			WriteF32(buf, v.x);
			WriteF32(buf, v.y);
			WriteF32(buf, v.z);
		}
		void WriteString(std::vector<uint8_t>& buf, const std::string& s)
		{
			const uint16_t len = static_cast<uint16_t>(
				s.size() > 0xFFFFu ? 0xFFFFu : s.size());
			WriteU16(buf, len);
			buf.insert(buf.end(), s.begin(), s.begin() + len);
		}
	}

	bool SaveDungeonPortalsBin(const std::vector<DungeonPortalInstance>& instances,
		std::vector<uint8_t>& outBytes, std::string& outError)
	{
		(void)outError;
		outBytes.clear();
		outBytes.reserve(16u + instances.size() * 96u);
		WriteU32(outBytes, kDungeonPortalsBinMagic);
		WriteU32(outBytes, kDungeonPortalsBinVersion);
		WriteU32(outBytes, static_cast<uint32_t>(instances.size()));
		WriteU32(outBytes, 0u);

		for (const auto& inst : instances)
		{
			WriteU64(outBytes, inst.guid);
			WriteString(outBytes, inst.dungeonTemplateId);
			WriteString(outBytes, inst.displayName);
			WriteString(outBytes, inst.decorativeMeshPath);
			WriteVec3(outBytes, inst.worldPosition);
			WriteVec3(outBytes, inst.eulerRotationDeg);
			WriteF32(outBytes, inst.triggerRadius);
			WriteU16(outBytes, inst.requiredLevel);
			WriteU8(outBytes, inst.minDifficulty);
			WriteU8(outBytes, inst.maxDifficulty);
			uint8_t flags = 0u;
			if (inst.isOneShot)           flags |= 0x01u;
			if (inst.persistsAcrossLogin) flags |= 0x02u;
			WriteU8(outBytes, flags);
		}
		return true;
	}

	bool LoadDungeonPortalsBin(std::span<const uint8_t> bytes,
		std::vector<DungeonPortalInstance>& outInstances, std::string& outError)
	{
		outInstances.clear();
		if (bytes.size() < 16u)
		{
			outError = "DungeonPortals: file too small";
			return false;
		}
		auto readU8 = [&](size_t& c, uint8_t& v) -> bool {
			if (c + 1u > bytes.size()) return false;
			v = bytes[c]; c += 1u; return true;
		};
		auto readU16 = [&](size_t& c, uint16_t& v) -> bool {
			if (c + 2u > bytes.size()) return false;
			v = static_cast<uint16_t>(bytes[c])
			  | (static_cast<uint16_t>(bytes[c + 1]) << 8);
			c += 2u; return true;
		};
		auto readU32 = [&](size_t& c, uint32_t& v) -> bool {
			if (c + 4u > bytes.size()) return false;
			v = 0u;
			for (int i = 0; i < 4; ++i)
				v |= static_cast<uint32_t>(bytes[c + i]) << (i * 8);
			c += 4u; return true;
		};
		auto readU64 = [&](size_t& c, uint64_t& v) -> bool {
			if (c + 8u > bytes.size()) return false;
			v = 0u;
			for (int i = 0; i < 8; ++i)
				v |= static_cast<uint64_t>(bytes[c + i]) << (i * 8);
			c += 8u; return true;
		};
		auto readF32 = [&](size_t& c, float& v) -> bool {
			uint32_t bits;
			if (!readU32(c, bits)) return false;
			std::memcpy(&v, &bits, 4);
			return true;
		};
		auto readVec3 = [&](size_t& c, engine::math::Vec3& v) -> bool {
			return readF32(c, v.x) && readF32(c, v.y) && readF32(c, v.z);
		};
		auto readString = [&](size_t& c, std::string& s) -> bool {
			uint16_t len = 0u;
			if (!readU16(c, len)) return false;
			if (c + len > bytes.size()) return false;
			s.assign(reinterpret_cast<const char*>(bytes.data() + c), len);
			c += len;
			return true;
		};

		size_t cursor = 0u;
		uint32_t magic = 0u, version = 0u, instanceCount = 0u, reserved = 0u;
		if (!readU32(cursor, magic) || !readU32(cursor, version)
			|| !readU32(cursor, instanceCount) || !readU32(cursor, reserved))
		{
			outError = "DungeonPortals: header truncated";
			return false;
		}
		if (magic != kDungeonPortalsBinMagic)
		{
			outError = "DungeonPortals: bad magic (expected LCDP)";
			return false;
		}
		if (version == 0u || version > kDungeonPortalsBinVersion)
		{
			outError = "DungeonPortals: unsupported version";
			return false;
		}

		// P0 (audit 2026-06-05, 3.1) — borne le reserve par la taille restante
		// du fichier (une instance sérialisée ≥ ~41 octets, borne prudente 32) :
		// un count corrompu (0xFFFFFFFF) provoquait un std::bad_alloc non
		// catché avant toute lecture. La boucle échoue proprement si le count
		// reste mensonger (« truncated »).
		outInstances.reserve(std::min<size_t>(instanceCount,
			(bytes.size() - cursor) / 32u));
		for (uint32_t i = 0; i < instanceCount; ++i)
		{
			DungeonPortalInstance inst;
			if (!readU64(cursor, inst.guid)) { outError = "DungeonPortals: guid"; return false; }
			if (!readString(cursor, inst.dungeonTemplateId)) { outError = "DungeonPortals: tid"; return false; }
			if (!readString(cursor, inst.displayName))       { outError = "DungeonPortals: name"; return false; }
			if (!readString(cursor, inst.decorativeMeshPath)){ outError = "DungeonPortals: mesh"; return false; }
			if (!readVec3(cursor, inst.worldPosition))       { outError = "DungeonPortals: pos"; return false; }
			if (!readVec3(cursor, inst.eulerRotationDeg))    { outError = "DungeonPortals: rot"; return false; }
			if (!readF32(cursor, inst.triggerRadius))        { outError = "DungeonPortals: radius"; return false; }
			if (!readU16(cursor, inst.requiredLevel))        { outError = "DungeonPortals: level"; return false; }
			if (!readU8(cursor, inst.minDifficulty))         { outError = "DungeonPortals: dmin"; return false; }
			if (!readU8(cursor, inst.maxDifficulty))         { outError = "DungeonPortals: dmax"; return false; }
			uint8_t flags = 0u;
			if (!readU8(cursor, flags))                      { outError = "DungeonPortals: flags"; return false; }
			inst.isOneShot           = (flags & 0x01u) != 0u;
			inst.persistsAcrossLogin = (flags & 0x02u) != 0u;
			outInstances.push_back(std::move(inst));
		}
		return true;
	}
}
