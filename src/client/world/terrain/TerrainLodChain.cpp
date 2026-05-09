#include "engine/world/terrain/TerrainLodChain.h"
#include "engine/world/OutputVersion.h"

#include <cstring>

namespace engine::world::terrain
{
	namespace
	{
		/// Box filter 2×2 sur `parent` → produit un `TerrainLod` à `newRes` /
		/// `newCell`. Préconditions : parent.resolution == newRes * 2 - 1
		/// (alignement 257 → 129 → 65 → 33 garantit qu'un voisin x2+1 existe
		/// toujours dans la grille parent).
		TerrainLod DownsampleBoxFilter(const TerrainLod& parent, uint32_t newRes, float newCell)
		{
			TerrainLod out;
			out.resolution = newRes;
			out.cellSizeMeters = newCell;
			out.heights.resize(static_cast<size_t>(newRes) * newRes);
			for (uint32_t z = 0; z < newRes; ++z)
			{
				for (uint32_t x = 0; x < newRes; ++x)
				{
					const uint32_t px = x * 2u;
					const uint32_t pz = z * 2u;
					const float h00 = parent.heights[(pz + 0u) * parent.resolution + (px + 0u)];
					const float h10 = parent.heights[(pz + 0u) * parent.resolution + (px + 1u)];
					const float h01 = parent.heights[(pz + 1u) * parent.resolution + (px + 0u)];
					const float h11 = parent.heights[(pz + 1u) * parent.resolution + (px + 1u)];
					out.heights[z * newRes + x] = (h00 + h10 + h01 + h11) * 0.25f;
				}
			}
			return out;
		}

		uint8_t* Write32(uint8_t* dst, uint32_t v)
		{ std::memcpy(dst, &v, sizeof(v)); return dst + sizeof(v); }
		uint8_t* WriteF32(uint8_t* dst, float v)
		{ std::memcpy(dst, &v, sizeof(v)); return dst + sizeof(v); }
		uint8_t* Write64(uint8_t* dst, uint64_t v)
		{ std::memcpy(dst, &v, sizeof(v)); return dst + sizeof(v); }
	}

	TerrainLodChain GenerateLodChain(const TerrainChunk& lod0)
	{
		// Adapte LOD0 dans la struct TerrainLod pour réutiliser DownsampleBoxFilter.
		TerrainLod parent;
		parent.resolution = lod0.resolutionX;
		parent.cellSizeMeters = lod0.cellSizeMeters;
		parent.heights = lod0.heights;

		TerrainLodChain chain;
		for (uint32_t i = 0; i < kPersistedLodCount; ++i)
		{
			chain.lods[i] = DownsampleBoxFilter(parent, kLodResolutions[i], kLodCellSizes[i]);
			parent = chain.lods[i];
		}
		return chain;
	}

	bool SaveTerrainLodsBin(const TerrainLodChain& chain,
		std::vector<uint8_t>& outBytes, std::string& outError)
	{
		const size_t headerSize = 24u;
		// payload : uint32 lodCount + pour chaque LOD : uint32 res + float cell + float[res²] heights
		size_t payloadSize = 4u; // lodCount
		for (const auto& lod : chain.lods)
		{
			if (lod.resolution == 0u || lod.heights.size() != static_cast<size_t>(lod.resolution) * lod.resolution)
			{
				outError = "TerrainLodChain: resolution / heights size mismatch";
				return false;
			}
			payloadSize += 4u + 4u + lod.heights.size() * sizeof(float);
		}
		const size_t totalSize = headerSize + payloadSize;
		outBytes.assign(totalSize, 0u);

		uint8_t* p = outBytes.data() + headerSize;
		const uint32_t lodCount = kPersistedLodCount;
		p = Write32(p, lodCount);
		for (const auto& lod : chain.lods)
		{
			p = Write32(p, lod.resolution);
			p = WriteF32(p, lod.cellSizeMeters);
			std::memcpy(p, lod.heights.data(), lod.heights.size() * sizeof(float));
			p += lod.heights.size() * sizeof(float);
		}

		std::span<const uint8_t> payload(outBytes.data() + headerSize, payloadSize);
		const uint64_t contentHash = engine::world::ComputeXxHash64(payload);

		uint8_t* h = outBytes.data();
		h = Write32(h, kTerrainLodsMagic);
		h = Write32(h, kTerrainLodsVersion);
		h = Write32(h, engine::world::kZoneBuilderVersion);
		h = Write32(h, engine::world::kZoneEngineVersion);
		h = Write64(h, contentHash);
		(void)h;
		return true;
	}

	bool LoadTerrainLodsBin(std::span<const uint8_t> bytes,
		TerrainLodChain& outChain, std::string& outError)
	{
		const size_t headerSize = 24u;
		if (bytes.size() < headerSize + 4u)
		{ outError = "terrain_lods.bin: too small"; return false; }

		engine::world::OutputVersionHeader hdr;
		if (!engine::world::ReadOutputVersionHeader(bytes, hdr, outError)) return false;
		if (hdr.magic != kTerrainLodsMagic)
		{ outError = "terrain_lods.bin: bad magic"; return false; }
		if (hdr.formatVersion != kTerrainLodsVersion)
		{ outError = "terrain_lods.bin: bad version"; return false; }

		std::span<const uint8_t> payload = bytes.subspan(headerSize);
		if (engine::world::ComputeXxHash64(payload) != hdr.contentHash)
		{ outError = "terrain_lods.bin: contentHash mismatch"; return false; }

		const uint8_t* p = bytes.data() + headerSize;
		uint32_t lodCount = 0;
		std::memcpy(&lodCount, p, 4); p += 4;
		if (lodCount != kPersistedLodCount)
		{ outError = "terrain_lods.bin: unexpected lodCount"; return false; }

		for (uint32_t i = 0; i < kPersistedLodCount; ++i)
		{
			uint32_t res = 0;
			float cell = 0.0f;
			std::memcpy(&res,  p, 4); p += 4;
			std::memcpy(&cell, p, 4); p += 4;
			if (res != kLodResolutions[i])
			{ outError = "terrain_lods.bin: lod resolution mismatch"; return false; }
			if (cell != kLodCellSizes[i])
			{ outError = "terrain_lods.bin: lod cellSize mismatch"; return false; }
			outChain.lods[i].resolution = res;
			outChain.lods[i].cellSizeMeters = cell;
			outChain.lods[i].heights.assign(static_cast<size_t>(res) * res, 0.0f);
			std::memcpy(outChain.lods[i].heights.data(), p,
				outChain.lods[i].heights.size() * sizeof(float));
			p += outChain.lods[i].heights.size() * sizeof(float);
		}
		return true;
	}
}
