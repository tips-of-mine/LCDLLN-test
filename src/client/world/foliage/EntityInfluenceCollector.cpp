// M100.21 — Implémentation du collecteur d'influences (pur).

#include "src/client/world/foliage/EntityInfluenceCollector.h"

#include <algorithm>
#include <cmath>

namespace engine::world::foliage
{
	std::vector<EntityInfluence> CollectEntityInfluences(
		float camX, float camZ, const std::vector<EntityCandidate>& entities)
	{
		struct Scored { float dist2; EntityInfluence inf; };
		std::vector<Scored> scored;
		scored.reserve(entities.size());

		const float range2 = kInfluenceRangeMeters * kInfluenceRangeMeters;
		for (const EntityCandidate& e : entities)
		{
			const float dx = e.x - camX, dz = e.z - camZ;
			const float d2 = dx * dx + dz * dz;
			if (d2 > range2) continue; // hors portée
			EntityInfluence inf;
			inf.positionX = e.x; inf.positionZ = e.z;
			inf.radiusMeters = e.radiusMeters; inf.falloffPower = e.falloffPower;
			scored.push_back({ d2, inf });
		}

		std::stable_sort(scored.begin(), scored.end(),
		                 [](const Scored& a, const Scored& b) { return a.dist2 < b.dist2; });

		std::vector<EntityInfluence> out;
		const size_t n = std::min<size_t>(scored.size(), static_cast<size_t>(kMaxEntityInfluences));
		out.reserve(n);
		for (size_t i = 0; i < n; ++i) out.push_back(scored[i].inf);
		return out;
	}

	float ComputeFlexionMagnitude(const EntityInfluence& inf, float worldX, float worldZ, float heightWeight)
	{
		const float dx = worldX - inf.positionX, dz = worldZ - inf.positionZ;
		const float dist = std::sqrt(dx * dx + dz * dz);
		const float r = (inf.radiusMeters > 1e-5f) ? inf.radiusMeters : 1e-5f;
		if (dist >= r) return 0.0f;
		const float w = std::pow(1.0f - dist / r, inf.falloffPower);
		return w * 0.6f * heightWeight; // miroir foliage.vert
	}
}
