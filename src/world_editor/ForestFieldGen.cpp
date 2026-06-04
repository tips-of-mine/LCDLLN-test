// M100.19 — Implémentation des générateurs Forest & Field (purs, déterministes).

#include "src/world_editor/ForestFieldGen.h"

#include <algorithm>
#include <cmath>
#include <random>
#include <string>

namespace engine::editor::world
{
	using engine::math::Vec3;
	using engine::world::foliage::FoliageInstance;

	namespace
	{
		constexpr float kPi = 3.14159265358979323846f;

		uint32_t Fnv1a(const std::string& s)
		{
			uint32_t h = 2166136261u;
			for (unsigned char c : s) { h ^= c; h *= 16777619u; }
			return h;
		}

		// Aire (valeur absolue) d'un polygone en XZ via la formule du lacet.
		float PolygonArea(const std::vector<Vec3>& poly)
		{
			if (poly.size() < 3) return 0.0f;
			double a = 0.0;
			for (size_t i = 0; i < poly.size(); ++i)
			{
				const Vec3& p0 = poly[i];
				const Vec3& p1 = poly[(i + 1) % poly.size()];
				a += static_cast<double>(p0.x) * p1.z - static_cast<double>(p1.x) * p0.z;
			}
			return static_cast<float>(std::fabs(a) * 0.5);
		}

		// Test point-dans-polygone (ray casting) en XZ.
		bool PointInPolygon(const std::vector<Vec3>& poly, float x, float z)
		{
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

	std::vector<FoliageInstance> GenerateForest(
		const std::vector<Vec3>& polygon, const ForestRecipe& recipe,
		const engine::world::foliage::FoliageLibrary& library, const TerrainSampler& sampler)
	{
		std::vector<FoliageInstance> out;
		if (polygon.size() < 3 || recipe.entries.empty()) return out;

		const float area = PolygonArea(polygon);
		const float totalDensity = recipe.TotalDensity();
		const int target = static_cast<int>(std::lround(static_cast<double>(area) * totalDensity));
		if (target <= 0) return out;

		// bbox
		float minX = polygon[0].x, maxX = polygon[0].x, minZ = polygon[0].z, maxZ = polygon[0].z;
		for (const auto& p : polygon)
		{
			minX = std::min(minX, p.x); maxX = std::max(maxX, p.x);
			minZ = std::min(minZ, p.z); maxZ = std::max(maxZ, p.z);
		}

		// poids cumulés
		std::vector<float> cum;
		float wsum = 0.0f;
		for (const auto& e : recipe.entries) { wsum += (e.weight > 0.0f ? e.weight : 0.0f); cum.push_back(wsum); }
		if (wsum <= 0.0f) return out;

		std::mt19937_64 rng(recipe.seed);
		std::uniform_real_distribution<float> ux(minX, maxX), uz(minZ, maxZ), u01(0.0f, 1.0f);
		std::uniform_real_distribution<float> uyaw(0.0f, 2.0f * kPi), uscale(0.95f, 1.05f);

		int accepted = 0;
		const int maxTries = target * 64 + 64;
		for (int t = 0; t < maxTries && accepted < target; ++t)
		{
			const float x = ux(rng), z = uz(rng);
			if (!PointInPolygon(polygon, x, z)) continue;
			++accepted; // point in-polygon « consommé » (densité)

			// sélection d'asset par poids
			const float r = u01(rng) * wsum;
			size_t idx = 0;
			while (idx + 1 < cum.size() && r > cum[idx]) ++idx;
			const ForestRecipeEntry& entry = recipe.entries[idx];

			const TerrainSample ts = sampler(x, z);
			if (const auto* asset = library.FindAsset(entry.assetId))
			{
				if (!engine::world::foliage::PassesRules(asset->rules, ts.slopeDeg, ts.altMeters, ts.splatLayer))
					continue; // filtré par les règles
			}

			FoliageInstance inst;
			inst.assetIdHash = Fnv1a(entry.assetId);
			inst.position = Vec3(x, ts.terrainY, z);
			inst.rotationY = uyaw(rng);
			inst.scale = uscale(rng);
			out.push_back(inst);
		}
		return out;
	}

	std::vector<FoliageInstance> GenerateField(
		const FieldParams& params, uint32_t assetIdHash, const TerrainSampler& sampler)
	{
		std::vector<FoliageInstance> out;
		if (params.spacing <= 0.0f || params.width <= 0.0f || params.depth <= 0.0f) return out;

		const float c = std::cos(params.rotationDeg * kPi / 180.0f);
		const float s = std::sin(params.rotationDeg * kPi / 180.0f);

		std::mt19937_64 rng(params.seed);
		std::uniform_real_distribution<float> uscale(0.95f, 1.05f);

		const int nx = static_cast<int>(params.width / params.spacing);
		const int nz = static_cast<int>(params.depth / params.spacing);
		for (int j = 0; j <= nz; ++j)
		{
			for (int i = 0; i <= nx; ++i)
			{
				const float lx = static_cast<float>(i) * params.spacing;
				const float lz = static_cast<float>(j) * params.spacing;
				// rotation de la grille autour du coin
				const float wx = params.corner.x + (lx * c - lz * s);
				const float wz = params.corner.z + (lx * s + lz * c);
				const TerrainSample ts = sampler(wx, wz);
				FoliageInstance inst;
				inst.assetIdHash = assetIdHash;
				inst.position = Vec3(wx, ts.terrainY, wz);
				inst.rotationY = params.rotationDeg * kPi / 180.0f;
				inst.scale = uscale(rng);
				out.push_back(inst);
			}
		}
		return out;
	}
}
