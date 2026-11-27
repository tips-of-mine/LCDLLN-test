// tools/world_gen — lieux bâtis (lot carte v1.0.132) : formes GÉNÉRÉES des POI de genre
// tour, chateau, phare, cascade et pont, à partir des pièces modulaires de meshes/props/
// (murs, arches, sols, toits de tour), posées en décor (scatter.bin).
//
// Les murs (Wall_*) ont une boîte de collision « mur » et les sols (Floor_*) un dessus
// marchable (game/data/collision/building_pieces.json) : on entre dans une tour par sa
// porte, on marche sur un pont. Toutes les formes sont déterministes (graine + id du POI).
// Un POI à l'état « ruine » (clé `state` du master) perd des pans de mur, ses étages hauts
// et son toit. Appelé par la génération de cellule après les agglomérations (WorldGen.cpp).

#include "WorldGenContext.h"
#include "Random.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace tools::world_gen
{
	namespace
	{
		constexpr double kPi = 3.141592653589793;
		constexpr uint8_t kSolid = 1u, kPiece = 2u, kOccluder = 4u; ///< bits de ScatterInstance::flags
		constexpr double kWallH = 3.12; ///< hauteur d'un mur modulaire (m, échelle 1)

		/// Lacet (degrés) qui aligne l'axe X local d'un mesh sur la direction \p d (XZ).
		double YawAlong(Vec2 d) { return std::atan2(-d.z, d.x) * 57.29577951308232; }

		/// Pose une pièce de meshes/props/ à \p y mètres au-dessus du sol (heightmap finale).
		/// Une pièce solide de bâtiment (Wall_*, Floor_*, clôture) passe par le catalogue de
		/// collision (bit « pièce » : murs = barrières, sols = dessus marchable) ; les murs
		/// occultent la caméra ; arbres et rochers gardent la collision cylindrique.
		void Piece(World& w, const char* mesh, Vec2 p, double y, double yaw, double scale, bool solid)
		{
			ScatterDef s;
			s.mesh = std::string("meshes/props/") + mesh + ".gltf";
			s.position = p; s.yOffset = y; s.yawDeg = yaw; s.scale = scale;
			const std::string m(mesh);
			// Pièce du catalogue de collision (building_pieces.json) : murs, sols, barrières et,
			// atelier décors T2-T3, modules de pont et pont-levis (dessus marchable).
			const bool piece = m.rfind("Wall_", 0) == 0 || m.rfind("Floor_", 0) == 0 || m.rfind("Prop_WoodenFence", 0) == 0
				|| m.rfind("Bridge_", 0) == 0 || m.rfind("Boardwalk_", 0) == 0 || m.rfind("Drawbridge_", 0) == 0;
			s.flags = solid ? static_cast<uint8_t>(kSolid | (piece ? kPiece : 0u) | (m.rfind("Wall_", 0) == 0 ? kOccluder : 0u)) : 0u;
			s.cullClass = m.rfind("Wall_", 0) == 0 || m.rfind("Roof_", 0) == 0 ? 0 : 1; // bâti : grande distance de vue
			w.scatter.push_back(std::move(s));
		}

		/// Générateur pseudo-aléatoire déterministe propre à un POI.
		struct LandmarkRng
		{
			uint64_t s;
			double Next() { s = Mix64(s + 0x9E3779B97F4A7C15ull); return ToUnit(s); }
		};

		/// Tour (ronde si \p round, sinon carrée de côté 2·r) de \p levels étages de murs de pierre,
		/// porte au rez-de-chaussée vers \p doorDir, fenêtres aux étages, sol de dalles, toit conique
		/// (tour ronde intacte) ou créneaux. \p ruin : pans manquants (plus haut, plus de manques),
		/// pas de toit. \p baseY : altitude du pied au-dessus du sol (m).
		/// Effet de bord : w.scatter. Renvoie la hauteur du sommet (m au-dessus du pied).
		double BuildTower(World& w, Vec2 c, double r, int levels, bool round, Vec2 doorDir, bool ruin, LandmarkRng& rng, double baseY = 0.0, bool roof = true)
		{
			// Segments de 2 m (échelle s pour fermer exactement le pourtour).
			const double perim = round ? 2.0 * kPi * r : 8.0 * r;
			const int n = std::max(round ? 6 : 4, static_cast<int>(std::lround(perim / 2.0)) / (round ? 1 : 4) * (round ? 1 : 4));
			const double s = perim / n / 2.0;
			const double doorA = std::atan2(doorDir.z, doorDir.x);
			int doorK = 0;
			double best = 1e9;
			std::vector<Vec2> pos(static_cast<size_t>(n)), tan(static_cast<size_t>(n));
			for (int k = 0; k < n; ++k)
			{
				if (round)
				{
					const double a = (k + 0.5) * 2.0 * kPi / n;
					pos[k] = c + Vec2{ std::cos(a), std::sin(a) } * r;
					tan[k] = { -std::sin(a), std::cos(a) };
					const double da = std::fabs(std::remainder(a - doorA, 2.0 * kPi));
					if (da < best) { best = da; doorK = k; }
				}
				else
				{
					const int side = k / (n / 4), idx = k % (n / 4);
					static const Vec2 corner[4] = { { -1, -1 }, { 1, -1 }, { 1, 1 }, { -1, 1 } };
					static const Vec2 dir[4] = { { 1, 0 }, { 0, 1 }, { -1, 0 }, { 0, -1 } };
					pos[k] = c + corner[side] * r + dir[side] * ((idx + 0.5) * 2.0 * s);
					tan[k] = dir[side];
					const Vec2 out = pos[k] - c;
					const double da = std::fabs(std::remainder(std::atan2(out.z, out.x) - doorA, 2.0 * kPi));
					if (da < best) { best = da; doorK = k; }
				}
			}
			const double h = kWallH * s;
			int top = levels;
			for (int lv = 0; lv < levels; ++lv)
			{
				const double ruinP = ruin ? Clamp(0.1 + 0.6 * lv / std::max(1, levels - 1), 0.0, 0.85) : 0.0;
				for (int k = 0; k < n; ++k)
				{
					if (ruin && lv > 0 && rng.Next() < ruinP) continue;
					const char* mesh = "Wall_UnevenBrick_Straight";
					if (lv == 0 && k == doorK) mesh = "Wall_UnevenBrick_Door_Round";
					else if (lv > 0 && k % 3 == 1) mesh = "Wall_UnevenBrick_Window_Thin_Round";
					Piece(w, mesh, pos[k], baseY + lv * h, YawAlong(tan[k]), s, true);
				}
				if (ruin && lv >= std::max(1, levels / 2) && rng.Next() < 0.5) { top = lv + 1; break; }
			}
			// Sol de dalles au rez-de-chaussée (dessus marchable).
			for (double x = -r + 1.0; x < r; x += 2.0)
				for (double z = -r + 1.0; z < r; z += 2.0)
					if (!round || x * x + z * z < (r - 0.6) * (r - 0.6))
						Piece(w, "Floor_Brick", c + Vec2{ x, z }, baseY + 0.02, 0.0, 1.0, true);
			const double topY = baseY + top * h;
			if (!ruin && roof && round)
				Piece(w, "Roof_Tower_RoundTiles", c, topY, 0.0, Clamp(r / 2.6, 0.6, 3.0), false);
			else if (!ruin)
				for (int k = 0; k < n; k += 2) // créneaux
					Piece(w, "Wall_UnevenBrick_Straight", pos[k], topY, YawAlong(tan[k]), 0.4 * s, true);
			// Éboulis au pied d'une ruine.
			if (ruin)
				for (int k = 0; k < 6; ++k)
				{
					const double a = rng.Next() * 2.0 * kPi, d = r + 1.0 + rng.Next() * 4.0;
					Piece(w, (k % 2) ? "Prop_Brick2" : "Rock_Medium_3", c + Vec2{ std::cos(a), std::sin(a) } * d, baseY, rng.Next() * 360.0, (k % 2) ? 1.6 : 0.7, false);
				}
			return top * h;
		}

		/// Mur droit de \p a à \p b (segments de 2 m mis à l'échelle), \p levels étages, créneaux
		/// au sommet ; \p gate : arche au milieu. Effet de bord : w.scatter.
		void BuildWall(World& w, Vec2 a, Vec2 b, int levels, bool ruin, bool gate, LandmarkRng& rng)
		{
			const double len = std::hypot(b.x - a.x, b.z - a.z);
			if (len < 1.0) return;
			const int n = std::max(1, static_cast<int>(std::lround(len / 2.0)));
			const double s = len / n / 2.0;
			const Vec2 d = (b - a) * (1.0 / len);
			for (int k = 0; k < n; ++k)
			{
				const Vec2 p = a + d * ((k + 0.5) * 2.0 * s);
				const bool mid = gate && k == n / 2;
				for (int lv = 0; lv < levels; ++lv)
				{
					if (ruin && rng.Next() < 0.25 + 0.3 * lv) continue;
					const char* mesh = (mid && lv == 0) ? "Wall_Arch" : "Wall_UnevenBrick_Straight";
					Piece(w, mesh, p, lv * kWallH * s, YawAlong(d), s, !(mid && lv == 0));
				}
				if (!ruin && k % 2 == 0) Piece(w, "Wall_UnevenBrick_Straight", p, levels * kWallH * s, YawAlong(d), 0.4 * s, true);
			}
		}

		/// Château : enceinte circulaire (rayon 26 m × échelle) à créneaux et porte en arche vers
		/// \p gateDir, 4 tours d'angle, donjon rond central, corps de logis accolé au donjon, arbres
		/// dans la cour ; douves sèches creusées autour (château intact). Ruine : pans manquants,
		/// sans toits ni arbres. Effet de bord : w.scatter, w.height (douves).
		void BuildCastle(World& w, Vec2 c, double sc, Vec2 gateDir, bool ruin, LandmarkRng& rng)
		{
			const double R = 26.0 * sc;
			const int segs = std::max(12, static_cast<int>(std::lround(2.0 * kPi * R / 8.0)));
			const double gateA = std::atan2(gateDir.z, gateDir.x);
			int gateK = 0; double best = 1e9;
			for (int k = 0; k < segs; ++k)
			{
				const double a = (k + 0.5) * 2.0 * kPi / segs;
				const double da = std::fabs(std::remainder(a - gateA, 2.0 * kPi));
				if (da < best) { best = da; gateK = k; }
			}
			for (int k = 0; k < segs; ++k)
			{
				const double a0 = k * 2.0 * kPi / segs, a1 = (k + 1) * 2.0 * kPi / segs;
				BuildWall(w, c + Vec2{ std::cos(a0), std::sin(a0) } * R, c + Vec2{ std::cos(a1), std::sin(a1) } * R, 2, ruin, k == gateK, rng);
			}
			// Tours d'angle.
			for (int t = 0; t < 4; ++t)
			{
				const double a = gateA + kPi / 4.0 + t * kPi / 2.0;
				BuildTower(w, c + Vec2{ std::cos(a), std::sin(a) } * R, 3.5 * sc, 3, true, Vec2{ -std::cos(a), -std::sin(a) }, ruin, rng);
			}
			// Donjon et corps de logis (rectangle accolé au donjon, côté opposé à la porte).
			const Vec2 back{ -std::cos(gateA), -std::sin(gateA) };
			const Vec2 keep = c + back * (6.0 * sc);
			BuildTower(w, keep, 5.0 * sc, ruin ? 4 : 5, true, Vec2{ std::cos(gateA), std::sin(gateA) }, ruin, rng);
			const Vec2 side{ -back.z, back.x };
			const Vec2 lc = keep + side * (10.0 * sc);
			const double hw = 6.0 * sc, hd = 3.5 * sc;
			const Vec2 q0 = lc - side * hw - back * hd, q1 = lc + side * hw - back * hd, q2 = lc + side * hw + back * hd, q3 = lc - side * hw + back * hd;
			BuildWall(w, q0, q1, 2, ruin, true, rng); BuildWall(w, q1, q2, 2, ruin, false, rng);
			BuildWall(w, q2, q3, 2, ruin, false, rng); BuildWall(w, q3, q0, 2, ruin, false, rng);
			if (!ruin)
			{
				Piece(w, "Roof_RoundTiles_8x12", lc, 2.0 * kWallH, YawAlong(back), sc, false);
				for (int k = 0; k < 3; ++k) // arbres de la cour
				{
					const double a = gateA + (k - 1) * 0.7;
					Piece(w, "CommonTree_2", c + Vec2{ std::cos(a), std::sin(a) } * (14.0 * sc), 0.0, rng.Next() * 360.0, 0.9 + 0.3 * rng.Next(), true);
				}
				// Pont-levis abaissé devant la porte (atelier décors T3, pl.42), charnière au pied
				// de l'arche, tablier vers l'extérieur (+Z local).
				Piece(w, "Drawbridge_1", c + gateDir * (R + 0.4), 0.05, std::atan2(gateDir.x, gateDir.z) * 180.0 / kPi, 1.0, true);
				// Douves sèches : anneau creusé de 3 m entre 1,2 R et 1,4 R (pont-levis = porte).
				const double r0 = R * 1.15, r1 = R * 1.4;
				for (int j = 0; j < w.height.height; ++j)
				{
					const double z = w.height.WorldZ(j);
					if (std::fabs(z - c.z) > r1 + 4.0) continue;
					for (int i = 0; i < w.height.width; ++i)
					{
						const double x = w.height.WorldX(i);
						const double d = std::hypot(x - c.x, z - c.z);
						if (d < r0 || d > r1) continue;
						const double ang = std::fabs(std::remainder(std::atan2(z - c.z, x - c.x) - gateA, 2.0 * kPi));
						if (ang * d < 4.0) continue; // passage devant la porte
						const double t = std::sin((d - r0) / (r1 - r0) * kPi);
						w.height.At(i, j) -= 3.0 * t;
					}
				}
			}
		}

		/// Cascade : trois vasques en gradins (lacs à surface plate, 4 m plus bas chacune) le long
		/// de \p dir, chaque terrasse aplanie à sa vasque, rochers aux chutes. Effet de bord :
		/// w.lakes, w.height, w.scatter.
		void BuildCascade(World& w, const PoiDef& p, Vec2 dir, LandmarkRng& rng)
		{
			const double y0 = w.HeightAt(p.center) + 6.0;
			const double radii[3] = { 9.0, 7.5, 6.5 };
			for (int k = 0; k < 3; ++k)
			{
				const Vec2 c = p.center + dir * (k * 17.0 - 17.0);
				const double water = y0 - 4.0 * k;
				const double r = radii[k];
				// Terrasse : le sol suit la vasque (berge 0,4 m au-dessus de l'eau, fond 1,4 m dessous).
				for (int j = std::max(0, w.height.IndexZ(c.z - r - 10)); j <= std::min(w.height.height - 1, w.height.IndexZ(c.z + r + 10)); ++j)
					for (int i = std::max(0, w.height.IndexX(c.x - r - 10)); i <= std::min(w.height.width - 1, w.height.IndexX(c.x + r + 10)); ++i)
					{
						const double d = std::hypot(w.height.WorldX(i) - c.x, w.height.WorldZ(j) - c.z);
						double& h = w.height.At(i, j);
						if (d < r) h = water - 1.4 * (1.0 - d / r) - 0.2;
						else if (d < r + 10.0) h = Lerp(water + 0.4, h, SmoothStep(r, r + 10.0, d));
					}
				Lake l;
				l.name = p.id + "_vasque_" + std::to_string(k);
				l.center = c; l.radiusM = r; l.waterY = water;
				for (int q = 0; q < 16; ++q) // contour CCW (water.bin), bord légèrement irrégulier
				{
					const double a = q * 2.0 * kPi / 16.0, rr = r * (0.92 + 0.08 * rng.Next());
					l.polygon.push_back(c + Vec2{ std::cos(a), std::sin(a) } * rr);
				}
				w.lakes.push_back(l);
				// Rochers à la chute (entre deux vasques) et autour.
				for (int q = 0; q < 5; ++q)
				{
					const Vec2 side{ -dir.z, dir.x };
					const Vec2 rp = c + dir * (r + 1.5) + side * ((q - 2) * 2.4);
					// Gros rochers moussus au pied des chutes (atelier décors T3, pl.10).
					Piece(w, (q % 2) ? "Rock_Mossy_2" : "Rock_Mossy_3", rp, water - 0.5 - 1.5 * rng.Next(), rng.Next() * 360.0, 0.8 + 0.4 * rng.Next(), true);
				}
			}
			w.Occupy(p.center, 40.0);
		}

		/// Pont isolé (lieu « Pont … ») : ravine creusée sous le pont (6 m de creux, 30 m de long)
		/// et pont de pierre à arches, ou de bois (\p wood), le long de \p dir. Ruine : tablier
		/// coupé au milieu. Effet de bord : w.height, w.scatter, occupation.
		void BuildBridgeLandmark(World& w, const PoiDef& p, Vec2 dir, bool wood, bool ruin, LandmarkRng& rng)
		{
			const bool rope = wood && p.name.find("corde") != std::string::npos;
			const Vec2 nrm{ -dir.z, dir.x };
			const double deck = w.HeightAt(p.center);
			// Ravine perpendiculaire au pont.
			for (int j = std::max(0, w.height.IndexZ(p.center.z - 22)); j <= std::min(w.height.height - 1, w.height.IndexZ(p.center.z + 22)); ++j)
				for (int i = std::max(0, w.height.IndexX(p.center.x - 22)); i <= std::min(w.height.width - 1, w.height.IndexX(p.center.x + 22)); ++i)
				{
					const Vec2 q{ w.height.WorldX(i) - p.center.x, w.height.WorldZ(j) - p.center.z };
					const double along = q.x * dir.x + q.z * dir.z, across = q.x * nrm.x + q.z * nrm.z;
					if (std::fabs(across) > 16.0) continue;
					const double t = SmoothStep(7.0, 2.0, std::fabs(along)) * SmoothStep(16.0, 10.0, std::fabs(across));
					w.height.At(i, j) -= 6.0 * t;
				}
			const int along = 8; // 16 m de tablier
			for (int u = 0; u < along; ++u)
			{
				if (ruin && (u == 3 || u == 4)) continue; // tablier effondré au milieu
				const Vec2 c = p.center + dir * ((u + 0.5 - along * 0.5) * 2.0);
				const double ground = w.HeightAt(c);
				// Atelier décors T2 (n° 81-83) : modules de pont — cordes et planches (n° 82),
				// passerelle de bois (n° 83) ou tablier et parapets de pierre (n° 81).
				if (rope)
				{
					Piece(w, "Bridge_Rope_Deck_1", c, deck - ground, YawAlong(dir), 1.0, true);
					continue;
				}
				for (int v = 0; v < 2; ++v)
					Piece(w, wood ? "Boardwalk_Deck_1" : "Bridge_Stone_Deck_1", c + nrm * ((v - 0.5) * 2.0), deck - ground, YawAlong(dir), 1.0, true);
				for (int side = -1; side <= 1; side += 2)
				{
					if (ruin && rng.Next() < 0.4) continue;
					const Vec2 e = c + nrm * (side * 2.2);
					if (wood) Piece(w, "Prop_WoodenFence_Single", e, deck - w.HeightAt(e), YawAlong(dir), 1.0, true);
					else Piece(w, "Bridge_Stone_Parapet_1", e, deck - w.HeightAt(e), YawAlong(dir), 1.0, true);
				}
			}
			if (!wood)
				for (int side = -1; side <= 1; side += 2)
					for (int u = 0; u < 3; ++u)
					{
						if (ruin && u == 1) continue;
						const Vec2 a = p.center + dir * ((u - 1) * 4.6) + nrm * (side * 2.05);
						Piece(w, "Bridge_Stone_Arch_1", a, deck - 6.6 - 0.35 - w.HeightAt(a), YawAlong(dir), 2.2, false);
					}
			w.Occupy(p.center, 18.0);
		}
	}

	// ---------------------------------------------------------------------------------
	// v1.0.140 — PALAIS IMPÉRIAL (POI de genre « palais », Lhynn) sur la plateforme du
	// sommet d'une acropole. Monument signature RÉINTERPRÉTÉ (LORE_BD §16.66) : façade
	// gothique dédiée (Palace_Imperial_Facade_1 : porte centrale traversante, statues
	// colossales à sujet neutre), corps du palais et enceinte en pièces modulaires, quatre
	// flèches (Palace_Imperial_Spire_1), parvis à braseros et cyprès, salle du trône
	// (tapis, trône, braseros), jardin à l'arrière (fontaine, haies, statues, cyprès).
	// v1.0.145 : la salle du trône devient une longue NEF de 72 × 26 m couverte (BD-T1 p.28),
	// porte à vantaux sculptés, statues colossales, trône sur une pyramide à degrés.
	// ---------------------------------------------------------------------------------
	namespace
	{
		/// Pose un monument dédié du palais (meshes/props/<mesh>.gltf) : solide, collision par
		/// le catalogue de pièces (game/data/collision/building_pieces.json : boîtes orientées),
		/// occulteur caméra, grande distance de vue. \p frontDir : direction (unitaire, XZ) vers
		/// laquelle le côté +Z local du mesh est tourné ; \p scale : échelle uniforme (la collision
		/// du catalogue suit la matrice monde). Effet de bord : w.scatter.
		void PalaceMonument(World& w, const char* mesh, Vec2 p, Vec2 frontDir, double scale)
		{
			ScatterDef s;
			s.mesh = std::string("meshes/props/") + mesh + ".gltf";
			s.position = p;
			s.yawDeg = std::atan2(frontDir.x, frontDir.z) * 180.0 / kPi;
			s.scale = scale;
			s.flags = static_cast<uint8_t>(kSolid | kPiece | kOccluder);
			s.cullClass = 0;
			w.scatter.push_back(std::move(s));
		}

		/// Construit le palais impérial centré sur \p c (centre de la plateforme). \p u :
		/// direction unitaire vers l'escalier (porte, façade) ; \p topR : rayon de la
		/// plateforme (m, 70 m pour Lhynn depuis v1.0.145 : la nef de 72 m, la façade, le parvis
		/// et le jardin y tiennent). Repère local : u (avant), v (gauche). Effet de bord :
		/// w.scatter, w.pointLights, occupation de w.
		void BuildImperialPalace(World& w, const PoiDef& p, Vec2 c, Vec2 u, double topR, LandmarkRng& rng)
		{
			const Vec2 v{ -u.z, u.x };
			auto at = [&](double du, double dv) { return c + u * du + v * dv; };
			const double yawU = std::atan2(u.x, u.z) * 180.0 / kPi; // +Z local vers u
			// Enceinte octogonale (2 étages de murs), ouverte de 10 m vers l'escalier entre deux
			// tours ; tours rondes à trois angles arrière.
			const double Re = topR - 3.0;
			const double au = std::atan2(u.z, u.x);
			std::vector<Vec2> corner(8);
			for (int k = 0; k < 8; ++k)
			{
				const double a = au + kPi / 8.0 + k * kPi / 4.0;
				corner[static_cast<size_t>(k)] = c + Vec2{ std::cos(a), std::sin(a) } * Re;
			}
			for (int k = 0; k < 8; ++k)
			{
				const Vec2 a = corner[static_cast<size_t>((k + 7) % 8)], b = corner[static_cast<size_t>(k)];
				if (k == 0)
				{
					// Côté de l'escalier : deux pans et un passage de 10 m au milieu.
					const Vec2 mid = (a + b) * 0.5, d = (b - a) * (1.0 / std::hypot(b.x - a.x, b.z - a.z));
					BuildWall(w, a, mid - d * 5.0, 2, false, false, rng);
					BuildWall(w, mid + d * 5.0, b, 2, false, false, rng);
					for (int sg = -1; sg <= 1; sg += 2)
						BuildTower(w, mid + d * (sg * 7.6), 2.6, 4, true, u, false, rng);
				}
				else
					BuildWall(w, a, b, 2, false, false, rng);
			}
			for (int k = 2; k <= 6; k += 2)
				BuildTower(w, corner[static_cast<size_t>(k)], 2.6, 4, true, Vec2{ 0.0, 0.0 } - u, false, rng);
			// Retour du propriétaire (25/11/2026 : « le palais n'est pas assez volumineux ») :
			// monuments dédiés à l'échelle kScale (façade ≈ 65 × 49 m, flèches ≈ 62 m).
			const double kScale = 1.3;
			auto yawTo = [](Vec2 d) { return std::atan2(d.x, d.z) * 180.0 / kPi; }; // +Z local vers d
			// Pièce du catalogue de collision (boîtes orientées de building_pieces.json), grande
			// distance de vue ; \p occluder : fondu d'occlusion caméra.
			auto catalogPiece = [&](const char* mesh, Vec2 q, double y, double yaw, double scale, bool occluder) {
				ScatterDef sd;
				sd.mesh = std::string("meshes/props/") + mesh + ".gltf";
				sd.position = q; sd.yOffset = y; sd.yawDeg = yaw; sd.scale = scale;
				sd.flags = static_cast<uint8_t>(kSolid | kPiece | (occluder ? kOccluder : 0u));
				sd.cullClass = 0;
				w.scatter.push_back(std::move(sd));
			};
			auto light = [&](const std::string& id, Vec2 q, double y, double radius, double intensity) {
				PointLightDef L;
				L.id = p.id + "_" + id; L.position = q; L.y = w.HeightAt(q) + y;
				L.radiusM = radius; L.intensity = intensity; L.r = 1.0; L.g = 0.62; L.b = 0.3;
				w.pointLights.push_back(L);
			};
			// v1.0.145 (demande du propriétaire du 27/11/2026, BD-T1 p.28 cases 1, 2, 6 et 8 ;
			// BD-T3 p.24 case 7) — SALLE DU TRÔNE en longue NEF : 72 × 26 m, murs de 9 étages
			// (≈ 28 m) à hautes baies, toiture d'ardoise à voûte brisée (Palace_Hall_Roof_1),
			// entrée par la porte monumentale à vantaux sculptés (Door_Throne_Imperial_1) juste
			// derrière la haute baie de la façade, deux rangées de statues colossales
			// encapuchonnées (Statue_Hall_Hooded_1), rosace au-dessus du trône, trône au sommet
			// d'une pyramide à degrés (Throne_Dais_Imperial_1) couverte du tapis rouge. La
			// plateforme de l'acropole passe de 56 à 70 m de rayon (arbitrage Q4 b, master).
			// Repère : la nef va de hallFront (porte) à hallBack (fond, côté mer).
			const double hallHalf = 13.0;
			const double hallFront = std::min(34.0, topR - 36.0);
			const double hallBack = hallFront - 72.0;
			const int hallLevels = 9;
			const double hallH = hallLevels * kWallH;
			// Façade dédiée : son parement arrière (Z local −2 m × 1,3) contre le mur avant de la nef.
			PalaceMonument(w, "Palace_Imperial_Facade_1", at(hallFront + 3.0, 0.0), u, kScale);
			// Murs de la nef : modules de 2 m, hautes baies (fenêtres en lancette) aux étages 5 à 7
			// une travée sur trois ; mur avant ouvert sur la porte (10,4 m), mur du fond plein.
			auto naveWall = [&](Vec2 a, Vec2 b, bool windows) {
				const double len = std::hypot(b.x - a.x, b.z - a.z);
				const int n = std::max(1, static_cast<int>(std::lround(len / 2.0)));
				const double sc = len / n / 2.0;
				const Vec2 d = (b - a) * (1.0 / len);
				for (int k = 0; k < n; ++k)
				{
					const Vec2 q = a + d * ((k + 0.5) * 2.0 * sc);
					for (int lv = 0; lv < hallLevels; ++lv)
					{
						const bool win = windows && lv >= 5 && lv <= 7 && k % 3 == 1;
						Piece(w, win ? "Wall_UnevenBrick_Window_Thin_Round" : "Wall_UnevenBrick_Straight", q, lv * kWallH * sc, YawAlong(d), sc, true);
					}
				}
			};
			for (int sg = -1; sg <= 1; sg += 2)
			{
				naveWall(at(hallBack, sg * hallHalf), at(hallFront, sg * hallHalf), true);
				naveWall(at(hallFront, sg * hallHalf), at(hallFront, sg * 5.6), false);
			}
			naveWall(at(hallBack, -hallHalf), at(hallBack, hallHalf), false);
			// Mur au-dessus de la porte : de son linteau (18,2 m) au haut des murs, 6 modules.
			{
				const int nLin = 6;
				const double scL = 2.0 * 5.6 / nLin / 2.0;
				for (int k = 0; k < nLin; ++k)
					for (double y = 18.2; y < hallH - 0.1; y += kWallH * scL)
						Piece(w, "Wall_UnevenBrick_Straight", at(hallFront, -5.6 + (k + 0.5) * 2.0 * scL), y, YawAlong(v), scL, true);
			}
			// Porte de la salle du trône (vantaux ouverts vers l'intérieur) : le plan du cadre est
			// à +1,8 m du pivot du mesh (Z local), aligné sur le mur avant.
			catalogPiece("Door_Throne_Imperial_1", at(hallFront - 1.8, 0.0), 0.0, yawTo(u), 1.0, false);
			// Sol de dalles de la nef.
			for (double du = hallBack + 1.0; du < hallFront; du += 2.0)
				for (double dv = -hallHalf + 1.0; dv < hallHalf; dv += 2.0)
					Piece(w, "Floor_Brick", at(du, dv), 0.02, yawU, 1.0, true);
			// Toiture (voûte brisée vue de l'intérieur), posée sur le haut des murs ; pas de collision.
			{
				ScatterDef roof;
				roof.mesh = "meshes/props/Palace_Hall_Roof_1.gltf";
				roof.position = at(0.5 * (hallFront + hallBack), 0.0);
				roof.yOffset = hallH;
				roof.yawDeg = yawTo(u);
				roof.cullClass = 0;
				w.scatter.push_back(std::move(roof));
			}
			// Estrade pyramidale et trône au fond ; rosace au-dessus (mur du fond).
			const double daisU = hallBack + 9.0;
			catalogPiece("Throne_Dais_Imperial_1", at(daisU, 0.0), 0.0, yawU, 1.0, false);
			Piece(w, "Throne_Wood_1", at(daisU - 0.6, 0.0), 3.92, yawU, 1.6, true);
			Piece(w, "Window_Rose_1", at(hallBack + 0.4, 0.0), 15.0, YawAlong(v), 2.6, false);
			// Tapis rouge de la porte au pied de l'estrade (3 m de large).
			for (double du = hallFront - 9.0; du > daisU + 7.0 + 4.0; du -= 18.0)
				Piece(w, "Carpet_Runner_1", at(du, 0.0), 0.04, yawU, 1.5, false);
			Piece(w, "Carpet_Runner_1", at(daisU + 7.0 + 9.0, 0.0), 0.05, yawU, 1.5, false);
			// Statues colossales le long des murs, tournées vers l'axe ; braseros entre elles.
			int nStat = 0;
			for (double du = hallFront - 10.0; du > daisU + 9.0; du -= 12.0)
				for (int sg = -1; sg <= 1; sg += 2)
				{
					catalogPiece("Statue_Hall_Hooded_1", at(du, sg * (hallHalf - 2.8)), 0.0, yawTo(v * static_cast<double>(-sg)), 1.0, false);
					const Vec2 b = at(du - 6.0, sg * (hallHalf - 3.2));
					Piece(w, "Brazier_Column_1", b, 0.0, 0.0, 1.2, true);
					if (sg > 0 && nStat % 2 == 0) light("nef_" + std::to_string(nStat), b, 2.8, 16.0, 2.0);
					if (sg > 0) ++nStat;
				}
			for (int sg = -1; sg <= 1; sg += 2)
			{
				const Vec2 b = at(daisU + 2.0, sg * 9.0);
				Piece(w, "Brazier_Column_1", b, 0.0, 0.0, 1.5, true);
				light(std::string("trone_") + (sg > 0 ? "1" : "0"), b, 3.4, 18.0, 2.4);
			}
			// Flèches : deux aux angles du fond, deux au droit du milieu de la nef (hors des murs).
			for (int sg = -1; sg <= 1; sg += 2)
			{
				PalaceMonument(w, "Palace_Imperial_Spire_1", at(hallBack - 4.0, sg * (hallHalf + 6.0)), u, kScale);
				PalaceMonument(w, "Palace_Imperial_Spire_1", at(0.5 * (hallFront + hallBack), sg * (hallHalf + 6.0)), u, kScale);
			}
			// Parvis : braseros allumés, cyprès en pots, statues à sujet neutre (entre la façade
			// et le bord de la plateforme, où part l'escalier).
			const double parvis = hallFront + 15.0;
			for (int k = 0; k < 2; ++k)
				for (int sg = -1; sg <= 1; sg += 2)
				{
					const Vec2 b = at(parvis + 3.0 + 9.0 * k, sg * 7.0);
					Piece(w, "Brazier_Column_1", b, 0.0, 0.0, 1.4, true);
					light("brasero_" + std::to_string(k * 2 + (sg > 0 ? 1 : 0)), b, 3.2, 14.0, 2.2);
				}
			for (double du = parvis + 1.0; du <= topR - 6.0; du += 4.0)
				for (int sg = -1; sg <= 1; sg += 2)
					Piece(w, "Tree_Cypress_Potted_1", at(du, sg * 15.0), 0.0, rng.Next() * 360.0, 1.0, true);
			for (int sg = -1; sg <= 1; sg += 2)
				Piece(w, "Statue_Garden_1", at(parvis + 7.0, sg * 22.0), 0.0, yawU + (sg > 0 ? -90.0 : 90.0), 1.0, true);
			// Jardin à l'arrière (T4 pl.7), entre le fond de la nef et l'enceinte : fontaine, haies
			// taillées, cyprès, statues.
			const double garden = hallBack - 14.0;
			if (garden - 6.0 < -(topR - 4.0)) { w.Occupy(c, topR); return; } // plateforme trop petite
			Piece(w, "Fountain_Basin_1", at(garden, 0.0), 0.0, rng.Next() * 45.0, 1.2, true);
			for (int k = 0; k < 8; ++k)
			{
				const double a = k * kPi / 4.0;
				Piece(w, "Hedge_Trimmed_1", at(garden + 5.0 * std::cos(a), 5.0 * std::sin(a)), 0.0, 0.0, 1.0, true);
			}
			for (int sg = -1; sg <= 1; sg += 2)
			{
				for (int k = 0; k < 3; ++k)
					Piece(w, k % 2 ? "Tree_Cypress_2" : "Tree_Cypress_1", at(hallBack - 6.0 - 6.0 * k, sg * 10.0), 0.0, rng.Next() * 360.0, 0.8, true);
				Piece(w, "Statue_Garden_2", at(garden, sg * 8.5), 0.0, yawU + (sg > 0 ? -90.0 : 90.0), 1.0, true);
			}
			w.Occupy(c, topR);
		}

		/// v1.0.143 — PALAIS DU MAGISTER (Magistrya, BD-T2 p.20, 23, 25 ; LORE_BD §16.69), au
		/// sommet de la ville-tour (POI de genre « palais » sur la plateforme d'un pic). Repère :
		/// \p u vers l'arrivée de la rampe (portail), \p v à gauche. Construit : parapet de la
		/// plateforme (ouvert sur la rampe), quatre tours rondes aux angles ; SALLE DU TRÔNE en
		/// longueur (46 × 18 m, murs de 6 étages, sol de dalles), portail gothique à rosace sans
		/// pentagramme (Portal_Gothic_1, Window_Rose_1), deux rangées de colonnes torsadées
		/// colossales (Column_Twisted_Giant_1, ≈ 16 m), tapis, braseros, trône sur estrade et
		/// fauteuil sculpté ; LABORATOIRE au fond (porte de bois en plein cintre, charpente, table
		/// de pierre, étagères d'apothicaire, alambic, chaudron, chaînes à crochet). Signatures de
		/// la BD (pentagramme marqué « H », machine d'extraction) non reproduites. Effet de bord :
		/// w.scatter, w.pointLights, occupation de w.
		void BuildMagisterPalace(World& w, const PoiDef& p, Vec2 c, Vec2 u, double topR, LandmarkRng& rng)
		{
			const Vec2 v{ -u.z, u.x };
			auto at = [&](double du, double dv) { return c + u * du + v * dv; };
			const double yawU = std::atan2(u.x, u.z) * 180.0 / kPi; // +Z local vers u
			auto light = [&](const std::string& id, Vec2 q, double y, double radius, double intensity) {
				PointLightDef L;
				L.id = p.id + "_" + id; L.position = q; L.y = w.HeightAt(q) + y;
				L.radiusM = radius; L.intensity = intensity; L.r = 1.0; L.g = 0.62; L.b = 0.3;
				w.pointLights.push_back(L);
			};
			// Parapet de la plateforme, ouvert de 14 m sur l'arrivée de la rampe.
			const double rimR = topR - 0.5;
			const int nRim = static_cast<int>(std::lround(2.0 * kPi * rimR / 2.0));
			const double au = std::atan2(u.z, u.x);
			for (int k = 0; k < nRim; ++k)
			{
				const double a = (k + 0.5) * 2.0 * kPi / nRim;
				if (std::fabs(std::remainder(a - au, 2.0 * kPi)) * rimR < 7.0) continue;
				const Vec2 dir{ std::cos(a), std::sin(a) };
				Piece(w, "Bridge_Stone_Parapet_1", c + dir * rimR, 0.0, YawAlong(Vec2{ -dir.z, dir.x }), 1.0, true);
			}
			// Tours rondes aux angles (« des tours dans les angles », p.20).
			for (int k = 0; k < 4; ++k)
			{
				const double a = au + kPi / 4.0 + k * kPi / 2.0;
				const Vec2 dir{ std::cos(a), std::sin(a) };
				BuildTower(w, c + dir * (topR - 4.5), 2.8, 7, true, dir * -1.0, false, rng);
			}
			// Salle du trône : murs (6 étages ≈ 18,7 m), passage au portail, arche vers le laboratoire.
			// Dimensions proportionnées à la plateforme (rayon 32 m : salle de 34 × 16 m).
			const double hallFront = 0.6 * topR, hallBack = -0.45 * topR, hallHalf = 8.0;
			for (int sg = -1; sg <= 1; sg += 2)
			{
				BuildWall(w, at(hallBack, sg * hallHalf), at(hallFront, sg * hallHalf), 6, false, false, rng);
				BuildWall(w, at(hallFront, sg * hallHalf), at(hallFront, sg * 4.0), 6, false, false, rng);
			}
			BuildWall(w, at(hallBack, -hallHalf), at(hallBack, hallHalf), 6, false, true, rng);
			for (double du = hallBack + 1.0; du < hallFront; du += 2.0)
				for (double dv = -hallHalf + 1.0; dv < hallHalf; dv += 2.0)
					Piece(w, "Floor_Brick", at(du, dv), 0.02, yawU, 1.0, true);
			// Portail gothique (rosace sans pentagramme) et rosace au-dessus.
			Piece(w, "Portal_Gothic_1", at(hallFront + 0.5, 0.0), 0.0, YawAlong(v), 1.25, false);
			Piece(w, "Window_Rose_1", at(hallFront + 0.4, 0.0), 13.2, YawAlong(v), 1.2, false);
			for (int sg = -1; sg <= 1; sg += 2)
			{
				const Vec2 b = at(hallFront + 4.0, sg * 6.0);
				Piece(w, "Brazier_Column_1", b, 0.0, 0.0, 1.4, true);
				light("parvis_" + std::to_string(sg > 0 ? 1 : 0), b, 3.2, 14.0, 2.0);
			}
			// Colonnes torsadées colossales (deux rangées), tapis, braseros.
			// (collision par le catalogue de pièces : socle, fût, chapiteau ; grande distance de vue)
			for (int k = 0; k < 4; ++k)
				for (int sg = -1; sg <= 1; sg += 2)
				{
					ScatterDef col;
					col.mesh = "meshes/props/Column_Twisted_Giant_1.gltf";
					col.position = at(hallFront - 4.0 - 7.0 * k, sg * 4.8);
					col.yawDeg = rng.Next() * 360.0;
					col.flags = static_cast<uint8_t>(kSolid | kPiece);
					col.cullClass = 0;
					w.scatter.push_back(std::move(col));
				}
			for (int k = 0; k < 2; ++k)
				Piece(w, "Carpet_Runner_1", at(hallFront - 7.0 - 12.0 * k, 0.0), 0.06, yawU, 1.0, false);
			for (int k = 0; k < 3; ++k)
				for (int sg = -1; sg <= 1; sg += 2)
				{
					const Vec2 b = at(hallFront - 7.5 - 7.0 * k, sg * 2.6);
					Piece(w, "Brazier_Column_1", b, 0.0, 0.0, 1.0, true);
					if (sg > 0) light("salle_" + std::to_string(k), b, 2.4, 12.0, 1.8);
				}
			// Estrade (dalles relevées), trône face au portail, fauteuil sculpté à côté.
			for (double du = hallBack + 2.0; du <= hallBack + 6.0; du += 2.0)
				for (double dv = -3.0; dv <= 3.0; dv += 2.0)
					Piece(w, "Bridge_Stone_Deck_1", at(du, dv), 0.36, yawU, 1.0, true);
			Piece(w, "Throne_Wood_1", at(hallBack + 3.0, 0.0), 0.37, yawU, 1.5, true);
			Piece(w, "Armchair_Carved_1", at(hallBack + 3.4, 2.2), 0.37, yawU, 1.0, true);
			// Laboratoire (p.23, p.25) : murs de 3 étages, sol de dalles, porte de bois en plein
			// cintre ouverte dans l'arche, charpente, mobilier d'alchimiste.
			const double labBack = -0.84 * topR, labHalf = 7.0;
			for (int sg = -1; sg <= 1; sg += 2)
				BuildWall(w, at(labBack, sg * labHalf), at(hallBack, sg * labHalf), 3, false, false, rng);
			BuildWall(w, at(labBack, -labHalf), at(labBack, labHalf), 3, false, false, rng);
			for (double du = labBack + 1.0; du < hallBack; du += 2.0)
				for (double dv = -labHalf + 1.0; dv < labHalf; dv += 2.0)
					Piece(w, "Floor_Brick", at(du, dv), 0.02, yawU, 1.0, true);
			Piece(w, "Door_2_Round", at(hallBack - 0.4, -1.2), 0.0, yawU + 70.0, 1.0, false);
			const double labMid = 0.5 * (labBack + hallBack);
			for (int k = 0; k < 3; ++k)
			{
				const double du = hallBack - 2.5 - 3.6 * k;
				Piece(w, "Beam_Truss_1", at(du, 0.0), 6.2, YawAlong(v), 1.75, false);
				Piece(w, "Chain_Hook_1", at(du, -2.5 + 2.5 * k), 3.0, 0.0, 1.0, false);
			}
			Piece(w, "Table_Stone_1", at(labMid, 0.0), 0.0, YawAlong(u), 1.2, true);
			Piece(w, "Alembic_1", at(labMid, 0.5), 1.08, 0.0, 1.0, false);
			Piece(w, "Book_5", at(labMid - 0.2, -0.6), 1.09, 30.0, 1.0, false);
			Piece(w, "Cauldron", at(labBack + 2.0, 4.0), 0.0, 0.0, 1.2, true);
			for (int k = 0; k < 2; ++k)
				for (int sg = -1; sg <= 1; sg += 2)
					Piece(w, "Shelf_Apothecary_1", at(hallBack - 3.0 - 4.0 * k, sg * (labHalf - 0.6)), 0.0, YawAlong(u) + (sg > 0 ? 180.0 : 0.0), 1.0, true);
			Piece(w, "Shelf_Small_Bottles", at(labBack + 0.6, 2.0), 0.0, yawU, 1.0, false);
			light("laboratoire", at(labMid, 0.0), 4.0, 12.0, 1.5);
			w.Occupy(c, topR);
		}
	}

	void BuildLandmarks(World& w)
	{
		for (const PoiDef& p : w.pois)
		{
			if (p.kind != "tour" && p.kind != "chateau" && p.kind != "phare" && p.kind != "cascade" && p.kind != "pont" && p.kind != "palais") continue;
			if (!w.OwnsPoint(p.center)) continue;
			LandmarkRng rng{ Hash(w.master.seed, HashString(p.id.c_str()), 7131) };
			const bool ruin = p.state == "ruine";
			// Orientation : vers la route la plus proche (porte, pont), sinon au hasard.
			Vec2 toward{ std::cos(rng.Next() * 2.0 * kPi), 0.0 };
			toward.z = std::sqrt(std::max(0.0, 1.0 - toward.x * toward.x));
			Vec2 roadDir = toward;
			double bd = 1e18;
			for (const Road& r : w.roads)
				for (size_t k = 0; k + 1 < r.points.size(); k += 3)
				{
					const double d = std::hypot(r.points[k].x - p.center.x, r.points[k].z - p.center.z);
					if (d < bd && d > 1.0)
					{
						bd = d;
						toward = (r.points[k] - p.center) * (1.0 / d);
						const Vec2 t = r.points[k + 1] - r.points[k];
						const double tl = std::hypot(t.x, t.z);
						if (tl > 1e-6) roadDir = t * (1.0 / tl);
					}
				}
			const std::string low = p.name;
			if (p.kind == "tour")
			{
				// Tour Carrée de la Lune Noire : trente étages (BD) ; tours de guet : 3 à 5.
				const bool carree = low.find("Carr") != std::string::npos;
				const int levels = carree ? 24 : 3 + static_cast<int>(rng.Next() * 3.0);
				BuildTower(w, p.center, (carree ? 5.0 : 3.0) * p.scale, levels, !carree, toward, ruin, rng);
				w.Occupy(p.center, (carree ? 8.0 : 5.0) * p.scale);
			}
			else if (p.kind == "phare")
			{
				const double top = BuildTower(w, p.center, 3.0 * p.scale, 6, true, toward, ruin, rng, 0.0, true);
				if (!ruin)
				{
					// Feu du phare : lanterne et lumière en tête de tour.
					Piece(w, "Lantern_Wall", p.center + Vec2{ 3.0 * p.scale, 0.0 }, top - 1.0, 90.0, 1.4, false);
					PointLightDef L;
					L.id = p.id + "_feu"; L.position = p.center; L.y = w.HeightAt(p.center) + top + 1.0;
					L.radiusM = 45.0; L.intensity = 3.0; L.r = 1.0; L.g = 0.75; L.b = 0.35;
					w.pointLights.push_back(L);
				}
				w.Occupy(p.center, 5.0 * p.scale);
			}
			else if (p.kind == "chateau")
			{
				BuildCastle(w, p.center, p.scale, toward, ruin, rng);
				w.Occupy(p.center, 40.0 * p.scale);
			}
			else if (p.kind == "palais")
			{
				// v1.0.140 : sur la plateforme de l'acropole la plus proche (porte vers son
				// escalier) ; sans acropole, plateforme de 56 m tournée vers la route.
				const AcropoleShape* ac = nullptr;
				const TowerShape* tw = nullptr;
				for (const CityDef& c : w.cities)
				{
					if (c.acropole.enabled && std::hypot(c.acropole.summit.x - p.center.x, c.acropole.summit.z - p.center.z) < c.acropole.topRadiusM)
						ac = &c.acropole;
					if (c.tower.enabled && std::hypot(c.tower.center.x - p.center.x, c.tower.center.z - p.center.z) < c.tower.topRadiusM)
						tw = &c.tower;
				}
				// v1.0.143 : sommet d'une ville-tour (Magistrya) : palais du Magister, portail vers
				// l'arrivée de la cage d'escalier 0 (direction du ponton). v1.0.143 : le sommet de
				// l'aiguille n'est pas un plateau de heightmap (rocher en retrait, dallage posé par
				// BuildTowerCity à l'altitude du sommet) : les pièces du palais, posées par rapport
				// au sol, sont ramenées à un sol PLAT à cette altitude.
				if (tw)
				{
					const size_t s0 = w.scatter.size(), l0 = w.pointLights.size();
					BuildMagisterPalace(w, p, tw->center, Vec2{ std::cos(tw->entryRad), std::sin(tw->entryRad) }, tw->topRadiusM, rng);
					double summitY = 0.0;
					for (const CityDef& c : w.cities)
						if (&c.tower == tw) summitY = c.plateauY + tw->heightM;
					// Pierre ocre orangé comme toute la ville (BD-T2 p.19-20) : les murs gris du kit
					// (tours et murs communs aux châteaux) passent au grès de Magistrya.
					const std::string grey = "/Wall_UnevenBrick_", ochre = "/Wall_Sandstone_";
					for (size_t k = s0; k < w.scatter.size(); ++k)
					{
						w.scatter[k].yOffset += summitY - w.HeightAt(w.scatter[k].position);
						const size_t at = w.scatter[k].mesh.find(grey);
						if (at != std::string::npos) w.scatter[k].mesh.replace(at, grey.size(), ochre);
					}
					for (size_t k = l0; k < w.pointLights.size(); ++k)
						w.pointLights[k].y += summitY - w.HeightAt(w.pointLights[k].position);
				}
				else
					BuildImperialPalace(w, p, ac ? ac->summit : p.center, ac ? ac->stairDir : toward, ac ? ac->topRadiusM : 56.0, rng);
			}
			else if (p.kind == "cascade")
				BuildCascade(w, p, roadDir, rng);
			else if (p.kind == "pont")
			{
				const bool wood = p.name.find("bois") != std::string::npos || p.name.find("corde") != std::string::npos;
				BuildBridgeLandmark(w, p, roadDir, wood, ruin, rng);
			}
			++w.report.landmarks;
		}
	}

	// ---------------------------------------------------------------------------------
	// v1.0.135 — PETITS LIEUX DE DÉCOR : de quoi donner envie de quitter la route.
	// Treillis GLOBAL de ~1,1 km : au plus un petit lieu par case, tiré au hasard
	// (graine + case), sur les terres ouvertes, loin des routes, des villes, de l'eau et
	// des reliefs posés. Décor seul (aucune incidence sur le jeu : ni PNJ, ni cimetière).
	// ---------------------------------------------------------------------------------
	namespace
	{
		/// Cercle de pierres levées : 7 à 11 blocs dressés autour d'une pierre centrale.
		/// Effet de bord : w.scatter.
		void BuildStandingStones(World& w, Vec2 c, LandmarkRng& rng)
		{
			const int n = 7 + static_cast<int>(rng.Next() * 5.0);
			const double r = 6.5 + rng.Next() * 4.0;
			static const char* rocks[] = { "Rock_Medium_1", "Rock_Medium_2", "Rock_Medium_3" };
			for (int k = 0; k < n; ++k)
			{
				const double a = (k + 0.15 * rng.Next()) * 2.0 * kPi / n;
				if (rng.Next() < 0.12) continue; // une pierre tombée de-ci de-là
				ScatterDef s;
				s.mesh = std::string("meshes/props/") + rocks[k % 3] + ".gltf";
				s.position = c + Vec2{ std::cos(a), std::sin(a) } * r;
				s.scale = 1.9 + rng.Next() * 0.9;
				s.yawDeg = rng.Next() * 360.0;
				s.tiltDeg = rng.Next() * 8.0;
				s.yOffset = -0.2 * s.scale;
				s.flags = kSolid;
				s.cullClass = 0;
				w.scatter.push_back(std::move(s));
			}
			Piece(w, "Rock_Medium_2", c, -0.4, rng.Next() * 360.0, 2.6, true);
		}

		/// Ruine de ferme ou de chapelle : deux pans de mur en équerre, éboulis, poterie brisée.
		/// Effet de bord : w.scatter.
		void BuildSmallRuin(World& w, Vec2 c, LandmarkRng& rng)
		{
			const double a = rng.Next() * 2.0 * kPi;
			const Vec2 u{ std::cos(a), std::sin(a) }, v{ -std::sin(a), std::cos(a) };
			const double lu = 6.0 + rng.Next() * 5.0, lv = 4.0 + rng.Next() * 4.0;
			const Vec2 corner = c - u * (lu * 0.5) - v * (lv * 0.5);
			BuildWall(w, corner, corner + u * lu, 1 + (rng.Next() < 0.4 ? 1 : 0), true, false, rng);
			BuildWall(w, corner, corner + v * lv, 1, true, rng.Next() < 0.5, rng);
			for (int k = 0; k < 5; ++k)
			{
				const Vec2 p = c + u * ((rng.Next() - 0.3) * lu) + v * ((rng.Next() - 0.3) * lv);
				Piece(w, k % 2 ? "Prop_Brick3" : "Prop_Brick1", p, 0.0, rng.Next() * 360.0, 1.2 + rng.Next() * 0.6, false);
			}
			Piece(w, "Vase_Rubble_Medium", c + v * (lv * 0.2), 0.0, rng.Next() * 360.0, 1.0, false);
		}

		/// Campement abandonné : chariot, caisses, tonneaux, sac, chaudron sur un feu éteint.
		/// Effet de bord : w.scatter.
		void BuildCamp(World& w, Vec2 c, LandmarkRng& rng)
		{
			const double a = rng.Next() * 360.0;
			const Vec2 d{ std::cos(a * kPi / 180.0), std::sin(a * kPi / 180.0) };
			Piece(w, "Prop_Wagon", c + d * 4.5, 0.0, a, 1.0, true);
			Piece(w, "Cauldron", c, 0.0, rng.Next() * 360.0, 1.0, false);
			for (int k = 0; k < 6; ++k)
			{
				const double b = (k / 6.0) * 2.0 * kPi + rng.Next() * 0.3;
				Piece(w, "Rock_Medium_3", c + Vec2{ std::cos(b), std::sin(b) } * 1.1, -0.15, rng.Next() * 360.0, 0.25, false);
			}
			static const char* stuff[] = { "Crate_Wooden", "Barrel", "Bag", "FarmCrate_Empty", "Barrel_Apples", "Bucket_Wooden_1" };
			for (int k = 0; k < 5; ++k)
			{
				const double b = rng.Next() * 2.0 * kPi;
				Piece(w, stuff[static_cast<int>(rng.Next() * 6.0) % 6], c + Vec2{ std::cos(b), std::sin(b) } * (2.6 + rng.Next() * 2.0),
					0.0, rng.Next() * 360.0, 1.0, k < 2);
			}
			Piece(w, "Torch_Metal", c - d * 3.0, 0.0, 0.0, 1.0, false);
			// Atelier décors (BD T1 pl.4-6, 13, 15) : abri de toile et feu à broche.
			const Vec2 side{ -d.z, d.x };
			Piece(w, rng.Next() < 0.5 ? "Tent_Awning_1" : "Tent_Pavilion_1", c + side * 7.0, 0.0, a + 90.0, 1.0, true);
			Piece(w, "Campfire_Spit_1", c - side * 3.2, 0.0, rng.Next() * 360.0, 1.0, false);
			// Atelier décors T5 (pl.41) : mâts-torches aux abords du camp.
			Piece(w, "Torch_Pole_1", c + side * 5.0 - d * 1.0, 0.0, 0.0, 1.0, false);
			Piece(w, "Torch_Pole_1", c - side * 5.0 + d * 1.0, 0.0, 0.0, 1.0, false);
			// Atelier décors T4 (pl.39) : guérite de bois carrée, un camp sur trois.
			if (rng.Next() < 0.35) Piece(w, "Watchpost_Wood_1", c + d * 9.0 + side * 4.0, 0.0, a, 1.0, true);
			// Atelier décors T3 : baquet de bain (pl.5) ; chariot blindé de mercenaires (pl.25).
			Piece(w, "Tub_Wood_1", c + side * 3.5 + d * 2.5, 0.0, rng.Next() * 360.0, 1.0, true);
			if (rng.Next() < 0.15) Piece(w, "Wagon_Armored_1", c - d * 7.0, 0.0, a + 90.0, 1.0, true);
			// Atelier décors T2 : bûcher de fête (pl.23) dans un camp sur trois, bât de mulet.
			if (rng.Next() < 0.33) Piece(w, "Bonfire_Stack_1", c - side * 9.0 + d * 3.0, 0.0, rng.Next() * 360.0, 1.0, true);
			Piece(w, "PackSaddle_1", c + d * 2.4 - side * 1.5, 0.0, rng.Next() * 360.0, 1.0, false);
			// Atelier décors T8 (pl.37) : paillasses alignées près de l'abri de toile.
			for (int k = 0; k < 2; ++k)
				Piece(w, "Bed_Pallet_1", c + side * 4.4 - d * (3.0 + k * 1.1), 0.0, a, 1.0, false);
		}

		/// Champ de bataille abandonné (BD T1 pl.36-44) : débris de chariot, piques,
		/// mantelets, échelle d'assaut, palissade de pieux et un engin de siège.
		/// Effet de bord : w.scatter.
		void BuildBattlefield(World& w, Vec2 c, LandmarkRng& rng)
		{
			const double a = rng.Next() * 360.0;
			const Vec2 u{ std::cos(a * kPi / 180.0), std::sin(a * kPi / 180.0) }, v{ -u.z, u.x };
			// Engin de siège : catapulte, tour de siège ou trébuchet à contrepoids (BD T2 pl.33).
			const double eng = rng.Next();
			Piece(w, eng < 0.45 ? "Catapult_1" : (eng < 0.75 ? "SiegeTower_1" : "Trebuchet_1"), c, 0.0, a, 1.0, true);
			Piece(w, "Watchpost_Wood_1", c - v * 16.0 + u * 6.0, 0.0, a, 1.0, true);   // guérite (T4, pl.39)
			// Atelier décors T5 (pl.22, 31, 41) : chevaux de frise, baliste, ossements, mâts-torches.
			for (int k = -1; k <= 1; ++k)
				Piece(w, "Stakes_Chevaux_1", c - v * 12.0 + u * (k * 4.2), 0.0, a, 1.0, true);
			Piece(w, "Ballista_1", c + u * 9.0 + v * 6.0, 0.0, a + 90.0, 1.0, true);
			Piece(w, "Tent_Striped_1", c - u * 14.0 - v * 4.0, 0.0, a, 1.0, true);   // tente de commandement (T4, pl.39)
			// Atelier décors T6 (pl.8, 36, 48) : table de campagne à carte et liasses devant la
			// tente de commandement ; bûcher à poteau à l'écart.
			Piece(w, "Map_Table_1", c - u * 9.5 - v * 4.0, 0.0, a + 90.0, 1.0, true);
			Piece(w, "Paper_Stack_1", c - u * 9.5 - v * 5.6, 0.0, a, 1.0, false);
			Piece(w, "Pyre_Stake_1", c + u * 15.0 - v * 3.0, 0.0, a, 1.0, true);
			// Atelier décors T10 (pl.23, 38) : bélier couvert abandonné.
			Piece(w, "Battering_Ram_1", c + u * 14.0 + v * 10.5, 0.0, a, 1.0, true);
			for (int k = 0; k < 4; ++k)
			{
				const double b = rng.Next() * 2.0 * kPi;
				static const char* bones[] = { "Bones_Scatter_1", "Bones_Scatter_2", "Bones_Scatter_3" };
				Piece(w, bones[k % 3], c + Vec2{ std::cos(b), std::sin(b) } * (5.0 + rng.Next() * 14.0), 0.0, rng.Next() * 360.0, 1.0, false);
			}
			Piece(w, "Torch_Pole_1", c - v * 16.0 + u * 8.5, 0.0, 0.0, 1.0, false);
			// Atelier décors T8 (pl.21) : levée de terre étayée de planches face à l'engin.
			for (int k = -1; k <= 1; ++k)
				Piece(w, "Earthwork_Plank_1", c - v * 21.0 + u * (k * 6.0), 0.0, a, 1.0, true);
			for (int k = -2; k <= 2; ++k)   // ligne de palissade de campagne
				Piece(w, "Palisade_Stake_1", c + v * 14.0 + u * (k * 2.0), 0.0, a, 1.0, true);
			for (int k = 0; k < 4; ++k)
				Piece(w, "Mantlet_1", c + u * (-10.0 + k * 6.5) - v * (8.0 + rng.Next() * 3.0), 0.0, a + 180.0 + (rng.Next() - 0.5) * 30.0, 1.0, true);
			static const char* debris[] = { "Debris_Wheel_1", "Pike_Stake_1", "Pike_Stake_1", "SiegeLadder_1", "Shield_Wooden", "Prop_Brick2" };
			for (int k = 0; k < 9; ++k)
			{
				const double b = rng.Next() * 2.0 * kPi;
				const Vec2 p = c + Vec2{ std::cos(b), std::sin(b) } * (6.0 + rng.Next() * 16.0);
				Piece(w, debris[static_cast<int>(rng.Next() * 6.0) % 6], p, 0.0, rng.Next() * 360.0, 1.0, false);
			}
		}

		/// Champ clôturé (rectangle de clôtures, une entrée) avec caisses de récolte.
		/// Effet de bord : w.scatter.
		void BuildFencedField(World& w, Vec2 c, LandmarkRng& rng)
		{
			const double a = rng.Next() * 2.0 * kPi;
			const Vec2 u{ std::cos(a), std::sin(a) }, v{ -std::sin(a), std::cos(a) };
			const double hu = 12.0 + rng.Next() * 8.0, hv = 8.0 + rng.Next() * 6.0;
			const Vec2 cs[4] = { c - u * hu - v * hv, c + u * hu - v * hv, c + u * hu + v * hv, c - u * hu + v * hv };
			for (int e = 0; e < 4; ++e)
			{
				const Vec2 A = cs[e], B = cs[(e + 1) % 4];
				const double len = std::hypot(B.x - A.x, B.z - A.z);
				const int n = std::max(1, static_cast<int>(std::lround(len / 2.0)));
				const Vec2 dir = (B - A) * (1.0 / len);
				for (int k = 0; k < n; ++k)
				{
					if (e == 0 && k == n / 2) continue; // l'entrée
					Piece(w, "Prop_WoodenFence_Single", A + dir * ((k + 0.5) * len / n), 0.0, YawAlong(dir), len / n / 2.0, true);
				}
			}
			for (int k = 0; k < 3; ++k)
				Piece(w, k == 2 ? "FarmCrate_Carrot" : "FarmCrate_Apple", c + u * ((rng.Next() - 0.5) * hu) + v * ((rng.Next() - 0.5) * hv),
					0.0, rng.Next() * 360.0, 1.0, false);
		}
	}

	void BuildMicroLandmarks(World& w)
	{
		if (IsInfernalMap(w.master)) return; // aux Enfers : rien (carte nue, explicite)
		const double step = 1100.0;
		const double sea = w.master.scale.seaLevelM;
		const int gi0 = static_cast<int>(std::floor(w.terrainOriginX / step)) - 1;
		const int gj0 = static_cast<int>(std::floor(w.terrainOriginZ / step)) - 1;
		const int n = static_cast<int>(w.terrainSizeM / step) + 3;
		for (int jj = 0; jj < n; ++jj)
			for (int ii = 0; ii < n; ++ii)
			{
				const int i = gi0 + ii, j = gj0 + jj;
				const uint64_t h = Hash(w.master.seed, 0xB0B1E5ull, static_cast<uint64_t>(static_cast<int64_t>(i)), static_cast<uint64_t>(static_cast<int64_t>(j)));
				if (ToUnit(h) > 0.8) continue; // ~4 cases sur 5 ont un petit lieu
				const Vec2 c{ (i + 0.15 + 0.7 * ToUnit(Mix64(h + 1))) * step, (j + 0.15 + 0.7 * ToUnit(Mix64(h + 2))) * step };
				if (!w.OwnsPoint(c)) continue;
				// Tout le petit lieu (≤ 30 m de rayon) reste dans la cellule : pas d'instance hors emprise.
				bool inside = true;
				for (const Vec2& d : { Vec2{ 40.0, 0.0 }, Vec2{ -40.0, 0.0 }, Vec2{ 0.0, 40.0 }, Vec2{ 0.0, -40.0 } })
					inside = inside && w.OwnsPoint(c + d);
				if (!inside) continue;
				const int bi = w.biome.IndexX(c.x), bj = w.biome.IndexZ(c.z);
				if (!w.biome.InBounds(bi, bj)) continue;
				const char code = EffectiveCover(w.biome.At(bi, bj), c.x, c.z, w.master.seed);
				const bool open = code == '.' || code == 'r' || code == 'p' || code == '^' || code == ':' || code == 'l' || code == 't' || code == 'o';
				if (!open) continue;
				const double alt = w.HeightAt(c);
				if (alt < sea + 3.0 || alt > 1900.0 || w.IsWater(c) || w.SlopeDegAt(c) > 12.0) continue;
				const int ri = w.roadDist.IndexX(c.x), rj = w.roadDist.IndexZ(c.z);
				if (w.roadDist.InBounds(ri, rj) && w.roadDist.At(ri, rj) < 35.0) continue;
				const int ui = w.urban.IndexX(c.x), uj = w.urban.IndexZ(c.z);
				if (w.urban.InBounds(ui, uj) && w.urban.At(ui, uj) != 0) continue;
				bool nearCity = false;
				for (const CityDef& ct : w.cities)
					if (std::hypot(c.x - ct.center.x, c.z - ct.center.z) < ct.radiusM + 250.0) { nearCity = true; break; }
				if (nearCity || w.IsOccupied(c, 30.0)) continue;
				bool calm = false; // abords des reliefs posés (gouffre, crevasse, colline) : intacts
				for (const WorldMaster::CalmZone& z : w.master.calmZones)
					if (std::hypot(c.x - z.center.x, c.z - z.center.z) < z.radiusM + 500.0) { calm = true; break; }
				if (calm) continue;
				LandmarkRng rng{ Mix64(h + 3) };
				const double t = rng.Next();
				if (t < 0.24) BuildStandingStones(w, c, rng);
				else if (t < 0.46) BuildSmallRuin(w, c, rng);
				else if (t < 0.64) BuildCamp(w, c, rng);
				else if (t < 0.78) BuildBattlefield(w, c, rng); // atelier décors (BD T1)
				else BuildFencedField(w, c, rng);
				w.Occupy(c, 24.0);
				++w.report.microLandmarks;
			}
	}
}

