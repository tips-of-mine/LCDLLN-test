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
#include <cstdio>
#include <cmath>
#include <limits>
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

		/// Profil d'altitude d'un code de case. \p infernal : carte des Enfers (profils historiques
		/// de v1.0.71, inchangés) ; sinon Royaumes, montagnes à l'échelle des Pyrénées depuis la
		/// v1.0.130 (décision du 19/11/2026 : une montagne fait au moins 1 400 m, les sommets
		/// enneigés culminent vers 3 400 m comme l'Aneto).
		CodeProfile ProfileOf(char c, bool infernal)
		{
			if (!infernal)
				switch (c)
				{
				case '^': return { 1300.0, 120.0, 1000.0, 0 };  // montagne : 1 400 à 2 200 m
				case 'M': return { 1750.0, 150.0, 1500.0, 0 };  // chaîne : crêtes de 2 000 à 2 900 m
				case '*': return { 2250.0, 160.0, 1600.0, 0 };  // sommets enneigés : jusqu'à ≈ 3 400 m
				case 'V': return { 520.0, 80.0, 700.0, 0 };     // volcanique : cônes de 1 000 à 1 500 m
				default: break;
				}
			switch (c)
			{
			case ' ': return { 6.0, 3.0, 0.0, 0 };       // mer profonde
			case '-': return { 26.0, 4.0, 0.0, 0 };      // haut-fond
			case ',': return { 39.5, 3.0, 0.0, 0 };      // côte (le rivage passe dedans)
			case '_': return { 43.0, 2.5, 0.0, 0 };      // plage
			case 'o': return { 50.0, 10.0, 0.0, 0 };     // îlot
			// Amplitudes des terres basses : le bruit de gradient a un écart-type
			// d'environ 0,2 ; une amplitude A donne donc un relief de ±0,2 A environ.
			case '.': return { 62.0, 46.0, 0.0, 0 };     // plaine (collines douces)
			case 'r': return { 56.0, 32.0, 0.0, 0 };     // plaine + rivière
			case 'p': return { 58.0, 36.0, 0.0, 0 };     // plaine + lac
			case 'T': return { 80.0, 70.0, 0.0, 0 };     // forêt
			case 't': return { 72.0, 56.0, 0.0, 0 };     // lisière
			case 'R': return { 66.0, 46.0, 0.0, 0 };     // forêt + rivière
			case 'L': return { 70.0, 50.0, 0.0, 0 };     // forêt + lac
			case '~': return { 84.0, 48.0, 0.0, 1 };     // désert (dunes)
			case ':': return { 80.0, 50.0, 0.0, 0 };     // terres arides
			case 'l': return { 72.0, 40.0, 0.0, 0 };     // aride + lac
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
		bool IsLakeCode(char c) { return c == 'L' || c == 'l' || c == 'p'; }

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

	double CrevasseOuterSlopeM(const ReliefDef& r)
	{
		return std::max(r.wallM, 0.4 * r.footRadiusM);
	}

	double GouffreGalleryDistance(const ReliefDef& r, Vec2 p, double* outAlong)
	{
		double best = 1e18, along = 0.0;
		for (size_t k = 0; k + 1 < r.gallery.size(); ++k)
		{
			double t = 0.0;
			const double d = DistPointSegment(p, r.gallery[k], r.gallery[k + 1], &t);
			if (d < best)
			{
				best = d;
				along = r.galleryAlong[k] + t * (r.galleryAlong[k + 1] - r.galleryAlong[k]);
			}
		}
		if (outAlong) *outAlong = along;
		return best;
	}

	double GouffreFloorRadiusAt(const ReliefDef& r, double angleRad)
	{
		// Contour lobé (± 13 %) : fosse naturelle, pas un cercle tracé au compas.
		return r.floorRadiusM * (1.0 + 0.08 * std::sin(3.0 * angleRad + r.phase) + 0.05 * std::sin(5.0 * angleRad + 2.0 * r.phase));
	}

	double GouffreHeight(const ReliefDef& r, double x, double z, double h)
	{
		const double dx = x - r.center.x, dz = z - r.center.z;
		if (std::fabs(dx) > r.footRadiusM || std::fabs(dz) > r.footRadiusM) return h;
		double out = h;
		// Fosse : fond plat à floorY, parois abruptes (S de wall_m de large).
		const double ang = std::atan2(dz, dx);
		const double rf = GouffreFloorRadiusAt(r, ang);
		const double pit = 1.0 - SmoothStep(rf, rf + r.wallM, std::hypot(dx, dz));
		if (pit > 0.0) out = std::min(out, Lerp(h, r.floorY, pit));
		// Galerie : fente dont le sol monte régulièrement du fond à l'entrée.
		if (r.gallery.size() >= 2)
		{
			double along = 0.0;
			const double s = GouffreGalleryDistance(r, { x, z }, &along);
			const double slot = 1.0 - SmoothStep(r.galleryHalfWidthM, r.galleryHalfWidthM + r.galleryWallM, s);
			if (slot > 0.0)
			{
				const double run = std::max(1.0, r.galleryAlong.back() - r.galleryExitM);
				const double y = Lerp(r.floorY, r.entranceY, Clamp((along - r.galleryExitM) / run, 0.0, 1.0));
				out = std::min(out, Lerp(h, y, slot));
			}
		}
		return out;
	}

	bool InGouffre(const ReliefDef& r, Vec2 p, double marginM)
	{
		if (r.type != "gouffre") return false;
		const double dx = p.x - r.center.x, dz = p.z - r.center.z;
		if (std::fabs(dx) > r.footRadiusM + marginM || std::fabs(dz) > r.footRadiusM + marginM) return false;
		if (std::hypot(dx, dz) < GouffreFloorRadiusAt(r, std::atan2(dz, dx)) + r.wallM + marginM) return true;
		return r.gallery.size() >= 2 && GouffreGalleryDistance(r, p, nullptr) < r.galleryHalfWidthM + r.galleryWallM + marginM;
	}

	bool InAnyGouffre(const WorldMaster& m, Vec2 p, double marginM)
	{
		for (const ReliefDef& r : m.reliefs)
			if (InGouffre(r, p, marginM)) return true;
		return false;
	}

	namespace
	{
		/// v1.0.140 — vrai pendant le calcul du plateau d'une ville en gradins : MacroHeight y
		/// rend le relief SANS les acropoles (le plateau est celui de la ville basse).
		thread_local bool g_acropoleOff = false;
	}

	double AcropoleTerrace(const AcropoleShape& a, double d)
	{
		if (!a.enabled || a.terraces <= 0) return 0.0;
		if (d <= a.topRadiusM) return a.heightM;
		const double stepH = a.heightM / a.terraces;
		const double period = a.bankM + a.terraceM;
		const double x = d - a.topRadiusM;
		const int k = static_cast<int>(std::floor(x / period));
		if (k >= a.terraces) return 0.0;
		const double r = x - k * period;
		const double hi = a.heightM - k * stepH, lo = hi - stepH;
		// Talus linéaire (pente constante ≤ 40°, praticable), puis replat.
		return r < a.bankM ? Lerp(hi, lo, r / a.bankM) : lo;
	}

	double AcropoleOffset(const AcropoleShape& a, Vec2 p)
	{
		if (!a.enabled) return 0.0;
		const double dx = p.x - a.summit.x, dz = p.z - a.summit.z;
		const double d = std::hypot(dx, dz);
		// Revers (côté opposé à la ville) : distances étirées de backFactor au-delà de la
		// plateforme (fondu sur 53° autour de la perpendiculaire) — la butte reste sur la terre
		// ferme et tombe en falaise côté mer ; côté ville, gradins praticables inchangés.
		const double cosT = d > 1e-6 ? (dx * a.stairDir.x + dz * a.stairDir.z) / d : 1.0;
		const double back = SmoothStep(0.0, 0.6, -cosT);
		const double dEff = d <= a.topRadiusM ? d : a.topRadiusM + (d - a.topRadiusM) * (1.0 + (a.backFactor - 1.0) * back);
		const double terr = dEff < a.FootRadiusM() + 1.0 ? AcropoleTerrace(a, dEff) : 0.0;
		// Escalier monumental : rampe de pente 0,6 (marche 0,18 × 0,30 m) dans l'axe sommet →
		// grand-place, du bord de la plateforme jusqu'au niveau de la place, puis à plat.
		const double along = dx * a.stairDir.x + dz * a.stairDir.z;
		const double across = std::fabs(-dx * a.stairDir.z + dz * a.stairDir.x);
		if (along < a.topRadiusM - 2.0 || along > a.stairBottomM + 4.0 || across > a.stairHalfWidthM + 4.0) return terr;
		const double plazaY = AcropoleTerrace(a, a.stairBottomM);
		const double ramp = Clamp(a.heightM - 0.6 * (along - a.topRadiusM), plazaY, a.heightM);
		const double wAcross = 1.0 - SmoothStep(a.stairHalfWidthM, a.stairHalfWidthM + 4.0, across);
		const double wAlong = 1.0 - SmoothStep(a.stairBottomM, a.stairBottomM + 4.0, along);
		return Lerp(terr, ramp, wAcross * wAlong);
	}

	bool OnAcropoleSummit(const WorldMaster& m, Vec2 p, double marginM)
	{
		for (const ReliefDef& r : m.reliefs)
			if (r.type == "acropole" && std::hypot(p.x - r.acropole.summit.x, p.z - r.acropole.summit.z) <= r.acropole.topRadiusM + marginM)
				return true;
		return false;
	}

	// ---------------------------------------------------------------------------------
	// v1.0.143 — VILLE-TOUR (relief `pic`, Magistrya). Fonctions pures de la
	// forme : rocher de l'aiguille, cages d'escalier (volées, paliers). La heightmap et les
	// bâtiments sont posés par BuildTowerCity (Settlements.cpp).
	// ---------------------------------------------------------------------------------
	namespace
	{
		constexpr double kTowerPi = 3.14159265358979323846;
		/// Volées par niveau de bâtiments (trois tiers de niveau).
		constexpr int kTowerFlightsPerTier = 3;
		/// Demi-écart (m) entre les axes des deux voies d'une cage.
		constexpr double kTowerLaneM = 0.95;
	}

	double TowerShaftAngle(const TowerShape& t, int j)
	{
		return t.entryRad + 2.0 * kTowerPi * j / std::max(1, t.shaftsLow);
	}

	int TowerShaftTiers(const TowerShape& t, int j)
	{
		const int ratio = std::max(1, t.shaftsLow / std::max(1, t.shaftsHigh));
		if (j % ratio != 0) return std::max(0, t.SplitTier() - 1);
		// Cage face au laboratoire du palais (derrière la salle du trône) : arrêt un niveau
		// sous le sommet, le plafond est le dallage de la plateforme.
		if (std::fabs(std::remainder(TowerShaftAngle(t, j) - t.entryRad - kTowerPi, 2.0 * kTowerPi)) < 20.0 * kTowerPi / 180.0)
			return t.Tiers() - 1;
		return t.Tiers();
	}

	bool TowerShaftOpenAt(const TowerShape& t, int j, int tier)
	{
		return tier >= 0 && tier < TowerShaftTiers(t, j) + (TowerShaftTiers(t, j) < t.Tiers() ? 1 : 0);
	}

	double TowerShaftOuterR(const TowerShape& t, int tier)
	{
		return t.EnvelopeR((tier + 3) * t.TierM());
	}

	double TowerStairScale(const TowerShape& t)
	{
		return t.TierM() / kTowerFlightsPerTier / kTowerStairRiseM;
	}

	std::vector<TowerFlight> TowerShaftFlights(const TowerShape& t, int j)
	{
		std::vector<TowerFlight> out;
		if (!t.enabled) return out;
		const double s = TowerStairScale(t);
		const double run = (kTowerStairFrontM + kTowerStairBackM) * s;
		const int levels = TowerShaftTiers(t, j) * kTowerFlightsPerTier;
		for (int g = 0; g < levels; ++g)
		{
			TowerFlight f;
			f.level = g;
			f.tier = g / kTowerFlightsPerTier;
			f.inward = g % 2 == 0;
			f.lane = f.inward ? -kTowerLaneM : kTowerLaneM;
			const double rs = TowerShaftOuterR(t, f.tier);
			const double rOuter = rs - kTowerLandingM, rInner = rOuter - run;
			f.rBottom = f.inward ? rOuter : rInner;
			f.rTop = f.inward ? rInner : rOuter;
			f.rise = t.TierM() / kTowerFlightsPerTier;
			f.yBottom = g * f.rise;
			out.push_back(f);
		}
		return out;
	}

	std::vector<TowerLanding> TowerShaftLandings(const TowerShape& t, int j)
	{
		std::vector<TowerLanding> out;
		const std::vector<TowerFlight> fl = TowerShaftFlights(t, j);
		if (fl.empty()) return out;
		const double rise = fl.front().rise;
		for (size_t g = 0; g <= fl.size(); ++g)
		{
			TowerLanding L;
			L.level = static_cast<int>(g);
			L.y = g * rise;
			// Palier pair : côté lac (départ d'une volée vers l'aiguille) ; impair : côté aiguille.
			L.outer = g % 2 == 0;
			double lo = 1e18, hi = -1e18;
			if (g > 0) { lo = std::min(lo, fl[g - 1].rTop); hi = std::max(hi, fl[g - 1].rTop); }
			if (g < fl.size()) { lo = std::min(lo, fl[g].rBottom); hi = std::max(hi, fl[g].rBottom); }
			if (L.outer) { L.rIn = lo; L.rOut = hi + kTowerLandingM; }
			else { L.rIn = lo - kTowerLandingM; L.rOut = hi; }
			out.push_back(L);
		}
		return out;
	}

	double TowerOffset(const TowerShape& t, Vec2 p)
	{
		if (!t.enabled) return 0.0;
		const double r = std::hypot(p.x - t.center.x, p.z - t.center.z);
		if (r > t.SocleR()) return -1e9;
		const double span = t.baseRadiusM - t.topRadiusM;
		if (span <= 1e-6) return 0.0;
		// Cône du rocher : l'altitude où l'enveloppe passe kTowerRockInsetM plus loin que r.
		const double h = (t.baseRadiusM - (r + kTowerRockInsetM)) * t.heightM / span;
		return std::clamp(h, 0.0, t.heightM - 0.5);
	}

	bool OnTowerSummit(const WorldMaster& m, Vec2 p, double marginM)
	{
		for (const ReliefDef& r : m.reliefs)
			if (r.type == "pic" && std::hypot(p.x - r.tower.center.x, p.z - r.tower.center.z) <= r.tower.topRadiusM + marginM)
				return true;
		return false;
	}

	Vec2 CityRoadAnchor(const CityDef& c)
	{
		if (!c.tower.enabled) return c.center;
		return c.tower.center + Vec2{ std::cos(c.tower.entryRad), std::sin(c.tower.entryRad) } * (c.tower.lakeRadiusM * 1.08 + kTowerRoadReachM);
	}

	double ReliefContribution(const ReliefDef& r, double x, double z)
	{
		// Pic (v1.0.143) : sculpté dans la cellule (BuildTowerCity), aucune élévation macro.
		if (r.type == "pic") return 0.0;
		// Acropole (v1.0.140) : gradins et escalier, avant le masque des terres émergées.
		if (r.type == "acropole") return AcropoleOffset(r.acropole, { x, z });
		const double dx = x - r.center.x, dz = z - r.center.z;
		// Gouffre (v1.0.107) : il creuse jusqu'à une altitude absolue (GouffreHeight,
		// appliqué par ReliefOffset), il n'ajoute aucune élévation.
		if (r.type == "gouffre" || r.type == "falaise") return 0.0;
		if (r.type == "massif")
		{
			// Dôme au contour lobé (pied entre 0,76 et 1,24 × foot_radius_m), découpé par
			// un bruit en crêtes de 1,6 km : les crêtes gardent 100 % de la hauteur, les
			// ravins 35 % (vallées encaissées entre les pics).
			const double reach = r.footRadiusM * 1.25;
			if (std::fabs(dx) > reach || std::fabs(dz) > reach) return 0.0;
			const double ang = std::atan2(dz, dx);
			const double lobe = 1.0 + 0.16 * std::sin(3.0 * ang + r.phase) + 0.08 * std::sin(5.0 * ang + 2.0 * r.phase);
			const double s = 1.0 - SmoothStep(0.0, r.footRadiusM, std::hypot(dx, dz) / lobe);
			if (s <= 0.0) return 0.0;
			return r.heightM * s * (0.35 + 0.65 * Ridged(x, z, 1600.0, 4, r.noiseSeed));
		}
		if (r.type == "colline")
		{
			if (std::fabs(dx) > r.footRadiusM || std::fabs(dz) > r.footRadiusM) return 0.0;
			// Plateau sommital plat, puis flanc en S jusqu'au pied (pente max ≈ 1,5 × moyenne).
			return r.heightM * (1.0 - SmoothStep(r.topRadiusM, r.footRadiusM, std::hypot(dx, dz)));
		}
		// Crevasse : massif (bord extérieur en pente douce, contour ondulé) moins une
		// fente à fond plat aux parois raides — segment de `back_m` derrière la ville
		// jusqu'au-delà du massif, du côté de l'ouverture ; bout arrondi au fond (cul-de-sac).
		const double outer = CrevasseOuterSlopeM(r);
		const double reach = r.footRadiusM * 1.16 + outer;
		if (std::fabs(dx) > reach || std::fabs(dz) > reach) return 0.0;
		const double ang = std::atan2(dz, dx);
		const double lobe = 1.0 + 0.10 * std::sin(3.0 * ang + 0.7) + 0.06 * std::sin(5.0 * ang + 2.1);
		const double massif = 1.0 - SmoothStep(r.footRadiusM - outer, r.footRadiusM + outer, std::hypot(dx, dz) / lobe);
		if (massif <= 0.0) return 0.0;
		const double along = dx * r.dir.x + dz * r.dir.z;            // > 0 : vers l'ouverture
		const double across = std::fabs(-dx * r.dir.z + dz * r.dir.x);
		const double a = Clamp(along, -r.backM, reach + 100.0);
		const double s = std::hypot(along - a, across);              // distance à l'axe de la fente
		const double wall = SmoothStep(r.halfWidthM, r.halfWidthM + r.wallM, s);
		return r.heightM * massif * wall;
	}

	namespace
	{
		/// Vrai pendant la construction des grilles de falaise : MacroHeight y rend le
		/// relief SANS les falaises (la distance à la mer ne dépend pas d'elle-même).
		thread_local bool g_falaiseBuilding = false;

		/// Distance (m) du point au rectangle de la case (n, e) ; 0 dedans.
		double DistToCellRect(int n, int e, double x, double z)
		{
			const engine::world::cells::CellPoint c = engine::world::cells::CellCenter({ n, e });
			const double h = engine::world::cells::kCellSizeMeters * 0.5;
			const double dx = std::max(0.0, std::fabs(x - c.x) - h), dz = std::max(0.0, std::fabs(z - c.z) - h);
			return std::hypot(dx, dz);
		}

		/// Poids des cases d'une falaise en (x, z) : 1 dans une case, s'éteint sur fade_m.
		double FalaiseCellWeight(const ReliefDef& r, double x, double z)
		{
			double d = 1e18;
			for (const auto& [n, e] : r.cells) d = std::min(d, DistToCellRect(n, e, x, z));
			return 1.0 - SmoothStep(0.0, r.fadeM, d);
		}

		/// Construit la grille de distance SIGNÉE à la côte de la falaise : relief sans
		/// falaises échantillonné tous les 24 m ; masque « sous la mer » lissé par un
		/// filtre majoritaire de 408 m (les marais côtiers piquetés d'îlots et de mares
		/// deviennent une côte nette) ; la mer « reliée au large » part des échantillons du
		/// masque lissé dont le code du lore est marin (' ', '-') et se propage (4 voisins) dans le masque ; le reste (mares intérieures) compte comme
		/// terre. Distances par chanfrein 3-4 (deux passes) : positive à terre (distance au
		/// large), négative en mer (distance à la terre), côte au passage par zéro.
		FalaiseGrid BuildFalaiseGrid(const WorldMaster& m, const ReliefDef& r)
		{
			FalaiseGrid g;
			g.step = 24.0;
			// Une seule grille sur l'emprise des cases (et de leurs voisines côtières), plus
			// la marge d'extinction : une seule côte, aucun raccord entre grilles.
			const double margin = r.fadeM + r.beachM + r.wallM + 300.0;
			const double half = engine::world::cells::kCellSizeMeters * 0.5;
			double x0 = 1e18, x1 = -1e18, z0 = 1e18, z1 = -1e18;
			for (const auto& [n, e] : r.cells)
			{
				const engine::world::cells::CellPoint c = engine::world::cells::CellCenter({ n, e });
				x0 = std::min(x0, c.x - half - margin); x1 = std::max(x1, c.x + half + margin);
				z0 = std::min(z0, c.z - half - margin); z1 = std::max(z1, c.z + half + margin);
			}
			g.x0 = x0; g.z0 = z0;
			g.nx = static_cast<int>(std::ceil((x1 - x0) / g.step)) + 1;
			g.nz = static_cast<int>(std::ceil((z1 - z0) / g.step)) + 1;
			const size_t count = static_cast<size_t>(g.nx) * static_cast<size_t>(g.nz);
			const double sea = m.scale.seaLevelM;
			const double cs = engine::world::cells::kCellSizeMeters;
			std::vector<uint8_t> under(count, 0), ocean(count, 0);
			for (int j = 0; j < g.nz; ++j)
				for (int i = 0; i < g.nx; ++i)
					under[static_cast<size_t>(j) * g.nx + i] = MacroHeight(m, g.x0 + i * g.step, g.z0 + j * g.step) < sea ? 1 : 0;
			// Filtre majoritaire (fenêtre de 17 × 17 échantillons) par image intégrale.
			std::vector<int> integral(static_cast<size_t>(g.nx + 1) * (g.nz + 1), 0);
			for (int j = 0; j < g.nz; ++j)
				for (int i = 0; i < g.nx; ++i)
					integral[static_cast<size_t>(j + 1) * (g.nx + 1) + i + 1] = under[static_cast<size_t>(j) * g.nx + i]
						+ integral[static_cast<size_t>(j) * (g.nx + 1) + i + 1] + integral[static_cast<size_t>(j + 1) * (g.nx + 1) + i]
						- integral[static_cast<size_t>(j) * (g.nx + 1) + i];
			const int R = 8;
			std::vector<uint8_t> smooth(count, 0);
			std::vector<size_t> stack;
			for (int j = 0; j < g.nz; ++j)
				for (int i = 0; i < g.nx; ++i)
				{
					const int i0 = std::max(0, i - R), i1 = std::min(g.nx, i + R + 1), j0 = std::max(0, j - R), j1 = std::min(g.nz, j + R + 1);
					const int sum = integral[static_cast<size_t>(j1) * (g.nx + 1) + i1] - integral[static_cast<size_t>(j0) * (g.nx + 1) + i1]
						- integral[static_cast<size_t>(j1) * (g.nx + 1) + i0] + integral[static_cast<size_t>(j0) * (g.nx + 1) + i0];
					const size_t k = static_cast<size_t>(j) * g.nx + i;
					if (2 * sum <= (i1 - i0) * (j1 - j0)) continue;
					smooth[k] = 1;
					const double x = g.x0 + i * g.step, z = g.z0 + j * g.step;
					const char code = LoreCode(m, static_cast<int>(std::lround(-z / cs)), static_cast<int>(std::lround(x / cs)));
					if (code == ' ' || code == '-') { ocean[k] = 1; stack.push_back(k); }
				}
			while (!stack.empty())
			{
				const size_t k = stack.back(); stack.pop_back();
				const int i = static_cast<int>(k % g.nx), j = static_cast<int>(k / g.nx);
				const int ni[4] = { i - 1, i + 1, i, i }, nj[4] = { j, j, j - 1, j + 1 };
				for (int q = 0; q < 4; ++q)
				{
					if (ni[q] < 0 || nj[q] < 0 || ni[q] >= g.nx || nj[q] >= g.nz) continue;
					const size_t kk = static_cast<size_t>(nj[q]) * g.nx + ni[q];
					if (smooth[kk] && !ocean[kk]) { ocean[kk] = 1; stack.push_back(kk); }
				}
			}
			// Aucune mer dans la grille : grille sans information (ignorée à la lecture).
			g.hasOcean = std::find(ocean.begin(), ocean.end(), uint8_t{ 1 }) != ocean.end();
			if (!g.hasOcean) return g;
			g.dist.assign(count, 0.0f);
			// Chanfrein 3-4 (unités de pas / 3) vers les échantillons « cible ».
			const float inf = std::numeric_limits<float>::max() / 4.0f;
			auto chamfer = [&](uint8_t target) {
				std::vector<float> d(count, inf);
				for (size_t k = 0; k < count; ++k) if (ocean[k] == target) d[k] = 0.0f;
				auto relax = [&](int i, int j, int di, int dj, float w)
				{
					const int a2 = i + di, b2 = j + dj;
					if (a2 < 0 || b2 < 0 || a2 >= g.nx || b2 >= g.nz) return;
					float& cur = d[static_cast<size_t>(j) * g.nx + i];
					cur = std::min(cur, d[static_cast<size_t>(b2) * g.nx + a2] + w);
				};
				for (int j = 0; j < g.nz; ++j)
					for (int i = 0; i < g.nx; ++i)
					{ relax(i, j, -1, 0, 3.f); relax(i, j, 0, -1, 3.f); relax(i, j, -1, -1, 4.f); relax(i, j, 1, -1, 4.f); }
				for (int j = g.nz - 1; j >= 0; --j)
					for (int i = g.nx - 1; i >= 0; --i)
					{ relax(i, j, 1, 0, 3.f); relax(i, j, 0, 1, 3.f); relax(i, j, 1, 1, 4.f); relax(i, j, -1, 1, 4.f); }
				return d;
			};
			const std::vector<float> toOcean = chamfer(1), toLand = chamfer(0);
			const float toM = static_cast<float>(g.step / 3.0), halfStep = static_cast<float>(g.step * 0.5);
			for (size_t k = 0; k < count; ++k)
			{
				// Côte à mi-chemin entre un échantillon de mer et un de terre.
				if (ocean[k]) g.dist[k] = (toLand[k] >= inf) ? -1e9f : -(toLand[k] * toM - halfStep);
				else g.dist[k] = (toOcean[k] >= inf) ? 1e9f : toOcean[k] * toM - halfStep;
			}
			return g;
		}

		/// Grilles de la falaise, construites à la première demande (une fois, thread-safe).
		const std::vector<FalaiseGrid>& FalaiseGrids(const WorldMaster& m, const ReliefDef& r)
		{
			std::call_once(r.falaise->once, [&]()
			{
				const bool prev = g_falaiseBuilding;
				g_falaiseBuilding = true;
				r.falaise->grids.push_back(BuildFalaiseGrid(m, r));
				g_falaiseBuilding = prev;
			});
			return r.falaise->grids;
		}

		/// Distance signée (m) à la côte en (x, z) (positive à terre), bilinéaire, moyenne
		/// des grilles qui couvrent le point pondérée par l'éloignement de leur bord (fondu
		/// de 600 m) : les grilles voisines, échantillonnées aux mêmes points, se raccordent
		/// sans marche ; très grande hors des grilles. \param coverage somme des poids,
		/// bornée à 1 : la falaise s'éteint avec elle au bord des grilles qui voient la mer.
		double FalaiseDistToSea(const WorldMaster& m, const ReliefDef& r, double x, double z, double& coverage)
		{
			double sum = 0.0, wsum = 0.0;
			for (const FalaiseGrid& g : FalaiseGrids(m, r))
			{
				const double fx = (x - g.x0) / g.step, fz = (z - g.z0) / g.step;
				if (!g.hasOcean || fx < 0.0 || fz < 0.0 || fx > g.nx - 1.0 || fz > g.nz - 1.0) continue;
				const double edge = std::min({ fx, fz, g.nx - 1.0 - fx, g.nz - 1.0 - fz }) * g.step;
				const double wg = SmoothStep(0.0, 600.0, edge);
				if (wg <= 0.0) continue;
				const int i = std::min(static_cast<int>(fx), g.nx - 2), j = std::min(static_cast<int>(fz), g.nz - 2);
				const double tx = fx - i, tz = fz - j;
				auto at = [&](int a, int b) { return static_cast<double>(g.dist[static_cast<size_t>(b) * g.nx + a]); };
				const double v = (at(i, j) * (1 - tx) + at(i + 1, j) * tx) * (1 - tz) + (at(i, j + 1) * (1 - tx) + at(i + 1, j + 1) * tx) * tz;
				sum += wg * Clamp(v, -5000.0, 5000.0);
				wsum += wg;
			}
			coverage = std::min(1.0, wsum);
			return wsum > 0.0 ? sum / wsum : 1e9;
		}
	}

	double FalaiseOffset(const WorldMaster& m, const ReliefDef& r, double x, double z, double baseH)
	{
		if (g_falaiseBuilding || !r.falaise) return 0.0;
		const double w = FalaiseCellWeight(r, x, z);
		if (w <= 0.0) return 0.0;
		const double sea = m.scale.seaLevelM;
		double coverage = 0.0;
		const double dist = FalaiseDistToSea(m, r, x, z, coverage);
		if (dist >= 1e8) return 0.0; // aucune côte connue ici
		if (dist <= 0.0)
		{
			// Côté mer : les îlots du marais côtier s'enfoncent sous l'eau (8 m de fondu).
			const double target = sea - 1.5;
			return (baseH > target) ? w * coverage * SmoothStep(0.0, -8.0, dist) * (target - baseH) : 0.0;
		}
		// Côté terre : plage comblée au niveau du rivage (mares comprises), puis la paroi.
		const double fill = (baseH < sea + 0.5) ? (sea + 0.5 - baseH) * SmoothStep(0.0, 4.0, dist) : 0.0;
		const double prof = SmoothStep(r.beachM, r.beachM + r.wallM, dist);
		// Hauteur variable le long de la côte (± 15 %, ondulation de ≈ 1,8 km).
		const double var = 1.0 + 0.3 * Clamp(Fbm(x, z, 1800.0, 2, r.noiseSeed), -0.5, 0.5);
		return w * coverage * (fill + r.heightM * var * prof);
	}

	double FalaiseWallAt(const WorldMaster& m, double x, double z, double extraM)
	{
		double best = 0.0;
		for (const ReliefDef& r : m.reliefs)
		{
			if (r.type != "falaise" || !r.falaise) continue;
			const double w = FalaiseCellWeight(r, x, z);
			if (w <= 0.0) continue;
			double coverage = 0.0;
			const double dist = FalaiseDistToSea(m, r, x, z, coverage);
			if (dist >= 1e8) continue;
			const double band = SmoothStep(r.beachM - 6.0, r.beachM + 4.0, dist)
				* (1.0 - SmoothStep(r.beachM + r.wallM + 4.0 + extraM, r.beachM + r.wallM + 20.0 + extraM, dist));
			best = std::max(best, w * coverage * band);
		}
		return best;
	}

	double ReliefOffset(const WorldMaster& m, double x, double z, double baseH)
	{
		double h = 0.0, massif = 0.0;
		bool gouffre = false;
		for (const ReliefDef& r : m.reliefs)
		{
			if (r.type == "gouffre") { gouffre = true; continue; }
			// Falaise (v1.0.120) : côte nette tirée du masque lissé ; îlots du marais noyés côté
			// mer, plage et mares comblées côté terre, puis paroi et plateau (FalaiseOffset).
			if (r.type == "falaise") { h += FalaiseOffset(m, r, x, z, baseH); continue; }
			// Acropole (v1.0.140) : même masque que les massifs (la côte ne bouge pas) ;
			// ignorée pendant le calcul du plateau de sa ville (g_acropoleOff).
			if (r.type == "acropole") { if (!g_acropoleOff) massif += ReliefContribution(r, x, z); continue; }
			// Pic (v1.0.143) : hors du relief macro (sculpté dans sa cellule, BuildTowerCity).
			if (r.type == "pic") continue;
			(r.type == "massif" ? massif : h) += ReliefContribution(r, x, z);
		}
		// Massifs (v1.0.77) : seulement sur les terres émergées (0,5 à 3 m au-dessus de
		// la mer pour le fondu) ; la côte ne bouge pas et le relief y monte en falaise.
		if (massif > 0.0) h += massif * SmoothStep(m.scale.seaLevelM + 0.5, m.scale.seaLevelM + 3.0, baseH);
		// Gouffres (v1.0.107) : creusés en dernier, jusqu'à leur altitude absolue ;
		// hors de leur boîte, GouffreHeight rend l'altitude telle quelle (écart nul).
		if (gouffre)
			for (const ReliefDef& r : m.reliefs)
				if (r.type == "gouffre")
				{
					const double cur = baseH + h;
					h += GouffreHeight(r, x, z, cur) - cur;
				}
		return h;
	}

	const ReliefDef* ReliefOfCity(const WorldMaster& m, const std::string& cityId)
	{
		for (const ReliefDef& r : m.reliefs)
			if (r.cityId == cityId) return &r;
		return nullptr;
	}

	void MacroBiomeWarp(const WorldMaster& m, double x, double z, double& bx, double& bz)
	{
		const uint64_t seed = m.seed;
		// Déformation forte (≈ 1,3 case) et fine (lisières à l'échelle du marcheur).
		bx = x + Fbm(x, z, 1.33 * kC, 3, seed + 13) * 1.3 * kC
			+ Fbm(x, z, 0.35 * kC, 2, seed + 14) * 0.3 * kC + Fbm(x, z, 300.0, 3, seed + 17) * 380.0;
		bz = z + Fbm(x, z, 1.33 * kC, 3, seed + 15) * 1.3 * kC
			+ Fbm(x, z, 0.35 * kC, 2, seed + 16) * 0.3 * kC + Fbm(x, z, 300.0, 3, seed + 18) * 380.0;
	}

	char MacroBiomeFromWarp(const WorldMaster& m, double bx, double bz)
	{
		const int be = static_cast<int>(std::lround(bx / kC));
		const int bn = static_cast<int>(std::lround(-bz / kC));
		return LoreCode(m, bn, be);
	}

	char MacroBiome(const WorldMaster& m, double x, double z)
	{
		double bx = 0.0, bz = 0.0;
		MacroBiomeWarp(m, x, z, bx, bz);
		return MacroBiomeFromWarp(m, bx, bz);
	}

	double InfernalLavaAt(const WorldMaster& m, double x, double z)
	{
		if (!IsInfernalMap(m)) return 0.0;
		const uint64_t seed = m.seed;
		double lava = 0.0;
		// Distance au lieu (hors mers de lave) ou à la ville la plus proche : les
		// fleuves s'en écartent (portails, Citadelle, palais restent praticables).
		double clear = 1e18;
		for (const PoiDef& p : m.pois)
		{
			const engine::world::cells::CellPoint c = engine::world::cells::CellCenter({ p.n, p.e });
			// Position manuelle (v1.0.95) prioritaire sur le décalage v1.0.72.
			const double px = c.x + (p.hasPosition ? p.positionXM : p.offsetX);
			const double pz = c.z + (p.hasPosition ? p.positionZM : p.offsetZ);
			const double d = std::hypot(x - px, z - pz);
			if (p.kind == "volcan")
			{
				// Mer de lave (T17 p16, T21 p36, T22 p22) : disque au bord ondulé.
				const double r = 1700.0 * (1.0 + 0.28 * Fbm(x, z, 650.0, 2, seed + 9101));
				lava = std::max(lava, SmoothStep(r, r - 160.0, d));
			}
			else
				clear = std::min(clear, d);
		}
		for (const CityDef& c : m.cities)
		{
			const engine::world::cells::CellPoint cc = engine::world::cells::CellCenter({ c.n, c.e });
			// Ville posée à la main (v1.0.95) : distance à sa position ; sinon au centre de case.
			const double cx = cc.x + (c.hasPosition ? c.positionXM : 0.0);
			const double cz = cc.z + (c.hasPosition ? c.positionZM : 0.0);
			clear = std::min(clear, std::hypot(x - cx, z - cz));
		}
		// Fleuves de lave (T16 p16 : canyon de falaises rouges, fleuves et cascades
		// de lave jaune-orange) : lignes de niveau d'un bruit à grande échelle, dans
		// la croix volcanique seulement.
		if (MacroBiome(m, x, z) == 'V')
		{
			const double ridge = std::fabs(Fbm(x, z, 3800.0, 2, seed + 9102));
			const double river = SmoothStep(0.032, 0.012, ridge) * SmoothStep(550.0, 950.0, clear);
			lava = std::max(lava, river);
		}
		return Clamp(lava, 0.0, 1.0);
	}

	/// v1.0.131 — point visé dans une case de passage d'une route du lore : un point de la
	/// case tiré au hasard (déterministe : graine, case) à ±3 km du centre, au lieu du centre
	/// lui-même ; les routes ne relient plus les centres des cases en ligne droite.
	/// PracticablePointInCell le ramène ensuite sur un nœud franchissable à 600 m des bords.
	/// Cases d'eau et de montagne : le centre (historique).
	Vec2 JitteredCellPoint(const WorldMaster& m, int n, int e)
	{
		const engine::world::cells::CellPoint ctr = engine::world::cells::CellCenter({ n, e });
		// Cases d'eau (côte, plage, îlot, rivière, lac, marais) : le centre, car un point au
		// hasard tomberait souvent de l'autre côté d'un estuaire ou d'un lac.
		const char code = LoreCode(m, n, e);
		// v1.0.131 : seulement en pays ouvert (plaine, forêt, lisière, désert, terres arides) ; en montagne,
		// le col passe par le centre (cases de passage imposées : Passe des Dragons…).
		if (std::string(".Tt~:").find(code) == std::string::npos) return { ctr.x, ctr.z };
		const uint64_t h = Hash(m.seed, static_cast<uint64_t>(n + 1000), static_cast<uint64_t>(e + 1000), 4811);
		const Vec2 p{ ctr.x + (ToUnit(h) * 2.0 - 1.0) * 3000.0, ctr.z + (ToUnit(Mix64(h)) * 2.0 - 1.0) * 3000.0 };
		// Près d'un relief posé (gouffre, crevasse, colline : abords calmes + 1 km), le centre :
		// un point tiré au pied du relief laisserait la route dans une impasse.
		for (const WorldMaster::CalmZone& c : m.calmZones)
			if (std::hypot(p.x - c.center.x, p.z - c.center.z) < c.radiusM + 1000.0) return { ctr.x, ctr.z };
		return p;
	}

	namespace
	{
		/// v1.0.130 — proximité (0..1) d'une route du lore au point (x, z) : 1 sur le tracé
		/// (corde entre centres de cases successifs, lue en un point déformé : couloir sinueux),
		/// 0 au-delà de 2 km. Grille calculée une
		/// fois (PassField) ; 0 si le master n'a pas de champ (tests) ou pas de route.
		double PassAt(const WorldMaster& m, double x, double z)
		{
			PassField* f = m.passes.get();
			if (!f || m.loreRoads.empty()) return 0.0;
			std::call_once(f->once, [&]() {
				const int rows = static_cast<int>(m.gridRows.size());
				f->originX = (m.scale.westE - 0.5) * kC;
				f->originZ = -(m.scale.northN + 0.5) * kC;
				f->width = static_cast<int>(m.scale.gridCols * kC / f->step) + 1;
				f->height = static_cast<int>(rows * kC / f->step) + 1;
				f->value.assign(static_cast<size_t>(f->width) * f->height, 0.0f);
				const double reach = 2000.0, core = 500.0;
				std::vector<Vec2> anchors;
				for (const LoreRoadDef& r : m.loreRoads)
					for (size_t k = 0; k + 1 < r.cellNE.size(); ++k)
					{
						// Ancres : centre des cases d'extrémité, point visé des cases de passage (comme
						// le tracé des routes, JitteredCellPoint).
						auto anchorOf = [&](size_t q) {
							const auto [cn, ce] = r.cellNE[q];
							if (q == 0 || q + 1 == r.cellNE.size())
							{
								const engine::world::cells::CellPoint c = engine::world::cells::CellCenter({ cn, ce });
								return Vec2{ c.x, c.z };
							}
							return JitteredCellPoint(m, cn, ce);
						};
						const Vec2 A = anchorOf(k), B = anchorOf(k + 1);
						anchors.push_back(A); anchors.push_back(B);
						const int i0 = std::max(0, static_cast<int>((std::min(A.x, B.x) - reach - f->originX) / f->step));
						const int i1 = std::min(f->width - 1, static_cast<int>((std::max(A.x, B.x) + reach - f->originX) / f->step) + 1);
						const int j0 = std::max(0, static_cast<int>((std::min(A.z, B.z) - reach - f->originZ) / f->step));
						const int j1 = std::min(f->height - 1, static_cast<int>((std::max(A.z, B.z) + reach - f->originZ) / f->step) + 1);
						for (int j = j0; j <= j1; ++j)
							for (int i = i0; i <= i1; ++i)
							{
								const double d = DistPointSegment({ f->originX + i * f->step, f->originZ + j * f->step }, A, B, nullptr);
								float& v = f->value[static_cast<size_t>(j) * f->width + i];
								v = std::max(v, static_cast<float>(SmoothStep(reach, core, d)));
							}
					}
				// Distance (m, plafonnée à 6 km) à l'ancre la plus proche : la déformation du
				// couloir s'annule près des ancres (la route y entre et en sort).
				f->anchorDist.assign(f->value.size(), 6000.0f);
				for (const Vec2& A : anchors)
				{
					const int i0 = std::max(0, static_cast<int>((A.x - 6000.0 - f->originX) / f->step)), i1 = std::min(f->width - 1, static_cast<int>((A.x + 6000.0 - f->originX) / f->step) + 1);
					const int j0 = std::max(0, static_cast<int>((A.z - 6000.0 - f->originZ) / f->step)), j1 = std::min(f->height - 1, static_cast<int>((A.z + 6000.0 - f->originZ) / f->step) + 1);
					for (int j = j0; j <= j1; ++j)
						for (int i = i0; i <= i1; ++i)
						{
							float& ad = f->anchorDist[static_cast<size_t>(j) * f->width + i];
							ad = std::min(ad, static_cast<float>(std::hypot(f->originX + i * f->step - A.x, f->originZ + j * f->step - A.z)));
						}
				}
			});
			// Couloir sinueux : la grille est lue en un point déformé (±1,4 km, ≈ 4 km), le col
			// serpente au lieu de tracer une saignée droite entre deux centres de cases.
			{
				const int ai = Clamp(static_cast<int>(std::lround((x - f->originX) / f->step)), 0, f->width - 1);
				const int aj = Clamp(static_cast<int>(std::lround((z - f->originZ) / f->step)), 0, f->height - 1);
				const double taper = SmoothStep(1500.0, 5000.0, f->anchorDist[static_cast<size_t>(aj) * f->width + ai]);
				const double ox = x, oz = z;
				x += Fbm(ox, oz, 4200.0, 2, m.seed + 71) * 4500.0 * taper;
				z += Fbm(ox, oz, 4200.0, 2, m.seed + 72) * 4500.0 * taper;
			}
			const double fx = (x - f->originX) / f->step, fz = (z - f->originZ) / f->step;
			const int i = static_cast<int>(std::floor(fx)), j = static_cast<int>(std::floor(fz));
			if (i < 0 || j < 0 || i + 1 >= f->width || j + 1 >= f->height) return 0.0;
			const double tx = fx - i, tz = fz - j;
			auto at = [&](int ii, int jj) { return static_cast<double>(f->value[static_cast<size_t>(jj) * f->width + ii]); };
			return Lerp(Lerp(at(i, j), at(i + 1, j), tx), Lerp(at(i, j + 1), at(i + 1, j + 1), tx), tz);
		}
	}

	double MacroHeight(const WorldMaster& m, double x, double z)
	{
		const uint64_t seed = m.seed;
		const bool infernal = IsInfernalMap(m);
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
		// v1.0.130 — reliefs posés à la main (gouffre, crevasse, colline) : leurs abords gardent
		// le relief historique (profils d'avant les Pyrénées, sans grandes collines ni cols),
		// sur lequel leurs formes et leurs profondeurs ont été réglées. `calm` = 0 jusqu'à 2 km
		// de son emprise (accès des routes du lore), 1 au-delà de 3,5 km (zones relevées au
		// chargement du master).
		double calm = 1.0;
		if (!infernal)
			for (const WorldMaster::CalmZone& c : m.calmZones)
				calm = std::min(calm, SmoothStep(c.radiusM + 2000.0, c.radiusM + 3500.0, std::hypot(x - c.center.x, z - c.center.z)));
		CodeProfile acc{ 0, 0, 0, 0 };
		for (int k = 0; k < 4; ++k)
		{
			const char code = LoreCode(m, in + dn[k], ie + de[k]);
			const CodeProfile pn = ProfileOf(code, infernal);
			const CodeProfile po = calm < 1.0 ? ProfileOf(code, true) : pn;
			acc.base += Lerp(po.base, pn.base, calm) * wts[k];
			acc.amp += Lerp(po.amp, pn.amp, calm) * wts[k];
			acc.ridge += Lerp(po.ridge, pn.ridge, calm) * wts[k];
			acc.dune += pn.dune * wts[k];
		}
		double h = acc.base;
		// Caractère régional (≈ 7 km) : alternance de pays plats et vallonnés à
		// l'intérieur d'une même case du lore (évite la monotonie sur 10 km).
		const double region = Clamp(Fbm(x, z, 7000.0, 2, seed + 24) + 0.5, 0.0, 1.0);
		const double ampMod = 0.45 + 1.1 * region;
		h += Fbm(x, z, 2400.0, 6, seed + 21) * acc.amp * ampMod;  // collines (km)
		// Relief à l'échelle du marcheur (distance de vue ≤ 1,6 km) : c'est lui qui
		// rend le terrain lisible ; les grandes ondulations seules paraissent plates.
		h += Fbm(x, z, 1000.0, 4, seed + 27) * acc.amp * 1.2 * ampMod; // buttes
		h += Fbm(x, z, 320.0, 4, seed + 22) * acc.amp * 0.8;      // mamelons (centaines de m)
		h += Fbm(x, z, 110.0, 3, seed + 28) * 6.0;                // bosses (dizaines de m)
		h += Fbm(x, z, 45.0, 2, seed + 23) * 0.8;                 // grain du sol
		// Hors montagnes : chaînons de collines (crêtes douces) dans les pays
		// vallonnés, et vallons (lignes creuses) qui structurent le paysage.
		const double lowland = (1.0 - SmoothStep(80.0, 300.0, acc.ridge)) * SmoothStep(44.0, 58.0, acc.base);
		if (lowland > 0.01)
		{
			const double hilly = SmoothStep(0.35, 0.80, region);
			h += (Ridged(x, z, 1800.0, 4, seed + 25) - 0.3) * 110.0 * hilly * lowland;
			const double vf = 1.0 - std::fabs(Fbm(x, z, 5200.0, 3, seed + 26));
			h -= 24.0 * SmoothStep(0.86, 0.99, vf) * lowland;
			// Plancher souple : l'intérieur des terres ne plonge pas sous la mer
			// (les étendues d'eau intérieures sont des lacs explicites).
			const double floorY = m.scale.seaLevelM + 6.0;
			if (h < floorY) h = Lerp(h, floorY - (floorY - h) * 0.2, lowland);
			// v1.0.130 — GRANDES COLLINES (Royaumes) : buttes isolées de 150 à 400 m au-dessus de
			// la plaine (longueur d'onde ≈ 6 km), dans la moitié des pays vallonnés ; jamais près
			// des côtes ni dans les marais (`lowland` nul), donc le rivage ne bouge pas.
			if (!infernal)
			{
				const double big = SmoothStep(0.06, 0.42, Fbm(x, z, 6200.0, 3, seed + 61));
				const double where = SmoothStep(0.30, 0.60, Clamp(Fbm(x, z, 16000.0, 2, seed + 62) + 0.5, 0.0, 1.0));
				h += 380.0 * big * where * lowland * SmoothStep(58.0, 66.0, acc.base) * calm;
			}
		}
		if (acc.ridge > 0.5)
		{
			if (infernal)
				h += (Ridged(x, z, 4500.0, 6, seed + 31) - 0.35) * acc.ridge;
			else
			{
				// v1.0.130 — cols : près d'une route du lore, la montagne s'abaisse (col de
				// 1 000 à 1 800 m entre des crêtes de 2 000 à 3 000 m) ; les routes passent.
				const double pass = PassAt(m, x, z) * calm;
				h -= 0.30 * std::max(0.0, acc.base - 300.0) * pass;
				// v1.0.130 — montagnes des Royaumes : grandes crêtes continues (crête de ≈ 9 km,
				// coordonnées déformées : les chaînes s'allongent au lieu de former des taches)
				// et reliefs secondaires (≈ 2,6 km) ; sommets jusqu'à ≈ 3 400 m.
				const double wx = x + Fbm(x, z, 14000.0, 2, seed + 63) * 9000.0;
				const double wz = z + Fbm(x, z, 14000.0, 2, seed + 64) * 9000.0;
				const double crest = Ridged(wx, wz * 0.55, 9000.0, 5, seed + 65);
				const double detail = Ridged(x, z, 2600.0, 4, seed + 66);
				const double ridgeNew = (0.75 * (crest - 0.35) + 0.45 * (detail - 0.35)) * acc.ridge * (1.0 - 0.7 * pass);
				const double ridgeOld = calm < 1.0 ? (Ridged(x, z, 4500.0, 6, seed + 31) - 0.35) * acc.ridge : ridgeNew;
				h += Lerp(ridgeOld, ridgeNew, calm);
			}
		}
		if (acc.dune > 0.01)
			h += std::sin((x * 0.8 + z * 0.6) / 60.0 + Fbm(x, z, 900.0, 2, seed + 41) * 3.0) * 4.0 * acc.dune;
		// Reliefs locaux posés à la main (v1.0.72) : colline de Vaerdhun, crevasse de Vharholdt ;
		// massifs de l'Île de la Trêve (v1.0.77).
		if (!m.reliefs.empty()) h += ReliefOffset(m, x, z, h);
		// v1.0.130 — abords calmes : le plafond historique (height_scale_m 1 200 m) y est
		// rétabli, sans quoi les anciens massifs y dresseraient des parois infranchissables
		// (route 000074 vers le gouffre de Thurvhal).
		if (calm < 1.0) h = Lerp(std::min(h, 1198.0), h, calm);
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
			// v1.0.143 : île centrale (pic d'une ville-tour) hors de l'eau.
			if (l.islandRadiusM > 0.0 && dx * dx + dz * dz < l.islandRadiusM * l.islandRadiusM) continue;
			// EAU.5 : contour réel (polygone bruité ±8 %), plus le cercle moyen.
			const double r = LakeRadiusAt(l, std::atan2(dz, dx)) + 2.0;
			if (dx * dx + dz * dz < r * r) return true;
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
			// v1.0.143 : une ville-tour occupe son pic jusqu'au bord du socle.
			if (c.tower.enabled) c.radiusM = c.tower.SocleR();
			const engine::world::cells::CellPoint ctr = engine::world::cells::CellCenter({ c.n, c.e });
			// Position posée à la main (v1.0.95, « tu places, le jeu suit ») : la ville est
			// posée exactement là, sans recherche de site ; son plateau (même calcul que
			// ci-dessous) aplanit le relief autour et le relève au-dessus de la mer.
			if (c.hasPosition)
			{
				c.center = { ctr.x + c.positionXM, ctr.z + c.positionZM };
				// v1.0.140 : une ville en gradins (acropole) garde le plateau de sa ville basse,
				// relevé sur le relief SANS la butte (sinon la moyenne la soulèverait).
				g_acropoleOff = c.acropole.enabled;
				if (MacroHeight(m, c.center.x, c.center.z) < sea + 6.0)
					g.Warn("position manuelle de " + c.id + " dans l'eau : plateau releve au-dessus de la mer");
				c.plateauY = std::max(MeanMacroHeight(m, c.center, c.radiusM + 6.0, 12.0), sea + 5.0);
				g_acropoleOff = false;
				continue;
			}
			Vec2 best{ ctr.x, ctr.z };
			double bestScore = 1e18;
			// Ville de départ : ancrée au centre de sa case (point d'arrivée au boot,
			// repère des cellules), tant qu'il est au sec ; son plateau aplanit le
			// relief. Le site n'est cherché ailleurs que si le centre est dans l'eau.
			// Ville ancrée sur un relief local (v1.0.72 : sommet de colline, fond de
			// crevasse) : toujours au centre de sa case, là où le relief est dessiné.
			const bool anchored = ReliefOfCity(m, c.id) != nullptr;
			const double searchR = (anchored || (!c.startFor.empty() && MacroHeight(m, ctr.x, ctr.z) >= sea + 6.0)) ? 0.0 : 3500.0;
			for (double dz = -searchR; dz <= searchR; dz += 250.0)
				for (double dx = -searchR; dx <= searchR; dx += 250.0)
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
			if (bestScore > 1e17 && !anchored) g.Warn("aucun site sec pour " + c.id + " : centre de case retenu");
			if (anchored && MacroHeight(m, ctr.x, ctr.z) < sea + 6.0) g.Warn("relief de " + c.id + " : centre de case dans l'eau");
			c.center = best;
			g_acropoleOff = c.acropole.enabled; // v1.0.140 : plateau de la ville basse
			c.plateauY = std::max(MeanMacroHeight(m, c.center, c.radiusM + 6.0, 12.0), sea + 5.0);
			g_acropoleOff = false;
		}
		g.pois = m.pois;
		for (PoiDef& p : g.pois)
		{
			const engine::world::cells::CellPoint ctr = engine::world::cells::CellCenter({ p.n, p.e });
			// Position posée à la main (v1.0.95) : exactement là, jamais écartée de la
			// ville de la case ni des autres POI (les POI automatiques s'écartent d'elle).
			if (p.hasPosition)
			{
				p.center = { ctr.x + p.positionXM, ctr.z + p.positionZM };
				continue;
			}
			p.center = { ctr.x + p.offsetX, ctr.z + p.offsetZ }; // décalage v1.0.72 (0 par défaut)
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
			// Plusieurs POI dans la même case (ex. temple et Seuil dans la case de
			// Lhynn) : décalés d'un quart de tour autour de la ville de la case (ou de
			// 400 m vers l'est sans ville) tant qu'ils sont à moins de 300 m d'un POI
			// déjà placé.
			const CityDef* host = nullptr;
			for (const CityDef& c : g.cities)
				if (c.n == p.n && c.e == p.e) { host = &c; break; }
			for (int tries = 0; tries < 8; ++tries)
			{
				bool clash = false;
				for (const PoiDef& q : g.pois)
				{
					if (&q == &p) break; // POI suivants pas encore placés
					if (q.n == p.n && q.e == p.e && std::hypot(q.center.x - p.center.x, q.center.z - p.center.z) < 300.0) clash = true;
				}
				if (!clash) break;
				if (host)
				{
					const Vec2 d = p.center - host->center;
					const double a = std::atan2(d.z, d.x) + 1.5707963267948966;
					p.center = host->center + Vec2{ std::cos(a), std::sin(a) } * std::hypot(d.x, d.z);
				}
				else
					p.center.x += 400.0;
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
				// v1.0.143 : la case d'une ville-tour a SON lac, centré sur le pic (ci-dessous).
				bool towerCell = false;
				for (const CityDef& c : g.cities) towerCell = towerCell || (c.tower.enabled && c.n == n && c.e == e);
				if (towerCell) continue;
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
		// v1.0.143 — lac d'une ville-tour (Magistrya, « au centre d'un grand lac ») : centré sur le
		// pic, rayon du relief, île centrale = socle du pic (pas d'eau, lit non creusé).
		for (const CityDef& c : g.cities)
		{
			if (!c.tower.enabled) continue;
			Lake lake;
			lake.name = "lac_" + c.id;
			lake.center = c.tower.center;
			lake.radiusM = c.tower.lakeRadiusM;
			lake.islandRadiusM = c.tower.SocleR() + 4.0;
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
			// Polygone CCW (vu de dessus, X est / Z sud), sommets bruités.
			const int verts = std::clamp(static_cast<int>(lake.radiusM / 6.0), 28, 96);
			for (int k = 0; k < verts; ++k)
			{
				const double a = 6.283185307179586 * k / verts;
				const double rr = lake.radiusM * (1.0 + 0.08 * Fbm(std::cos(a) * 3.0, std::sin(a) * 3.0, 1.0, 2, Hash(m.seed, HashString(lake.name.c_str()))));
				lake.polygon.push_back({ lake.center.x + std::cos(a) * rr, lake.center.z + std::sin(a) * rr });
			}
			lake.waterY = LakeWaterLevel(m, lake,
				MeanMacroHeight(m, lake.center, lake.radiusM + 30.0, std::max(4.0, lake.radiusM / 12.0)));
		}
		// v1.0.143 : le plateau d'une ville-tour est son socle, au ras de son lac.
		for (CityDef& c : g.cities)
			if (c.tower.enabled)
				for (const Lake& l : g.lakes)
					if (l.name == "lac_" + c.id) c.plateauY = l.waterY + kTowerSocleAboveWaterM;
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

		/// Lot carte v1.0.129 — coût (m pondérés) pour qu'un cours d'eau passe du nœud \p a au
		/// nœud voisin \p b de la grille grossière, dans le SENS DU COURANT (a amont, b aval) :
		/// longueur × bruit de vallée (0,45 à 1,55 : les cours serpentent au lieu de suivre les
		/// diagonales de la grille) × préférence pour les terres basses, plus une forte pénalité
		/// si l'eau devrait remonter (b plus haut que a sur la grille remplie).
		double FlowStepCost(const Grid<double>& f, int ai, int aj, int bi, int bj, double sea, uint64_t seed)
		{
			const double len = f.step * ((ai != bi && aj != bj) ? 1.41421356 : 1.0);
			const double x = f.WorldX(bi), z = f.WorldZ(bj);
			const double nz = Clamp(0.5 + 1.6 * Fbm(x, z, 2600.0, 3, seed + 931), 0.0, 1.0);
			const double low = 1.0 + std::max(0.0, f.At(bi, bj) - sea) / 350.0;
			return len * (0.45 + 1.1 * nz) * low + 300.0 * std::max(0.0, f.At(bi, bj) - f.At(ai, aj));
		}

		/// Lot carte v1.0.129 — arbre d'écoulement du continent : pour chaque nœud de la grille
		/// remplie, le nœud voisin VERS LEQUEL l'eau s'écoule (-1 = mer ou bord). Dijkstra depuis
		/// la mer et les bords avec FlowStepCost : les chemins vers la mer suivent les vallées et
		/// serpentent sur les plats (l'ancienne descente de plus grande pente traçait des marches
		/// d'escalier à 45° et 90° sur les cuvettes remplies). Déterministe.
		std::vector<int32_t> FlowTree(const Grid<double>& f, double sea, uint64_t seed)
		{
			const int nx = f.width, nz = f.height;
			std::vector<int32_t> down(static_cast<size_t>(nx) * nz, -2);
			std::vector<double> cost(static_cast<size_t>(nx) * nz, 1e300);
			using Item = std::pair<double, int32_t>;
			std::priority_queue<Item, std::vector<Item>, std::greater<Item>> pq;
			for (int j = 0; j < nz; ++j)
				for (int i = 0; i < nx; ++i)
					if (i == 0 || j == 0 || i == nx - 1 || j == nz - 1 || f.At(i, j) < sea - 0.5)
					{
						const int32_t q = j * nx + i;
						cost[static_cast<size_t>(q)] = 0.0; down[static_cast<size_t>(q)] = -1; pq.push({ 0.0, q });
					}
			std::vector<uint8_t> done(static_cast<size_t>(nx) * nz, 0);
			while (!pq.empty())
			{
				const auto [c, q] = pq.top(); pq.pop();
				if (done[static_cast<size_t>(q)]) continue;
				done[static_cast<size_t>(q)] = 1;
				const int i = q % nx, j = q / nx;
				for (int dj = -1; dj <= 1; ++dj)
					for (int di = -1; di <= 1; ++di)
					{
						const int ni = i + di, nj = j + dj;
						if ((!di && !dj) || !f.InBounds(ni, nj)) continue;
						const int32_t nq = nj * nx + ni;
						if (done[static_cast<size_t>(nq)]) continue;
						// L'eau va de (ni, nj) vers (i, j) : amont = voisin, aval = nœud courant.
						const double nc = c + FlowStepCost(f, ni, nj, i, j, sea, seed);
						if (nc < cost[static_cast<size_t>(nq)])
						{
							cost[static_cast<size_t>(nq)] = nc; down[static_cast<size_t>(nq)] = q; pq.push({ nc, nq });
						}
					}
			}
			return down;
		}

		/// Lot carte v1.0.129 — chemin d'un cours d'eau entre deux points (sens du courant),
		/// A* 8-connexe sur la grille remplie avec FlowStepCost, limité à la boîte des deux
		/// points élargie de 6 km. Renvoie les centres de nœuds (sans le premier), ou vide.
		std::vector<Vec2> CourseAStar(const Grid<double>& f, Vec2 a, Vec2 b, double sea, uint64_t seed)
		{
			const double margin = 6000.0;
			const int i0 = std::max(0, f.IndexX(std::min(a.x, b.x) - margin)), i1 = std::min(f.width - 1, f.IndexX(std::max(a.x, b.x) + margin));
			const int j0 = std::max(0, f.IndexZ(std::min(a.z, b.z) - margin)), j1 = std::min(f.height - 1, f.IndexZ(std::max(a.z, b.z) + margin));
			const int w = i1 - i0 + 1, h = j1 - j0 + 1;
			if (w <= 1 || h <= 1) return {};
			const int si = Clamp(f.IndexX(a.x), i0, i1), sj = Clamp(f.IndexZ(a.z), j0, j1);
			const int ti = Clamp(f.IndexX(b.x), i0, i1), tj = Clamp(f.IndexZ(b.z), j0, j1);
			std::vector<double> gc(static_cast<size_t>(w) * h, 1e300);
			std::vector<int32_t> from(static_cast<size_t>(w) * h, -1);
			std::vector<uint8_t> done(static_cast<size_t>(w) * h, 0);
			using Item = std::pair<double, int32_t>;
			std::priority_queue<Item, std::vector<Item>, std::greater<Item>> pq;
			auto L = [&](int i, int j) { return (j - j0) * w + (i - i0); };
			auto heur = [&](int i, int j) { return std::hypot(i - ti, j - tj) * f.step * 0.45; };
			gc[static_cast<size_t>(L(si, sj))] = 0.0;
			pq.push({ heur(si, sj), L(si, sj) });
			bool found = false;
			while (!pq.empty())
			{
				const int32_t q = pq.top().second; pq.pop();
				if (done[static_cast<size_t>(q)]) continue;
				done[static_cast<size_t>(q)] = 1;
				const int i = q % w + i0, j = q / w + j0;
				if (i == ti && j == tj) { found = true; break; }
				for (int dj = -1; dj <= 1; ++dj)
					for (int di = -1; di <= 1; ++di)
					{
						const int ni = i + di, nj = j + dj;
						if ((!di && !dj) || ni < i0 || nj < j0 || ni > i1 || nj > j1) continue;
						const int32_t nq = L(ni, nj);
						if (done[static_cast<size_t>(nq)]) continue;
						const double nc = gc[static_cast<size_t>(q)] + FlowStepCost(f, i, j, ni, nj, sea, seed);
						if (nc < gc[static_cast<size_t>(nq)]) { gc[static_cast<size_t>(nq)] = nc; from[static_cast<size_t>(nq)] = q; pq.push({ nc + heur(ni, nj), nq }); }
					}
			}
			if (!found) return {};
			std::vector<Vec2> out;
			for (int32_t q = L(ti, tj); q >= 0 && q != L(si, sj); q = from[static_cast<size_t>(q)])
				out.push_back({ f.WorldX(q % w + i0), f.WorldZ(q / w + j0) });
			std::reverse(out.begin(), out.end());
			return out;
		}
	}

	void BuildMacroRivers(GlobalWorld& g)
	{
		const WorldMaster& m = g.master;
		const WorldScale& s = m.scale;
		const double sea = s.seaLevelM;
		const int rows = static_cast<int>(m.gridRows.size());

		// Cases tenues par un cours d'eau tracé à la main (`river_courses`) : leur
		// composante n'a pas de rivière automatique.
		std::vector<uint8_t> courseCell(static_cast<size_t>(rows) * s.gridCols, 0);
		for (const RiverCourseDef& rc : m.riverCourses)
			for (auto [n, e] : rc.cellNE)
				courseCell[static_cast<size_t>(s.northN - n) * s.gridCols + (e - s.westE)] = 1;

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

		// Descente de plus grande pente sur la grille remplie, depuis le nœud (ci, cj),
		// jusqu'à la mer (ou le bord) ; les points sont ajoutés à `path`.
		// Lot carte v1.0.129 : l'eau suit l'arbre d'écoulement (FlowTree) depuis le nœud
		// (ci, cj) jusqu'à la mer (ou le bord) ; les points sont ajoutés à `path`.
		const std::vector<int32_t> flowDown = FlowTree(filled, sea, m.seed);
		auto descend = [&](int ci, int cj, std::vector<Vec2>& path) {
			int32_t q = cj * filled.width + ci;
			for (int stepIdx = 0; stepIdx < 60000 && q >= 0; ++stepIdx)
			{
				const int i = q % filled.width, j = q / filled.width;
				path.push_back({ filled.WorldX(i), filled.WorldZ(j) });
				if (filled.At(i, j) < sea - 0.5) break;
				q = flowDown[static_cast<size_t>(q)];
			}
		};

		// Méandres, lissage, ré-échantillonnage, surface et largeur d'une rivière.
		// `meanderWeight(p)` (0..1) atténue les méandres (1 = ±60 m, plus ±`bigMeanderM`
		// sur ~7 km pour un grand cours tracé à la main) ; largeur de `widthSrc`
		// (source) à `widthSrc + widthGain` (aval).
		auto finish = [&](std::vector<Vec2> path, const std::string& name, double widthSrc, double widthGain,
			double bigMeanderM, const auto& meanderWeight) {
			// Petits méandres : appliqués de proche en proche (comportement historique).
			// Grands méandres : normale prise sur le tracé d'origine (pas de boucle).
			const std::vector<Vec2> straight = path;
			for (size_t k = 1; k + 1 < path.size(); ++k)
			{
				const double w = meanderWeight(path[k]);
				if (bigMeanderM > 0.0)
				{
					const Vec2 d0 = straight[std::min(k + 8, straight.size() - 1)] - straight[k >= 8 ? k - 8 : 0];
					const double l0 = std::hypot(d0.x, d0.z);
					if (l0 > 1e-6)
						path[k] = path[k] + Vec2{ -d0.z / l0, d0.x / l0 } * (Fbm(straight[k].x, straight[k].z, 7000.0, 2, m.seed + 917) * 2.0 * bigMeanderM * w);
				}
				const Vec2 d = path[k + 1] - path[k - 1];
				const double l = std::hypot(d.x, d.z);
				if (l < 1e-6) continue;
				const double off = Fbm(path[k].x, path[k].z, 1800.0, 3, m.seed + 909) * 120.0 * w;
				path[k] = path[k] + Vec2{ -d.z / l, d.x / l } * off;
			}
			River river;
			river.name = name;
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
				river.widthM[k] = widthSrc + widthGain * t;
				prev = y;
			}
			return river;
		};

		int riverIndex = 0;
		for (const auto& cells : comps)
		{
			bool ownedByCourse = false;
			for (auto [r, c] : cells)
				if (courseCell[static_cast<size_t>(r) * s.gridCols + c]) ownedByCourse = true;
			if (ownedByCourse) continue;
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
			descend(static_cast<int>(Clamp(filled.IndexX(src.x), 1, filled.width - 2)),
				static_cast<int>(Clamp(filled.IndexZ(src.z), 1, filled.height - 2)), path);
			if (path.size() < 12) continue; // trop court : pas une rivière
			g.rivers.push_back(finish(std::move(path), "riviere_" + std::to_string(riverIndex++), 10.0, 22.0, 0.0,
				[](Vec2) { return 1.0; }));
		}

		// Cours tracés à la main : centres des cases dans l'ordre (le centre de la
		// ville-île remplace celui de sa case), contournement des autres villes,
		// puis descente vers la mer depuis la dernière case (case d'embouchure,
		// côtière : une cuvette intérieure sous le niveau de la mer arrêterait la
		// descente avant la côte).
		// Distance minimale (m) entre un cours tracé à la main et la ville \p c : rayon
		// urbain + 400 m, ou pied de la colline + 150 m pour une ville perchée (v1.0.72 :
		// les rivières de Vaerdhun naissent au pied de sa colline).
		auto riverClearance = [&](const CityDef& c) {
			const ReliefDef* r = ReliefOfCity(m, c.id);
			return (r && r->type == "colline") ? std::max(c.radiusM + 400.0, r->footRadiusM + 150.0) : c.radiusM + 400.0;
		};
		// Atténuation des méandres sur une colline (0 au pied, 1 à 1,5 km au-delà) :
		// le lit ne remonte pas le flanc.
		auto hillCalm = [&](Vec2 p) {
			double w = 1.0;
			for (const ReliefDef& r : m.reliefs)
				if (r.type == "colline")
					w *= SmoothStep(r.footRadiusM + 150.0, r.footRadiusM + 1650.0, std::hypot(p.x - r.center.x, p.z - r.center.z));
			return w;
		};
		for (const RiverCourseDef& rc : m.riverCourses)
		{
			const CityDef* island = rc.islandCity.empty() ? nullptr : g.FindCity(rc.islandCity);
			std::vector<Vec2> way;
			for (auto [n, e] : rc.cellNE)
			{
				const engine::world::cells::CellPoint p = engine::world::cells::CellCenter({ n, e });
				way.push_back((island && island->n == n && island->e == e) ? island->center : Vec2{ p.x, p.z });
			}
			// Lot carte v1.0.129 : les ancres intermédiaires (centres de cases) sont décalées de
			// ±1,8 km (bruit déterministe) et chaque tronçon suit le terrain (CourseAStar) : le
			// cours ne relie plus les centres des cases en ligne droite.
			for (size_t k = 1; k + 1 < way.size(); ++k)
			{
				if (island && std::hypot(way[k].x - island->center.x, way[k].z - island->center.z) < 1.0) continue;
				const Vec2 o = way[k];
				way[k].x = o.x + Clamp(Fbm(o.x, o.z, 9000.0, 2, m.seed + 941) * 4000.0, -1800.0, 1800.0);
				way[k].z = o.z + Clamp(Fbm(o.x, o.z, 9000.0, 2, m.seed + 942) * 4000.0, -1800.0, 1800.0);
			}
			std::vector<Vec2> path{ way.front() };
			for (size_t k = 0; k + 1 < way.size(); ++k)
			{
				// Tronçons qui touchent la ville-île : ligne droite (historique), pour que les deux
				// bras l'encerclent symétriquement ; ailleurs, le tracé suit la vallée.
				const bool islandSeg = island && (std::hypot(way[k].x - island->center.x, way[k].z - island->center.z) < 1.0
					|| std::hypot(way[k + 1].x - island->center.x, way[k + 1].z - island->center.z) < 1.0);
				std::vector<Vec2> seg = islandSeg ? std::vector<Vec2>{} : CourseAStar(filled, way[k], way[k + 1], sea, m.seed);
				if (seg.empty())
				{
					// Repli : ligne droite (250 m) si l'A* échoue (ne devrait pas arriver).
					const double len = std::hypot(way[k + 1].x - way[k].x, way[k + 1].z - way[k].z);
					const int steps = std::max(1, static_cast<int>(std::ceil(len / 250.0)));
					for (int i = 1; i <= steps; ++i) seg.push_back(way[k] + (way[k + 1] - way[k]) * (static_cast<double>(i) / steps));
				}
				if (!seg.empty()) seg.back() = way[k + 1];
				path.insert(path.end(), seg.begin(), seg.end());
			}
			// Contournement des villes (sauf la ville-île) : chaque point trop proche est
			// repoussé sur le cercle de dégagement.
			for (size_t k = 0; k < path.size(); ++k)
			{
				Vec2& p = path[k];
				for (const CityDef& c : g.cities)
				{
					if (island && c.id == island->id) continue;
					const double need = riverClearance(c);
					const double d = std::hypot(p.x - c.center.x, p.z - c.center.z);
					if (d >= need) continue;
					if (d > 1e-6) p = c.center + (p - c.center) * (need / d);
					else
					{
						// Source au centre même d'une ville (ville perchée) : vers l'aval du cours.
						const Vec2 v = path[std::min(k + 1, path.size() - 1)] - path[k > 0 ? k - 1 : 0];
						const double l = std::hypot(v.x, v.z);
						if (l > 1e-6) p = c.center + v * (need / l);
					}
				}
			}
			if (rc.toSea)
				descend(static_cast<int>(Clamp(filled.IndexX(way.back().x), 1, filled.width - 2)),
					static_cast<int>(Clamp(filled.IndexZ(way.back().z), 1, filled.height - 2)), path);

			// Rayon de l'île : plateau urbain + raccord + demi-lit + berge.
			const double half = rc.widthM * 0.5;
			const double ri = island ? island->radiusM + 6.0 + std::max(46.0, island->radiusM * 0.6) + half + 40.0 : 0.0;
			const double split = 2.5 * ri; // demi-longueur de la zone des deux bras (m)
			River main = finish(std::move(path), rc.id, rc.widthM * 0.6, rc.widthM * 0.4, 700.0, [&](Vec2 p) {
				return hillCalm(p) * (island ? SmoothStep(split, split + 1500.0, std::hypot(p.x - island->center.x, p.z - island->center.z)) : 1.0);
			});
			if (!island)
			{
				g.rivers.push_back(std::move(main));
				continue;
			}
			// Île fluviale : point du lit le plus proche du centre, abscisse curviligne.
			const size_t np = main.points.size();
			size_t kc = 0;
			double dc = 1e18;
			for (size_t k = 0; k < np; ++k)
			{
				const double d = std::hypot(main.points[k].x - island->center.x, main.points[k].z - island->center.z);
				if (d < dc) { dc = d; kc = k; }
			}
			const size_t span = static_cast<size_t>(split / 6.0);
			if (kc < span || kc + span >= np)
			{
				g.Warn("cours " + rc.id + " : ile de " + rc.islandCity + " trop pres d'un bout du cours, bras non crees");
				g.rivers.push_back(std::move(main));
				continue;
			}
			const size_t k0 = kc - span, k1 = kc + span;
			Vec2 dir = main.points[k1] - main.points[k0];
			const double dl = std::max(1e-6, std::hypot(dir.x, dir.z));
			dir = dir * (1.0 / dl);
			const Vec2 nrm{ -dir.z, dir.x };
			// Bras = lit d'origine ramené sur l'axe passant par le centre, écarté de
			// ±1,15 ri au droit de la ville (cloche en cosinus nulle aux deux bouts) :
			// la ville reste à plus de ~1,15 ri de l'axe de chaque bras.
			River arm;
			arm.name = rc.id + "_bras";
			for (size_t k = k0; k <= k1; ++k)
			{
				const double u = (static_cast<double>(k) - static_cast<double>(kc)) * 6.0;
				const double bell = 0.5 * (1.0 + std::cos(3.141592653589793 * u / split));
				const Vec2 axis = island->center + dir * u;
				const Vec2 base = main.points[k] * (1.0 - bell) + axis * bell;
				const double w = main.widthM[k] * (1.0 - 0.35 * bell);
				arm.points.push_back(base - nrm * (1.15 * ri * bell));
				arm.waterY.push_back(main.waterY[k]);
				arm.widthM.push_back(w);
				main.points[k] = base + nrm * (1.15 * ri * bell);
				main.widthM[k] = w;
			}
			g.rivers.push_back(std::move(main));
			g.rivers.push_back(std::move(arm));
		}
		// Lot carte v1.0.129 — RUISSEAUX : petites rivières qui remplissent le monde. Sources sur
		// un treillis de 10 km décalé (bruit déterministe), en terre (≥ mer + 30 m) hors
		// déserts, terres arides, marais, volcans, côtes et îlots, à plus de 3 km d'un cours
		// d'eau, de 1,5 km d'une ville et de 600 m d'un lieu ; chacune suit l'arbre
		// d'écoulement jusqu'à la mer, un lac, ou une rivière qu'elle rejoint (confluent exact).
		// Contourne les villes comme les grands cours ; abandonnée si elle entre dans les
		// abords calmes d'un relief posé (gouffre, crevasse, colline : `calmZones`, relevés au
		// chargement — le réseau ne dépend donc pas de la présence du relief lui-même) ou si
		// elle fait moins de 5 km. Largeur 4 m à la source, 11 m à l'aval.
		// Aucun ruisseau aux Enfers : l'eau n'y coule pas, seuls les fleuves de lave du
		// master y serpentent (palette v1.0.83 : pas de bleu ni de vert sur la carte).
		if (!IsInfernalMap(g.master))
		{
			const int nx = filled.width, nz = filled.height;
			// Nœuds occupés par un cours d'eau (index du point de rivière) et zone d'exclusion des sources.
			std::vector<int32_t> riverAt(static_cast<size_t>(nx) * nz, -1);
			std::vector<std::pair<int, int>> riverRef; // (rivière, point)
			std::vector<uint8_t> nearRiver(static_cast<size_t>(nx) * nz, 0);
			auto mark = [&](int ri) {
				const River& r = g.rivers[static_cast<size_t>(ri)];
				for (size_t k = 0; k < r.points.size(); k += 6)
				{
					const int i = filled.IndexX(r.points[k].x), j = filled.IndexZ(r.points[k].z);
					if (!filled.InBounds(i, j)) continue;
					const int32_t q = j * nx + i;
					if (riverAt[static_cast<size_t>(q)] < 0)
					{
						riverAt[static_cast<size_t>(q)] = static_cast<int32_t>(riverRef.size());
						riverRef.push_back({ ri, static_cast<int>(k) });
					}
					for (int dj = -12; dj <= 12; ++dj)
						for (int di = -12; di <= 12; ++di)
							if (filled.InBounds(i + di, j + dj) && di * di + dj * dj <= 144) nearRiver[static_cast<size_t>((j + dj) * nx + i + di)] = 1;
				}
			};
			for (int ri = 0; ri < static_cast<int>(g.rivers.size()); ++ri) mark(ri);
			auto lakeAt = [&](Vec2 p) {
				for (const Lake& l : g.lakes)
					if (std::hypot(p.x - l.center.x, p.z - l.center.z) < l.radiusM + 60.0) return true;
				return false;
			};
			// Vrai si p est à moins de margin des abords calmes d'un relief posé.
			auto inCalm = [&](Vec2 p, double margin) {
				for (const WorldMaster::CalmZone& c : m.calmZones)
					if (std::hypot(p.x - c.center.x, p.z - c.center.z) < c.radiusM + margin) return true;
				return false;
			};
			const double lattice = 10000.0;
			int streamIndex = 0;
			for (double z0 = g.minZ + lattice * 0.5; z0 < g.maxZ; z0 += lattice)
				for (double x0 = g.minX + lattice * 0.5; x0 < g.maxX; x0 += lattice)
				{
					const Vec2 src{ x0 + Fbm(x0, z0, 5000.0, 2, m.seed + 951) * 12000.0, z0 + Fbm(x0, z0, 5000.0, 2, m.seed + 952) * 12000.0 };
					const int si = filled.IndexX(src.x), sj = filled.IndexZ(src.z);
					if (si < 2 || sj < 2 || si >= nx - 2 || sj >= nz - 2) continue;
					if (g.coarse.At(si, sj) < sea + 30.0 || nearRiver[static_cast<size_t>(sj * nx + si)]) continue;
					const char code = MacroBiome(m, src.x, src.z);
					if (code == '~' || code == ':' || code == 'l' || code == '%' || code == '&' || code == 'V' || code == ',' || code == '_' || code == 'o' || code == '-' || code == ' ') continue;
					bool blocked = false;
					for (const CityDef& c : g.cities) blocked = blocked || std::hypot(src.x - c.center.x, src.z - c.center.z) < c.radiusM + 1500.0;
					for (const PoiDef& pd : g.pois) blocked = blocked || std::hypot(src.x - pd.center.x, src.z - pd.center.z) < 600.0;
					if (blocked || inCalm(src, 400.0)) continue;
					// Descente le long de l'arbre jusqu'à la mer, un lac ou une rivière.
					std::vector<Vec2> path;
					int32_t q = sj * nx + si;
					int joinRef = -1;
					for (int stepIdx = 0; stepIdx < 4000 && q >= 0; ++stepIdx)
					{
						const int i = q % nx, j = q / nx;
						const Vec2 p{ filled.WorldX(i), filled.WorldZ(j) };
						if (stepIdx > 0)
						{
							int32_t hit = riverAt[static_cast<size_t>(q)];
							for (int dj = -1; dj <= 1 && hit < 0; ++dj)
								for (int di = -1; di <= 1 && hit < 0; ++di)
									if (filled.InBounds(i + di, j + dj)) hit = riverAt[static_cast<size_t>((j + dj) * nx + i + di)];
							if (hit >= 0) { joinRef = hit; break; }
						}
						path.push_back(p);
						if (filled.At(i, j) < sea - 0.5 || lakeAt(p)) break;
						if (inCalm(p, 0.0)) { path.clear(); break; }
						q = flowDown[static_cast<size_t>(q)];
					}
					if (joinRef >= 0)
					{
						const auto [ri, k] = riverRef[static_cast<size_t>(joinRef)];
						path.push_back(g.rivers[static_cast<size_t>(ri)].points[static_cast<size_t>(k)]);
					}
					if (path.size() < 20) continue; // < 5 km : pas un ruisseau
					// Contournement des villes et des lieux (le confluent reste exact).
					for (size_t k = 0; k + 1 < path.size(); ++k)
					{
						Vec2& p = path[k];
						for (const CityDef& c : g.cities)
						{
							const double need = riverClearance(c);
							const double d = std::hypot(p.x - c.center.x, p.z - c.center.z);
							if (d < need && d > 1e-6) p = c.center + (p - c.center) * (need / d);
						}
						for (const PoiDef& pd : g.pois)
						{
							const double d = std::hypot(p.x - pd.center.x, p.z - pd.center.z);
							if (d < 150.0 && d > 1e-6) p = pd.center + (p - pd.center) * (150.0 / d);
						}
					}
					River stream = finish(std::move(path), "ruisseau_" + std::to_string(streamIndex), 4.0, 7.0, 0.0,
						[&](Vec2 p) { return 0.6 * hillCalm(p); });
					// Le lissage entre nœuds contournés peut mordre sur une ville : ruisseau abandonné.
					bool intoCity = false;
					for (size_t k = 0; k < stream.points.size() && !intoCity; k += 2)
						for (const CityDef& c : g.cities)
							if (std::hypot(stream.points[k].x - c.center.x, stream.points[k].z - c.center.z) < c.radiusM + 60.0) { intoCity = true; break; }
					if (intoCity) continue;
					++streamIndex;
					g.rivers.push_back(std::move(stream));
					mark(static_cast<int>(g.rivers.size()) - 1);
				}
			std::printf("[world_gen] ruisseaux : %d (lot carte v1.0.129)\n", streamIndex);
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
		FillSeaPockets(w);
	}

	void FillSeaPockets(World& w)
	{
		const double sea = w.master.scale.seaLevelM;
		w.report.seaPocketSamples += FillLandlockedSea(w.height, w.biome, sea, sea + 0.5);
		w.hasSea = false;
		for (double hv : w.height.data)
			if (hv < sea) { w.hasSea = true; break; }
	}

	size_t FillLandlockedSea(Grid<double>& height, const Grid<char>& biome, double seaLevel, double fillY)
	{
		const int W = height.width, H = height.height;
		if (W <= 0 || H <= 0) return 0;
		const bool useBiome = biome.data.size() == height.data.size();
		std::vector<uint8_t> sea(height.data.size(), 0); // 1 = relié à la mer
		std::vector<size_t> stack;
		auto seed = [&](size_t idx) {
			if (!sea[idx] && height.data[idx] < seaLevel) { sea[idx] = 1; stack.push_back(idx); }
		};
		for (int j = 0; j < H; ++j)
			for (int i = 0; i < W; ++i)
			{
				const size_t idx = static_cast<size_t>(j) * W + i;
				const bool border = i == 0 || j == 0 || i == W - 1 || j == H - 1;
				const bool marine = useBiome && (biome.data[idx] == ' ' || biome.data[idx] == '-');
				if (border || marine) seed(idx);
			}
		while (!stack.empty())
		{
			const size_t idx = stack.back();
			stack.pop_back();
			const int i = static_cast<int>(idx % W), j = static_cast<int>(idx / W);
			if (i > 0) seed(idx - 1);
			if (i < W - 1) seed(idx + 1);
			if (j > 0) seed(idx - W);
			if (j < H - 1) seed(idx + W);
		}
		size_t filled = 0;
		for (size_t idx = 0; idx < height.data.size(); ++idx)
			if (!sea[idx] && height.data[idx] < seaLevel)
			{
				height.data[idx] = fillY;
				++filled;
			}
		return filled;
	}

	double LakeRadiusAt(const Lake& lake, double angleRad)
	{
		const size_t n = lake.polygon.size();
		if (n < 3) return lake.radiusM;
		constexpr double kTwoPi = 6.283185307179586;
		double a = std::fmod(angleRad, kTwoPi);
		if (a < 0.0) a += kTwoPi;
		const double f = a / kTwoPi * static_cast<double>(n);
		const size_t k0 = static_cast<size_t>(f) % n, k1 = (k0 + 1) % n;
		const double t = f - std::floor(f);
		auto radius = [&](size_t k) {
			return std::hypot(lake.polygon[k].x - lake.center.x, lake.polygon[k].z - lake.center.z);
		};
		return Lerp(radius(k0), radius(k1), t);
	}

	double LakeWaterLevel(const WorldMaster& m, const Lake& lake, double meanHeight)
	{
		// Relief de base juste au-delà du contour (10 m), 64 directions.
		std::vector<double> rim;
		rim.reserve(64);
		for (int k = 0; k < 64; ++k)
		{
			const double a = 6.283185307179586 * k / 64.0;
			const double r = LakeRadiusAt(lake, a) + 10.0;
			rim.push_back(MacroHeight(m, lake.center.x + std::cos(a) * r, lake.center.z + std::sin(a) * r));
		}
		// 15e centile : quelques creux isolés de la rive ne font pas tout baisser
		// (la digue de CarveLake les comble).
		std::nth_element(rim.begin(), rim.begin() + 9, rim.end());
		const double rimLow = rim[9];
		return std::max(m.scale.seaLevelM + 0.5, std::min(meanHeight - 0.8, rimLow - 0.3));
	}

	void CarveLake(Grid<double>& height, const Grid<float>& riverDist, const Grid<uint8_t>& urban, const Lake& lake, double floorY)
	{
		constexpr double kBeachM = 40.0;     // fondu de la rive vers le relief d'origine (m)
		constexpr double kBankAboveM = 0.3;  // sol au ras du bord : au-dessus de l'eau (m)
		constexpr double kDamSlope = 0.35;   // pente de la digue naturelle (≈ 19°)
		constexpr double kDamCrestM = 8.0;   // crête de la digue, au-dessus de l'eau (m)
		constexpr double kDamReachM = 120.0; // portée maximale de la digue (m)
		constexpr float kRiverKeepM = 12.0f; // pas de digue sur un lit de rivière (m)
		constexpr double kEdgeRiseM = 6.0;   // remontée du fond vers la rive, dans le polygone (m)
		double rMax = lake.radiusM;
		for (const Vec2& p : lake.polygon)
			rMax = std::max(rMax, std::hypot(p.x - lake.center.x, p.z - lake.center.z));
		const double depthMax = std::min(9.0, 1.0 + lake.radiusM * 0.025);
		const double bankY = lake.waterY + kBankAboveM;
		const bool hasRivers = riverDist.data.size() == height.data.size();
		const bool hasUrban = urban.data.size() == height.data.size();
		ForDisc(height, lake.center, rMax + kDamReachM, [&](int i, int j, double d) {
			const double dx = height.WorldX(i) - lake.center.x, dz = height.WorldZ(j) - lake.center.z;
			const double rr = std::max(1.0, LakeRadiusAt(lake, std::atan2(dz, dx)));
			double& h = height.At(i, j);
			// v1.0.143 : île centrale (pic d'une ville-tour, sculpté ensuite) : lit non creusé.
			if (d < lake.islandRadiusM) return;
			if (d <= rr)
			{
				// Fond : parabole sous la surface (0,6 m au bord, ≤ 9,6 m au centre).
				const double q = d / rr;
				// Jamais sous floorY (la mer) : la nappe de la mer couvre toute une
				// cellule côtière et apparaîtrait sous l'eau du lac.
				const double bed = std::max(floorY, lake.waterY - (depthMax * (1.0 - q * q) + 0.6));
				const double inside = rr - d;
				if (inside < kEdgeRiseM)
				{
					// Bande du bord : le sol remonte AU-DESSUS de l'eau au ras du
					// polygone ; le rivage réel est un peu en dedans et le bord de
					// la nappe reste enfoui sous la rive (jamais visible, même entre
					// deux échantillons de la heightmap).
					const double edgeY = Lerp(bankY, bed, SmoothStep(0.0, kEdgeRiseM, inside));
					// Un lit de rivière qui entre ou sort du lac n'est jamais rehaussé.
					const bool river = hasRivers && riverDist.At(i, j) < kRiverKeepM;
					h = river ? std::min(h, edgeY) : edgeY;
				}
				else
					h = std::min(h, bed);
				return;
			}
			const double out = d - rr;
			// Plateau urbain (aplani avant) : jamais retouché par la rive.
			if (hasUrban && urban.At(i, j) != 0) return;
			if (h >= bankY)
			{
				// Relief plus haut : grève jusqu'au bord de l'eau, élargie quand la
				// rive domine l'eau de haut (pente moyenne ≤ 22° environ).
				const double beach = std::clamp((h - bankY) * 2.5, kBeachM, kDamReachM);
				if (out < beach) h = Lerp(bankY, h, SmoothStep(0.0, beach, out));
			}
			else if (!hasRivers || riverDist.At(i, j) >= kRiverKeepM)
			{
				// Relief plus bas : digue naturelle (sinon la nappe flotte au-dessus du vide).
				h = std::max(h, bankY - std::max(0.0, out - kDamCrestM) * kDamSlope);
			}
		});
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
			// v1.0.143 — ville-tour : pic, socle et lac sculptés par BuildTowerCity (après l'eau).
			if (c.tower.enabled) continue;
			if (c.acropole.enabled)
			{
				// v1.0.140 — ville en gradins : plateau de la ville basse + gradins et escalier
				// (AcropoleOffset), raccordés au relief (qui porte déjà la butte, MacroHeight).
				const double R = c.radiusM + 6.0, blend = std::max(46.0, c.radiusM * 0.6);
				ForDisc(w.height, c.center, R + blend, [&](int i, int j, double d) {
					const Vec2 q{ w.height.WorldX(i), w.height.WorldZ(j) };
					const double t = SmoothStep(R, R + blend, d);
					const double ripple = Fbm(q.x, q.z, 30.0, 2, w.master.seed + 77) * 0.25;
					w.height.At(i, j) = Lerp(c.plateauY + AcropoleOffset(c.acropole, q) + ripple, w.height.At(i, j), t);
					if (d <= R) w.urban.At(i, j) = 1;
				});
				continue;
			}
			flatten(c.center, c.radiusM + 6.0, std::max(46.0, c.radiusM * 0.6), c.plateauY, true);
		}
		for (const PoiDef& p : w.pois)
		{
			// v1.0.140 : un lieu posé sur le sommet d'une acropole (palais) garde la plateforme.
			if (OnAcropoleSummit(w.master, p.center, 0.0)) continue;
			// v1.0.143 : idem sur le sommet d'une ville-tour (palais du Magister).
			if (OnTowerSummit(w.master, p.center, 0.0)) continue;
			// v1.0.132 : un château (enceinte, douves) demande un plateau plus large.
			const double R = p.kind == "chateau" ? 40.0 * p.scale + 8.0 : 40.0;
			double sum = 0.0, cnt = 0.0;
			ForDisc(w.height, p.center, R, [&](int i, int j, double) { sum += w.height.At(i, j); cnt += 1.0; });
			flatten(p.center, R, 60.0, std::max(cnt > 0 ? sum / cnt : sea + 8.0, sea + 5.0), false);
		}
	}

	void CarveWater(World& w)
	{
		const double sea = w.master.scale.seaLevelM;
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
				// EAU.5 : en amont de l'embouchure, lit jamais sous la mer (la nappe de
				// la mer d'une cellule côtière apparaîtrait sous la rivière).
				const double bed = river.waterY[k] > sea + 0.6 ? std::max(river.waterY[k] - 2.4, sea + 0.1)
				                                               : river.waterY[k] - 2.4;
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
			CarveLake(w.height, w.riverDist, w.urban, lake, sea + 0.1); // EAU.5
	}
}
