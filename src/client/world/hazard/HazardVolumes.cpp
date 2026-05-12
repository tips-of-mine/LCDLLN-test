// src/client/world/hazard/HazardVolumes.cpp
#include "src/client/world/hazard/HazardVolumes.h"

#include "src/client/world/OutputVersion.h"

#include <cmath>
#include <cstring>

namespace engine::world::hazard
{
	namespace
	{
		/// Écrit un uint32_t little-endian à dst, retourne le pointeur avancé.
		uint8_t* Write32(uint8_t* dst, uint32_t v)
		{
			std::memcpy(dst, &v, sizeof(v));
			return dst + sizeof(v);
		}

		/// Écrit un uint64_t little-endian à dst, retourne le pointeur avancé.
		uint8_t* Write64(uint8_t* dst, uint64_t v)
		{
			std::memcpy(dst, &v, sizeof(v));
			return dst + sizeof(v);
		}

		/// Écrit un float 32 bits à dst, retourne le pointeur avancé.
		uint8_t* WriteF32(uint8_t* dst, float v)
		{
			std::memcpy(dst, &v, sizeof(v));
			return dst + sizeof(v);
		}

		/// Écrit un Vec3 (3 × float) à dst, retourne le pointeur avancé.
		uint8_t* WriteVec3(uint8_t* dst, const engine::math::Vec3& v)
		{
			std::memcpy(dst,      &v.x, 4);
			std::memcpy(dst + 4,  &v.y, 4);
			std::memcpy(dst + 8,  &v.z, 4);
			return dst + 12;
		}

		/// Taille fixe d'une HazardInstance sérialisée : 60 octets.
		/// uint32 type(4) + uint32 shape(4) + vec3 position(12) + vec3 boxHalfExtents(12)
		/// + float cylRadius(4) + float cylHeight(4) + float sinkRateMps(4)
		/// + float maxDepthMeters(4) + float slowdownMul(4)
		/// + uint32 escapeMode(4) + uint32 requiredItemId(4) = 60
		constexpr size_t kHazardInstanceBytes = 60u;
	} // namespace


	bool PointInHazard(const HazardInstance& hz, engine::math::Vec3 worldPos) noexcept
	{
		switch (hz.shape)
		{
			case HazardShape::Box:
			{
				const float dx = worldPos.x - hz.position.x;
				const float dy = worldPos.y - hz.position.y;
				const float dz = worldPos.z - hz.position.z;
				return std::fabs(dx) <= hz.boxHalfExtents.x
					&& std::fabs(dy) <= hz.boxHalfExtents.y
					&& std::fabs(dz) <= hz.boxHalfExtents.z;
			}
			case HazardShape::Cylinder:
			{
				if (worldPos.y < hz.position.y) return false;
				if (worldPos.y > hz.position.y + hz.cylHeight) return false;
				const float dx = worldPos.x - hz.position.x;
				const float dz = worldPos.z - hz.position.z;
				return (dx * dx + dz * dz) <= (hz.cylRadius * hz.cylRadius);
			}
		}
		return false;
	}

	bool SaveHazardsBin(const HazardScene& scene,
		std::vector<uint8_t>& outBytes, std::string& outError)
	{
		(void)outError;  // SaveHazardsBin ne fail jamais (alloc-only)

		constexpr size_t kHeaderSize = 24u;
		// Payload : hazardCount(4) + N × kHazardInstanceBytes
		const size_t payloadSize = 4u + scene.hazards.size() * kHazardInstanceBytes;
		const size_t totalSize   = kHeaderSize + payloadSize;

		outBytes.assign(totalSize, 0u);

		// --- Écriture du payload (après le header) ---
		uint8_t* p = outBytes.data() + kHeaderSize;

		p = Write32(p, static_cast<uint32_t>(scene.hazards.size()));

		for (const auto& h : scene.hazards)
		{
			p = Write32(p,  static_cast<uint32_t>(h.type));
			p = Write32(p,  static_cast<uint32_t>(h.shape));
			p = WriteVec3(p, h.position);
			p = WriteVec3(p, h.boxHalfExtents);
			p = WriteF32(p,  h.cylRadius);
			p = WriteF32(p,  h.cylHeight);
			p = WriteF32(p,  h.sinkRateMps);
			p = WriteF32(p,  h.maxDepthMeters);
			p = WriteF32(p,  h.slowdownMul);
			p = Write32(p,  static_cast<uint32_t>(h.escapeMode));
			p = Write32(p,  h.requiredItemId);
		}

		// --- ContentHash xxhash64 sur le payload (sans header) ---
		std::span<const uint8_t> payload(outBytes.data() + kHeaderSize, payloadSize);
		const uint64_t contentHash = engine::world::ComputeXxHash64(payload);

		// --- Écriture du header OutputVersionHeader (24 octets) ---
		uint8_t* h = outBytes.data();
		h = Write32(h, kHazardsMagic);
		h = Write32(h, kHazardsVersion);
		h = Write32(h, engine::world::kZoneBuilderVersion);
		h = Write32(h, engine::world::kZoneEngineVersion);
		h = Write64(h, contentHash);
		(void)h;
		return true;
	}

