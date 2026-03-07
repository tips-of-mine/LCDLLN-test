#pragma once

#include <cstdint>

namespace engine::render
{
	/// M07.1: TAA subpixel jitter using Halton(2,3) sequence for reprojection.
	/// Jitter in NDC: (2 * jitterPx) / resolution (spec M07.1).

	/// Radical inverse in given base (0 < base). Returns value in [0, 1).
	inline float Halton(uint32_t base, uint32_t index)
	{
		float result = 0.0f;
		float f = 1.0f;
		uint32_t i = index;
		while (i != 0u)
		{
			f /= static_cast<float>(base);
			result += f * static_cast<float>(i % base);
			i /= base;
		}
		return result;
	}

	/// Number of Halton samples in the cycle (8–16 per ticket).
	constexpr uint32_t kTaaHaltonN = 8u;

	/// Returns subpixel jitter in NDC for the given sample index (0 .. kTaaHaltonN-1).
	/// jitterNDC = (2 * (halton - 0.5)) / resolution, so range per axis is [-1/res, 1/res].
	/// \param sampleIndex  Frame index mod N (0-based).
	/// \param width        Viewport width (pixels).
	/// \param height       Viewport height (pixels).
	/// \param outJitterX   Output NDC jitter X.
	/// \param outJitterY   Output NDC jitter Y.
	inline void GetJitterNdc(uint32_t sampleIndex, uint32_t width, uint32_t height,
		float& outJitterX, float& outJitterY)
	{
		const uint32_t i = sampleIndex % kTaaHaltonN;
		const float h2 = Halton(2u, i);
		const float h3 = Halton(3u, i);
		// jitterPx in [-0.5, 0.5], NDC = (2 * jitterPx) / resolution
		const float jitterPxX = h2 - 0.5f;
		const float jitterPxY = h3 - 0.5f;
		const float rw = (width > 0u) ? (1.0f / static_cast<float>(width)) : 0.0f;
		const float rh = (height > 0u) ? (1.0f / static_cast<float>(height)) : 0.0f;
		outJitterX = (2.0f * jitterPxX) * rw;
		outJitterY = (2.0f * jitterPxY) * rh;
	}
}
