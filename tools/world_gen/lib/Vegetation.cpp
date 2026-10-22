// tools/world_gen — végétation et minéraux dispersés.
//
// Échantillonnage par GRILLE JITTERISÉE (une case = une tentative) pour chaque
// classe (arbres, buissons, petites plantes, rochers) : chaque tentative tire
// sa position et son acceptation d'un hash de (graine, classe, case) — donc
// déterministe et indépendant de l'ordre. La densité acceptée dépend du biome
// du lore sous le point, modulée par un bruit basse fréquence (bosquets,
// clairières) ; les exclusions (eau, routes, bâti, pente) s'appliquent ensuite.

#include "WorldGenContext.h"
#include "Random.h"

#include <algorithm>
#include <cmath>

namespace tools::world_gen
{
	namespace
	{
		constexpr uint8_t kSolid = 1u;

		enum class VegClass { Tree, Bush, Small, Rock };

		struct Species
		{
			const char* mesh;   ///< nom sous meshes/props/ (sans extension)
			double weight;
			double scaleMin, scaleMax;
			double trunkRadius; ///< rayon de collision à l'échelle 1 (0 = traversable)
		};

		/// Densités (instances / 100 m²) par classe pour un code de biome.
		struct BiomeDensity { double tree, bush, small, rock; };

		BiomeDensity DensityOf(char c)
		{
			switch (c)
			{
			case 'T': case 'R': case 'L': return { 1.05, 1.7, 0.9, 0.08 };
			case 't':                     return { 0.45, 1.1, 0.8, 0.06 };
			case '.': case 'r':           return { 0.055, 0.25, 0.45, 0.04 };
			case '_': case ',':           return { 0.0, 0.06, 0.15, 0.05 };
			case '~':                     return { 0.02, 0.03, 0.0, 0.12 };
			case ':': case 'l':           return { 0.04, 0.10, 0.06, 0.12 };
			case '^':                     return { 0.17, 0.12, 0.05, 0.35 };
			case 'M':                     return { 0.08, 0.06, 0.02, 0.40 };
			case '*':                     return { 0.0, 0.0, 0.0, 0.25 };
			case 'V':                     return { 0.05, 0.02, 0.0, 0.42 };
			case '%': case '&':           return { 0.28, 0.6, 1.3, 0.03 };
			case 'o':                     return { 0.3, 0.5, 0.3, 0.06 };
			default:                      return { 0.0, 0.0, 0.0, 0.0 };
			}
		}

		const std::vector<Species>& TreesFor(char c, double altitude)
		{
			static const std::vector<Species> broadleaf = {
				{ "CommonTree_1", 1, 0.85, 1.25, 0.55 }, { "CommonTree_2", 1, 0.85, 1.25, 0.55 },
				{ "CommonTree_3", 1, 0.85, 1.2, 0.55 }, { "CommonTree_4", 1, 0.85, 1.25, 0.55 },
				{ "CommonTree_5", 1, 0.85, 1.25, 0.55 }, { "Pine_1", 0.5, 0.85, 1.2, 0.45 },
				{ "Pine_3", 0.5, 0.85, 1.2, 0.45 }, { "TwistedTree_2", 0.12, 0.55, 0.75, 0.7 },
				{ "DeadTree_3", 0.08, 0.8, 1.1, 0.45 } };
			static const std::vector<Species> conifer = {
				{ "Pine_1", 1, 0.85, 1.3, 0.45 }, { "Pine_2", 1, 0.85, 1.3, 0.45 }, { "Pine_3", 1, 0.85, 1.3, 0.45 },
				{ "Pine_4", 1, 0.8, 1.2, 0.45 }, { "Pine_5", 1, 0.85, 1.3, 0.45 }, { "DeadTree_1", 0.15, 0.8, 1.1, 0.45 } };
			static const std::vector<Species> swamp = {
				{ "TwistedTree_1", 1, 0.45, 0.7, 0.7 }, { "TwistedTree_3", 1, 0.5, 0.75, 0.7 },
				{ "TwistedTree_4", 1, 0.5, 0.75, 0.7 }, { "DeadTree_2", 1.2, 0.8, 1.2, 0.45 },
				{ "DeadTree_4", 1.0, 0.8, 1.2, 0.45 } };
			static const std::vector<Species> dead = {
				{ "DeadTree_1", 1, 0.8, 1.2, 0.45 }, { "DeadTree_2", 1, 0.8, 1.2, 0.45 },
				{ "DeadTree_3", 1, 0.8, 1.2, 0.45 }, { "DeadTree_4", 1, 0.8, 1.2, 0.45 }, { "DeadTree_5", 1, 0.8, 1.2, 0.45 } };
			static const std::vector<Species> grove = {
				{ "CommonTree_1", 1, 0.9, 1.3, 0.55 }, { "CommonTree_2", 1, 0.9, 1.3, 0.55 },
				{ "CommonTree_4", 1, 0.9, 1.3, 0.55 }, { "CommonTree_5", 1, 0.9, 1.3, 0.55 }, { "Pine_2", 0.4, 0.9, 1.2, 0.45 } };
			if (c == '%' || c == '&') return swamp;
			if (c == '~' || c == ':' || c == 'V' || c == 'l') return dead;
			if (c == '^' || c == 'M' || altitude > 95.0) return conifer;
			if (c == '.' || c == 'r' || c == 'o') return grove;
			return broadleaf;
		}

