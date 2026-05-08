// engine/world/water/WaterSurfaces.cpp
#include "engine/world/water/WaterSurfaces.h"

#include "engine/world/OutputVersion.h"

#include <cstring>

namespace engine::world::water
{
	namespace
	{
		uint8_t* Write32(uint8_t* dst, uint32_t v)
		{
			std::memcpy(dst, &v, sizeof(v));
			return dst + sizeof(v);
		}
		uint8_t* Write64(uint8_t* dst, uint64_t v)
		{
			std::memcpy(dst, &v, sizeof(v));
			return dst + sizeof(v);
		}
		uint8_t* WriteVec3(uint8_t* dst, const engine::math::Vec3& v)
		{
			std::memcpy(dst,     &v.x, 4);
			std::memcpy(dst + 4, &v.y, 4);
			std::memcpy(dst + 8, &v.z, 4);
			return dst + 12;
		}

		size_t LakeSize(const LakeInstance& l)
		{
			// nameLen(4) + name + vertexCount(4) + polygon*(12) + bottomColor(12) + turbidity(4) + waterLevelY(4)
			return 4u + l.name.size() + 4u + l.polygon.size() * 12u + 12u + 4u + 4u;
		}
		size_t RiverSize(const RiverInstance& r)
		{
			// nameLen(4) + name + nodeCount(4) + nodes*(12+4+4)
			return 4u + r.name.size() + 4u + r.nodes.size() * 20u;
		}

		size_t ComputePayloadSize(const WaterScene& s)
		{
			size_t total = 4u + 4u;  // lakeCount + riverCount
			for (const auto& l : s.lakes)  total += LakeSize(l);
			for (const auto& r : s.rivers) total += RiverSize(r);
			return total;
		}
	}

	bool SaveWaterBin(const WaterScene& scene,
		std::vector<uint8_t>& outBytes, std::string& outError)
	{
		(void)outError;  // SaveWaterBin actuel ne fail jamais (alloc-only)
		const size_t headerSize = 24u;
		const size_t payloadSize = ComputePayloadSize(scene);
		const size_t totalSize = headerSize + payloadSize;

		outBytes.assign(totalSize, 0u);
		uint8_t* p = outBytes.data() + headerSize;

		p = Write32(p, static_cast<uint32_t>(scene.lakes.size()));
		p = Write32(p, static_cast<uint32_t>(scene.rivers.size()));

		for (const auto& lake : scene.lakes)
		{
			p = Write32(p, static_cast<uint32_t>(lake.name.size()));
			std::memcpy(p, lake.name.data(), lake.name.size());
			p += lake.name.size();
			p = Write32(p, static_cast<uint32_t>(lake.polygon.size()));
			for (const auto& v : lake.polygon)
				p = WriteVec3(p, v);
			p = WriteVec3(p, lake.bottomColor);
			std::memcpy(p, &lake.turbidity, 4);   p += 4;
			std::memcpy(p, &lake.waterLevelY, 4); p += 4;
		}
		for (const auto& river : scene.rivers)
		{
			p = Write32(p, static_cast<uint32_t>(river.name.size()));
			std::memcpy(p, river.name.data(), river.name.size());
			p += river.name.size();
			p = Write32(p, static_cast<uint32_t>(river.nodes.size()));
			for (const auto& node : river.nodes)
			{
				p = WriteVec3(p, node.position);
				std::memcpy(p, &node.widthMeters, 4); p += 4;
				std::memcpy(p, &node.depthMeters, 4); p += 4;
			}
		}

		// ContentHash xxhash64 sur payload post-header
		std::span<const uint8_t> payload(outBytes.data() + headerSize, totalSize - headerSize);
		const uint64_t contentHash = engine::world::ComputeXxHash64(payload);

		uint8_t* h = outBytes.data();
		h = Write32(h, kWaterMagic);
		h = Write32(h, kWaterVersion);
		h = Write32(h, engine::world::kZoneBuilderVersion);
		h = Write32(h, engine::world::kZoneEngineVersion);
		h = Write64(h, contentHash);
		(void)h;
		return true;
	}

