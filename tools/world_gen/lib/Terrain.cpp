// tools/world_gen — relief, sites des villes, rivières et lacs.
//
// Couche MACRO (continent, une fois par exécution) :
//   - MacroHeight / MacroBiome : fonctions PURES de la position globale. Chaque
//     code de case du lore porte une altitude de base, une amplitude de relief
//     doux (fbm) et une amplitude de crêtes (ridged), interpolées entre centres
//     de cases à une position DÉFORMÉE (domain warp) : côtes et lisières perdent
//     l'aspect quadrillé. Deux cellules voisines évaluent la même fonction sur
//     leur frontière commune → raccord exact.
//   - sites des villes : meilleur emplacement (plat, au sec) dans la case ;
//   - rivières : descente sur une grille grossière (250 m) dont les cuvettes
//     sont remplies (priority-flood), lissée puis rééchantillonnée à 6 m ;
//   - lacs : un par case « lac » du lore + lacs manuels ancrés sur une ville.
//
// Couche CELLULE : heightmap 2049² (≈ 4,9 m), plateaux urbains, creusement des
// lits de rivière et des lacs à partir des données macro.

#include "WorldGenContext.h"
#include "Parallel.h"
#include "Random.h"

#include <algorithm>
#include <cmath>
#include <queue>

namespace tools::world_gen
{
	namespace
	{
		constexpr double kC = engine::world::cells::kCellSizeMeters;

		struct CodeProfile
		{
			double base;   ///< altitude moyenne (m)
			double amp;    ///< amplitude du relief doux (m)
			double ridge;  ///< amplitude des crêtes (m)
			double dune;   ///< poids des dunes (désert)
		};

		CodeProfile ProfileOf(char c)
		{
			switch (c)
			{
			case ' ': return { 6.0, 3.0, 0.0, 0 };       // mer profonde
			case '-': return { 26.0, 4.0, 0.0, 0 };      // haut-fond
			case ',': return { 39.5, 3.0, 0.0, 0 };      // côte (le rivage passe dedans)
			case '_': return { 43.0, 2.5, 0.0, 0 };      // plage
			case 'o': return { 50.0, 10.0, 0.0, 0 };     // îlot
			case '.': return { 62.0, 22.0, 0.0, 0 };     // plaine (collines douces)
			case 'r': return { 56.0, 16.0, 0.0, 0 };     // plaine + rivière
			case 'T': return { 78.0, 34.0, 0.0, 0 };     // forêt
			case 't': return { 70.0, 28.0, 0.0, 0 };     // lisière
			case 'R': return { 66.0, 24.0, 0.0, 0 };     // forêt + rivière
			case 'L': return { 70.0, 26.0, 0.0, 0 };     // forêt + lac
			case '~': return { 82.0, 28.0, 0.0, 1 };     // désert (dunes)
			case ':': return { 78.0, 24.0, 0.0, 0 };     // terres arides
			case 'l': return { 72.0, 20.0, 0.0, 0 };     // aride + lac
			case '^': return { 250.0, 80.0, 360.0, 0 };  // montagne
			case 'M': return { 340.0, 100.0, 500.0, 0 }; // chaîne
			case '*': return { 460.0, 120.0, 580.0, 0 }; // sommets enneigés
			case 'V': return { 190.0, 60.0, 240.0, 0 };  // volcanique
			case '%': return { 42.5, 2.5, 0.0, 0 };      // marais
			case '&': return { 42.0, 2.0, 0.0, 0 };      // marais + rivière
			default:  return { 60.0, 20.0, 0.0, 0 };
			}
		}

		bool IsRiverCode(char c) { return c == 'r' || c == 'R' || c == '&'; }
		bool IsLakeCode(char c) { return c == 'L' || c == 'l'; }

