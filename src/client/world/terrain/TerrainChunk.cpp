#include "src/client/world/terrain/TerrainChunk.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace engine::world::terrain
{
	TerrainChunk TerrainChunk::MakeFlat(float height)
	{
		TerrainChunk c;
		const size_t n = static_cast<size_t>(kTerrainResolution) * kTerrainResolution;
		c.heights.assign(n, height);
		c.heightMin = height;
		c.heightMax = height;
		return c;
	}

	float TerrainChunk::SampleHeight(float localX, float localZ) const
	{
		if (heights.empty()) return 0.0f;
		const float fx = localX / cellSizeMeters;
		const float fz = localZ / cellSizeMeters;
		const float maxX = static_cast<float>(resolutionX - 1);
		const float maxZ = static_cast<float>(resolutionZ - 1);
		const float cx = std::clamp(fx, 0.0f, maxX);
		const float cz = std::clamp(fz, 0.0f, maxZ);
		const uint32_t x0 = static_cast<uint32_t>(std::floor(cx));
		const uint32_t z0 = static_cast<uint32_t>(std::floor(cz));
		const uint32_t x1 = std::min(x0 + 1u, resolutionX - 1u);
		const uint32_t z1 = std::min(z0 + 1u, resolutionZ - 1u);
		const float tx = cx - static_cast<float>(x0);
		const float tz = cz - static_cast<float>(z0);
		const float h00 = heights[z0 * resolutionX + x0];
		const float h10 = heights[z0 * resolutionX + x1];
		const float h01 = heights[z1 * resolutionX + x0];
		const float h11 = heights[z1 * resolutionX + x1];
		const float h0 = h00 * (1.0f - tx) + h10 * tx;
		const float h1 = h01 * (1.0f - tx) + h11 * tx;
		return h0 * (1.0f - tz) + h1 * tz;
	}

	void TerrainChunk::RecomputeBounds()
	{
		if (heights.empty()) { heightMin = 0.0f; heightMax = 0.0f; return; }
		auto minIt = heights.begin();
		auto maxIt = heights.begin();
		for (auto it = heights.begin(); it != heights.end(); ++it)
		{
			if (*it < *minIt) minIt = it;
			if (*it > *maxIt) maxIt = it;
		}
		heightMin = *minIt;
		heightMax = *maxIt;
	}

	namespace
	{
		/// Vérifie qu'un `TerrainChunk` est conforme aux invariants format.
		/// Appelé avant l'écriture (Save) et après la lecture (Load).
		bool ValidateChunk(const TerrainChunk& c, std::string& outError)
		{
			if (c.resolutionX != kTerrainResolution || c.resolutionZ != kTerrainResolution)
			{ outError = "TerrainChunk: resolution must be 257x257"; return false; }
			if (c.cellSizeMeters != kTerrainCellSizeMeters)
			{ outError = "TerrainChunk: cellSize must be 1.0m"; return false; }
			if (c.heightMin > c.heightMax)
			{ outError = "TerrainChunk: heightMin > heightMax"; return false; }
			if (c.heightMin < kTerrainHeightMinMeters || c.heightMax > kTerrainHeightMaxMeters)
			{ outError = "TerrainChunk: height bounds out of valid range"; return false; }
			if (c.heights.size() !=
				static_cast<size_t>(c.resolutionX) * c.resolutionZ)
			{ outError = "TerrainChunk: heights size mismatch resolution"; return false; }
			return true;
		}

		/// Écrit `value` (uint32) à `dst` en little-endian (memcpy = format hôte
		/// little-endian sur toutes les plateformes cibles, identique au style
		/// OutputVersion.cpp).
		uint8_t* Write32(uint8_t* dst, uint32_t value)
		{
			std::memcpy(dst, &value, sizeof(value));
			return dst + sizeof(value);
		}

		/// Écrit un float (4 octets) à `dst`.
		uint8_t* WriteF32(uint8_t* dst, float value)
		{
			std::memcpy(dst, &value, sizeof(value));
			return dst + sizeof(value);
		}

		/// Écrit un uint64 (8 octets) à `dst`.
		uint8_t* Write64(uint8_t* dst, uint64_t value)
		{
			std::memcpy(dst, &value, sizeof(value));
			return dst + sizeof(value);
		}
	}

	bool SaveTerrainBin(const TerrainChunk& chunkIn, std::vector<uint8_t>& outBytes, std::string& outError)
	{
		// Travailler sur une copie pour pouvoir RecomputeBounds sans muter l'entrée.
		TerrainChunk chunk = chunkIn;
		chunk.RecomputeBounds();
		if (!ValidateChunk(chunk, outError)) return false;

		// Layout total :
		//   [0..23]   OutputVersionHeader (24 octets)
		//   [24..27]  resolutionX (uint32)
		//   [28..31]  resolutionZ (uint32)
		//   [32..35]  cellSizeMeters (float)
		//   [36..39]  heightMin (float)
		//   [40..43]  heightMax (float)
		//   [44..47]  reserved=0 (uint32)
		//   [48..N]   heights (resolutionX * resolutionZ * float)
		const size_t headerSize = 24u;
		const size_t metaSize = 6u * 4u; // 6 champs de 4 octets après le header
		const size_t heightsSize =
			static_cast<size_t>(chunk.resolutionX) * chunk.resolutionZ * sizeof(float);
		const size_t totalSize = headerSize + metaSize + heightsSize;
		outBytes.assign(totalSize, 0u);

		// 1) Écrire la portion post-header (metadata + heights).
		uint8_t* p = outBytes.data() + headerSize;
		p = Write32(p, chunk.resolutionX);
		p = Write32(p, chunk.resolutionZ);
		p = WriteF32(p, chunk.cellSizeMeters);
		p = WriteF32(p, chunk.heightMin);
		p = WriteF32(p, chunk.heightMax);
		p = Write32(p, 0u); // reserved
		std::memcpy(p, chunk.heights.data(), heightsSize);

		// 2) Calculer le contentHash sur la portion post-header puis écrire le header.
		std::span<const uint8_t> payload(outBytes.data() + headerSize,
			outBytes.size() - headerSize);
		const uint64_t contentHash = engine::world::ComputeXxHash64(payload);

		uint8_t* h = outBytes.data();
		h = Write32(h, kTerrainMagic);
		h = Write32(h, kTerrainVersion);
		h = Write32(h, engine::world::kZoneBuilderVersion);
		h = Write32(h, engine::world::kZoneEngineVersion);
		h = Write64(h, contentHash);
		(void)h;
		return true;
	}

	bool LoadTerrainBin(std::span<const uint8_t> bytes, TerrainChunk& outChunk, std::string& outError)
	{
		const size_t headerSize = 24u;
		const size_t metaSize = 6u * 4u;
		const size_t heightsSize =
			static_cast<size_t>(kTerrainResolution) * kTerrainResolution * sizeof(float);
		const size_t expectedSize = headerSize + metaSize + heightsSize;

		if (bytes.size() != expectedSize)
		{ outError = "terrain.bin: unexpected size"; return false; }

		engine::world::OutputVersionHeader hdr;
		if (!engine::world::ReadOutputVersionHeader(bytes, hdr, outError)) return false;
		if (hdr.magic != kTerrainMagic)
		{ outError = "terrain.bin: bad magic"; return false; }
		if (hdr.formatVersion != kTerrainVersion)
		{ outError = "terrain.bin: bad version"; return false; }

		std::span<const uint8_t> payload = bytes.subspan(headerSize);
		if (engine::world::ComputeXxHash64(payload) != hdr.contentHash)
		{ outError = "terrain.bin: contentHash mismatch"; return false; }

		// Lecture metadata.
		const uint8_t* p = bytes.data() + headerSize;
		std::memcpy(&outChunk.resolutionX,    p, 4); p += 4;
		std::memcpy(&outChunk.resolutionZ,    p, 4); p += 4;
		std::memcpy(&outChunk.cellSizeMeters, p, 4); p += 4;
		std::memcpy(&outChunk.heightMin,      p, 4); p += 4;
		std::memcpy(&outChunk.heightMax,      p, 4); p += 4;
		uint32_t reserved = 0;
		std::memcpy(&reserved, p, 4); p += 4;

		const size_t n =
			static_cast<size_t>(outChunk.resolutionX) * outChunk.resolutionZ;
		outChunk.heights.assign(n, 0.0f);
		std::memcpy(outChunk.heights.data(), p, n * sizeof(float));

		if (!ValidateChunk(outChunk, outError)) return false;
		return true;
	}
}
