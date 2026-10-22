// tools/world_gen — terrain : altitude de base depuis la grille du lore,
// plateaux urbains, rivières et lacs.
//
// Principe : chaque code de cellule du lore porte une altitude de base, une
// amplitude de relief doux (fbm) et une amplitude de crêtes (ridged). Ces trois
// paramètres sont interpolés en continu entre centres de cellules (poids
// lissés) à une position légèrement DÉFORMÉE par un bruit (domain warp) : les
// côtes et lisières perdent leur aspect quadrillé tout en respectant la carte
// du lore à l'échelle de la cellule.

#include "WorldGenContext.h"
#include "Random.h"

#include <algorithm>
#include <cmath>
#include <queue>

namespace tools::world_gen
{
	bool ParseCellId(const std::string& id, int& n, int& e);

	namespace
	{
		struct CodeProfile
		{
			double base;   ///< altitude moyenne (m)
			double amp;    ///< amplitude du relief doux (m)
			double ridge;  ///< amplitude des crêtes (m)
		};

		CodeProfile ProfileOf(char c)
		{
			switch (c)
			{
			case ' ': return { 6.0, 3.0, 0.0 };     // mer profonde
			case '-': return { 28.0, 3.0, 0.0 };    // haut-fond
			case ',': return { 39.5, 2.0, 0.0 };    // côte (le rivage passe dedans)
			case '_': return { 42.5, 1.5, 0.0 };    // plage
			case 'o': return { 47.0, 4.0, 0.0 };    // îlot
			case '.': return { 50.0, 5.0, 0.0 };    // plaine
			case 'r': return { 48.0, 4.0, 0.0 };    // plaine + rivière
			case 'T': return { 56.0, 8.0, 0.0 };    // forêt
			case 't': return { 53.0, 6.0, 0.0 };    // lisière
			case 'R': return { 52.0, 6.0, 0.0 };    // forêt + rivière
			case 'L': return { 54.0, 6.0, 0.0 };    // forêt + lac
			case '~': return { 58.0, 9.0, 0.0 };    // désert (dunes)
			case ':': return { 56.0, 7.0, 0.0 };    // terres arides
			case 'l': return { 54.0, 5.0, 0.0 };    // aride + lac
			case '^': return { 110.0, 25.0, 120.0 };// montagne
			case 'M': return { 140.0, 30.0, 170.0 };// chaîne
			case '*': return { 190.0, 35.0, 190.0 };// sommets enneigés
			case 'V': return { 85.0, 20.0, 70.0 };  // volcanique
			case '%': return { 41.5, 1.2, 0.0 };    // marais
			case '&': return { 41.2, 1.0, 0.0 };    // marais + rivière
			default:  return { 50.0, 5.0, 0.0 };
			}
		}

		bool IsRiverCode(char c) { return c == 'r' || c == 'R' || c == '&'; }
		bool IsLakeCode(char c) { return c == 'L' || c == 'l'; }

		double CityRadius(const std::string& kind)
		{
			if (kind == "capitale") return 62.0;
			if (kind == "grande_ville") return 48.0;
			if (kind == "ville") return 38.0;
			if (kind == "forteresse") return 32.0;
			if (kind == "village") return 28.0;
			if (kind == "camp") return 26.0;
			return 18.0; // hameau
		}

		/// Applique une fonction à chaque échantillon d'un disque de la grille.
		template <typename F>
		void ForDisc(Grid<double>& g, Vec2 c, double r, F&& fn)
		{
			const int i0 = std::max(0, g.IndexX(c.x - r) - 1), i1 = std::min(g.width - 1, g.IndexX(c.x + r) + 1);
			const int j0 = std::max(0, g.IndexZ(c.z - r) - 1), j1 = std::min(g.height - 1, g.IndexZ(c.z + r) + 1);
			for (int j = j0; j <= j1; ++j)
				for (int i = i0; i <= i1; ++i)
				{
					const double dx = g.WorldX(i) - c.x, dz = g.WorldZ(j) - c.z;
					const double d = std::sqrt(dx * dx + dz * dz);
					if (d <= r) fn(i, j, d);
				}
		}
	}