		/// Rayon urbain (m) selon le type d'agglomération (échelle 10 km).
		double CityRadius(const std::string& kind, const std::string& procedural)
		{
			if (procedural == "ferme") return 26.0;
			if (procedural == "mine") return 34.0;
			if (kind == "capitale") return 180.0;
			if (kind == "grande_ville") return 130.0;
			if (kind == "ville") return 100.0;
			if (kind == "forteresse") return 90.0;
			if (kind == "village") return 70.0;
			if (kind == "camp") return 55.0;
			return 45.0; // hameau
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

		/// Moyenne de MacroHeight sur un disque (pas \p step m), au-dessus de la mer.
		double MeanMacroHeight(const WorldMaster& m, Vec2 c, double r, double step)
		{
			double sum = 0.0, cnt = 0.0;
			for (double dz = -r; dz <= r; dz += step)
				for (double dx = -r; dx <= r; dx += step)
					if (dx * dx + dz * dz <= r * r) { sum += MacroHeight(m, c.x + dx, c.z + dz); cnt += 1.0; }
			return cnt > 0.0 ? sum / cnt : m.scale.seaLevelM + 8.0;
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

	std::vector<Vec2> SmoothPolyline(const std::vector<Vec2>& in, int iterations)
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

	std::vector<Vec2> ResamplePolyline(const std::vector<Vec2>& in, double stepM)
	{
		std::vector<Vec2> out;
		if (in.empty()) return out;
		out.push_back(in.front());
		double carry = 0.0;
		for (size_t i = 0; i + 1 < in.size(); ++i)
		{
			const Vec2 a = in[i], b = in[i + 1];
			const double len = std::hypot(b.x - a.x, b.z - a.z);
			if (len < 1e-9) continue;
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

	// ---------------------------------------------------------- occupation creuse

	void SparseOccupancy::Set(int x, int z)
	{
		const int tx = static_cast<int>(std::floor(static_cast<double>(x) / kTile));
		const int tz = static_cast<int>(std::floor(static_cast<double>(z) / kTile));
		std::vector<uint8_t>& t = m_tiles[Key(tx, tz)];
		if (t.empty()) t.assign(static_cast<size_t>(kTile) * kTile, 0);
		t[static_cast<size_t>(z - tz * kTile) * kTile + static_cast<size_t>(x - tx * kTile)] = 1;
	}

	bool SparseOccupancy::Get(int x, int z) const
	{
		const int tx = static_cast<int>(std::floor(static_cast<double>(x) / kTile));
		const int tz = static_cast<int>(std::floor(static_cast<double>(z) / kTile));
		const auto it = m_tiles.find(Key(tx, tz));
		if (it == m_tiles.end()) return false;
		return it->second[static_cast<size_t>(z - tz * kTile) * kTile + static_cast<size_t>(x - tx * kTile)] != 0;
	}

	// ------------------------------------------------------------- fonctions macro

	char LoreCode(const WorldMaster& m, int n, int e)
	{
		const int row = m.scale.northN - n;
		const int col = e - m.scale.westE;
		if (row < 0 || row >= static_cast<int>(m.gridRows.size()) || col < 0 || col >= m.scale.gridCols)
			return ' ';
		return m.gridRows[static_cast<size_t>(row)][static_cast<size_t>(col)];
	}

	char MacroBiome(const WorldMaster& m, double x, double z)
	{
		const uint64_t seed = m.seed;
		// Déformation forte (≈ 1,3 case) et fine (lisières à l'échelle du marcheur).
		const double bx = x + Fbm(x, z, 1.33 * kC, 3, seed + 13) * 1.3 * kC
			+ Fbm(x, z, 0.35 * kC, 2, seed + 14) * 0.3 * kC + Fbm(x, z, 300.0, 3, seed + 17) * 380.0;
		const double bz = z + Fbm(x, z, 1.33 * kC, 3, seed + 15) * 1.3 * kC
			+ Fbm(x, z, 0.35 * kC, 2, seed + 16) * 0.3 * kC + Fbm(x, z, 300.0, 3, seed + 18) * 380.0;
		const int be = static_cast<int>(std::lround(bx / kC));
		const int bn = static_cast<int>(std::lround(-bz / kC));
		return LoreCode(m, bn, be);
	}

	double MacroHeight(const WorldMaster& m, double x, double z)
	{
		const uint64_t seed = m.seed;
		// Domain warp (~±0,3 case) : côtes et reliefs organiques.
		const double wx = x + Fbm(x, z, 2.0 * kC, 3, seed + 11) * 0.9 * kC;
		const double wz = z + Fbm(x, z, 2.0 * kC, 3, seed + 12) * 0.9 * kC;
		// Coordonnées de case fractionnaires (centres aux entiers ; nord = -Z).
		const double ef = wx / kC, nf = -wz / kC;
		const double e0 = std::floor(ef), n0 = std::floor(nf);
		const double te = SmoothStep(0.0, 1.0, ef - e0), tn = SmoothStep(0.0, 1.0, nf - n0);
		const int ie = static_cast<int>(e0), in = static_cast<int>(n0);
		const double wts[4] = { (1 - te) * (1 - tn), te * (1 - tn), (1 - te) * tn, te * tn };
		const int de[4] = { 0, 1, 0, 1 }, dn[4] = { 0, 0, 1, 1 };
		CodeProfile acc{ 0, 0, 0, 0 };
		for (int k = 0; k < 4; ++k)
		{
			const CodeProfile p = ProfileOf(LoreCode(m, in + dn[k], ie + de[k]));
			acc.base += p.base * wts[k];
			acc.amp += p.amp * wts[k];
			acc.ridge += p.ridge * wts[k];
			acc.dune += p.dune * wts[k];
		}
		double h = acc.base;
		// Caractère régional (≈ 7 km) : alternance de pays plats et vallonnés à
		// l'intérieur d'une même case du lore (évite la monotonie sur 10 km).
		const double region = Clamp(Fbm(x, z, 7000.0, 2, seed + 24) + 0.5, 0.0, 1.0);
		const double ampMod = 0.45 + 1.1 * region;
		h += Fbm(x, z, 2400.0, 6, seed + 21) * acc.amp * ampMod;  // collines (km)
		h += Fbm(x, z, 320.0, 4, seed + 22) * acc.amp * 0.18;     // mamelons (centaines de m)
		h += Fbm(x, z, 45.0, 2, seed + 23) * 0.8;                 // grain du sol
		// Hors montagnes : chaînons de collines (crêtes douces) dans les pays
		// vallonnés, et vallons (lignes creuses) qui structurent le paysage.
		const double lowland = (1.0 - SmoothStep(80.0, 300.0, acc.ridge)) * SmoothStep(44.0, 58.0, acc.base);
		if (lowland > 0.01)
		{
			const double hilly = SmoothStep(0.45, 0.85, region);
			h += (Ridged(x, z, 1800.0, 4, seed + 25) - 0.3) * 48.0 * hilly * lowland;
			const double vf = 1.0 - std::fabs(Fbm(x, z, 5200.0, 3, seed + 26));
			h -= 14.0 * SmoothStep(0.86, 0.99, vf) * lowland;
			// Plancher souple : l'intérieur des terres ne plonge pas sous la mer
			// (les étendues d'eau intérieures sont des lacs explicites).
			const double floorY = m.scale.seaLevelM + 6.0;
			if (h < floorY) h = Lerp(h, floorY - (floorY - h) * 0.2, lowland);
		}
		if (acc.ridge > 0.5)
			h += (Ridged(x, z, 4500.0, 6, seed + 31) - 0.35) * acc.ridge;
		if (acc.dune > 0.01)
			h += std::sin((x * 0.8 + z * 0.6) / 60.0 + Fbm(x, z, 900.0, 2, seed + 41) * 3.0) * 4.0 * acc.dune;
		return Clamp(h, 1.0, m.scale.heightScaleM - 2.0);
	}

	// ------------------------------------------------------ méthodes des contextes

	const CityDef* GlobalWorld::FindCity(const std::string& id) const
	{
		for (const CityDef& c : cities)
			if (c.id == id) return &c;
		return nullptr;
	}

	bool World::CellAt(Vec2 p, int& n, int& e) const
	{
		e = static_cast<int>(std::lround(p.x / kC));
		n = -static_cast<int>(std::lround(p.z / kC));
		const int row = master.scale.northN - n;
		const int col = e - master.scale.westE;
		return row >= 0 && row < static_cast<int>(master.gridRows.size()) && col >= 0 && col < master.scale.gridCols;
	}

	double World::SlopeDegAt(Vec2 p) const
	{
		const double s = height.step;
		const double hx = SampleBilinear(height, p.x + s, p.z) - SampleBilinear(height, p.x - s, p.z);
		const double hz = SampleBilinear(height, p.x, p.z + s) - SampleBilinear(height, p.x, p.z - s);
		const double gr = std::sqrt(hx * hx + hz * hz) / (2.0 * s);
		return std::atan(gr) * 57.29577951308232;
	}

	bool World::IsWater(Vec2 p) const
	{
		if (HeightAt(p) < master.scale.seaLevelM + 0.4) return true;
		if (!riverDist.data.empty())
		{
			const int i = riverDist.IndexX(p.x), j = riverDist.IndexZ(p.z);
			if (riverDist.InBounds(i, j) && riverDist.At(i, j) < 3.0f) return true;
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

	bool World::OwnsPoint(Vec2 p) const
	{
		return p.x >= terrainOriginX && p.z >= terrainOriginZ
			&& p.x < terrainOriginX + terrainSizeM && p.z < terrainOriginZ + terrainSizeM;
	}

	void World::Occupy(Vec2 c, double r)
	{
		const int i0 = static_cast<int>(std::floor(c.x - r)), i1 = static_cast<int>(std::ceil(c.x + r));
		const int j0 = static_cast<int>(std::floor(c.z - r)), j1 = static_cast<int>(std::ceil(c.z + r));
		const double r2 = r * r;
		for (int j = j0; j <= j1; ++j)
			for (int i = i0; i <= i1; ++i)
			{
				const double dx = i + 0.5 - c.x, dz = j + 0.5 - c.z;
				if (dx * dx + dz * dz <= r2) occupied.Set(i, j);
			}
	}

	bool World::IsOccupied(Vec2 c, double r) const
	{
		const int i0 = static_cast<int>(std::floor(c.x - r)), i1 = static_cast<int>(std::ceil(c.x + r));
		const int j0 = static_cast<int>(std::floor(c.z - r)), j1 = static_cast<int>(std::ceil(c.z + r));
		const double r2 = r * r;
		for (int j = j0; j <= j1; ++j)
			for (int i = i0; i <= i1; ++i)
			{
				const double dx = i + 0.5 - c.x, dz = j + 0.5 - c.z;
				if (dx * dx + dz * dz <= r2 && occupied.Get(i, j)) return true;
			}
		return false;
	}

	namespace
	{
		/// Parcourt les cases d'1 m couvertes par un rectangle orienté.
		template <typename F>
		void ForRect(Vec2 c, double hx, double hz, double yawDeg, F&& fn)
		{
			const double a = yawDeg * 3.14159265358979323846 / 180.0;
			const double ca = std::cos(a), sa = std::sin(a);
			const double r = std::sqrt(hx * hx + hz * hz);
			const int i0 = static_cast<int>(std::floor(c.x - r)), i1 = static_cast<int>(std::ceil(c.x + r));
			const int j0 = static_cast<int>(std::floor(c.z - r)), j1 = static_cast<int>(std::ceil(c.z + r));
			for (int j = j0; j <= j1; ++j)
				for (int i = i0; i <= i1; ++i)
				{
					const double dx = i + 0.5 - c.x, dz = j + 0.5 - c.z;
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
		ForRect(c, hx, hz, yawDeg, [&](int i, int j) { occupied.Set(i, j); return false; });
	}

	bool World::IsOccupiedRect(Vec2 c, double hx, double hz, double yawDeg) const
	{
		bool hit = false;
		ForRect(c, hx, hz, yawDeg, [&](int i, int j) { if (occupied.Get(i, j)) { hit = true; return true; } return false; });
		return hit;
	}

	const CityDef* World::FindCity(const std::string& id) const
	{
		for (const CityDef& c : cities)
			if (c.id == id) return &c;
		return g ? g->FindCity(id) : nullptr;
	}

	// ----------------------------------------------------------- couche macro

	void BuildMacroTerrain(GlobalWorld& g)
	{
		const WorldMaster& m = g.master;
		const WorldScale& s = m.scale;
		const int rows = static_cast<int>(m.gridRows.size());
		g.minX = (s.westE - 0.5) * kC;
		g.maxX = (s.westE + s.gridCols - 0.5) * kC;
		g.minZ = -(s.northN + 0.5) * kC;
		g.maxZ = -(s.northN - rows + 0.5) * kC;

		// Grille grossière (250 m) du relief de base : coûts des routes, rivières,
		// carte du monde. Calcul parallèle (rangées indépendantes).
		const double step = 250.0;
		const int nx = static_cast<int>(std::lround((g.maxX - g.minX) / step)) + 1;
		const int nz = static_cast<int>(std::lround((g.maxZ - g.minZ) / step)) + 1;
		g.coarse.Init(nx, nz, g.minX, g.minZ, step, 0.0);
		ParallelRows(nz, [&](int j) {
			for (int i = 0; i < nx; ++i) g.coarse.At(i, j) = MacroHeight(m, g.coarse.WorldX(i), g.coarse.WorldZ(j));
		});

		// Sites des villes : le meilleur emplacement de la case (plat, au sec,
		// pas trop loin du centre), sur un treillis de 250 m.
		const double sea = s.seaLevelM;
		g.cities = m.cities;
		for (CityDef& c : g.cities)
		{
			c.radiusM = CityRadius(c.kind, c.procedural);
			const engine::world::cells::CellPoint ctr = engine::world::cells::CellCenter({ c.n, c.e });
			Vec2 best{ ctr.x, ctr.z };
			double bestScore = 1e18;
			for (double dz = -3500.0; dz <= 3500.0; dz += 250.0)
				for (double dx = -3500.0; dx <= 3500.0; dx += 250.0)
				{
					const Vec2 p{ ctr.x + dx, ctr.z + dz };
					const double h0 = MacroHeight(m, p.x, p.z);
					if (h0 < sea + 6.0) continue;
					double worst = 0.0, lowest = h0;
					const double R = c.radiusM + 60.0;
					for (int k = 0; k < 8; ++k)
					{
						const double a = k * 0.7853981633974483;
						const double h = MacroHeight(m, p.x + std::cos(a) * R, p.z + std::sin(a) * R);
						worst = std::max(worst, std::fabs(h - h0));
						lowest = std::min(lowest, h);
					}
					if (lowest < sea + 3.0) continue; // pied dans l'eau
					const double score = std::atan(worst / R) * 57.29577951308232 + std::hypot(dx, dz) / 400.0;
					if (score < bestScore) { bestScore = score; best = p; }
				}
			if (bestScore > 1e17) g.Warn("aucun site sec pour " + c.id + " : centre de case retenu");
			c.center = best;
			c.plateauY = std::max(MeanMacroHeight(m, c.center, c.radiusM + 6.0, 12.0), sea + 5.0);
		}
		g.pois = m.pois;
		for (PoiDef& p : g.pois)
		{
			const engine::world::cells::CellPoint ctr = engine::world::cells::CellCenter({ p.n, p.e });
			p.center = { ctr.x, ctr.z };
			// Hors du plateau (et de son raccord) d'une ville de la même case.
			for (const CityDef& c : g.cities)
			{
				if (c.n != p.n || c.e != p.e) continue;
				const double need = c.radiusM + 6.0 + std::max(46.0, c.radiusM * 0.6) + 100.0 + 100.0;
				const double d = std::hypot(p.center.x - c.center.x, p.center.z - c.center.z);
				if (d < need)
				{
					const double a = d > 1.0 ? std::atan2(p.center.z - c.center.z, p.center.x - c.center.x)
						: ToUnit(Hash(m.seed, HashString(p.id.c_str()))) * 6.283185307179586;
					p.center = c.center + Vec2{ std::cos(a), std::sin(a) } * need;
				}
			}
		}
		for (const CityDef& c : g.cities)
			if (!c.startFor.empty()) g.startCenters.push_back(c.center);

		// Lacs du lore : un par case « lac », repoussé hors des villes de la case.
		int lakeIndex = 0;
		for (int r = 0; r < rows; ++r)
			for (int col = 0; col < s.gridCols; ++col)
			{
				if (!IsLakeCode(m.gridRows[static_cast<size_t>(r)][static_cast<size_t>(col)])) continue;
				const int n = s.northN - r, e = s.westE + col;
				const engine::world::cells::CellPoint ctr = engine::world::cells::CellCenter({ n, e });
				const uint64_t hs = Hash(m.seed, static_cast<uint64_t>(n + 1000), static_cast<uint64_t>(e + 1000), 555);
				Lake lake;
				lake.name = "lac_" + std::to_string(lakeIndex++);
				lake.center = { ctr.x + (ToUnit(hs) - 0.5) * 5000.0, ctr.z + (ToUnit(Mix64(hs)) - 0.5) * 5000.0 };
				lake.radiusM = 180.0 + ToUnit(Mix64(hs + 1)) * 240.0;
				for (const CityDef& c : g.cities)
				{
					const double d = std::hypot(lake.center.x - c.center.x, lake.center.z - c.center.z);
					const double need = c.radiusM + lake.radiusM + 160.0;
					if (d < need)
					{
						const double a = d > 1.0 ? std::atan2(lake.center.z - c.center.z, lake.center.x - c.center.x)
							: ToUnit(Mix64(hs + 2)) * 6.283185307179586;
						lake.center = c.center + Vec2{ std::cos(a), std::sin(a) } * need;
					}
				}
				g.lakes.push_back(std::move(lake));
			}
		// Lacs manuels (ex. étang du hameau de départ), ancrés sur une ville.
		for (const ExtraLakeDef& el : m.extraLakes)
		{
			Lake lake;
			lake.name = el.id;
			lake.center = { el.x, el.z };
			if (!el.cityId.empty())
				if (const CityDef* c = g.FindCity(el.cityId)) lake.center = c->center + Vec2{ el.x, el.z };
			lake.radiusM = el.radiusM;
			// Un lac (et sa berge de 40 m) doit rester dans une seule cellule.
			const engine::world::cells::CellCoord lc = engine::world::cells::CellFromPosition(lake.center.x, lake.center.z);
			if (engine::world::cells::DistanceToCellBorder(lake.center.x, lake.center.z) < lake.radiusM + 40.0)
				g.Warn("lac manuel '" + el.id + "' trop proche du bord de " + engine::world::cells::CellDirName(lc));
			g.lakes.push_back(std::move(lake));
		}
		for (Lake& lake : g.lakes)
		{
			lake.waterY = std::max(sea + 0.5, MeanMacroHeight(m, lake.center, lake.radiusM + 30.0, std::max(4.0, lake.radiusM / 12.0)) - 0.8);
			// Polygone CCW (vu de dessus, X est / Z sud), sommets bruités.
			const int verts = std::clamp(static_cast<int>(lake.radiusM / 6.0), 28, 96);
			for (int k = 0; k < verts; ++k)
			{
				const double a = 6.283185307179586 * k / verts;
				const double rr = lake.radiusM * (1.0 + 0.08 * Fbm(std::cos(a) * 3.0, std::sin(a) * 3.0, 1.0, 2, Hash(m.seed, HashString(lake.name.c_str()))));
				lake.polygon.push_back({ lake.center.x + std::cos(a) * rr, lake.center.z + std::sin(a) * rr });
			}
		}
	}

	namespace
	{
		/// Remplit les cuvettes de la grille grossière (priority-flood, Barnes 2014) :
		/// une descente de plus grande pente atteint toujours la mer ou le bord.
		/// Les agglomérations forment des « dômes » contournés par l'écoulement.
		Grid<double> FilledCoarse(const GlobalWorld& g)
		{
			Grid<double> f = g.coarse;
			const int nx = f.width, nz = f.height;
			for (const CityDef& c : g.cities)
			{
				const double R = c.radiusM + 400.0;
				const int i0 = std::max(0, f.IndexX(c.center.x - R)), i1 = std::min(nx - 1, f.IndexX(c.center.x + R));
				const int j0 = std::max(0, f.IndexZ(c.center.z - R)), j1 = std::min(nz - 1, f.IndexZ(c.center.z + R));
				for (int j = j0; j <= j1; ++j)
					for (int i = i0; i <= i1; ++i)
					{
						const double d = std::hypot(f.WorldX(i) - c.center.x, f.WorldZ(j) - c.center.z);
						f.At(i, j) += 60.0 * SmoothStep(R, c.radiusM + 100.0, d);
					}
			}
			struct Item
			{
				double h; int i, j;
				bool operator>(const Item& o) const { return h > o.h || (h == o.h && (j > o.j || (j == o.j && i > o.i))); }
			};
			std::priority_queue<Item, std::vector<Item>, std::greater<Item>> pq;
			std::vector<uint8_t> closed(static_cast<size_t>(nx) * nz, 0);
			const double sea = g.master.scale.seaLevelM;
			for (int j = 0; j < nz; ++j)
				for (int i = 0; i < nx; ++i)
					if (i == 0 || j == 0 || i == nx - 1 || j == nz - 1 || f.At(i, j) < sea)
					{
						pq.push({ f.At(i, j), i, j });
						closed[static_cast<size_t>(j) * nx + i] = 1;
					}
			while (!pq.empty())
			{
				const Item it = pq.top(); pq.pop();
				for (int dj = -1; dj <= 1; ++dj)
					for (int di = -1; di <= 1; ++di)
					{
						const int ni = it.i + di, nj = it.j + dj;
						if ((!di && !dj) || !f.InBounds(ni, nj)) continue;
						uint8_t& cl = closed[static_cast<size_t>(nj) * nx + ni];
						if (cl) continue;
						cl = 1;
						double& h = f.At(ni, nj);
						if (h <= it.h) h = it.h + 1e-3; // cuvette remplie avec une pente infime
						pq.push({ h, ni, nj });
					}
			}
			return f;
		}
	}

	void BuildMacroRivers(GlobalWorld& g)
	{
		const WorldMaster& m = g.master;
		const WorldScale& s = m.scale;
		const double sea = s.seaLevelM;
		const int rows = static_cast<int>(m.gridRows.size());

		// Une rivière par composante connexe (8-voisinage) de cases « avec rivière ».
		std::vector<int> comp(static_cast<size_t>(rows) * s.gridCols, -1);
		std::vector<std::vector<std::pair<int, int>>> comps;
		for (int r = 0; r < rows; ++r)
			for (int c = 0; c < s.gridCols; ++c)
			{
				if (!IsRiverCode(m.gridRows[static_cast<size_t>(r)][static_cast<size_t>(c)]) || comp[static_cast<size_t>(r) * s.gridCols + c] >= 0) continue;
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
							if (slot >= 0 || !IsRiverCode(m.gridRows[static_cast<size_t>(nr)][static_cast<size_t>(nc)])) continue;
							slot = static_cast<int>(comps.size());
							stack.push_back({ nr, nc });
						}
				}
				comps.push_back(std::move(cells));
			}

		const Grid<double> filled = FilledCoarse(g);
		int riverIndex = 0;
		for (const auto& cells : comps)
		{
			// Source = point le plus haut de la composante (centres de cases).
			Vec2 src{};
			double best = -1e9;
			for (auto [r, c] : cells)
			{
				const engine::world::cells::CellPoint p = engine::world::cells::CellCenter({ s.northN - r, s.westE + c });
				const double h = SampleBilinear(g.coarse, p.x, p.z);
				if (h > best) { best = h; src = { p.x, p.z }; }
			}
			std::vector<Vec2> path;
			int ci = static_cast<int>(Clamp(filled.IndexX(src.x), 1, filled.width - 2));
			int cj = static_cast<int>(Clamp(filled.IndexZ(src.z), 1, filled.height - 2));
			std::vector<uint8_t> visited(filled.data.size(), 0);
			for (int stepIdx = 0; stepIdx < 60000; ++stepIdx)
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
						// Petit bruit pour méandrer sur le plat.
						const double jitter = ToUnit(Hash(m.seed, static_cast<uint64_t>(ni), static_cast<uint64_t>(nj), 991)) * 0.02;
						const double hv = (filled.At(ni, nj) - filled.At(ci, cj)) / diag + jitter;
						if (hv < bh) { bh = hv; bi = ni; bj = nj; }
					}
				if (bi < 0) break;
				ci = bi; cj = bj;
				if (ci <= 0 || cj <= 0 || ci >= filled.width - 1 || cj >= filled.height - 1) break;
			}
			if (path.size() < 12) continue; // trop court : pas une rivière
			// Méandres : déplacement latéral bruité (≈ ±60 m) avant lissage.
			for (size_t k = 1; k + 1 < path.size(); ++k)
			{
				const Vec2 d = path[k + 1] - path[k - 1];
				const double l = std::hypot(d.x, d.z);
				if (l < 1e-6) continue;
				const double off = Fbm(path[k].x, path[k].z, 1800.0, 3, m.seed + 909) * 120.0;
				path[k] = path[k] + Vec2{ -d.z / l, d.x / l } * off;
			}
			River river;
			river.name = "riviere_" + std::to_string(riverIndex++);
			river.points = ResamplePolyline(SmoothPolyline(path, 4), 6.0);
			const size_t np = river.points.size();
			// Surface : altitude du relief lissée (minimum glissant ±150 m) moins 1,6 m,
			// strictement non croissante vers l'aval.
			std::vector<double> ground(np);
			for (size_t k = 0; k < np; ++k) ground[k] = MacroHeight(m, river.points[k].x, river.points[k].z);
			river.waterY.resize(np);
			river.widthM.resize(np);
			double prev = 1e9;
			for (size_t k = 0; k < np; ++k)
			{
				double lo = ground[k];
				for (int o = -25; o <= 25; o += 5)
				{
					const long q = static_cast<long>(k) + o;
					if (q >= 0 && q < static_cast<long>(np)) lo = std::min(lo, ground[static_cast<size_t>(q)]);
				}
				const double t = static_cast<double>(k) / static_cast<double>(np - 1);
				double y = std::min(lo - 1.6, prev - 0.002);
				y = std::max(y, sea - 0.05);
				river.waterY[k] = y;
				river.widthM[k] = 10.0 + 22.0 * t;
				prev = y;
			}
			g.rivers.push_back(std::move(river));
		}
		g.report.rivers = g.rivers.size();
		g.report.lakes = g.lakes.size();
	}

	// ----------------------------------------------------------- couche cellule

	void BuildCellTerrain(World& w)
	{
		const int N = w.master.scale.heightmapRes;
		const double step = w.terrainSizeM / (N - 1);
		w.height.Init(N, N, w.terrainOriginX, w.terrainOriginZ, step, 0.0);
		w.biome.Init(N, N, w.terrainOriginX, w.terrainOriginZ, step, ' ');
		const WorldMaster& m = w.master;
		ParallelRows(N, [&](int j) {
			for (int i = 0; i < N; ++i)
			{
				const double x = w.height.WorldX(i), z = w.height.WorldZ(j);
				w.height.At(i, j) = MacroHeight(m, x, z);
				w.biome.At(i, j) = MacroBiome(m, x, z);
			}
		});
		const double sea = m.scale.seaLevelM;
		for (double hv : w.height.data)
			if (hv < sea) { w.hasSea = true; break; }
	}

	void FlattenSettlements(World& w)
	{
		const double sea = w.master.scale.seaLevelM;
		w.urban.Init(w.height.width, w.height.height, w.height.originX, w.height.originZ, w.height.step, 0);
		auto flatten = [&](Vec2 c, double R, double blend, double y, bool mark) {
			ForDisc(w.height, c, R + blend, [&](int i, int j, double d) {
				const double t = SmoothStep(R, R + blend, d);
				const double ripple = Fbm(w.height.WorldX(i), w.height.WorldZ(j), 30.0, 2, w.master.seed + 77) * 0.25;
				w.height.At(i, j) = Lerp(y + ripple, w.height.At(i, j), t);
				if (mark && d <= R) w.urban.At(i, j) = 1;
			});
		};
		for (CityDef& c : w.cities)
		{
			// Lieux procéduraux : plateau calculé localement (n'influence pas les
			// cellules voisines, ils restent à distance des bords).
			if (!c.procedural.empty())
			{
				double sum = 0.0, cnt = 0.0;
				ForDisc(w.height, c.center, c.radiusM + 6.0, [&](int i, int j, double) { sum += w.height.At(i, j); cnt += 1.0; });
				c.plateauY = std::max(cnt > 0 ? sum / cnt : sea + 8.0, sea + 5.0);
			}
			flatten(c.center, c.radiusM + 6.0, std::max(46.0, c.radiusM * 0.6), c.plateauY, true);
		}
		for (const PoiDef& p : w.pois)
		{
			double sum = 0.0, cnt = 0.0;
			ForDisc(w.height, p.center, 40.0, [&](int i, int j, double) { sum += w.height.At(i, j); cnt += 1.0; });
			flatten(p.center, 40.0, 60.0, std::max(cnt > 0 ? sum / cnt : sea + 8.0, sea + 5.0), false);
		}
	}

	void CarveWater(World& w)
	{
		w.riverDist.Init(w.height.width, w.height.height, w.height.originX, w.height.originZ, w.height.step, 1e9f);
		auto inCity = [&](double x, double z) {
			for (const CityDef& c : w.cities)
				if (std::hypot(x - c.center.x, z - c.center.z) < c.radiusM + 6.0) return true;
			return false;
		};
		for (const River& river : w.rivers)
		{
			const size_t np = river.points.size();
			for (size_t k = 0; k < np; ++k)
			{
				const Vec2 p = river.points[k];
				const double half = river.widthM[k] * 0.5;
				const double bed = river.waterY[k] - 2.4;
				const double reach = half + 30.0;
				ForDisc(w.height, p, reach, [&](int i, int j, double d) {
					// Jamais de lit creusé dans une ville (plateau préservé).
					if (inCity(w.height.WorldX(i), w.height.WorldZ(j))) return;
					double& h = w.height.At(i, j);
					if (d <= half)
						h = std::min(h, Lerp(bed, river.waterY[k] - 0.4, SmoothStep(0.0, half, d)));
					else
					{
						// Berges : jamais sous la surface (pas de débordement), pente douce.
						const double t = SmoothStep(half, reach, d);
						const double bank = river.waterY[k] + 0.35;
						h = std::min(h, Lerp(bank, std::max(h, bank), t));
						if (d < half + 6.0) h = std::max(h, bank);
					}
					float& rd = w.riverDist.At(i, j);
					rd = std::min(rd, static_cast<float>(std::max(0.0, d - half)));
				});
			}
		}
		for (const Lake& lake : w.lakes)
		{
			const double R = lake.radiusM;
			ForDisc(w.height, lake.center, R + 40.0, [&](int i, int j, double d) {
				double& h = w.height.At(i, j);
				if (d <= R)
				{
					const double depth = std::min(9.0, 1.0 + R * 0.025) * (1.0 - (d / R) * (d / R)) + 0.6;
					h = std::min(h, lake.waterY - depth);
				}
				else
				{
					const double t = SmoothStep(R, R + 40.0, d);
					h = Lerp(std::min(h, lake.waterY + 0.3), h, t);
				}
			});
		}
	}
}