	bool LoadHazardsBin(std::span<const uint8_t> bytes,
		HazardScene& outScene, std::string& outError)
	{
		outScene.hazards.clear();

		constexpr size_t kHeaderSize = 24u;
		// Minimum : header(24) + hazardCount(4)
		if (bytes.size() < kHeaderSize + 4u)
		{
			outError = "HazardScene: file too small";
			return false;
		}

		// --- Validation du header ---
		engine::world::OutputVersionHeader hdr;
		if (!engine::world::ReadOutputVersionHeader(bytes, hdr, outError))
			return false;
		if (hdr.magic != kHazardsMagic)
		{
			outError = "HazardScene: bad magic (expected HAZA)";
			return false;
		}
		if (hdr.formatVersion != kHazardsVersion)
		{
			outError = "HazardScene: bad version";
			return false;
		}

		// --- Validation du contentHash ---
		std::span<const uint8_t> payload = bytes.subspan(kHeaderSize);
		if (engine::world::ComputeXxHash64(payload) != hdr.contentHash)
		{
			outError = "HazardScene: contentHash mismatch (file corrupted)";
			return false;
		}

		// --- Lecture du payload ---
		const uint8_t* p   = bytes.data() + kHeaderSize;
		const uint8_t* end = bytes.data() + bytes.size();

		auto readU32 = [&](uint32_t& v) -> bool {
			if (end - p < 4) return false;
			std::memcpy(&v, p, 4); p += 4; return true;
		};
		auto readF32 = [&](float& v) -> bool {
			if (end - p < 4) return false;
			std::memcpy(&v, p, 4); p += 4; return true;
		};
		auto readVec3 = [&](engine::math::Vec3& v) -> bool {
			if (end - p < 12) return false;
			std::memcpy(&v.x, p,     4);
			std::memcpy(&v.y, p + 4, 4);
			std::memcpy(&v.z, p + 8, 4);
			p += 12; return true;
		};

		uint32_t hazardCount = 0;
		if (!readU32(hazardCount))
		{ outError = "HazardScene: hazardCount truncated"; return false; }

		// Vérifie que les données sont suffisantes avant d'allouer
		if (static_cast<size_t>(end - p) < static_cast<size_t>(hazardCount) * kHazardInstanceBytes)
		{ outError = "HazardScene: payload truncated"; return false; }

		outScene.hazards.reserve(hazardCount);
		for (uint32_t i = 0; i < hazardCount; ++i)
		{
			HazardInstance h;
			uint32_t typeRaw = 0, shapeRaw = 0, escapeModeRaw = 0;

			if (!readU32(typeRaw))              { outError = "HazardScene: hazard type truncated";          return false; }
			if (!readU32(shapeRaw))             { outError = "HazardScene: hazard shape truncated";         return false; }
			if (!readVec3(h.position))          { outError = "HazardScene: hazard position truncated";      return false; }
			if (!readVec3(h.boxHalfExtents))    { outError = "HazardScene: hazard boxHalfExtents truncated";return false; }
			if (!readF32(h.cylRadius))          { outError = "HazardScene: hazard cylRadius truncated";     return false; }
			if (!readF32(h.cylHeight))          { outError = "HazardScene: hazard cylHeight truncated";     return false; }
			if (!readF32(h.sinkRateMps))        { outError = "HazardScene: hazard sinkRateMps truncated";   return false; }
			if (!readF32(h.maxDepthMeters))     { outError = "HazardScene: hazard maxDepthMeters truncated";return false; }
			if (!readF32(h.slowdownMul))        { outError = "HazardScene: hazard slowdownMul truncated";   return false; }
			if (!readU32(escapeModeRaw))        { outError = "HazardScene: hazard escapeMode truncated";    return false; }
			if (!readU32(h.requiredItemId))     { outError = "HazardScene: hazard requiredItemId truncated";return false; }

			h.type       = static_cast<HazardType>(typeRaw);
			h.shape      = static_cast<HazardShape>(shapeRaw);
			h.escapeMode = static_cast<EscapeMode>(escapeModeRaw);

			outScene.hazards.push_back(h);
		}

		return true;
	}
}
