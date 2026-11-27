// src/client/world/water/SeaGrid.cpp
// EAU.6 — Maillage fin de la mer autour du joueur (voir SeaGrid.h).
#include "src/client/world/water/SeaGrid.h"

#include "src/client/world/water/SeaSwell.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace engine::world::water
{
	bool OceanRect(const LakeInstance& lake, SeaRect& out)
	{
		if (!lake.isOcean || lake.polygon.size() != 4)
			return false;
		float x0 = lake.polygon[0].x, x1 = x0, z0 = lake.polygon[0].z, z1 = z0;
		for (const engine::math::Vec3& p : lake.polygon)
		{
			x0 = std::min(x0, p.x); x1 = std::max(x1, p.x);
			z0 = std::min(z0, p.z); z1 = std::max(z1, p.z);
		}
		// Chaque sommet doit être un coin du rectangle englobant (tolérance 1 cm).
		constexpr float kTol = 0.01f;
		for (const engine::math::Vec3& p : lake.polygon)
		{
			const bool onX = std::fabs(p.x - x0) <= kTol || std::fabs(p.x - x1) <= kTol;
			const bool onZ = std::fabs(p.z - z0) <= kTol || std::fabs(p.z - z1) <= kTol;
			if (!onX || !onZ)
				return false;
		}
		out = SeaRect{ x0, z0, x1, z1 };
		return !out.Empty();
	}

	float SnapToStep(float v, float step)
	{
		if (!(step > 0.0f))
			return v;
		return std::floor(v / step + 0.5f) * step;
	}

	void SubtractSeaRect(const SeaRect& rect, const SeaRect& hole, std::vector<SeaRect>& out)
	{
		if (rect.Empty())
			return;
		// Partie commune.
		const float hx0 = std::max(rect.x0, hole.x0), hx1 = std::min(rect.x1, hole.x1);
		const float hz0 = std::max(rect.z0, hole.z0), hz1 = std::min(rect.z1, hole.z1);
		if (!(hx1 > hx0) || !(hz1 > hz0))
		{
			out.push_back(rect);
			return;
		}
		const SeaRect pieces[4] = {
			{ rect.x0, rect.z0, rect.x1, hz0 },  // bande sud (toute la largeur)
			{ rect.x0, hz1,     rect.x1, rect.z1 },  // bande nord (toute la largeur)
			{ rect.x0, hz0,     hx0,     hz1 },  // ouest du trou
			{ hx1,     hz0,     rect.x1, hz1 },  // est du trou
		};
		for (const SeaRect& p : pieces)
			if (!p.Empty())
				out.push_back(p);
	}

	bool BuildSeaGrid(const std::vector<LakeInstance>& lakes, float centerX, float centerZ,
		const SeaGridSettings& settings, const SeaGroundSampler& ground, SeaGridCpu& out)
	{
		out = SeaGridCpu{};
		out.centerX = centerX;
		out.centerZ = centerZ;
		if (!(settings.stepM > 0.0f) || !(settings.halfSizeM >= settings.stepM))
			return false;

		const int n = static_cast<int>(std::lround(2.0f * settings.halfSizeM / settings.stepM));
		const float x0 = centerX - settings.halfSizeM;
		const float z0 = centerZ - settings.halfSizeM;
		out.area = SeaRect{ x0, z0, x0 + static_cast<float>(n) * settings.stepM, z0 + static_cast<float>(n) * settings.stepM };

		// Rectangles de mer qui recoupent la grille (la scène contient la mer de
		// toutes les cellules générées) et niveau de la mer (premier trouvé).
		std::vector<SeaRect> rects;
		bool haveOcean = false;
		float seaLevel = 0.0f;
		for (uint32_t i = 0; i < static_cast<uint32_t>(lakes.size()); ++i)
		{
			SeaRect r;
			if (!OceanRect(lakes[i], r))
				continue;
			if (r.x1 <= out.area.x0 || r.x0 >= out.area.x1 || r.z1 <= out.area.z0 || r.z0 >= out.area.z1)
				continue;
			if (!haveOcean)
			{
				haveOcean = true;
				seaLevel = lakes[i].waterLevelY;
				out.oceanLakeIndex = i;
			}
			rects.push_back(r);
		}
		if (!haveOcean)
		{
			out.area = SeaRect{};
			return false;
		}

		const int side = n + 1;
		const uint32_t kNone = std::numeric_limits<uint32_t>::max();
		auto slotOf = [side](int i, int j) {
			return static_cast<size_t>(j) * static_cast<size_t>(side) + static_cast<size_t>(i);
		};
		std::vector<uint32_t> remap(static_cast<size_t>(side) * static_cast<size_t>(side), kNone);
		// Profondeur par sommet, échantillonnée une seule fois.
		std::vector<float> depth(remap.size(), std::numeric_limits<float>::quiet_NaN());
		auto depthAt = [&](int i, int j) -> float {
			float& d = depth[slotOf(i, j)];
			if (std::isnan(d))
			{
				const float x = x0 + static_cast<float>(i) * settings.stepM;
				const float z = z0 + static_cast<float>(j) * settings.stepM;
				bool known = false;
				const float h = ground ? ground(x, z, known) : 0.0f;
				d = (known && std::isfinite(h)) ? (seaLevel - h) : kSeaDeepWaterM;
				if (!(known && std::isfinite(h)))
					++out.unknownSamples;
			}
			return d;
		};
		auto vertexAt = [&](int i, int j) -> uint32_t {
			uint32_t& slot = remap[slotOf(i, j)];
			if (slot == kNone)
			{
				SeaGridVertex v;
				const float x = x0 + static_cast<float>(i) * settings.stepM;
				const float z = z0 + static_cast<float>(j) * settings.stepM;
				v.position = engine::math::Vec3{ x, seaLevel, z };
				v.depthM = depthAt(i, j);
				// Fondu de bord (distance de Tchebychev au centre) : 0 exactement
				// sur le bord de la grille, raccord sans marche avec la mer plate.
				const float r = std::max(std::fabs(x - centerX), std::fabs(z - centerZ));
				const float e0 = settings.halfSizeM - settings.edgeFadeM;
				float t = (r - e0) / std::max(settings.edgeFadeM, 1e-3f);
				t = std::clamp(t, 0.0f, 1.0f);
				v.edgeFade = 1.0f - t * t * (3.0f - 2.0f * t);
				slot = static_cast<uint32_t>(out.vertices.size());
				out.vertices.push_back(v);
			}
			return slot;
		};

		for (int j = 0; j < n; ++j)
			for (int i = 0; i < n; ++i)
			{
				const float cx = x0 + (static_cast<float>(i) + 0.5f) * settings.stepM;
				const float cz = z0 + (static_cast<float>(j) + 0.5f) * settings.stepM;
				bool inSea = false;
				for (const SeaRect& r : rects)
					if (r.Contains(cx, cz)) { inSea = true; break; }
				if (!inSea)
					continue;
				// Carreau entièrement sous la terre : jamais atteint par l'eau.
				if (depthAt(i, j) < settings.dryCutoffM && depthAt(i + 1, j) < settings.dryCutoffM
					&& depthAt(i + 1, j + 1) < settings.dryCutoffM && depthAt(i, j + 1) < settings.dryCutoffM)
					continue;
				const uint32_t a = vertexAt(i, j);
				const uint32_t b = vertexAt(i + 1, j);
				const uint32_t c = vertexAt(i + 1, j + 1);
				const uint32_t d = vertexAt(i, j + 1);
				// Même sens que BuildLakeMesh (CCW dans XZ, normale +Y).
				out.indices.push_back(a); out.indices.push_back(b); out.indices.push_back(c);
				out.indices.push_back(a); out.indices.push_back(c); out.indices.push_back(d);
			}
		return out.Valid();
	}
}