	// ------------------------------------------------------------------ helpers

	double SampleBilinear(const Grid<double>& g, double x, double z)
	{
		if (g.data.empty()) return 0.0;
		double fx = (x - g.originX) / g.step, fz = (z - g.originZ) / g.step;
		fx = Clamp(fx, 0.0, g.width - 1.000001);
		fz = Clamp(fz, 0.0, g.height - 1.000001);
		const int i = static_cast<int>(fx), j = static_cast<int>(fz);
		const double tx = fx - i, tz = fz - j;
		const double a = Lerp(g.At(i, j), g.At(i + 1, j), tx);
		const double b = Lerp(g.At(i, j + 1), g.At(i + 1, j + 1), tx);
		return Lerp(a, b, tz);
	}

	double DistPointSegment(Vec2 p, Vec2 a, Vec2 b, double* outT)
	{
		const Vec2 ab = b - a, ap = p - a;
		const double len2 = ab.x * ab.x + ab.z * ab.z;
		double t = len2 > 1e-12 ? (ap.x * ab.x + ap.z * ab.z) / len2 : 0.0;
		t = Clamp(t, 0.0, 1.0);
		if (outT) *outT = t;
		const double dx = a.x + ab.x * t - p.x, dz = a.z + ab.z * t - p.z;
		return std::sqrt(dx * dx + dz * dz);
	}

	bool World::CellAt(Vec2 p, int& n, int& e) const
	{
		const double C = master.scale.cellSizeM;
		e = static_cast<int>(std::lround(p.x / C)) + master.scale.originE;
		n = master.scale.originN - static_cast<int>(std::lround(p.z / C));
		const int row = master.scale.northN - n;
		const int col = e - master.scale.westE;
		return row >= 0 && row < static_cast<int>(master.gridRows.size()) && col >= 0 && col < master.scale.gridCols;
	}

	char World::CellCode(int n, int e) const
	{
		const int row = master.scale.northN - n;
		const int col = e - master.scale.westE;
		if (row < 0 || row >= static_cast<int>(master.gridRows.size()) || col < 0 || col >= master.scale.gridCols)
			return ' ';
		return master.gridRows[static_cast<size_t>(row)][static_cast<size_t>(col)];
	}

	double World::SlopeDegAt(Vec2 p) const
	{
		const double s = height.step;
		const double hx = SampleBilinear(height, p.x + s, p.z) - SampleBilinear(height, p.x - s, p.z);
		const double hz = SampleBilinear(height, p.x, p.z + s) - SampleBilinear(height, p.x, p.z - s);
		const double g = std::sqrt(hx * hx + hz * hz) / (2.0 * s);
		return std::atan(g) * 57.29577951308232;
	}

	bool World::IsWater(Vec2 p) const
	{
		if (HeightAt(p) < master.scale.seaLevelM + 0.4) return true;
		if (!riverDist.data.empty())
		{
			const int i = riverDist.IndexX(p.x), j = riverDist.IndexZ(p.z);
			if (riverDist.InBounds(i, j) && riverDist.At(i, j) < 7.5f) return true;
		}
		for (const Lake& l : lakes)
		{
			const double dx = p.x - l.center.x, dz = p.z - l.center.z;
			if (dx * dx + dz * dz < (l.radiusM + 2.0) * (l.radiusM + 2.0)) return true;
		}
		return false;
	}

	bool World::InPlayableBounds(Vec2 p, double marginM) const
	{
		return p.x > terrainOriginX + marginM && p.z > terrainOriginZ + marginM
			&& p.x < terrainOriginX + terrainSizeM - marginM && p.z < terrainOriginZ + terrainSizeM - marginM;
	}

