#include "src/world_editor/volumes/MeshInsertIo.h"

#include <algorithm> // std::min — plafond du reserve (P0 audit 3.1)
#include <cstring>

namespace engine::editor::world::volumes
{
	namespace
	{
		void WriteU8(std::vector<uint8_t>& buf, uint8_t v)  { buf.push_back(v); }
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

	bool SaveMeshInsertsBin(const std::vector<MeshInsertInstance>& instances,
		std::vector<uint8_t>& outBytes, std::string& outError)
	{
		(void)outError;
		outBytes.clear();
		outBytes.reserve(16u + instances.size() * 96u);

		// Header.
		WriteU32(outBytes, kMeshInsertsBinMagic);
		WriteU32(outBytes, kMeshInsertsBinVersion);
		WriteU32(outBytes, static_cast<uint32_t>(instances.size()));
		WriteU32(outBytes, 0u); // reserved

		for (const auto& inst : instances)
		{
			WriteU64(outBytes, inst.guid);
			WriteString(outBytes, inst.gltfRelativePath);
			WriteVec3(outBytes, inst.worldPosition);
			WriteVec3(outBytes, inst.eulerRotationDeg);
			WriteF32(outBytes, inst.uniformScale);
			WriteString(outBytes, inst.insertCategory);
			WriteString(outBytes, inst.displayName);
			uint8_t flags = 0u;
			if (inst.hasInteriorVolume)   flags |= 0x01u;
			if (inst.castsShadow)         flags |= 0x02u;
			if (inst.receivesAudioReverb) flags |= 0x04u;
			if (inst.allowsWaterIngress)  flags |= 0x08u;
			WriteU8(outBytes, flags);
			WriteF32(outBytes, inst.lightProbeIntensity);
		}
		return true;
	}

	bool LoadMeshInsertsBin(std::span<const uint8_t> bytes,
		std::vector<MeshInsertInstance>& outInstances, std::string& outError)
	{
		outInstances.clear();
		if (bytes.size() < 16u)
		{
			outError = "MeshInserts: file too small";
			return false;
		}
		auto readU8 = [&](size_t& cursor, uint8_t& v) -> bool {
			if (cursor + 1u > bytes.size()) return false;
			v = bytes[cursor]; cursor += 1u; return true;
		};
		auto readU16 = [&](size_t& cursor, uint16_t& v) -> bool {
			if (cursor + 2u > bytes.size()) return false;
			v = static_cast<uint16_t>(bytes[cursor])
			  | (static_cast<uint16_t>(bytes[cursor + 1]) << 8);
			cursor += 2u; return true;
		};
		auto readU32 = [&](size_t& cursor, uint32_t& v) -> bool {
			if (cursor + 4u > bytes.size()) return false;
			v = 0u;
			for (int i = 0; i < 4; ++i)
				v |= static_cast<uint32_t>(bytes[cursor + i]) << (i * 8);
			cursor += 4u; return true;
		};
		auto readU64 = [&](size_t& cursor, uint64_t& v) -> bool {
			if (cursor + 8u > bytes.size()) return false;
			v = 0u;
			for (int i = 0; i < 8; ++i)
				v |= static_cast<uint64_t>(bytes[cursor + i]) << (i * 8);
			cursor += 8u; return true;
		};
		auto readF32 = [&](size_t& cursor, float& v) -> bool {
			uint32_t bits;
			if (!readU32(cursor, bits)) return false;
			std::memcpy(&v, &bits, 4);
			return true;
		};
		auto readVec3 = [&](size_t& cursor, engine::math::Vec3& v) -> bool {
			return readF32(cursor, v.x) && readF32(cursor, v.y) && readF32(cursor, v.z);
		};
		auto readString = [&](size_t& cursor, std::string& s) -> bool {
			uint16_t len = 0u;
			if (!readU16(cursor, len)) return false;
			if (cursor + len > bytes.size()) return false;
			s.assign(reinterpret_cast<const char*>(bytes.data() + cursor), len);
			cursor += len;
			return true;
		};

		size_t cursor = 0u;
		uint32_t magic = 0u, version = 0u, instanceCount = 0u, reserved = 0u;
		if (!readU32(cursor, magic) || !readU32(cursor, version)
			|| !readU32(cursor, instanceCount) || !readU32(cursor, reserved))
		{
			outError = "MeshInserts: header truncated";
			return false;
		}
		if (magic != kMeshInsertsBinMagic)
		{
			outError = "MeshInserts: bad magic (expected LCMI)";
			return false;
		}
		if (version == 0u || version > kMeshInsertsBinVersion)
		{
			outError = "MeshInserts: unsupported version";
			return false;
		}

		// P0 (audit 2026-06-05, 3.1) — borne le reserve par ce que le fichier
		// peut PHYSIQUEMENT contenir : une instance sérialisée fait au moins
		// ~49 octets (guid + 3 chaînes + 2 vec3 + 2 f32 + flags) ; on prend 32
		// en borne prudente. Sans ce plafond, un .bin corrompu annonçant
		// count=0xFFFFFFFF provoquait un std::bad_alloc non catché (crash au
		// chargement) AVANT toute vérification des octets. La boucle ci-dessous
		// échoue proprement (« truncated ») si le count reste mensonger.
		outInstances.reserve(std::min<size_t>(instanceCount,
			(bytes.size() - cursor) / 32u));
		for (uint32_t i = 0; i < instanceCount; ++i)
		{
			MeshInsertInstance inst;
			if (!readU64(cursor, inst.guid)) { outError = "MeshInserts: guid truncated"; return false; }
			if (!readString(cursor, inst.gltfRelativePath)) { outError = "MeshInserts: gltf path"; return false; }
			if (!readVec3(cursor, inst.worldPosition))      { outError = "MeshInserts: pos"; return false; }
			if (!readVec3(cursor, inst.eulerRotationDeg))   { outError = "MeshInserts: rot"; return false; }
			if (!readF32(cursor, inst.uniformScale))        { outError = "MeshInserts: scale"; return false; }
			if (!readString(cursor, inst.insertCategory))   { outError = "MeshInserts: category"; return false; }
			if (!readString(cursor, inst.displayName))      { outError = "MeshInserts: name"; return false; }
			uint8_t flags = 0u;
			if (!readU8(cursor, flags))                     { outError = "MeshInserts: flags"; return false; }
			inst.hasInteriorVolume   = (flags & 0x01u) != 0u;
			inst.castsShadow         = (flags & 0x02u) != 0u;
			inst.receivesAudioReverb = (flags & 0x04u) != 0u;
			inst.allowsWaterIngress  = (flags & 0x08u) != 0u;
			if (!readF32(cursor, inst.lightProbeIntensity)) { outError = "MeshInserts: probe"; return false; }
			outInstances.push_back(std::move(inst));
		}
		return true;
	}
}