		const std::vector<Species>& BushesFor(char c)
		{
			static const std::vector<Species> forest = {
				{ "Bush_Common", 2, 0.8, 1.4, 0 }, { "Bush_Common_Flowers", 1, 0.8, 1.3, 0 },
				{ "Fern_1", 2.5, 0.32, 0.55, 0 }, { "Plant_1", 1, 0.8, 1.3, 0 }, { "Plant_1_Big", 0.6, 0.55, 0.85, 0 },
				{ "Plant_7_Big", 0.6, 0.7, 1.1, 0 } };
			static const std::vector<Species> open = {
				{ "Bush_Common", 2, 0.7, 1.2, 0 }, { "Bush_Common_Flowers", 1.5, 0.7, 1.2, 0 }, { "Plant_1", 0.5, 0.7, 1.1, 0 } };
			static const std::vector<Species> dry = { { "Bush_Common", 1, 0.5, 0.8, 0 }, { "Plant_7", 1, 0.8, 1.3, 0 } };
			if (c == '~' || c == ':' || c == 'V' || c == 'l' || c == '^' || c == 'M') return dry;
			if (c == 'T' || c == 'R' || c == 'L' || c == 't' || c == '%' || c == '&') return forest;
			return open;
		}

		const std::vector<Species>& SmallFor(char c)
		{
			static const std::vector<Species> forest = {
				{ "Mushroom_Common", 1.5, 0.8, 1.5, 0 }, { "Mushroom_Laetiporus", 0.7, 0.8, 1.3, 0 },
				{ "Clover_1", 1, 0.8, 1.4, 0 }, { "Clover_2", 1, 0.8, 1.4, 0 }, { "Flower_4_Group", 0.6, 0.8, 1.2, 0 },
				{ "Grass_Wispy_Short", 1, 0.8, 1.3, 0 } };
			static const std::vector<Species> meadow = {
				{ "Flower_3_Group", 1, 0.8, 1.3, 0 }, { "Flower_4_Group", 1, 0.8, 1.3, 0 },
				{ "Grass_Common_Tall", 2, 0.8, 1.3, 0 }, { "Grass_Common_Short", 2, 0.8, 1.4, 0 },
				{ "Grass_Wispy_Tall", 1, 0.8, 1.3, 0 }, { "Clover_1", 0.7, 0.8, 1.3, 0 }, { "Flower_3_Single", 0.5, 0.9, 1.4, 0 } };
			static const std::vector<Species> marsh = {
				{ "Grass_Wispy_Tall", 3, 0.9, 1.5, 0 }, { "Grass_Common_Tall", 1, 0.9, 1.4, 0 }, { "Mushroom_Common", 0.6, 0.8, 1.3, 0 } };
			if (c == '%' || c == '&') return marsh;
			if (c == 'T' || c == 'R' || c == 'L' || c == 't') return forest;
			return meadow;
		}

		const std::vector<Species>& RocksFor(char c)
		{
			static const std::vector<Species> rocks = {
				{ "Rock_Medium_1", 1, 0.7, 1.8, 0.9 }, { "Rock_Medium_2", 1, 0.7, 1.8, 0.9 }, { "Rock_Medium_3", 1, 0.7, 1.8, 0.9 },
				{ "Pebble_Round_1", 0.6, 2.0, 4.0, 0 }, { "Pebble_Square_2", 0.6, 2.0, 4.0, 0 } };
			static const std::vector<Species> big = {
				{ "Rock_Medium_1", 1, 1.4, 3.2, 1.0 }, { "Rock_Medium_2", 1, 1.4, 3.2, 1.0 }, { "Rock_Medium_3", 1, 1.4, 3.2, 1.0 } };
			if (c == '^' || c == 'M' || c == '*' || c == 'V') return big;
			return rocks;
		}

		const Species& Pick(const std::vector<Species>& v, double u)
		{
			double total = 0.0;
			for (const Species& s : v) total += s.weight;
			double acc = 0.0;
			for (const Species& s : v) { acc += s.weight / total; if (u <= acc) return s; }
			return v.back();
		}

		struct ClassParams
		{
			VegClass cls;
			double cellM;        ///< pas de la grille de tentatives
			double clearRoad;    ///< distance minimale au bord de chaussée
			double clearOccupy;  ///< rayon testé dans la grille d'occupation
			double maxSlope;     ///< pente max (°)
			uint8_t cull;
			uint64_t salt;
		};
	}