	void World::Occupy(Vec2 c, double r)
	{
		const int i0 = std::max(0, occupied.IndexX(c.x - r)), i1 = std::min(occupied.width - 1, occupied.IndexX(c.x + r));
		const int j0 = std::max(0, occupied.IndexZ(c.z - r)), j1 = std::min(occupied.height - 1, occupied.IndexZ(c.z + r));
		const double r2 = r * r;
		for (int j = j0; j <= j1; ++j)
			for (int i = i0; i <= i1; ++i)
			{
				const double dx = occupied.WorldX(i) - c.x, dz = occupied.WorldZ(j) - c.z;
				if (dx * dx + dz * dz <= r2) occupied.At(i, j) = 1;
			}
	}

	bool World::IsOccupied(Vec2 c, double r) const
	{
		const int i0 = std::max(0, occupied.IndexX(c.x - r)), i1 = std::min(occupied.width - 1, occupied.IndexX(c.x + r));
		const int j0 = std::max(0, occupied.IndexZ(c.z - r)), j1 = std::min(occupied.height - 1, occupied.IndexZ(c.z + r));
		const double r2 = r * r;
		for (int j = j0; j <= j1; ++j)
			for (int i = i0; i <= i1; ++i)
			{
				const double dx = occupied.WorldX(i) - c.x, dz = occupied.WorldZ(j) - c.z;
				if (dx * dx + dz * dz <= r2 && occupied.At(i, j)) return true;
			}
		return false;
	}

	namespace
	{
		/// Parcourt les cases de la grille d'occupation couvertes par un rectangle orienté.
		template <typename F>
		void ForRect(const Grid<uint8_t>& g, Vec2 c, double hx, double hz, double yawDeg, F&& fn)
		{
			const double a = yawDeg * 3.14159265358979323846 / 180.0;
			const double ca = std::cos(a), sa = std::sin(a);
			const double r = std::sqrt(hx * hx + hz * hz);
			const int i0 = std::max(0, g.IndexX(c.x - r)), i1 = std::min(g.width - 1, g.IndexX(c.x + r));
			const int j0 = std::max(0, g.IndexZ(c.z - r)), j1 = std::min(g.height - 1, g.IndexZ(c.z + r));
			for (int j = j0; j <= j1; ++j)
				for (int i = i0; i <= i1; ++i)
				{
					const double dx = g.WorldX(i) - c.x, dz = g.WorldZ(j) - c.z;
					// Monde -> local (inverse de RotateY : x' = c x + s z ; z' = -s x + c z).
					const double lx = ca * dx - sa * dz;
					const double lz = sa * dx + ca * dz;
					if (std::fabs(lx) <= hx && std::fabs(lz) <= hz)
						if (fn(i, j)) return;
				}
		}
	}

	void World::OccupyRect(Vec2 c, double hx, double hz, double yawDeg)
	{
		ForRect(occupied, c, hx, hz, yawDeg, [&](int i, int j) { occupied.At(i, j) = 1; return false; });
	}

	bool World::IsOccupiedRect(Vec2 c, double hx, double hz, double yawDeg) const
	{
		bool hit = false;
		ForRect(occupied, c, hx, hz, yawDeg, [&](int i, int j) { if (occupied.At(i, j)) { hit = true; return true; } return false; });
		return hit;
	}

	const CityDef* World::FindCity(const std::string& id) const
	{
		for (const CityDef& c : master.cities)
			if (c.id == id) return &c;
		return nullptr;
	}

	// ------------------------------------------------------------- terrain base

