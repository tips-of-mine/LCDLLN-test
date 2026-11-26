// src/client/world/water/SeaSwell.cpp
// EAU.6 — Houle de la mer et jet de rive (CPU). Double exact de water.vert :
// voir la règle de parité dans SeaSwell.h.
#include "src/client/world/water/SeaSwell.h"

#include <cmath>

namespace engine::world::water
{
	namespace
	{
		constexpr float kTwoPi = 6.2831853f;
		constexpr float kGravity = 9.81f;

		/// Même définition que smoothstep() en GLSL (bornes dans l'ordre e0 < e1
		/// ou inversées, comme en GLSL).
		float SmoothStep(float e0, float e1, float x)
		{
			float t = (x - e0) / (e1 - e0);
			t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
			return t * t * (3.0f - 2.0f * t);
		}
	}

	float SeaSwellDepthScale(float depthM)
	{
		return SmoothStep(kSeaSwellShallowM, kSeaSwellFullDepthM, depthM);
	}

	float SeaSwellHeight(float x, float z, float timeS, const SeaSwellParams& params)
	{
		float wx = params.windDirX, wz = params.windDirZ;
		const float wl = std::sqrt(wx * wx + wz * wz);
		if (wl > 1e-3f) { wx /= wl; wz /= wl; }
		else { wx = 1.0f; wz = 0.0f; }

		float h = 0.0f;
		for (int i = 0; i < 4; ++i)
		{
			const float a = kSeaSwellAngle[i];
			float dx = wx * std::cos(a) - wz * std::sin(a);
			float dz = wx * std::sin(a) + wz * std::cos(a);
			const float dl = std::sqrt(dx * dx + dz * dz);
			dx /= dl; dz /= dl;
			const float k = kTwoPi / kSeaSwellLengthM[i];
			const float c = std::sqrt(kGravity / k);  // vitesse de phase (eau profonde)
			const float ph = k * (dx * x + dz * z) - k * c * timeS;
			h += (kSeaSwellWeight[i] / kSeaSwellWeightSum) * std::sin(ph);
		}
		return params.amplitudeM * h;
	}

	float SeaSurfPhase(float depthM, float timeS)
	{
		return kTwoPi * (timeS / kSeaSurfPeriodS) + kSeaSurfPhasePerMeter * depthM;
	}

	float SeaSurge(float x, float z, float depthM, float timeS)
	{
		// Zone de bord : entière sur la plage et en eau très peu profonde,
		// éteinte au-delà de kSeaSurfZoneDepthM.
		const float zone = 1.0f - SmoothStep(0.8f, kSeaSurfZoneDepthM, depthM);
		if (zone <= 0.0f)
			return 0.0f;
		const float s = std::fmax(0.0f, std::sin(SeaSurfPhase(depthM, timeS)));
		// Variation lente le long du rivage : les vagues ne montent pas toutes
		// à la même hauteur partout.
		const float along = 0.75f + 0.25f * std::sin(0.031f * x + 0.017f * z + 0.05f * timeS);
		return kSeaRunUpM * along * s * s * zone;
	}

	float SeaSurfaceOffset(float x, float z, float depthM, float timeS, const SeaSwellParams& params)
	{
		return SeaSwellHeight(x, z, timeS, params) * SeaSwellDepthScale(depthM)
			+ SeaSurge(x, z, depthM, timeS);
	}

	long long SeaSurfBreakIndex(float timeS)
	{
		// Sommet au rivage : phase(0, t) = π/2 + 2πn  <=>  t / T − 1/4 = n.
		return static_cast<long long>(std::floor(timeS / kSeaSurfPeriodS - 0.25f));
	}
}
