#include "src/world_editor/volumes/bridge/VMapBridge.h"

#include "src/world_editor/volumes/MeshInsertDocument.h"
#include "src/world_editor/volumes/dungeons/DungeonPortalDocument.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>

namespace engine::editor::world::volumes::bridge
{
	namespace
	{
		constexpr float kDegToRad = 0.01745329251994329577f;

		void WriteU8(std::vector<uint8_t>& buf, uint8_t v) { buf.push_back(v); }
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
	}

	void TransformLocalAabbToWorld(
		const engine::math::Vec3& localMin, const engine::math::Vec3& localMax,
		const engine::math::Vec3& worldPosition, float yawDeg, float uniformScale,
		engine::math::Vec3& outMin, engine::math::Vec3& outMax)
	{
		const float scale = (uniformScale > 0.0f) ? uniformScale : 1.0f;
		const float yawRad = yawDeg * kDegToRad;
		const float c = std::cos(yawRad);
		const float s = std::sin(yawRad);

		constexpr float kInf = std::numeric_limits<float>::infinity();
		outMin = { +kInf, +kInf, +kInf };
		outMax = { -kInf, -kInf, -kInf };

		// Englobe les 8 coins transformés.
		for (int corner = 0; corner < 8; ++corner)
		{
			const float lx = (corner & 1) ? localMax.x : localMin.x;
			const float ly = (corner & 2) ? localMax.y : localMin.y;
			const float lz = (corner & 4) ? localMax.z : localMin.z;

			// Scale uniforme.
			const float sx = lx * scale;
			const float sy = ly * scale;
			const float sz = lz * scale;

			// Rotation autour de Y : (x, z) tournent, y inchangé.
			const float rx = sx * c - sz * s;
			const float rz = sx * s + sz * c;
			const float ry = sy;

			// Translation monde.
			const float wx = rx + worldPosition.x;
			const float wy = ry + worldPosition.y;
			const float wz = rz + worldPosition.z;

			outMin.x = std::min(outMin.x, wx);
			outMin.y = std::min(outMin.y, wy);
			outMin.z = std::min(outMin.z, wz);
			outMax.x = std::max(outMax.x, wx);
			outMax.y = std::max(outMax.y, wy);
			outMax.z = std::max(outMax.z, wz);
		}
	}

	void VMapBridge::Build(const MeshInsertDocument& meshDoc,
		const dungeons::DungeonPortalDocument& portalDoc,
		size_t& outUnresolvedCount)
	{
		m_proxies.clear();
		outUnresolvedCount = 0u;

		for (const auto& inst : meshDoc.All())
		{
			uint8_t kind = kVolumeKindUnknown;
			const engine::math::Vec3* localMin = nullptr;
			const engine::math::Vec3* localMax = nullptr;

			if (inst.insertCategory == "cave" && m_caveCatalog != nullptr)
			{
				kind = kVolumeKindCave;
				// On retrouve l'entrée catalogue via le gltfRelativePath
				// (clé de placement la plus fiable côté instance).
				for (const auto& e : m_caveCatalog->Entries())
				{
					if (e.gltfRelativePath == inst.gltfRelativePath)
					{
						localMin = &e.aabbMin;
						localMax = &e.aabbMax;
						break;
					}
				}
			}
			else if (inst.insertCategory == "overhang" && m_overhangCatalog != nullptr)
			{
				kind = kVolumeKindOverhang;
				for (const auto& e : m_overhangCatalog->Entries())
				{
					if (e.gltfRelativePath == inst.gltfRelativePath)
					{
						localMin = &e.aabbMin;
						localMax = &e.aabbMax;
						break;
					}
				}
			}
			else if (inst.insertCategory == "arch" && m_archCatalog != nullptr)
			{
				kind = kVolumeKindArch;
				for (const auto& e : m_archCatalog->Entries())
				{
					if (e.gltfRelativePath == inst.gltfRelativePath)
					{
						localMin = &e.aabbMin;
						localMax = &e.aabbMax;
						break;
					}
				}
			}

			VolumeAabbProxy proxy;
			proxy.sourceGuid = inst.guid;
			proxy.volumeKind = kind;

			if (localMin != nullptr && localMax != nullptr)
			{
				TransformLocalAabbToWorld(*localMin, *localMax,
					inst.worldPosition, inst.eulerRotationDeg.y, inst.uniformScale,
					proxy.worldMin, proxy.worldMax);
			}
			else
			{
				// Catalogue absent / entrée introuvable : proxy dégénéré
				// cube unitaire ±0.5 m autour de la position.
				++outUnresolvedCount;
				proxy.worldMin = {
					inst.worldPosition.x - 0.5f,
					inst.worldPosition.y - 0.5f,
					inst.worldPosition.z - 0.5f };
				proxy.worldMax = {
					inst.worldPosition.x + 0.5f,
					inst.worldPosition.y + 0.5f,
					inst.worldPosition.z + 0.5f };
			}
			m_proxies.push_back(proxy);
		}

		// Portails de donjon : pas de mesh associé → cube de côté
		// 2 * triggerRadius centré sur la position.
		for (const auto& portal : portalDoc.All())
		{
			VolumeAabbProxy proxy;
			proxy.sourceGuid = portal.guid;
			proxy.volumeKind = kVolumeKindDungeonPortal;
			const float r = (portal.triggerRadius > 0.0f) ? portal.triggerRadius : 0.5f;
			proxy.worldMin = {
				portal.worldPosition.x - r,
				portal.worldPosition.y - r,
				portal.worldPosition.z - r };
			proxy.worldMax = {
				portal.worldPosition.x + r,
				portal.worldPosition.y + r,
				portal.worldPosition.z + r };
			m_proxies.push_back(proxy);
		}
	}