	void BuildBaseTerrain(World& w)
	{
		const WorldScale& s = w.master.scale;
		const double C = s.cellSizeM;
		w.terrainSizeM = s.gridCols * C;
		w.terrainOriginX = (s.westE - s.originE - 0.5) * C;
		w.terrainOriginZ = -(s.northN - s.originN + 0.5) * C - s.padNorthRows * C;
		const double step = w.terrainSizeM / 1024.0;
		w.height.Init(1025, 1025, w.terrainOriginX, w.terrainOriginZ, step, 0.0);
		w.biome.Init(1025, 1025, w.terrainOriginX, w.terrainOriginZ, step, ' ');
		// Grille fine d'occupation à 1 m (bâtiments, mobilier, PNJ statiques).
		const int occN = static_cast<int>(w.terrainSizeM) + 1;
		w.occupied.Init(occN, occN, w.terrainOriginX, w.terrainOriginZ, 1.0, 0);

		// Résout les centres de villes / POI.
		for (CityDef& c : w.master.cities)
		{
			c.center = w.CellCenter(c.n, c.e);
			c.radiusM = CityRadius(c.kind);
		}
		for (PoiDef& p : w.master.pois) p.center = w.CellCenter(p.n, p.e);

		const uint64_t seed = w.master.seed;
		for (int j = 0; j < w.height.height; ++j)
		{
			for (int i = 0; i < w.height.width; ++i)
			{
				const double x = w.height.WorldX(i), z = w.height.WorldZ(j);
				// Domain warp (~35 % d'une cellule) : côtes et lisières organiques.
				// (fbm normalisé ≈ ±0,35 en pratique → déplacement effectif ~±0,3 cellule)
				const double wx = x + Fbm(x, z, 260.0, 3, seed + 11) * 0.9 * C;
				const double wz = z + Fbm(x + 917.0, z - 433.0, 260.0, 3, seed + 12) * 0.9 * C;
				// Coordonnées de cellule fractionnaires (centres aux entiers).
				const double ef = wx / C + s.originE;
				const double nf = s.originN - wz / C;
				const double e0 = std::floor(ef), n0 = std::floor(nf);
				const double te = SmoothStep(0.0, 1.0, ef - e0), tn = SmoothStep(0.0, 1.0, nf - n0);
				CodeProfile acc{ 0, 0, 0 };
				const int ie = static_cast<int>(e0), in = static_cast<int>(n0);
				const double wts[4] = { (1 - te) * (1 - tn), te * (1 - tn), (1 - te) * tn, te * tn };
				const int de[4] = { 0, 1, 0, 1 }, dn[4] = { 0, 0, 1, 1 };
				for (int k = 0; k < 4; ++k)
				{
					const CodeProfile p = ProfileOf(w.CellCode(in + dn[k], ie + de[k]));
					acc.base += p.base * wts[k];
					acc.amp += p.amp * wts[k];
					acc.ridge += p.ridge * wts[k];
				}
				// Biome : déformation PLUS forte et plus fine que l'altitude, pour que
				// lisières de forêt, bords de désert et marais perdent toute trace
				// de la grille du lore (sans changer la cellule dominante).
				{
					const double bx = x + Fbm(x - 311.0, z + 157.0, 170.0, 3, seed + 13) * 1.3 * C
						+ Fbm(x, z, 45.0, 2, seed + 14) * 0.3 * C;
					const double bz = z + Fbm(x + 211.0, z - 87.0, 170.0, 3, seed + 15) * 1.3 * C
						+ Fbm(x + 71.0, z + 29.0, 45.0, 2, seed + 16) * 0.3 * C;
					const int be = static_cast<int>(std::lround(bx / C + s.originE));
					const int bn = static_cast<int>(std::lround(s.originN - bz / C));
					w.biome.At(i, j) = w.CellCode(bn, be);
				}

				double h = acc.base;
				h += Fbm(x, z, 240.0, 5, seed + 21) * acc.amp;
				h += Fbm(x, z, 60.0, 3, seed + 22) * acc.amp * 0.25; // grain fin
				if (acc.ridge > 0.5)
				{
					const double r = Ridged(x, z, 340.0, 5, seed + 31);
					h += (r - 0.35) * acc.ridge;
				}
				// Dunes : ondulation directionnelle dans les déserts.
				const char bc = w.biome.At(i, j);
				if (bc == '~')
					h += std::sin((x * 0.8 + z * 0.6) / 22.0 + Fbm(x, z, 120.0, 2, seed + 41) * 3.0) * 2.2;
				// Liseré océanique : le terrain plonge vers le fond marin sur les
				// 1,2 dernières cellules du pourtour (pas de « mur » au bord du monde).
				{
					const double edge = std::min({ x - w.terrainOriginX, z - w.terrainOriginZ,
						w.terrainOriginX + w.terrainSizeM - x, w.terrainOriginZ + w.terrainSizeM - z });
					const double k = SmoothStep(1.2 * C, 0.25 * C, edge + Fbm(x, z, 300.0, 2, seed + 51) * 0.4 * C);
					h = Lerp(h, 6.0, k);
				}
				w.height.At(i, j) = Clamp(h, 1.0, s.heightScaleM - 2.0);
			}
		}
		w.baseHeight = w.height;
	}

