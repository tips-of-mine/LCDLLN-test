// M100.20 — Implémentation WindSystem (pur, miroir shader + eval zones).

#include "src/client/world/wind/WindSystem.h"

#include <cmath>

namespace engine::world::wind
{
	namespace
	{
		constexpr float kTwoPi = 6.28318530718f;

		bool PointInPolygon(const std::vector<engine::math::Vec3>& poly, float x, float z)
		{
			if (poly.size() < 3) return false;
			bool inside = false;
			for (size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++)
			{
				const float xi = poly[i].x, zi = poly[i].z;
				const float xj = poly[j].x, zj = poly[j].z;
				const bool intersect = ((zi > z) != (zj > z)) &&
					(x < (xj - xi) * (z - zi) / (zj - zi + 1e-9f) + xi);
				if (intersect) inside = !inside;
			}
			return inside;
		}
	} // namespace

	float SwayHeightWeight(float h)
	{
		if (h <= 0.0f) return 0.0f;
		if (h >= 1.0f) return 1.0f;
		return h * h * (3.0f - 2.0f * h); // smoothstep(0,1,h)
	}

	float ComputeSwayMagnitude(const WindParamsCpu& w, float worldX, float worldZ, float h)
	{
		const float heightWeight = SwayHeightWeight(h);
		const float wavePhase = (worldX * w.directionX + worldZ * w.directionZ) /
		                        (w.waveLengthMeters > 1e-5f ? w.waveLengthMeters : 1.0f)
		                        - w.waveSpeed * w.timeSeconds;
		const float wave = std::sin(wavePhase) * w.waveAmplitude;
		const float turb = (std::sin(w.timeSeconds * w.turbulenceFreq * kTwoPi + worldX) +
		                    std::sin(w.timeSeconds * w.turbulenceFreq * kTwoPi + worldZ)) * w.turbulenceAmp;
		const float swayScalar = (w.forceMps * 0.05f + wave + turb) * heightWeight;
		return std::fabs(swayScalar);
	}

	WindParamsCpu EvaluateWind(const WindParamsCpu& global, const std::vector<WindZone>& zones,
	                           float camX, float camZ)
	{
		WindParamsCpu eff = global;
		for (const WindZone& z : zones)
		{
			if (PointInPolygon(z.polygon, camX, camZ))
			{
				eff.directionX = z.directionX;
				eff.directionZ = z.directionZ;
				eff.forceMps = z.forceMps;
				eff.turbulenceFreq = z.turbulenceFreq;
				eff.turbulenceAmp = z.turbulenceAmp;
				break; // première zone gagnante
			}
		}
		return eff;
	}
}
