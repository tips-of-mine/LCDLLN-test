#include "src/client/world/terrain/SplatMap.h"

#include <cstring>

namespace engine::world::terrain
{
	SplatMap SplatMap::MakeUniform(uint32_t layerIndex)
	{
		SplatMap s;
		const size_t cellCount = static_cast<size_t>(kSplatResolution) * kSplatResolution;
		s.weights.assign(cellCount * kSplatLayerCount, 0u);
		const uint32_t safeLayer = (layerIndex < kSplatLayerCount) ? layerIndex : 0u;
		for (size_t cell = 0; cell < cellCount; ++cell)
			s.weights[cell * kSplatLayerCount + safeLayer] = 255u;
		return s;
	}

	bool SplatMap::IsValid() const
	{
		if (resolution != kSplatResolution || layerCount != kSplatLayerCount)
			return false;
		const size_t cellCount = static_cast<size_t>(resolution) * resolution;
		if (weights.size() != cellCount * layerCount)
			return false;
		for (size_t cell = 0; cell < cellCount; ++cell)
		{
			uint32_t sum = 0u;
			for (uint32_t layer = 0; layer < layerCount; ++layer)
				sum += weights[cell * layerCount + layer];
			if (sum != 255u) return false;
		}
		return true;
	}

	namespace
	{
		uint8_t* Write32(uint8_t* dst, uint32_t v)
		{ std::memcpy(dst, &v, sizeof(v)); return dst + sizeof(v); }
		uint8_t* Write64(uint8_t* dst, uint64_t v)
		{ std::memcpy(dst, &v, sizeof(v)); return dst + sizeof(v); }
	}

	bool SaveSplatBin(const SplatMap& splat, std::vector<uint8_t>& outBytes, std::string& outError)
	{
		if (!splat.IsValid())
		{ outError = "SplatMap: IsValid() failed (invariant somme=255 ou dimensions)"; return false; }

		const size_t headerSize = 24u;
		const size_t metaSize = 4u + 4u; // resolution + layerCount
		const size_t weightsSize =
			static_cast<size_t>(splat.resolution) * splat.resolution * splat.layerCount;
		const size_t totalSize = headerSize + metaSize + weightsSize;
		outBytes.assign(totalSize, 0u);

		// Post-header : metadata + weights.
		uint8_t* p = outBytes.data() + headerSize;
		p = Write32(p, splat.resolution);
		p = Write32(p, splat.layerCount);
		std::memcpy(p, splat.weights.data(), weightsSize);

		// ContentHash xxhash64 sur payload post-header.
		std::span<const uint8_t> payload(outBytes.data() + headerSize, totalSize - headerSize);
		const uint64_t contentHash = engine::world::ComputeXxHash64(payload);

		uint8_t* h = outBytes.data();
		h = Write32(h, kSplatMagic);
		h = Write32(h, kSplatVersion);
		h = Write32(h, engine::world::kZoneBuilderVersion);
		h = Write32(h, engine::world::kZoneEngineVersion);
		h = Write64(h, contentHash);
		(void)h;
		return true;
	}

	bool LoadSplatBin(std::span<const uint8_t> bytes, SplatMap& outSplat, std::string& outError)
	{
		const size_t headerSize = 24u;
		const size_t metaSize = 8u;
		const size_t weightsSize =
			static_cast<size_t>(kSplatResolution) * kSplatResolution * kSplatLayerCount;
		const size_t expectedSize = headerSize + metaSize + weightsSize;

		if (bytes.size() != expectedSize)
		{ outError = "splat.bin: unexpected size"; return false; }

		engine::world::OutputVersionHeader hdr;
		if (!engine::world::ReadOutputVersionHeader(bytes, hdr, outError)) return false;
		if (hdr.magic != kSplatMagic)
		{ outError = "splat.bin: bad magic"; return false; }
		if (hdr.formatVersion != kSplatVersion)
		{ outError = "splat.bin: bad version"; return false; }

		std::span<const uint8_t> payload = bytes.subspan(headerSize);
		if (engine::world::ComputeXxHash64(payload) != hdr.contentHash)
		{ outError = "splat.bin: contentHash mismatch"; return false; }

		const uint8_t* p = bytes.data() + headerSize;
		std::memcpy(&outSplat.resolution, p, 4); p += 4;
		std::memcpy(&outSplat.layerCount, p, 4); p += 4;
		if (outSplat.resolution != kSplatResolution)
		{ outError = "splat.bin: resolution mismatch"; return false; }
		if (outSplat.layerCount != kSplatLayerCount)
		{ outError = "splat.bin: layerCount mismatch"; return false; }

		outSplat.weights.assign(weightsSize, 0u);
		std::memcpy(outSplat.weights.data(), p, weightsSize);

		if (!outSplat.IsValid())
		{ outError = "splat.bin: invariant somme=255 violated"; return false; }
		return true;
	}
}