	// ------------------------------------------------------------ plateaux

	void FlattenSettlements(World& w)
	{
		const double sea = w.master.scale.seaLevelM;
		w.urban.Init(w.height.width, w.height.height, w.height.originX, w.height.originZ, w.height.step, 0);
		auto flatten = [&](Vec2 c, double R, double blend, double& outY, bool mark) {
			// Altitude de référence : moyenne pondérée du disque (plateau naturel),
			// remontée au-dessus du niveau marin (villes côtières).
			double sum = 0.0, cnt = 0.0;
			ForDisc(w.height, c, R, [&](int i, int j, double) { sum += w.height.At(i, j); cnt += 1.0; });
			double y = cnt > 0 ? sum / cnt : sea + 8.0;
			y = std::max(y, sea + 5.0);
			outY = y;
			ForDisc(w.height, c, R + blend, [&](int i, int j, double d) {
				const double t = SmoothStep(R, R + blend, d);
				const double ripple = Fbm(w.height.WorldX(i), w.height.WorldZ(j), 30.0, 2, w.master.seed + 77) * 0.25;
				w.height.At(i, j) = Lerp(y + ripple, w.height.At(i, j), t);
				if (mark && d <= R) w.urban.At(i, j) = 1;
			});
		};
		for (CityDef& c : w.master.cities)
			flatten(c.center, c.radiusM + 6.0, 46.0, c.plateauY, true);
		for (PoiDef& p : w.master.pois)
		{
			double y = 0.0;
			flatten(p.center, 16.0, 26.0, y, false);
		}
	}

	// ----------------------------------------------------------------- eau

	namespace
	{
		/// Remplissage des dépressions (priority-flood, Barnes 2014) sur une grille
		/// grossière : garantit qu'une descente de plus grande pente atteint la mer
		/// ou le bord du terrain (pas de cuvette piège pour les rivières).
		Grid<double> FilledCoarse(const World& w, int factor)
		{
			Grid<double> g;
			const int n = (w.height.width - 1) / factor + 1;
			g.Init(n, n, w.height.originX, w.height.originZ, w.height.step * factor, 0.0);
			for (int j = 0; j < n; ++j)
				for (int i = 0; i < n; ++i)
					g.At(i, j) = w.height.At(std::min(w.height.width - 1, i * factor), std::min(w.height.height - 1, j * factor));
			// Les agglomérations forment des « dômes » virtuels : l'écoulement les
			// contourne (une rivière longe la ville au lieu de traverser ses rues).
			for (const CityDef& c : w.master.cities)
			{
				const double R = c.radiusM + 30.0;
				const int i0 = std::max(0, g.IndexX(c.center.x - R)), i1 = std::min(n - 1, g.IndexX(c.center.x + R));
				const int j0 = std::max(0, g.IndexZ(c.center.z - R)), j1 = std::min(n - 1, g.IndexZ(c.center.z + R));
				for (int j = j0; j <= j1; ++j)
					for (int i = i0; i <= i1; ++i)
					{
						const double d = std::hypot(g.WorldX(i) - c.center.x, g.WorldZ(j) - c.center.z);
						g.At(i, j) += 40.0 * SmoothStep(R, c.radiusM, d);
					}
			}
			struct Item { double h; int i, j; bool operator>(const Item& o) const { return h > o.h || (h == o.h && (j > o.j || (j == o.j && i > o.i))); } };
			std::priority_queue<Item, std::vector<Item>, std::greater<Item>> pq;
			std::vector<uint8_t> closed(static_cast<size_t>(n) * n, 0);
			const double sea = w.master.scale.seaLevelM;
			for (int j = 0; j < n; ++j)
				for (int i = 0; i < n; ++i)
					if (i == 0 || j == 0 || i == n - 1 || j == n - 1 || g.At(i, j) < sea)
					{
						pq.push({ g.At(i, j), i, j });
						closed[static_cast<size_t>(j) * n + i] = 1;
					}
			while (!pq.empty())
			{
				const Item it = pq.top(); pq.pop();
				for (int dj = -1; dj <= 1; ++dj)
					for (int di = -1; di <= 1; ++di)
					{
						const int ni = it.i + di, nj = it.j + dj;
						if ((!di && !dj) || !g.InBounds(ni, nj)) continue;
						uint8_t& cl = closed[static_cast<size_t>(nj) * n + ni];
						if (cl) continue;
						cl = 1;
						double& h = g.At(ni, nj);
						if (h <= it.h) h = it.h + 1e-3; // remplit la cuvette avec une pente infime
						pq.push({ h, ni, nj });
					}
			}
			return g;
		}