	void BuildVegetation(World& w)
	{
		const double sea = w.master.scale.seaLevelM;
		const ClassParams classes[] = {
			{ VegClass::Tree,  9.5, 5.0, 2.6, 34.0, 0, 1001 },
			{ VegClass::Bush,  7.4, 2.5, 1.0, 40.0, 1, 2002 },
			{ VegClass::Small, 6.5, 1.0, 0.4, 42.0, 2, 3003 },
			{ VegClass::Rock,  14.0, 3.0, 1.6, 60.0, 1, 4004 },
		};
		const uint64_t seed = w.master.seed;
		const size_t before = w.scatter.size();
		for (const ClassParams& cp : classes)
		{
			const int n = static_cast<int>(w.terrainSizeM / cp.cellM);
			// Densité max théorique d'une case = 100 / cellM² instances/100 m².
			const double cellArea100 = cp.cellM * cp.cellM / 100.0;
			for (int j = 0; j < n; ++j)
				for (int i = 0; i < n; ++i)
				{
					const uint64_t h = Hash(seed, cp.salt, static_cast<uint64_t>(i), static_cast<uint64_t>(j));
					const Vec2 p{ w.terrainOriginX + (i + ToUnit(h)) * cp.cellM,
					              w.terrainOriginZ + (j + ToUnit(Mix64(h))) * cp.cellM };
					if (!w.InPlayableBounds(p, 24.0)) continue;
					const int bi = w.biome.IndexX(p.x), bj = w.biome.IndexZ(p.z);
					const char code = w.biome.At(bi, bj);
					const BiomeDensity d = DensityOf(code);
					double dens = 0.0;
					switch (cp.cls)
					{
					case VegClass::Tree: dens = d.tree; break;
					case VegClass::Bush: dens = d.bush; break;
					case VegClass::Small: dens = d.small; break;
					case VegClass::Rock: dens = d.rock; break;
					}
					if (dens <= 0.0) continue;
					// Bosquets et clairières : bruit basse fréquence.
					const double cluster = Fbm(p.x, p.z, cp.cls == VegClass::Tree ? 110.0 : 70.0, 3, seed + cp.salt);
					double mod = 1.0 + 1.1 * cluster;
					if (code == '.' || code == 'r') mod = std::max(0.0, cluster - 0.05) * 3.2; // copses isolés
					const double accept = dens * cellArea100 * std::max(0.0, mod);
					if (ToUnit(Mix64(h + 7)) >= accept) continue;

					// Exclusions.
					const double alt = w.HeightAt(p);
					if (alt < sea + 0.6 || w.IsWater(p)) continue;
					const int ri = w.roadDist.IndexX(p.x), rj = w.roadDist.IndexZ(p.z);
					if (w.roadDist.InBounds(ri, rj) && w.roadDist.At(ri, rj) < cp.clearRoad) continue;
					const int ui = w.urban.IndexX(p.x), uj = w.urban.IndexZ(p.z);
					if (w.urban.InBounds(ui, uj) && w.urban.At(ui, uj) != 0)
					{
						// Les villages elfes vivent SOUS les arbres ; ailleurs, l'urbain est dégagé.
						bool elfGrove = false;
						if (cp.cls == VegClass::Tree || cp.cls == VegClass::Small)
							for (const CityDef& c : w.master.cities)
								if (c.race == "elfes" && std::hypot(p.x - c.center.x, p.z - c.center.z) < c.radiusM + 2.0)
									elfGrove = w.urban.At(ui, uj) == 1;
						if (!elfGrove) continue;
					}
					if (w.IsOccupied(p, cp.clearOccupy)) continue;
					if (w.SlopeDegAt(p) > cp.maxSlope) continue;
					// Pas d'arbre sur les sommets enneigés au-dessus de 230 m.
					if (cp.cls == VegClass::Tree && alt > 230.0) continue;

					const std::vector<Species>* pool = nullptr;
					switch (cp.cls)
					{
					case VegClass::Tree: pool = &TreesFor(code, alt); break;
					case VegClass::Bush: pool = &BushesFor(code); break;
					case VegClass::Small: pool = &SmallFor(code); break;
					case VegClass::Rock: pool = &RocksFor(code); break;
					}
					const Species& sp = Pick(*pool, ToUnit(Mix64(h + 11)));
					ScatterDef s;
					s.mesh = std::string("meshes/props/") + sp.mesh + ".gltf";
					s.position = p;
					s.scale = Lerp(sp.scaleMin, sp.scaleMax, ToUnit(Mix64(h + 13)));
					s.yawDeg = ToUnit(Mix64(h + 17)) * 360.0;
					s.cullClass = cp.cull;
					if (cp.cls == VegClass::Tree) { s.tiltDeg = ToUnit(Mix64(h + 19)) * 3.5; s.yOffset = -0.15; s.flags = static_cast<uint8_t>(kSolid | 4u); }
					if (cp.cls == VegClass::Rock) { s.tiltDeg = ToUnit(Mix64(h + 19)) * 14.0; s.yOffset = -0.25 * s.scale; }
					if (sp.trunkRadius > 0.0)
					{
						s.flags |= kSolid;
						s.collisionRadius = sp.trunkRadius * (cp.cls == VegClass::Rock ? s.scale : std::sqrt(s.scale));
					}
					if (cp.cls == VegClass::Tree) w.Occupy(p, 1.6);
					else if (cp.cls == VegClass::Rock) w.Occupy(p, 1.0 * s.scale);
					w.scatter.push_back(std::move(s));
				}
		}
		(void)before;
	}
}
