// M100.31 — Implémentation de la génération d'hameau (pure, déterministe).

#include "src/world_editor/HamletGen.h"

#include "src/client/world/foliage/PoissonDiskSampler.h"

#include <algorithm>
#include <cmath>
#include <random>

namespace engine::editor::world
{
	using engine::math::Vec3;
	using engine::world::instances::PropInstance;

	namespace
	{
		uint32_t Fnv1a(const std::string& s) { uint32_t h = 2166136261u; for (unsigned char c : s) { h ^= c; h *= 16777619u; } return h; }

		bool PointInPolygon(const std::vector<Vec3>& poly, float x, float z)
		{
			if (poly.size() < 3) return false;
			bool inside = false;
			for (size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++)
			{
				const float xi = poly[i].x, zi = poly[i].z, xj = poly[j].x, zj = poly[j].z;
				if (((zi > z) != (zj > z)) && (x < (xj - xi) * (z - zi) / (zj - zi + 1e-9f) + xi)) inside = !inside;
			}
			return inside;
		}

		// Point le plus proche sur une polyline (XZ) + distance.
		bool NearestOnRoad(const std::vector<Vec3>& road, float x, float z, Vec3& outPoint, float& outDist)
		{
			if (road.size() < 2) return false;
			float best = 1e18f; Vec3 bestPt = road[0];
			for (size_t i = 1; i < road.size(); ++i)
			{
				const float ax = road[i-1].x, az = road[i-1].z, bx = road[i].x, bz = road[i].z;
				const float dx = bx - ax, dz = bz - az; const float len2 = dx*dx + dz*dz;
				float t = (len2 > 1e-9f) ? ((x-ax)*dx + (z-az)*dz)/len2 : 0.0f;
				t = std::max(0.0f, std::min(1.0f, t));
				const float cx = ax + t*dx, cz = az + t*dz;
				const float ex = x - cx, ez = z - cz; const float d = std::sqrt(ex*ex + ez*ez);
				if (d < best) { best = d; bestPt = Vec3(cx, 0.0f, cz); }
			}
			outPoint = bestPt; outDist = best; return true;
		}
	} // namespace

	std::vector<PropInstance> GenerateHamlet(
		const std::vector<Vec3>& polygon, const HamletRecipe& recipe,
		const HamletSampler& sampler, const std::vector<Vec3>& road, uint32_t& nextInstanceId)
	{
		std::vector<PropInstance> out;
		if (polygon.size() < 3 || recipe.houseMeshes.empty() || recipe.houseCount <= 0) return out;

		float minX = polygon[0].x, maxX = polygon[0].x, minZ = polygon[0].z, maxZ = polygon[0].z;
		for (const auto& p : polygon) { minX = std::min(minX, p.x); maxX = std::max(maxX, p.x); minZ = std::min(minZ, p.z); maxZ = std::max(maxZ, p.z); }

		// Candidats Poisson-disk (réutilise M100.18) garantissant min_spacing.
		auto candidates = engine::world::foliage::SamplePoissonDisk(maxX - minX, maxZ - minZ, recipe.minSpacing, recipe.seed);

		// Poids cumulés des meshes.
		std::vector<float> cum; float wsum = 0.0f;
		for (const auto& h : recipe.houseMeshes) { wsum += (h.second > 0.0f ? h.second : 0.0f); cum.push_back(wsum); }
		std::mt19937_64 rng(recipe.seed ^ 0x9E3779B97F4A7C15ull);
		std::uniform_real_distribution<float> u01(0.0f, 1.0f);

		for (const auto& cLocal : candidates)
		{
			if (static_cast<int>(out.size()) >= recipe.houseCount) break;
			float wx = minX + cLocal.x, wz = minZ + cLocal.z;
			if (!PointInPolygon(polygon, wx, wz)) continue;
			HamletTerrainSample ts = sampler ? sampler(wx, wz) : HamletTerrainSample{};
			if (ts.slopeDeg > recipe.maxSlopeDeg) continue;

			if (recipe.snapToRoad && road.size() >= 2)
			{
				Vec3 rp; float rd = 0.0f;
				if (NearestOnRoad(road, wx, wz, rp, rd) && rd <= recipe.roadSnapRangeMeters && rd > 1e-4f)
				{
					// Décale de `roadOffsetMeters` du côté du candidat.
					const float nx = (wx - rp.x) / rd, nz = (wz - rp.z) / rd;
					wx = rp.x + nx * recipe.roadOffsetMeters;
					wz = rp.z + nz * recipe.roadOffsetMeters;
					ts = sampler ? sampler(wx, wz) : ts;
				}
			}

			// Sélection mesh pondérée.
			uint32_t assetId = 0;
			if (wsum > 0.0f)
			{
				const float r = u01(rng) * wsum; size_t idx = 0;
				while (idx + 1 < cum.size() && r > cum[idx]) ++idx;
				assetId = Fnv1a(recipe.houseMeshes[idx].first);
			}

			PropInstance inst;
			inst.assetId = assetId;
			inst.position = Vec3(wx, ts.terrainY, wz);
			inst.rotationQuat[3] = 1.0f; // identité (orientation détaillée différée)
			inst.scale = Vec3(1.0f, 1.0f, 1.0f);
			inst.layerTag = static_cast<uint32_t>(engine::world::instances::PlacementLayer::Structures);
			inst.instanceId = nextInstanceId++;
			out.push_back(inst);
		}
		return out;
	}
}