	bool LoadWaterBin(std::span<const uint8_t> bytes,
		WaterScene& outScene, std::string& outError)
	{
		outScene.lakes.clear();
		outScene.rivers.clear();

		if (bytes.size() < 24u + 8u)
		{
			outError = "WaterScene: file too small";
			return false;
		}

		engine::world::OutputVersionHeader hdr;
		if (!engine::world::ReadOutputVersionHeader(bytes, hdr, outError))
			return false;
		if (hdr.magic != kWaterMagic)
		{
			outError = "WaterScene: bad magic (expected WATR)";
			return false;
		}
		if (hdr.formatVersion != kWaterVersion)
		{
			outError = "WaterScene: bad version";
			return false;
		}
		std::span<const uint8_t> payload = bytes.subspan(24);
		if (engine::world::ComputeXxHash64(payload) != hdr.contentHash)
		{
			outError = "WaterScene: contentHash mismatch (file corrupted)";
			return false;
		}

		const uint8_t* p = bytes.data() + 24;
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

		uint32_t lakeCount = 0, riverCount = 0;
		if (!readU32(lakeCount) || !readU32(riverCount))
		{ outError = "WaterScene: header truncated"; return false; }

		outScene.lakes.reserve(lakeCount);
		for (uint32_t i = 0; i < lakeCount; ++i)
		{
			LakeInstance lake;
			uint32_t nameLen = 0, vertCount = 0;
			if (!readU32(nameLen)) { outError = "WaterScene: lake nameLen truncated"; return false; }
			if (static_cast<size_t>(end - p) < nameLen)
			{ outError = "WaterScene: lake name truncated"; return false; }
			lake.name.assign(reinterpret_cast<const char*>(p), nameLen);
			p += nameLen;
			if (!readU32(vertCount)) { outError = "WaterScene: lake vertCount truncated"; return false; }
			if (static_cast<size_t>(end - p) < static_cast<size_t>(vertCount) * 12u)
			{ outError = "WaterScene: lake polygon truncated"; return false; }
			lake.polygon.resize(vertCount);
			for (uint32_t v = 0; v < vertCount; ++v)
				readVec3(lake.polygon[v]);
			if (!readVec3(lake.bottomColor))   { outError = "WaterScene: lake bottomColor truncated"; return false; }
			if (!readF32(lake.turbidity))      { outError = "WaterScene: lake turbidity truncated"; return false; }
			if (!readF32(lake.waterLevelY))    { outError = "WaterScene: lake waterLevelY truncated"; return false; }
			outScene.lakes.push_back(std::move(lake));
		}

		outScene.rivers.reserve(riverCount);
		for (uint32_t i = 0; i < riverCount; ++i)
		{
			RiverInstance river;
			uint32_t nameLen = 0, nodeCount = 0;
			if (!readU32(nameLen)) { outError = "WaterScene: river nameLen truncated"; return false; }
			if (static_cast<size_t>(end - p) < nameLen)
			{ outError = "WaterScene: river name truncated"; return false; }
			river.name.assign(reinterpret_cast<const char*>(p), nameLen);
			p += nameLen;
			if (!readU32(nodeCount)) { outError = "WaterScene: river nodeCount truncated"; return false; }
			if (static_cast<size_t>(end - p) < static_cast<size_t>(nodeCount) * 20u)
			{ outError = "WaterScene: river nodes truncated"; return false; }
			river.nodes.resize(nodeCount);
			for (uint32_t n = 0; n < nodeCount; ++n)
			{
				readVec3(river.nodes[n].position);
				readF32(river.nodes[n].widthMeters);
				readF32(river.nodes[n].depthMeters);
			}
			outScene.rivers.push_back(std::move(river));
		}

		return true;
	}
}