		std::vector<Vec2> Chaikin(const std::vector<Vec2>& in, int iterations)
		{
			std::vector<Vec2> p = in;
			for (int k = 0; k < iterations && p.size() > 2; ++k)
			{
				std::vector<Vec2> q;
				q.reserve(p.size() * 2);
				q.push_back(p.front());
				for (size_t i = 0; i + 1 < p.size(); ++i)
				{
					q.push_back(p[i] * 0.75 + p[i + 1] * 0.25);
					q.push_back(p[i] * 0.25 + p[i + 1] * 0.75);
				}
				q.push_back(p.back());
				p.swap(q);
			}
			return p;
		}

		std::vector<Vec2> Resample(const std::vector<Vec2>& in, double stepM)
		{
			std::vector<Vec2> out;
			if (in.empty()) return out;
			out.push_back(in.front());
			double carry = 0.0;
			for (size_t i = 0; i + 1 < in.size(); ++i)
			{
				const Vec2 a = in[i], b = in[i + 1];
				const double len = std::hypot(b.x - a.x, b.z - a.z);
				double t = stepM - carry;
				while (t <= len)
				{
					out.push_back(a + (b - a) * (t / len));
					t += stepM;
				}
				carry = len - (t - stepM);
			}
			if (std::hypot(out.back().x - in.back().x, out.back().z - in.back().z) > stepM * 0.3)
				out.push_back(in.back());
			return out;
		}
	}

	// Exposés pour Roads.cpp.
	std::vector<Vec2> SmoothPolyline(const std::vector<Vec2>& in, int iterations) { return Chaikin(in, iterations); }
	std::vector<Vec2> ResamplePolyline(const std::vector<Vec2>& in, double stepM) { return Resample(in, stepM); }

