#include "src/client/render/terrain/SplatSampling.h"

#include <cstddef>

namespace engine::render::terrain
{
	SplatSampleResult SampleDominantSplatLayerAtWorldXZ(const uint8_t* splatRgba,
		uint32_t splatWidth,
		uint32_t splatHeight,
		float terrainOriginX,
		float terrainOriginZ,
		float terrainWorldSize,
		float worldX,
		float worldZ)
	{
		SplatSampleResult out{};
		if (splatRgba == nullptr || splatWidth == 0u || splatHeight == 0u || terrainWorldSize <= 0.0f)
		{
			return out;
		}

		// Conversion monde -> UV (origine (originX, originZ), couvre [origine, origine + worldSize]).
		const float u = (worldX - terrainOriginX) / terrainWorldSize;
		const float v = (worldZ - terrainOriginZ) / terrainWorldSize;
		if (u < 0.0f || u > 1.0f || v < 0.0f || v > 1.0f)
		{
			// Hors du carre terrain : on ne tente meme pas un clamp (le caller doit pouvoir
			// distinguer "hors zone" de "dans zone, pas de son" via valid=false).
			return out;
		}

		// Sampling nearest (cf. doxy : transitions splat suffisamment graduelles vs cadence du pas).
		// Conversion UV -> indice texel : u * width donne un float dans [0, width], on clamp a [0, width-1].
		uint32_t tx = static_cast<uint32_t>(u * static_cast<float>(splatWidth));
		uint32_t ty = static_cast<uint32_t>(v * static_cast<float>(splatHeight));
		if (tx >= splatWidth)  { tx = splatWidth - 1u; }
		if (ty >= splatHeight) { ty = splatHeight - 1u; }

		const size_t pixelOffset = (static_cast<size_t>(ty) * splatWidth + tx) * 4u;
		const uint8_t r = splatRgba[pixelOffset + 0u];
		const uint8_t g = splatRgba[pixelOffset + 1u];
		const uint8_t b = splatRgba[pixelOffset + 2u];
		const uint8_t a = splatRgba[pixelOffset + 3u];

		// Identifie la couche dominante (channel max). Egalite : ordre stable R > G > B > A.
		uint8_t maxValue = r;
		uint32_t maxLayer = 0u;
		if (g > maxValue) { maxValue = g; maxLayer = 1u; }
		if (b > maxValue) { maxValue = b; maxLayer = 2u; }
		if (a > maxValue) { maxValue = a; maxLayer = 3u; }

		// Poids normalise : valeur du dominant / somme des 4 channels (0..1).
		const float sum = static_cast<float>(r) + static_cast<float>(g)
			+ static_cast<float>(b) + static_cast<float>(a);
		const float weight = (sum > 0.0f) ? (static_cast<float>(maxValue) / sum) : 0.0f;

		out.dominantLayer = maxLayer;
		out.dominantWeight = weight;
		out.valid = true;
		return out;
	}

} // namespace engine::render::terrain