	bool VMapBridge::Serialize(std::vector<uint8_t>& outBytes, std::string& outError) const
	{
		(void)outError;
		outBytes.clear();
		outBytes.reserve(16u + m_proxies.size() * 33u);
		WriteU32(outBytes, kVolumeCollisionMagic);
		WriteU32(outBytes, kVolumeCollisionVersion);
		WriteU32(outBytes, static_cast<uint32_t>(m_proxies.size()));
		WriteU32(outBytes, 0u);
		for (const auto& p : m_proxies)
		{
			WriteU64(outBytes, p.sourceGuid);
			WriteU8(outBytes, p.volumeKind);
			WriteVec3(outBytes, p.worldMin);
			WriteVec3(outBytes, p.worldMax);
		}
		return true;
	}

	bool VMapBridge::Deserialize(std::span<const uint8_t> bytes,
		std::vector<VolumeAabbProxy>& outProxies, std::string& outError)
	{
		outProxies.clear();
		if (bytes.size() < 16u)
		{
			outError = "VMapBridge: file too small";
			return false;
		}
		auto readU8 = [&](size_t& c, uint8_t& v) -> bool {
			if (c + 1u > bytes.size()) return false;
			v = bytes[c]; c += 1u; return true;
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

		size_t cursor = 0u;
		uint32_t magic = 0u, version = 0u, count = 0u, reserved = 0u;
		if (!readU32(cursor, magic) || !readU32(cursor, version)
			|| !readU32(cursor, count) || !readU32(cursor, reserved))
		{
			outError = "VMapBridge: header truncated";
			return false;
		}
		if (magic != kVolumeCollisionMagic)
		{
			outError = "VMapBridge: bad magic (expected LCVC)";
			return false;
		}
		if (version == 0u || version > kVolumeCollisionVersion)
		{
			outError = "VMapBridge: unsupported version";
			return false;
		}
		// P0 (audit 2026-06-05, 3.1) — borne le reserve par la taille restante
		// du fichier : un proxy sérialisé fait EXACTEMENT 33 octets (u64 guid +
		// 6 f32 AABB + u8 kind). Un count corrompu (0xFFFFFFFF) provoquait un
		// std::bad_alloc non catché avant toute lecture.
		outProxies.reserve(std::min<size_t>(count, (bytes.size() - cursor) / 33u));
		for (uint32_t i = 0; i < count; ++i)
		{
			VolumeAabbProxy p;
			if (!readU64(cursor, p.sourceGuid)) { outError = "VMapBridge: guid"; return false; }
			if (!readU8(cursor, p.volumeKind))  { outError = "VMapBridge: kind"; return false; }
			if (!readVec3(cursor, p.worldMin))  { outError = "VMapBridge: min"; return false; }
			if (!readVec3(cursor, p.worldMax))  { outError = "VMapBridge: max"; return false; }
			outProxies.push_back(p);
		}
		return true;
	}

	bool VMapBridge::WriteToDisk(const std::string& contentRoot, std::string& outError) const
	{
		const std::filesystem::path path =
			std::filesystem::path(contentRoot) / "instances" / "volume_collision.bin";
		std::error_code ec;
		std::filesystem::create_directories(path.parent_path(), ec);
		if (ec)
		{
			outError = "VMapBridge: mkdir failed: " + ec.message();
			return false;
		}
		std::vector<uint8_t> bytes;
		if (!Serialize(bytes, outError)) return false;
		std::ofstream f(path, std::ios::binary | std::ios::trunc);
		if (!f.good())
		{
			outError = "VMapBridge: cannot open " + path.string();
			return false;
		}
		f.write(reinterpret_cast<const char*>(bytes.data()),
			static_cast<std::streamsize>(bytes.size()));
		if (!f.good())
		{
			outError = "VMapBridge: write failed";
			return false;
		}
		return true;
	}
}
