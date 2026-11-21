// tools/world_gen — végétation et minéraux dispersés.
//
// Échantillonnage par GRILLE JITTERISÉE GLOBALE (une case = une tentative)
// pour chaque classe (arbres, buissons, petites plantes, rochers) : chaque
// tentative tire sa position et son acceptation d'un hash de (graine, classe,
// case globale) — déterministe, indépendant de l'ordre, et sans motif répété
// d'une cellule à l'autre (une case n'est écrite que par la cellule qui
// contient son point). La densité acceptée dépend du biome
// du lore sous le point, modulée par un bruit basse fréquence (bosquets,
// clairières) ; les exclusions (eau, routes, bâti, pente) s'appliquent ensuite.

#include "WorldGenContext.h"
#include "Parallel.h"
#include "Random.h"

#include "src/shared/world/instances/ForestMask.h"

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
			// Densités calibrées pour une cellule de 100 km² (≤ ~600 000 instances
			// en pleine forêt, ~20 Mo de scatter.bin) ; le client ne charge que les
			// chunks de 500 m proches du joueur.
			case 'T': case 'R': case 'L': return { 0.32, 0.20, 0.08, 0.04 };
			case 't':                     return { 0.14, 0.13, 0.07, 0.03 };
			case '.': case 'r': case 'p': return { 0.017, 0.03, 0.04, 0.02 };
			case '_': case ',':           return { 0.0, 0.007, 0.014, 0.025 };
			case '~':                     return { 0.006, 0.004, 0.0, 0.06 };
			case ':': case 'l':           return { 0.012, 0.012, 0.006, 0.06 };
			case '^':                     return { 0.05, 0.015, 0.005, 0.17 };
			case 'M':                     return { 0.025, 0.007, 0.002, 0.2 };
			case '*':                     return { 0.0, 0.0, 0.0, 0.12 };
			case 'V':                     return { 0.015, 0.003, 0.0, 0.21 };
			case '%': case '&':           return { 0.085, 0.07, 0.12, 0.015 };
			case 'o':                     return { 0.09, 0.06, 0.03, 0.03 };
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
				{ "DeadTree_3", 0.08, 0.8, 1.1, 0.45 },
				// Atelier décors (BD T1-T3) : chênes et hêtres de futaie, mesh à taille réelle.
				{ "Tree_Oak_1", 0.6, 0.9, 1.1, 0.7 }, { "Tree_Oak_2", 0.6, 0.9, 1.1, 0.7 },
				{ "Tree_Oak_3", 0.6, 0.9, 1.1, 0.7 }, { "Tree_Beech_1", 0.6, 0.9, 1.1, 0.5 },
				{ "Tree_Beech_2", 0.6, 0.9, 1.1, 0.5 }, { "Tree_Beech_3", 0.6, 0.9, 1.1, 0.5 } };
			static const std::vector<Species> conifer = {
				{ "Pine_1", 1, 0.85, 1.3, 0.45 }, { "Pine_2", 1, 0.85, 1.3, 0.45 }, { "Pine_3", 1, 0.85, 1.3, 0.45 },
				{ "Pine_4", 1, 0.8, 1.2, 0.45 }, { "Pine_5", 1, 0.85, 1.3, 0.45 }, { "DeadTree_1", 0.15, 0.8, 1.1, 0.45 } };
			static const std::vector<Species> swamp = {
				{ "TwistedTree_1", 1, 0.45, 0.7, 0.7 }, { "TwistedTree_3", 1, 0.5, 0.75, 0.7 },
				{ "TwistedTree_4", 1, 0.5, 0.75, 0.7 }, { "DeadTree_2", 1.2, 0.8, 1.2, 0.45 },
				{ "DeadTree_4", 1.0, 0.8, 1.2, 0.45 }, { "Tree_Willow_1", 0.8, 0.9, 1.1, 0.5 },
				{ "Tree_Willow_2", 0.8, 0.9, 1.1, 0.5 } };
			static const std::vector<Species> dead = {
				{ "DeadTree_1", 1, 0.8, 1.2, 0.45 }, { "DeadTree_2", 1, 0.8, 1.2, 0.45 },
				{ "DeadTree_3", 1, 0.8, 1.2, 0.45 }, { "DeadTree_4", 1, 0.8, 1.2, 0.45 }, { "DeadTree_5", 1, 0.8, 1.2, 0.45 } };
			static const std::vector<Species> grove = {
				{ "CommonTree_1", 1, 0.9, 1.3, 0.55 }, { "CommonTree_2", 1, 0.9, 1.3, 0.55 },
				{ "CommonTree_4", 1, 0.9, 1.3, 0.55 }, { "CommonTree_5", 1, 0.9, 1.3, 0.55 }, { "Pine_2", 0.4, 0.9, 1.2, 0.45 },
				{ "Tree_Oak_Lone_1", 0.7, 0.9, 1.1, 0.8 }, { "Tree_Oak_Lone_2", 0.7, 0.9, 1.1, 0.8 },
				{ "Tree_Poplar_1", 0.5, 0.9, 1.1, 0.45 }, { "Tree_Poplar_2", 0.5, 0.9, 1.1, 0.45 },
				{ "Tree_Cypress_1", 0.35, 0.9, 1.1, 0.35 }, { "Tree_Cypress_2", 0.35, 0.9, 1.1, 0.35 } };
			if (c == '%' || c == '&') return swamp;
			if (c == '~' || c == ':' || c == 'V' || c == 'l') return dead;
			if (c == '^' || c == 'M' || altitude > 900.0) return conifer; // v1.0.130 : conifères au-dessus de 900 m
			if (c == '.' || c == 'r' || c == 'p' || c == 'o') return grove;
			return broadleaf;
		}

		const std::vector<Species>& BushesFor(char c)
		{
			static const std::vector<Species> forest = {
				{ "Bush_Common", 2, 0.8, 1.4, 0 }, { "Bush_Common_Flowers", 1, 0.8, 1.3, 0 },
				{ "Fern_1", 2.5, 0.85, 1.2, 0 }, { "Plant_1", 1, 0.8, 1.3, 0 }, { "Plant_1_Big", 0.6, 0.55, 0.85, 0 },
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
				{ "Plant_Mullein_1", 0.25, 0.85, 1.15, 0 },
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
				{ "Pebble_Round_1", 0.6, 2.0, 4.0, 0 }, { "Pebble_Square_2", 0.6, 2.0, 4.0, 0 },
				{ "Standing_Stone_1", 0.04, 0.9, 1.1, 0.5 }, { "Standing_Stone_2", 0.04, 0.9, 1.1, 0.5 } };
			static const std::vector<Species> spires = {
				{ "Rock_Spire_1", 1, 0.8, 1.2, 4.0 }, { "Rock_Spire_2", 1, 0.8, 1.2, 5.0 }, { "Rock_Spire_3", 0.6, 0.8, 1.1, 6.0 },
				{ "Rock_Medium_1", 2, 1.4, 3.2, 1.0 }, { "Rock_Medium_2", 2, 1.4, 3.2, 1.0 } };
			static const std::vector<Species> big = {
				{ "Rock_Medium_1", 1, 1.4, 3.2, 1.0 }, { "Rock_Medium_2", 1, 1.4, 3.2, 1.0 }, { "Rock_Medium_3", 1, 1.4, 3.2, 1.0 } };
			if (c == '*') return spires; // falaises et à-pics (Faille de Kashogs, T1 pl.18-19)
			if (c == '^' || c == 'M' || c == 'V') return big;
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

	bool IsFillerForestCode(char code)
	{
		return code == 'T' || code == 'R' || code == 'L' || code == 't';
	}

	void BuildForestMask(World& w)
	{
		const int N = static_cast<int>(engine::world::instances::kForestMaskResolution);
		const double px = w.terrainSizeM / N;
		const double sea = w.master.scale.seaLevelM;
		const uint64_t seed = w.master.seed;
		w.forestMaskRes = N;
		w.forestMask.assign(static_cast<size_t>(N) * N, 0u);
		// v1.0.83 : aucune forêt de remplissage aux Enfers (carte vide, explicite).
		if (IsInfernalMap(w.master)) return;
		ParallelRows(N, [&](int j) {
			for (int i = 0; i < N; ++i)
			{
				const Vec2 p{ w.terrainOriginX + (i + 0.5) * px, w.terrainOriginZ + (j + 0.5) * px };
				const int bi = w.biome.IndexX(p.x), bj = w.biome.IndexZ(p.z);
				if (!w.biome.InBounds(bi, bj)) continue;
				const char code = EffectiveCover(w.biome.At(bi, bj), p.x, p.z, seed);
				// Pleine forêt 1 ; lisière ≈ 0,38 (un arbre tous les ~8 m, la vue passe).
				double cover = 0.0;
				if (code == 'T' || code == 'R' || code == 'L') cover = 1.0;
				else if (code == 't') cover = 0.38;
				if (cover <= 0.0) continue;
				// Exclusions (mêmes règles que les arbres stockés, élargies d'un
				// demi-texel : le client échantillonne la carte en bilinéaire).
				const double alt = w.HeightAt(p);
				if (alt < sea + 0.6 || alt > 2100.0 || w.IsWater(p)) continue; // v1.0.130 : limite des forêts ≈ 2 100 m
				const int ri = w.roadDist.IndexX(p.x), rj = w.roadDist.IndexZ(p.z);
				if (w.roadDist.InBounds(ri, rj) && w.roadDist.At(ri, rj) < 5.0 + px * 0.5) continue;
				const int ui = w.urban.IndexX(p.x), uj = w.urban.IndexZ(p.z);
				if (w.urban.InBounds(ui, uj) && w.urban.At(ui, uj) != 0)
				{
					// Les villages elfes vivent SOUS les arbres (emprise 1 seulement).
					bool elfGrove = false;
					for (const CityDef& c : w.cities)
						if (c.race == "elfes" && std::hypot(p.x - c.center.x, p.z - c.center.z) < c.radiusM + 2.0)
							elfGrove = w.urban.At(ui, uj) == 1;
					if (!elfGrove) continue;
				}
				if (w.IsOccupied(p, 1.8 + px * 0.5) || w.SlopeDegAt(p) > 34.0) continue;
				// Massifs et landes (≈ 1,5 km) puis fourrés et trouées (≈ 110 m).
				const double big = 0.75 + 0.25 * Clamp(Fbm(p.x, p.z, 1500.0, 2, seed + 1010) + 0.5, 0.0, 1.0);
				const double local = Clamp(1.0 + 1.1 * Fbm(p.x, p.z, 110.0, 3, seed + 1001), 0.35, 1.0);
				const double d = Clamp(cover * big * local, 0.0, 1.0);
				w.forestMask[static_cast<size_t>(j) * N + i] = static_cast<uint8_t>(std::lround(d * 255.0));
			}
		});
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
		// v1.0.83 : aux Enfers, ni broussailles, ni fleurs, ni herbes hautes ; seuls
		// des arbres morts (calcinés) et des rochers. Les Royaumes sont inchangés.
		const bool infernal = IsInfernalMap(w.master);
		for (const ClassParams& cp : classes)
		{
			if (infernal && (cp.cls == VegClass::Bush || cp.cls == VegClass::Small)) continue;
			// Treillis GLOBAL : la case (gi, gj) couvre [gi·cellM, (gi+1)·cellM[.
			const int gi0 = static_cast<int>(std::floor(w.terrainOriginX / cp.cellM)) - 1;
			const int gj0 = static_cast<int>(std::floor(w.terrainOriginZ / cp.cellM)) - 1;
			const int n = static_cast<int>(w.terrainSizeM / cp.cellM) + 3;
			// Densité max théorique d'une case = 100 / cellM² instances/100 m².
			const double cellArea100 = cp.cellM * cp.cellM / 100.0;
			for (int jj = 0; jj < n; ++jj)
				for (int ii = 0; ii < n; ++ii)
				{
					const int i = gi0 + ii, j = gj0 + jj;
					const uint64_t h = Hash(seed, cp.salt, static_cast<uint64_t>(static_cast<int64_t>(i)), static_cast<uint64_t>(static_cast<int64_t>(j)));
					const Vec2 p{ (i + ToUnit(h)) * cp.cellM, (j + ToUnit(Mix64(h))) * cp.cellM };
					if (!w.OwnsPoint(p)) continue;
					const int bi = w.biome.IndexX(p.x), bj = w.biome.IndexZ(p.z);
					const char code = EffectiveCover(w.biome.At(bi, bj), p.x, p.z, w.master.seed);
					// FORET.1 : les arbres des forêts et lisières sont posés par le client
					// (carte de densité terrain_forest.frms), pas stockés ici.
					if (cp.cls == VegClass::Tree && IsFillerForestCode(code)) continue;
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
					const double u = ToUnit(Mix64(h + 7));
					if (u >= dens * cellArea100 * 4.5) continue; // rejet précoce (modulation max)
					// Bosquets et clairières (bruit ~100 m) + massifs et landes (≈ 1,5 km).
					const double cluster = Fbm(p.x, p.z, cp.cls == VegClass::Tree ? 110.0 : 70.0, 3, seed + cp.salt);
					double mod = 1.0 + 1.1 * cluster;
					if (code == '.' || code == 'r' || code == 'p') mod = std::max(0.0, cluster - 0.05) * 3.2; // copses isolés
					mod *= 0.55 + 1.5 * Clamp(Fbm(p.x, p.z, 1500.0, 2, seed + cp.salt + 9) + 0.5, 0.0, 1.0);
					const double accept = dens * cellArea100 * std::max(0.0, mod);
					if (u >= accept) continue;

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
							for (const CityDef& c : w.cities)
								if (c.race == "elfes" && std::hypot(p.x - c.center.x, p.z - c.center.z) < c.radiusM + 2.0)
									elfGrove = w.urban.At(ui, uj) == 1;
						if (!elfGrove) continue;
					}
					if (w.IsOccupied(p, cp.clearOccupy)) continue;
					if (w.SlopeDegAt(p) > cp.maxSlope) continue;
					// Pas d'arbre au-dessus de la limite des forêts (v1.0.130 : ≈ 2 100 m, Pyrénées).
					if (cp.cls == VegClass::Tree && alt > (infernal ? 560.0 : 2100.0)) continue; // Enfers : inchangé

					const std::vector<Species>* pool = nullptr;
					switch (cp.cls)
					{
					// Enfers : le jeu d'arbres morts du biome volcanique, même dans les coins `M`.
					case VegClass::Tree: pool = &TreesFor(infernal ? 'V' : code, alt); break;
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

	void BuildShoreVegetation(World& w)
	{
		// Atelier décors (BD T1 pl.13-14) : roseaux et massettes sur les rives des lacs et
		// des rivières, nénuphars à la surface des lacs calme (posés à waterY : Y = sol
		// + yOffset). Royaumes seulement. Déterministe (Hash de la graine du monde).
		if (IsInfernalMap(w.master)) return;
		const uint64_t seed = w.master.seed;
		auto blocked = [&](Vec2 p, double occupy) {
			if (!w.OwnsPoint(p)) return true;
			const int ri = w.roadDist.IndexX(p.x), rj = w.roadDist.IndexZ(p.z);
			if (w.roadDist.InBounds(ri, rj) && w.roadDist.At(ri, rj) < 2.5) return true;
			const int ui = w.urban.IndexX(p.x), uj = w.urban.IndexZ(p.z);
			if (w.urban.InBounds(ui, uj) && w.urban.At(ui, uj) != 0) return true;
			return w.IsOccupied(p, occupy);
		};
		auto reed = [&](Vec2 p, uint64_t h) {
			ScatterDef s;
			s.mesh = (h & 1u) ? "meshes/props/Reed_1.gltf" : "meshes/props/Reed_2.gltf";
			s.position = p;
			s.scale = Lerp(0.85, 1.2, ToUnit(Mix64(h + 3)));
			s.yawDeg = ToUnit(Mix64(h + 5)) * 360.0;
			s.cullClass = 2;
			w.scatter.push_back(std::move(s));
		};
		// Lacs : roseaux en couronne sur la berge, nénuphars dans la bande intérieure.
		for (size_t li = 0; li < w.lakes.size(); ++li)
		{
			const Lake& l = w.lakes[li];
			const double perim = 2.0 * 3.14159265358979 * std::max(5.0, l.radiusM);
			const int steps = static_cast<int>(perim / 2.5);
			for (int k = 0; k < steps; ++k)
			{
				const uint64_t h = Hash(seed, 5101, li, static_cast<uint64_t>(k));
				if (ToUnit(h) > 0.55) continue;
				const double a = (k + ToUnit(Mix64(h))) * 2.0 * 3.14159265358979 / steps;
				const double r = LakeRadiusAt(l, a) + 0.5 + 2.0 * ToUnit(Mix64(h + 1));
				const Vec2 p{ l.center.x + std::cos(a) * r, l.center.z + std::sin(a) * r };
				if (blocked(p, 0.4) || w.SlopeDegAt(p) > 30.0) continue;
				reed(p, h);
			}
			const int pads = std::min(600, static_cast<int>(perim / 5.0));
			for (int k = 0; k < pads; ++k)
			{
				const uint64_t h = Hash(seed, 5202, li, static_cast<uint64_t>(k));
				const double a = ToUnit(h) * 2.0 * 3.14159265358979;
				const double rr = LakeRadiusAt(l, a) * (0.55 + 0.35 * ToUnit(Mix64(h + 1)));
				const Vec2 p{ l.center.x + std::cos(a) * rr, l.center.z + std::sin(a) * rr };
				if (!w.OwnsPoint(p)) continue;
				const double ground = w.HeightAt(p);
				if (ground > l.waterY - 0.3) continue; // pas de nénuphar sur une berge émergée
				ScatterDef s;
				s.mesh = (h & 2u) ? "meshes/props/WaterLily_1.gltf" : "meshes/props/WaterLily_2.gltf";
				s.position = p;
				s.yOffset = l.waterY - ground + 0.01;
				s.scale = Lerp(0.8, 1.3, ToUnit(Mix64(h + 3)));
				s.yawDeg = ToUnit(Mix64(h + 5)) * 360.0;
				s.cullClass = 2;
				w.scatter.push_back(std::move(s));
			}
		}
		// Rivières : roseaux sur la bande de berge (3,5 à 6 m de l'axe du lit).
		if (w.riverDist.data.empty()) return;
		const double cellM = 5.0;
		const int gi0 = static_cast<int>(std::floor(w.terrainOriginX / cellM)) - 1;
		const int gj0 = static_cast<int>(std::floor(w.terrainOriginZ / cellM)) - 1;
		const int n = static_cast<int>(w.terrainSizeM / cellM) + 3;
		for (int jj = 0; jj < n; ++jj)
			for (int ii = 0; ii < n; ++ii)
			{
				const int i = gi0 + ii, j = gj0 + jj;
				const uint64_t h = Hash(seed, 5303, static_cast<uint64_t>(static_cast<int64_t>(i)), static_cast<uint64_t>(static_cast<int64_t>(j)));
				if (ToUnit(h) > 0.45) continue;
				const Vec2 p{ (i + ToUnit(Mix64(h))) * cellM, (j + ToUnit(Mix64(h + 1))) * cellM };
				const int ri = w.riverDist.IndexX(p.x), rj = w.riverDist.IndexZ(p.z);
				if (!w.riverDist.InBounds(ri, rj)) continue;
				const float d = w.riverDist.At(ri, rj);
				if (d < 3.5f || d > 6.0f) continue;
				if (w.HeightAt(p) < w.master.scale.seaLevelM + 0.6) continue; // pas en mer
				if (blocked(p, 0.4) || w.SlopeDegAt(p) > 30.0) continue;
				reed(p, h);
			}
	}
}