	void BuildWater(World& w)
	{
		const WorldScale& s = w.master.scale;
		const double sea = s.seaLevelM;
		w.riverDist.Init(w.height.width, w.height.height, w.height.originX, w.height.originZ, w.height.step, 1e9f);

		// ---- Rivières : une par composante connexe de cellules « avec rivière ».
		const int rows = static_cast<int>(w.master.gridRows.size());
		std::vector<int> comp(static_cast<size_t>(rows) * s.gridCols, -1);
		std::vector<std::vector<std::pair<int, int>>> comps;
		for (int r = 0; r < rows; ++r)
			for (int c = 0; c < s.gridCols; ++c)
			{
				if (!IsRiverCode(w.master.gridRows[r][c]) || comp[static_cast<size_t>(r) * s.gridCols + c] >= 0) continue;
				std::vector<std::pair<int, int>> stack{ { r, c } }, cells;
				comp[static_cast<size_t>(r) * s.gridCols + c] = static_cast<int>(comps.size());
				while (!stack.empty())
				{
					auto [cr, cc] = stack.back(); stack.pop_back();
					cells.push_back({ cr, cc });
					for (int dr = -1; dr <= 1; ++dr)
						for (int dc = -1; dc <= 1; ++dc)
						{
							const int nr = cr + dr, nc = cc + dc;
							if (nr < 0 || nc < 0 || nr >= rows || nc >= s.gridCols) continue;
							int& slot = comp[static_cast<size_t>(nr) * s.gridCols + nc];
							if (slot >= 0 || !IsRiverCode(w.master.gridRows[nr][nc])) continue;
							slot = static_cast<int>(comps.size());
							stack.push_back({ nr, nc });
						}
				}
				comps.push_back(std::move(cells));
			}

		const Grid<double> filled = FilledCoarse(w, 2); // 10 m
		int riverIndex = 0;
		for (const auto& cells : comps)
		{
			// Source = cellule la plus haute de la composante.
			Vec2 src{};
			double best = -1e9;
			for (auto [r, c] : cells)
			{
				const int n = s.northN - r, e = s.westE + c;
				const Vec2 p = w.CellCenter(n, e);
				const double h = SampleBilinear(w.height, p.x, p.z);
				if (h > best) { best = h; src = p; }
			}
			// Remonte un peu vers l'amont (la source jaillit en amont de la cellule).
			std::vector<Vec2> path;
			int ci = filled.IndexX(src.x), cj = filled.IndexZ(src.z);
			ci = static_cast<int>(Clamp(ci, 1, filled.width - 2));
			cj = static_cast<int>(Clamp(cj, 1, filled.height - 2));
			std::vector<uint8_t> visited(filled.data.size(), 0);
			for (int stepIdx = 0; stepIdx < 900; ++stepIdx)
			{
				path.push_back({ filled.WorldX(ci), filled.WorldZ(cj) });
				visited[static_cast<size_t>(cj) * filled.width + ci] = 1;
				if (filled.At(ci, cj) < sea - 0.5) break;
				int bi = -1, bj = -1;
				double bh = 1e18;
				for (int dj = -1; dj <= 1; ++dj)
					for (int di = -1; di <= 1; ++di)
					{
						const int ni = ci + di, nj = cj + dj;
						if ((!di && !dj) || !filled.InBounds(ni, nj) || visited[static_cast<size_t>(nj) * filled.width + ni]) continue;
						const double diag = (di && dj) ? 1.41421356 : 1.0;
						// Petit bruit pour faire méandrer sur le plat.
						const double jitter = ToUnit(Hash(w.master.seed, static_cast<uint64_t>(ni), static_cast<uint64_t>(nj), 991)) * 0.02;
						const double hv = (filled.At(ni, nj) - filled.At(ci, cj)) / diag + jitter;
						if (hv < bh) { bh = hv; bi = ni; bj = nj; }
					}
				if (bi < 0) break;
				ci = bi; cj = bj;
				if (ci <= 0 || cj <= 0 || ci >= filled.width - 1 || cj >= filled.height - 1) break;
			}
			if (path.size() < 12) continue; // trop court : pas une rivière
			River river;
			river.name = "riviere_" + std::to_string(riverIndex++);
			river.points = Resample(Chaikin(path, 3), 6.0);
			// Profil de surface strictement non croissant vers l'aval.
			const size_t np = river.points.size();
			river.waterY.resize(np);
			river.widthM.resize(np);
			double prev = 1e9;
			for (size_t k = 0; k < np; ++k)
			{
				const double t = static_cast<double>(k) / static_cast<double>(np - 1);
				double y = SampleBilinear(w.height, river.points[k].x, river.points[k].z) - 1.6;
				y = std::min(y, prev - 0.01);
				y = std::max(y, sea - 0.05);
				river.waterY[k] = y;
				river.widthM[k] = 7.0 + 8.0 * t;
				prev = y;
			}
			// Creusement du lit + carte de distance.
			for (size_t k = 0; k < np; ++k)
			{
				const Vec2 p = river.points[k];
				const double half = river.widthM[k] * 0.5;
				const double bed = river.waterY[k] - 1.8;
				ForDisc(w.height, p, half + 14.0, [&](int i, int j, double d) {
					// Jamais de lit creusé dans une ville (plateau préservé).
					for (const CityDef& c : w.master.cities)
						if (std::hypot(w.height.WorldX(i) - c.center.x, w.height.WorldZ(j) - c.center.z) < c.radiusM + 6.0) return;
					double& h = w.height.At(i, j);
					const double t = SmoothStep(half * 0.6, half + 14.0, d);
					const double target = Lerp(bed, std::max(h, river.waterY[k] + 0.4), t);
					if (d <= half) h = std::min(h, Lerp(bed, river.waterY[k] - 0.3, SmoothStep(0.0, half, d)));
					else h = std::min(h, target);
					float& rd = w.riverDist.At(i, j);
					rd = std::min(rd, static_cast<float>(std::max(0.0, d - half)));
				});
			}
			w.rivers.push_back(std::move(river));
		}
		w.report.rivers = w.rivers.size();

		// ---- Lacs : un par cellule « lac ».
		int lakeIndex = 0;
		for (int r = 0; r < rows; ++r)
			for (int c = 0; c < s.gridCols; ++c)
			{
				if (!IsLakeCode(w.master.gridRows[r][c])) continue;
				const int n = s.northN - r, e = s.westE + c;
				Lake lake;
				lake.name = "lac_" + std::to_string(lakeIndex++);
				const uint64_t hs = Hash(w.master.seed, static_cast<uint64_t>(n + 1000), static_cast<uint64_t>(e + 1000), 555);
				lake.center = w.CellCenter(n, e) + Vec2{ (ToUnit(hs) - 0.5) * 30.0, (ToUnit(Mix64(hs)) - 0.5) * 30.0 };
				lake.radiusM = 24.0 + ToUnit(Mix64(hs + 1)) * 14.0;
				// Une ville bâtie sur une cellule « lac » garde la terre ferme : le lac
				// est repoussé à côté d'elle (berges, pont de bois du lore).
				for (const CityDef& c : w.master.cities)
				{
					const double dx = lake.center.x - c.center.x, dz = lake.center.z - c.center.z;
					const double d = std::hypot(dx, dz);
					const double need = c.radiusM + lake.radiusM + 22.0;
					if (d < need)
					{
						const double a = d > 1.0 ? std::atan2(dz, dx) : ToUnit(Mix64(hs + 2)) * 6.283185307179586;
						lake.center = c.center + Vec2{ std::cos(a), std::sin(a) } * need;
					}
				}
				w.lakes.push_back(std::move(lake));
			}
		// Lacs déclarés à la main dans le master (ex. étang du hameau de départ).
		for (const ExtraLakeDef& el : w.master.extraLakes)
		{
			Lake lake;
			lake.name = el.id;
			lake.center = { el.x, el.z };
			lake.radiusM = el.radiusM;
			w.lakes.push_back(std::move(lake));
		}
		for (Lake& lake : w.lakes)
		{
			double sum = 0.0, cnt = 0.0;
			ForDisc(w.height, lake.center, lake.radiusM + 10.0, [&](int i, int j, double) { sum += w.height.At(i, j); cnt += 1.0; });
			lake.waterY = std::max(sea + 0.5, (cnt > 0 ? sum / cnt : sea + 4.0) - 0.8);
			const double R = lake.radiusM;
			ForDisc(w.height, lake.center, R + 18.0, [&](int i, int j, double d) {
				double& h = w.height.At(i, j);
				if (d <= R)
				{
					const double depth = 3.5 * (1.0 - (d / R) * (d / R)) + 0.6;
					h = std::min(h, lake.waterY - depth);
				}
				else
				{
					const double t = SmoothStep(R, R + 18.0, d);
					h = Lerp(std::min(h, lake.waterY + 0.3), h, t);
				}
			});
			// Polygone CCW (vu de dessus, X est / Z sud) — 28 sommets bruités.
			for (int k = 0; k < 28; ++k)
			{
				const double a = 6.283185307179586 * k / 28.0; // CCW dans le plan XZ (même ordre que le bassin-test du client)
				const double rr = R * (1.0 + 0.08 * Fbm(std::cos(a) * 3.0, std::sin(a) * 3.0, 1.0, 2, Hash(w.master.seed, 777)));
				lake.polygon.push_back({ lake.center.x + std::cos(a) * rr, lake.center.z + std::sin(a) * rr });
			}
		}
		w.report.lakes = w.lakes.size();
	}
}
