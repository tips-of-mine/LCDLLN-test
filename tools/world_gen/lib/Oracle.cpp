// tools/world_gen — Camp de l'Oracle bâti selon le plan (lore v1.0.136, docs/world/LORE_BD.md §16.62).
//
// Remplace, pour une agglomération de mise en page `layout: "oracle"` du master (le Camp de
// l'Oracle, N000_E000), le village générique (maisons européennes, palissade ronde) par la
// disposition du « Plan de l'Oracle, extérieur » (vue de dessus, nord en haut) :
//   1. le ROCHER (tumulus, « énorme aérolithe ») : masse octogonale à facettes d'environ 300 m de
//      large, sculptée dans le terrain même (parois de ≈ 62 m à ≈ 74°, couronne à facettes jusqu'à
//      ≈ 90 m), au centre d'une vaste plaine aride aplanie (650 m, raccord jusqu'à 1 100 m) ;
//   7. HUIT ESCALIERS blancs (N, NE, E, SE, S, SO, O, NO), rampe de terrain sous des pièces
//      Stairs_Exterior_Straight, jusqu'à un palier et une PORTE fermée (façade de pierre, porte
//      ronde, rosace) ; la porte du SO, au bout du chemin des échoppes, est « la » porte de la BD ;
//   5. cinq TENTES PYRAMIDALES au pied du rocher (l'une est le temple) ;
//   4. l'« océan de tentes » : quinze rangs de tentes de pèlerins (Tent_Pilgrim_*) en anneaux
//      concentriques, du chemin de ronde jusqu'à ≈ 425 m, fanions, feux et bagages ;
//   6. une vingtaine de MENHIRS bleus sur un grand cercle (≈ 440-540 m) ;
//   3. au SUD-OUEST, le chemin d'arrivée (Passe des Dragons) bordé des échoppes des faux devins ;
//   9. les services du camp, en tentes et en étals : auberge (grand pavillon), potions, armes,
//      armures, temple, cimetière, cinq écuries (enclos).
// Les routes du lore qui arrivaient au centre s'arrêtent au chemin de ronde du pied du rocher.
//
// v1.0.141 (demande du propriétaire du 25/11/2026, planches T1-P003..005, T2-P044..046,
// T3-P005..007) :
//   - rocher en CRISTAL BLEU : couche de cristal du terrain (zone.json crystal_layer, rendu
//     client) et dalles de cristal maillées sur les parois et la couronne ;
//   - huit grandes portes réinterprétées (Oracle_Door_Grand : vantaux peints, croissant, sans
//     visage) sur socle de brique (Oracle_Door_Podium) ; escaliers ramenés à 6 m de large ;
//   - SALLE DE L'ORACLE creusée au cœur du rocher (sol au niveau des portes) : parois
//     sculptées, colonnes, dais gravé, colonne de brume figée et de lumière, chaînes, voûte
//     de cristal ; on y entre par la porte SO (passage conditionnel : quête
//     « L'Appel de l'Oracle ») ;
//   - SALLE DES ÉCHOS sous la facette est : raid (Gardien des Échos et échos de cristal),
//     entrée par la porte E (quête « L'Épreuve des Échos » et groupe d'au moins 3) ;
//   - PNJ de quête : veilleur de la grande porte (dehors), prêtresse du dais (dedans) ;
//   - camp extérieur : tentes de couleur, mâts à fanions, feux éclairés la nuit, bassins
//     des pèlerins (eau) et abreuvoirs, menhirs bleus ramenés à 3-5 m (T2-P044).
// Les portes restent fermées au rendu : c'est le shard qui fait passer (passages.json).
// Tentes de pèlerins modélisées (atelier décors, 23/11/2026) ; pavillon de l'auberge, échoppes et
// tentes pyramidales en panneaux Overhang_Roof_Plaster inclinés (deux ou quatre pans). Pas de matériau bleu : roche de l'Oracle en couches « roche » et « neige » (pâle)
// du splat, lumières ponctuelles bleues (portes, flancs, couronne, menhirs).

#include "WorldGenContext.h"
#include "Random.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace tools::world_gen
{
	namespace
	{
		constexpr double kPi = 3.14159265358979323846;
		constexpr uint8_t kSolid = 1u, kPiece = 2u, kOccluder = 4u; ///< bits de ScatterInstance::flags

		// --- Plan de l'Oracle (mètres, repère du rocher) ---------------------------------
		constexpr double kApothem = 150.0;   ///< demi-largeur du rocher octogonal (plan : ≈ 300 m)
		constexpr double kWallH = 62.0;      ///< hauteur des parois au-dessus de la plaine
		constexpr double kWallRun = 18.0;    ///< emprise horizontale d'une paroi (≈ 74°)
		constexpr double kCrownH = 28.0;     ///< couronne à facettes (sommet ≈ 90 m)
		constexpr double kDoorH = 22.0;      ///< palier des portes au-dessus de la plaine
		constexpr double kLandOut = 4.0;     ///< bord extérieur du palier (s, m, depuis le pied)
		constexpr double kLandIn = -10.0;    ///< fond de l'entaille du palier (s, m ; v1.0.141 : -16 -> -10, paroi
		                                     ///< d'au moins 10 m entre le palier E et la salle des Échos)
		constexpr double kLandHalfW = 11.0;  ///< demi-largeur du palier
		constexpr double kStairRun = 44.0;   ///< longueur de l'escalier (pente 1/2 : 2 m par pièce de 4 m)
		constexpr double kStairHalfW = 3.0;  ///< demi-largeur de l'escalier (v1.0.141 : 6 m de large, 2 pièces de 3 m, T2-P046)
		constexpr double kDoorS = -9.4;      ///< plan de la grande porte (s, m ; dos contre le fond de l'entaille)
		constexpr double kPodiumS = -3.0;    ///< centre du socle de brique (s, m ; 12 m de profondeur)
		constexpr double kPassInS = -6.5;    ///< déclencheur du passage d'entrée, devant la porte (s, m)
		constexpr double kPassOutS = 0.5;    ///< arrivée du passage de sortie, sur le palier (s, m)
		// --- v1.0.141 : salles intérieures (repère du rocher, m ; sol = palier des portes)
		constexpr double kHallR = 50.0;      ///< salle de l'Oracle : cratère du relief (rayon)
		constexpr double kHallWallR = 44.5;  ///< parois sculptées (Oracle_Hall_Wall, face au centre)
		constexpr int kHallPanels = 16;      ///< panneaux de paroi (et colonnes aux joints)
		constexpr double kHallVaultY = 40.0; ///< naissance de la voûte au-dessus du sol
		constexpr int kEchoFacet = 0;        ///< salle des Échos sous la facette est
		constexpr double kEchoD = 94.0;      ///< distance du centre de la salle des Échos
		constexpr double kEchoR = 28.0;      ///< salle des Échos : cratère du relief (rayon)
		constexpr double kEchoScale = 0.54;  ///< échelle des parois et colonnes (rayon 24 m, 21,6 m de haut)
		constexpr double kEchoVaultScale = 0.58; ///< voûte (rayon 30 m, naissance à 21 m)
		constexpr double kInDoorR = 42.5;    ///< porte intérieure de la salle de l'Oracle (rayon)
		constexpr double kInPassR = 40.5;    ///< déclencheur de sortie (rayon)
		constexpr double kInArriveR = 35.0;  ///< arrivée dans la salle de l'Oracle (rayon)
		constexpr double kPondR = 618.0;     ///< bassins des pèlerins (hors emprise du camp)
		constexpr double kRingR = kOracleRoadStopM; ///< chemin de ronde au pied du rocher (rayon, m)
		constexpr double kAlleyR = 295.0;    ///< allée entre les deux anneaux de tentes
		constexpr double kPlainR = 650.0;    ///< plaine aplanie (rayon, m)
		constexpr double kBlendR = 1100.0;   ///< raccord avec le relief naturel
		constexpr double kCampR = 600.0;     ///< emprise du camp (marque urbaine : pas de végétation)
		constexpr int kMainFacet = 3;        ///< facette SO (angle 135° : ouest et sud) : « la » porte de la BD

		double YawFacing(Vec2 f) { return std::atan2(-f.x, -f.z) * 180.0 / kPi; }   ///< axe -Z local vers f
		double YawAlong(Vec2 d) { return std::atan2(-d.z, d.x) * 180.0 / kPi; }     ///< axe X local vers d
		Vec2 Dir(double a) { return { std::cos(a), std::sin(a) }; }
		double Dot(Vec2 a, Vec2 b) { return a.x * b.x + a.z * b.z; }
		double Len(Vec2 a) { return std::hypot(a.x, a.z); }
		/// Normale extérieure de la facette k (k·45°, X = est, Z = sud : 0 = E, 2 = S, 4 = O, 6 = N).
		Vec2 FacetN(int k) { return Dir(k * kPi / 4.0); }
		/// Direction latérale de la facette k (normale tournée d'un quart de tour).
		Vec2 FacetT(int k) { const Vec2 n = FacetN(k); return { -n.z, n.x }; }

		/// Forme résolue du rocher : centre, altitude de la plaine, apothème de chaque facette
		/// (± 7 m autour de kApothem : octogone « à peu près » régulier, comme sur le plan).
		struct OracleShape
		{
			Vec2 o;
			double plainY = 0.0;
			double r[8] = {};
		};

		/// Relief du rocher en \p q (vecteur depuis le centre, m) : hauteur au-dessus de la plaine
		/// (parois planes de l'octogone, couronne à huit facettes tournée de 22,5°, paliers entaillés
		/// et rampes des huit escaliers). \p paint reçoit 0 (plaine), 1 (roche de l'Oracle) ou
		/// 2 (escalier ou palier blanc). Fonction pure.
		double RockRelief(const OracleShape& S, Vec2 q, uint8_t& paint)
		{
			// v1.0.141 — salles creusées au niveau des paliers (cratères couverts par les
			// voûtes maillées) : dallage (peinture 3), parois verticales du relief autour.
			if (Len(q) < kHallR || Len(q - FacetN(kEchoFacet) * kEchoD) < kEchoR) { paint = 3; return kDoorH; }
			double d8 = -1e18, d8r = -1e18;
			for (int k = 0; k < 8; ++k)
			{
				d8 = std::max(d8, Dot(q, FacetN(k)) - (S.r[k] - kApothem));
				d8r = std::max(d8r, Dot(q, Dir((k + 0.5) * kPi / 4.0)));
			}
			const double wall = Clamp((kApothem - d8) / kWallRun, 0.0, 1.0);
			const double crown = kCrownH * Clamp(1.0 - d8r / (kApothem - kWallRun), 0.0, 1.0) * wall;
			double h = kWallH * wall + crown;
			paint = h > 0.3 ? 1 : 0;
			for (int k = 0; k < 8; ++k)
			{
				const double s = Dot(q, FacetN(k)) - S.r[k], l = std::fabs(Dot(q, FacetT(k)));
				if (l < kLandHalfW && s >= kLandIn && s <= kLandOut) { paint = 2; return kDoorH; } // palier
				if (s > kLandOut && s < kLandOut + kStairRun && l < kStairHalfW + 3.0)
				{
					double hr = kDoorH * (1.0 - (s - kLandOut) / kStairRun);
					if (l > kStairHalfW) hr *= (kStairHalfW + 3.0 - l) / 3.0; // flancs de la rampe
					if (hr > h) { h = hr; paint = l <= kStairHalfW + 0.5 ? 2 : 1; }
				}
			}
			return h;
		}

		/// Hauteur de la rampe de l'escalier (m au-dessus de la plaine) à l'abscisse \p s.
		double RampH(double s) { return kDoorH * (1.0 - (s - kLandOut) / kStairRun); }

		/// Pose un décor de meshes/props/ (\p y : décalage vertical au-dessus du sol, \p tilt :
		/// rotation autour de l'axe X local, levant le bout -Z). Effet de bord : w.scatter.
		void Prop(World& w, const char* mesh, Vec2 p, double yaw, double scale, uint8_t flags, uint8_t cull,
			double collisionR = 0.0, double y = 0.0, double tilt = 0.0)
		{
			ScatterDef s;
			s.mesh = std::string("meshes/props/") + mesh + ".gltf";
			s.position = p; s.yawDeg = yaw; s.scale = scale; s.flags = flags; s.cullClass = cull;
			s.collisionRadius = collisionR; s.yOffset = y; s.tiltDeg = tilt;
			w.scatter.push_back(std::move(s));
		}

		/// Lumière ponctuelle (\p type « streetlamp » : fixe ; « torch » : flamme), \p y absolu.
		/// Effet de bord : w.pointLights.
		void Light(World& w, const std::string& id, Vec2 p, double y, double radius, double intensity,
			double r, double g, double b, const char* type = "streetlamp")
		{
			PointLightDef L;
			L.id = id; L.type = type; L.position = p; L.y = y; L.radiusM = radius; L.intensity = intensity;
			L.r = r; L.g = g; L.b = b;
			w.pointLights.push_back(std::move(L));
		}
		/// Bleu froid de la roche de l'Oracle (« colossale muraille bleue la nuit »).
		void BlueLight(World& w, const std::string& id, Vec2 p, double y, double radius, double intensity)
		{
			Light(w, id, p, y, radius, intensity, 0.32, 0.55, 1.0);
		}

		/// Tente à deux pans de \p pairs paires de panneaux Overhang_Roof_Plaster (2·s m de côté,
		/// inclinés de \p thetaDeg) le long du faîtage \p u, centrée en \p t. Le premier panneau
		/// porte la collision (cylindre) si \p solid. Effet de bord : w.scatter.
		/// \return point d'entrée (bout du faîtage côté -u, 1,2 m devant la toile).
		Vec2 RidgeTent(World& w, Vec2 t, Vec2 u, double s, int pairs, double thetaDeg, bool solid)
		{
			const Vec2 v{ -u.z, u.x };
			const double th = thetaDeg * kPi / 180.0;
			for (int p = 0; p < pairs; ++p)
			{
				const double along = (p - (pairs - 1) * 0.5) * 2.0 * s;
				for (int side = -1; side <= 1; side += 2)
				{
					const Vec2 c = t + u * along + v * (side * s * std::cos(th));
					const bool first = solid && p == 0 && side == -1;
					Prop(w, "Overhang_Roof_Plaster", c, YawFacing(v * static_cast<double>(-side)), s,
						first ? kSolid : 0u, 1, first ? s * std::cos(th) * 1.6 : 0.0, s * std::sin(th) + 0.2 * s - 0.05, thetaDeg);
				}
			}
			return t - u * (pairs * s + 1.2);
		}

		/// Tente pyramidale : quatre panneaux inclinés à 60° (base carrée de 2·s m, sommet à
		/// ≈ 1,73·s m), orientée par \p yaw0 (radians). Effet de bord : w.scatter.
		void PyramidTent(World& w, Vec2 t, double yaw0, double s, bool solid)
		{
			const double th = 60.0 * kPi / 180.0;
			for (int i = 0; i < 4; ++i)
			{
				const Vec2 v = Dir(yaw0 + i * kPi / 2.0);
				Prop(w, "Overhang_Roof_Plaster", t + v * (s * std::cos(th)), YawFacing(v * -1.0), s,
					(solid && i == 0) ? kSolid : 0u, 0, (solid && i == 0) ? s * 1.1 : 0.0, s * std::sin(th) + 0.2 * s - 0.05, 60.0);
			}
		}

		/// Étal (Stall_Empty, ou charrette Stall_Cart_Empty si \p cart) dont le comptoir regarde
		/// \p f. Effet de bord : w.scatter, occupation. \return place du marchand devant l'étal.
		Vec2 Stall(World& w, Vec2 p, Vec2 f, bool cart)
		{
			Prop(w, cart ? "Stall_Cart_Empty" : "Stall_Empty", p, YawFacing(f * -1.0), 1.0, kSolid, 0, 1.0);
			w.OccupyRect(p, cart ? 2.3 : 1.2, 0.9, YawFacing(f));
			return p + f * 1.6;
		}

		/// Distance (m) au bord de la chaussée la plus proche en \p p (1e9 hors grille).
		double RoadGap(const World& w, Vec2 p)
		{
			const int i = w.roadDist.IndexX(p.x), j = w.roadDist.IndexZ(p.z);
			return w.roadDist.InBounds(i, j) ? w.roadDist.At(i, j) : 1e9;
		}

		/// Inscrit une chaussée dans w.roadDist (distance au bord, comme FlattenRoads), sans
		/// toucher au relief. Effet de bord : w.roadDist.
		void StampRoad(World& w, const Road& r)
		{
			const double half = r.widthM * 0.5, reach = half + 9.0;
			for (size_t k = 0; k + 1 < r.points.size(); ++k)
			{
				const Vec2 a = r.points[k], b = r.points[k + 1];
				const int i0 = std::max(0, w.roadDist.IndexX(std::min(a.x, b.x) - reach)), i1 = std::min(w.roadDist.width - 1, w.roadDist.IndexX(std::max(a.x, b.x) + reach));
				const int j0 = std::max(0, w.roadDist.IndexZ(std::min(a.z, b.z) - reach)), j1 = std::min(w.roadDist.height - 1, w.roadDist.IndexZ(std::max(a.z, b.z) + reach));
				for (int j = j0; j <= j1; ++j)
					for (int i = i0; i <= i1; ++i)
					{
						const float d = static_cast<float>(DistPointSegment({ w.roadDist.WorldX(i), w.roadDist.WorldZ(j) }, a, b) - half);
						w.roadDist.At(i, j) = std::min(w.roadDist.At(i, j), d);
					}
			}
		}

		/// Sculpte la plaine et le rocher dans la heightmap (rayon kPlainR, raccord jusqu'à
		/// kBlendR), peint la roche et les escaliers (w.paint, lu par le splat) et marque
		/// l'emprise du camp (w.urban = 1 : pas de végétation). Effet de bord : w.height,
		/// w.paint, w.urban.
		void ShapeTerrain(World& w, const OracleShape& S)
		{
			if (w.paint.data.empty())
				w.paint.Init(w.height.width, w.height.height, w.height.originX, w.height.originZ, w.height.step, uint8_t(0));
			const int i0 = std::max(0, w.height.IndexX(S.o.x - kBlendR)), i1 = std::min(w.height.width - 1, w.height.IndexX(S.o.x + kBlendR));
			const int j0 = std::max(0, w.height.IndexZ(S.o.z - kBlendR)), j1 = std::min(w.height.height - 1, w.height.IndexZ(S.o.z + kBlendR));
			for (int j = j0; j <= j1; ++j)
				for (int i = i0; i <= i1; ++i)
				{
					const Vec2 q{ w.height.WorldX(i) - S.o.x, w.height.WorldZ(j) - S.o.z };
					const double d = Len(q);
					if (d >= kBlendR) continue;
					double& h = w.height.At(i, j);
					if (d < kPlainR)
					{
						uint8_t paint = 0;
						h = S.plainY + RockRelief(S, q, paint);
						w.paint.At(i, j) = paint;
					}
					else
						h = Lerp(S.plainY, h, SmoothStep(kPlainR, kBlendR, d));
					if (d < kCampR && w.urban.InBounds(i, j) && w.urban.At(i, j) == 0) w.urban.At(i, j) = 1;
				}
		}

		/// Coupe les routes au chemin de ronde (rayon \p cutR) : les points intérieurs sont
		/// retirés, une route qui traversait devient deux tronçons, chacun terminé par un point
		/// exactement sur le cercle ; profils recalés sur la plaine. Les distances à la chaussée
		/// sont effacées à l'intérieur du cercle. Effet de bord : w.roads, w.roadDist.
		/// \return angles (radians) des points de raccord sur le cercle (sorties du camp).
		std::vector<double> CutRoads(World& w, const OracleShape& S, double cutR)
		{
			std::vector<double> exits;
			std::vector<Road> out;
			for (Road& r : w.roads)
			{
				bool inside = false;
				for (const Vec2& p : r.points) inside = inside || Len(p - S.o) < cutR;
				if (!inside) { out.push_back(std::move(r)); continue; }
				const size_t n = r.points.size();
				size_t k = 0;
				while (k < n)
				{
					while (k < n && Len(r.points[k] - S.o) < cutR) ++k;
					const size_t a = k;
					while (k < n && Len(r.points[k] - S.o) >= cutR) ++k;
					const size_t b = k; // [a, b) dehors
					if (b - a < 2) continue;
					Road piece = r;
					piece.points.assign(r.points.begin() + static_cast<long>(a), r.points.begin() + static_cast<long>(b));
					piece.profileY.assign(r.profileY.begin() + static_cast<long>(a), r.profileY.begin() + static_cast<long>(b));
					// Raccord exact sur le cercle (côté où la route entrait dans le camp).
					auto junction = [&](size_t outIdx, size_t inIdx) {
						const Vec2 po = r.points[outIdx], pi = r.points[inIdx];
						const double dOut = Len(po - S.o), dIn = Len(pi - S.o);
						const double t = (dOut - cutR) / std::max(1e-6, dOut - dIn);
						return po + (pi - po) * t;
					};
					if (a > 0)
					{
						const Vec2 j = junction(a, a - 1);
						piece.points.insert(piece.points.begin(), j);
						piece.profileY.insert(piece.profileY.begin(), S.plainY);
						exits.push_back(std::atan2(j.z - S.o.z, j.x - S.o.x));
					}
					if (b < n)
					{
						const Vec2 j = junction(b - 1, b);
						piece.points.push_back(j);
						piece.profileY.push_back(S.plainY);
						exits.push_back(std::atan2(j.z - S.o.z, j.x - S.o.x));
					}
					for (size_t q = 0; q < piece.points.size(); ++q)
						if (Len(piece.points[q] - S.o) < kBlendR) piece.profileY[q] = w.HeightAt(piece.points[q]);
					out.push_back(std::move(piece));
				}
			}
			w.roads = std::move(out);
			// Plus de chaussée sur le rocher ni au pied (le chemin de ronde est réinscrit ensuite).
			const int i0 = std::max(0, w.roadDist.IndexX(S.o.x - cutR)), i1 = std::min(w.roadDist.width - 1, w.roadDist.IndexX(S.o.x + cutR));
			const int j0 = std::max(0, w.roadDist.IndexZ(S.o.z - cutR)), j1 = std::min(w.roadDist.height - 1, w.roadDist.IndexZ(S.o.z + cutR));
			for (int j = j0; j <= j1; ++j)
				for (int i = i0; i <= i1; ++i)
					if (std::hypot(w.roadDist.WorldX(i) - S.o.x, w.roadDist.WorldZ(j) - S.o.z) < cutR - 1.0) w.roadDist.At(i, j) = 1e9f;
			return exits;
		}

		/// Graphe de déplacement des PNJ (copie locale des aides de Settlements.cpp).
		int AddNode(SettlementLayout& L, Vec2 p) { L.nodes.push_back({ p, {} }); return static_cast<int>(L.nodes.size()) - 1; }
		void Link(SettlementLayout& L, int a, int b)
		{
			if (a == b || a < 0 || b < 0) return;
			auto& la = L.nodes[static_cast<size_t>(a)].links;
			if (std::find(la.begin(), la.end(), b) == la.end()) la.push_back(b);
			auto& lb = L.nodes[static_cast<size_t>(b)].links;
			if (std::find(lb.begin(), lb.end(), a) == lb.end()) lb.push_back(a);
		}
		/// Noeud le plus proche de \p p parmi \p ids (-1 si vide).
		int NearestOf(const SettlementLayout& L, const std::vector<int>& ids, Vec2 p)
		{
			int best = -1; double bd = 1e18;
			for (int id : ids)
			{
				const double d = Len(L.nodes[static_cast<size_t>(id)].p - p);
				if (d < bd) { bd = d; best = id; }
			}
			return best;
		}
		/// Chaîne de noeuds tous les ~9 m le long de \p pts, reliée à \p from. \return dernier noeud.
		int Chain(SettlementLayout& L, const std::vector<Vec2>& pts, int from)
		{
			int prev = from;
			for (const Vec2& p : ResamplePolyline(pts, 9.0))
			{
				if (prev >= 0 && Len(L.nodes[static_cast<size_t>(prev)].p - p) < 4.0) continue;
				const int id = AddNode(L, p);
				Link(L, prev, id);
				prev = id;
			}
			return prev;
		}
	}

	void BuildOracleCamp(World& w, CityDef& c, SettlementLayout& L)
	{
		Rng rng(Hash(w.master.seed, HashString(c.id.c_str()), 1136));
		OracleShape S;
		S.o = c.center;
		S.plainY = c.plateauY;
		for (int k = 0; k < 8; ++k) S.r[k] = kApothem + rng.Range(-7.0, 7.0);
		const double aMain = kMainFacet * kPi / 4.0;
		const Vec2 o = S.o;

		// --- 1. Plaine, rocher, escaliers (terrain), puis routes arrêtées au pied du rocher.
		ShapeTerrain(w, S);
		std::vector<double> exits = CutRoads(w, S, kRingR);
		// Chemin de ronde et chemin d'arrivée du sud-ouest (sentiers locaux, sans voyageurs).
		Road ring;
		ring.fromId = ring.toId = c.id; ring.widthM = 4.0; ring.path = true;
		const int ringPts = static_cast<int>(std::lround(2.0 * kPi * kRingR / 4.0));
		for (int k = 0; k <= ringPts; ++k) { ring.points.push_back(o + Dir(k * 2.0 * kPi / ringPts) * kRingR); ring.profileY.push_back(S.plainY); }
		Road sw;
		sw.fromId = c.id; sw.widthM = 4.0; sw.path = true;
		const Vec2 swDir = Dir(aMain), swSide{ -swDir.z, swDir.x };
		auto swAt = [&](double along) { // axe du chemin (légèrement sinueux au-delà des échoppes)
			const double wob = along > 420.0 ? 22.0 * std::sin((along - 420.0) / 140.0) : 0.0;
			return o + swDir * (kRingR + along) + swSide * wob;
		};
		for (double a = 0.0; a <= 860.0; a += 4.0) { sw.points.push_back(swAt(a)); sw.profileY.push_back(w.HeightAt(swAt(a))); }
		StampRoad(w, ring);
		StampRoad(w, sw);
		w.roads.push_back(ring);
		w.roads.push_back(sw);
		exits.push_back(aMain);
		// Rocher et pied d'escaliers réservés (pas de végétation, pas de tente).
		w.Occupy(o, kApothem / std::cos(kPi / 8.0) + 8.0);
		for (int k = 0; k < 8; ++k)
			w.OccupyRect(o + FacetN(k) * (S.r[k] + 24.0), kLandHalfW + 1.0, 28.0, YawFacing(FacetN(k)));

		// --- 7. Huit escaliers blancs et leurs grandes portes (v1.0.141 : escaliers de 6 m,
		//     socle de brique rouge sombre, grande porte peinte au fond de l'entaille ; T2-P046).
		for (int k = 0; k < 8; ++k)
		{
			const Vec2 n = FacetN(k), t = FacetT(k);
			const double stairYaw = YawFacing(n * -1.0); // l'escalier monte vers -Z local
			for (int i = 0; i < 15; ++i)
			{
				const double s = kLandOut + 1.5 + 3.0 * i;
				for (int col = -1; col <= 1; col += 2)
				{
					const Vec2 p = o + n * (S.r[k] + s) + t * (col * 1.5);
					Prop(w, "Stairs_Exterior_Straight", p, stairYaw, 1.5, 0, 0, 0.0, S.plainY + RampH(s) - 0.75 - w.HeightAt(p));
				}
			}
			// Socle : dessus à 0,2 m au-dessus du palier (marches avant vers l'escalier),
			// sans collision (on le traverse comme une dalle).
			const Vec2 podium = o + n * (S.r[k] + kPodiumS);
			Prop(w, "Oracle_Door_Podium", podium, YawFacing(n), 1.0, 0, 0, 0.0, S.plainY + kDoorH - 1.0 - w.HeightAt(podium));
			// Grande porte (face peinte vers l'extérieur), dos contre la paroi de cristal.
			const Vec2 door = o + n * (S.r[k] + kDoorS);
			Prop(w, "Oracle_Door_Grand", door, YawFacing(n), 1.0, kSolid, 0, 4.6, S.plainY + kDoorH + 0.2 - w.HeightAt(door));
			// Amas de cristaux de part et d'autre de la porte, lueur bleue du palier.
			for (int side = -1; side <= 1; side += 2)
			{
				const Vec2 cp = o + n * (S.r[k] + kDoorS + 0.5) + t * (side * 8.2);
				Prop(w, (k + side) % 2 ? "Oracle_Crystal_Cluster_1" : "Oracle_Crystal_Cluster_2", cp, rng.Range(0.0, 360.0), 1.0,
					kSolid, 0, 1.6, S.plainY + kDoorH - 0.2 - w.HeightAt(cp));
			}
			BlueLight(w, c.id + "_porte_" + std::to_string(k), o + n * (S.r[k] - 2.0), S.plainY + kDoorH + 6.0, 30.0, 4.0);
		}
		// Lueur bleue des flancs (entre les escaliers) et de la couronne : la muraille bleue de nuit.
		for (int k = 0; k < 8; ++k)
		{
			const double a = (k + 0.5) * kPi / 4.0;
			BlueLight(w, c.id + "_flanc_" + std::to_string(k), o + Dir(a) * (kApothem / std::cos(kPi / 8.0) + 22.0), S.plainY + 38.0, 130.0, 2.4);
		}
		BlueLight(w, c.id + "_couronne", o, S.plainY + kWallH + kCrownH + 25.0, 200.0, 3.0);
		// Couronne (v1.0.141) : dalles de cristal géantes sur les arêtes (T2-P044, T3-P005) ;
		// parois : dalles au pied, de part et d'autre des escaliers ; éboulis entre les escaliers.
		static const char* kSlabs[3] = { "Oracle_Crystal_Slab_1", "Oracle_Crystal_Slab_2", "Oracle_Crystal_Slab_3" };
		for (int k = 0; k < 8; ++k)
		{
			const double a = k * kPi / 4.0 + rng.Range(-0.15, 0.15);
			const Vec2 p = o + Dir(a) * (kApothem - kWallRun - 10.0);
			static const char* kRocks[] = { "Rock_Medium_1", "Rock_Medium_2", "Rock_Medium_3" };
			Prop(w, kSlabs[k % 3], p, rng.Range(0.0, 360.0), rng.Range(0.8, 1.1), 0, 0, 0.0, -3.0);
			// Arête suivante (entre deux portes) : deux dalles de plus sur la couronne.
			for (int q = 0; q < 2; ++q)
			{
				const Vec2 pc = o + Dir((k + 0.5) * kPi / 4.0 + (q ? 0.1 : -0.1)) * rng.Range(95.0, 120.0);
				Prop(w, kSlabs[(k + q + 1) % 3], pc, rng.Range(0.0, 360.0), rng.Range(0.7, 1.0), 0, 0, 0.0, -2.0);
			}
			// Pied des parois : deux dalles de chaque côté de l'escalier (hors des flancs de la rampe).
			const Vec2 n = FacetN(k), t = FacetT(k);
			for (int side = -1; side <= 1; side += 2)
				for (int q = 0; q < 2; ++q)
				{
					const Vec2 pf = o + n * (S.r[k] + rng.Range(-1.0, 2.0)) + t * (side * (24.0 + 22.0 * q + rng.Range(-3.0, 3.0)));
					Prop(w, kSlabs[(k + q) % 3], pf, rng.Range(0.0, 360.0), rng.Range(0.6, 0.95), kSolid, 0, 0.0, -1.5);
				}
			for (int q = 0; q < 2; ++q)
			{
				const double b = (k + 0.5) * kPi / 4.0 + (q ? 0.12 : -0.12) + rng.Range(-0.04, 0.04);
				const Vec2 f = o + Dir(b) * (kApothem / std::cos(kPi / 8.0) + rng.Range(4.0, 12.0));
				Prop(w, kRocks[(k + q) % 3], f, rng.Range(0.0, 360.0), rng.Range(2.2, 3.6), kSolid, 0, 3.0, -0.4);
			}
		}

		// --- 5. Tentes pyramidales au pied du rocher (l'une, près de la grande porte, est le temple).
		{
			static const int kGaps[5] = { 3, 0, 2, 5, 6 }; // sommets de l'octogone (k + 0,5)·45°
			for (int g = 0; g < 5; ++g)
			{
				const double a = (kGaps[g] + 0.5) * kPi / 4.0;
				const Vec2 p = o + Dir(a) * 190.0;
				PyramidTent(w, p, a + kPi / 4.0, g == 0 ? 4.2 : 3.4, true);
				w.Occupy(p, g == 0 ? 6.5 : 5.5);
				if (g == 0) // temple : autel et cierges devant l'entrée (côté chemin de ronde)
				{
					const Vec2 out = Dir(a);
					Prop(w, "Table_Large", p + out * 0.5, YawFacing(out), 1.0, kSolid, 1, 1.2);
					Prop(w, "Chalice", p + out * 0.5, 0.0, 1.0, 0, 2, 0.0, 0.81);
					for (int side = -1; side <= 1; side += 2)
						Prop(w, "CandleStick_Stand", p + out * 5.6 + Vec2{ -out.z, out.x } * (side * 2.2), 0.0, 1.2, 0, 1);
				}
			}
		}

		// --- Graphe des PNJ : parvis de la grande porte (noeud 0), chemin de ronde, escaliers.
		const Vec2 parvis = o + swDir * kRingR;
		AddNode(L, parvis);
		for (int k = 0; k < 8; ++k)
		{
			const int id = AddNode(L, parvis + Dir(k * kPi / 4.0 + 0.2) * 8.0);
			L.plazaRing.push_back(id);
			Link(L, 0, id);
		}
		for (size_t k = 0; k < L.plazaRing.size(); ++k) Link(L, L.plazaRing[k], L.plazaRing[(k + 1) % L.plazaRing.size()]);
		std::vector<int> ringIds;
		const int ringNodes = static_cast<int>(std::lround(2.0 * kPi * kRingR / 9.0));
		for (int k = 0; k < ringNodes; ++k) ringIds.push_back(AddNode(L, o + Dir(k * 2.0 * kPi / ringNodes) * kRingR));
		for (size_t k = 0; k < ringIds.size(); ++k) Link(L, ringIds[k], ringIds[(k + 1) % ringIds.size()]);
		for (int id : L.plazaRing) Link(L, id, NearestOf(L, ringIds, L.nodes[static_cast<size_t>(id)].p));
		for (int k = 0; k < 8; ++k) // pèlerins montant aux portes
		{
			const Vec2 n = FacetN(k);
			const int foot = NearestOf(L, ringIds, o + n * kRingR);
			Chain(L, { L.nodes[static_cast<size_t>(foot)].p, o + n * (S.r[k] + kLandOut + kStairRun + 2.0), o + n * (S.r[k] + 1.0) }, foot);
		}
		// Allée entre les anneaux de tentes, reliée au chemin de ronde par huit traverses.
		std::vector<int> alleyIds;
		const int alleyNodes = static_cast<int>(std::lround(2.0 * kPi * kAlleyR / 9.0));
		for (int k = 0; k < alleyNodes; ++k) alleyIds.push_back(AddNode(L, o + Dir(k * 2.0 * kPi / alleyNodes) * kAlleyR));
		for (size_t k = 0; k < alleyIds.size(); ++k) Link(L, alleyIds[k], alleyIds[(k + 1) % alleyIds.size()]);
		Road alley; // marque l'allée dans la carte des chaussées (tentes écartées) sans l'ajouter aux routes
		alley.widthM = 3.0;
		for (int k = 0; k <= alleyNodes; ++k) alley.points.push_back(o + Dir(k * 2.0 * kPi / alleyNodes) * kAlleyR);
		StampRoad(w, alley);
		for (int k = 0; k < 8; ++k)
		{
			const double a = (k + 0.5) * kPi / 4.0;
			const int from = NearestOf(L, ringIds, o + Dir(a) * kRingR);
			const int last = Chain(L, { L.nodes[static_cast<size_t>(from)].p, o + Dir(a) * kAlleyR }, from);
			Link(L, last, NearestOf(L, alleyIds, o + Dir(a) * kAlleyR));
			Road cross;
			cross.widthM = 3.0;
			cross.points = { o + Dir(a) * kRingR, o + Dir(a) * kAlleyR };
			StampRoad(w, cross);
		}
		// Routes arrivant au camp : noeuds jusqu'à la sortie de l'océan de tentes.
		for (const Road& r : w.roads)
		{
			if (r.path && r.fromId == c.id) continue; // chemin de ronde, chemin du sud-ouest
			std::vector<Vec2> pts;
			bool fromRing = false;
			if (!r.points.empty() && std::fabs(Len(r.points.front() - o) - kRingR) < 1.0) { fromRing = true; for (const Vec2& p : r.points) { if (Len(p - o) > 380.0) break; pts.push_back(p); } }
			else if (!r.points.empty() && std::fabs(Len(r.points.back() - o) - kRingR) < 1.0) { fromRing = true; for (size_t q = r.points.size(); q-- > 0;) { if (Len(r.points[q] - o) > 380.0) break; pts.push_back(r.points[q]); } }
			if (!fromRing || pts.size() < 2) continue;
			const int from = NearestOf(L, ringIds, pts.front());
			int prev = from;
			for (const Vec2& p : ResamplePolyline(pts, 9.0))
			{
				const int id = AddNode(L, p);
				Link(L, prev, id);
				if (std::fabs(Len(p - o) - kAlleyR) < 6.0) Link(L, id, NearestOf(L, alleyIds, p));
				prev = id;
			}
		}

		// --- 9. Services, autour du parvis de la grande porte (SO), hors du chemin des échoppes.
		// Auberge de Tormar Belrade : grand pavillon (3 paires de toiles), entrée vers le rocher.
		{
			const double a = aMain + 0.085;
			const Vec2 u = Dir(a), p = o + u * 236.0;
			// v1.0.141 : pavillon agrandi (faîte à ≈ 4 m : retour du 25/11/2026, tentes trop basses).
			L.innDoor = RidgeTent(w, p, u, 3.4, 3, 50.0, true);
			w.OccupyRect(p, 5.0, 11.0, YawFacing(u));
			const Vec2 side{ -u.z, u.x };
			Prop(w, "Table_Large", L.innDoor + side * 3.4 + u * 0.6, YawFacing(side), 1.0, kSolid, 1, 1.2);
			Prop(w, "Bench", L.innDoor + side * 3.4 - u * 0.4, YawFacing(u), 1.0, kSolid, 1, 0.5);
			Prop(w, "Bench", L.innDoor + side * 3.4 + u * 1.6, YawFacing(u * -1.0), 1.0, kSolid, 1, 0.5);
			Prop(w, "Mug", L.innDoor + side * 3.6 + u * 0.6, 0.0, 1.0, 0, 2, 0.0, 0.81);
			Prop(w, "Barrel", L.innDoor - side * 2.6 + u * 1.0, 0.0, 1.0, kSolid, 1, 0.4);
			Prop(w, "Barrel", L.innDoor - side * 3.4 + u * 1.8, 40.0, 1.0, kSolid, 1, 0.4);
			Prop(w, "Cauldron", p - side * 5.0, 0.0, 1.3, kSolid, 1, 0.7);
			Prop(w, "Banner_1", p + u * 8.2, YawFacing(u), 1.0, 0, 1, 0.0, 4.2);
			Prop(w, "Torch_Metal", L.innDoor + side * 1.6, YawFacing(u), 1.0, 0, 1, 0.0, 1.4);
			Light(w, c.id + "_auberge", L.innDoor + side * 1.6, S.plainY + 1.9, 9.0, 1.8, 1.0, 0.55, 0.18, "torch");
			w.Occupy(L.innDoor + side * 3.4, 2.2);
		}
		// Potions, armes, armures : étals face au chemin de ronde.
		{
			struct Goods { const char* a; const char* b; const char* c2; bool cart; };
			static const Goods kGoods[3] = {
				{ "Shelf_Small_Bottles", "Potion_1", "Cauldron", false },  // potions (Gilaude Valcombe)
				{ "Sword_Bronze", "Shield_Wooden", "Axe_Bronze", true },  // armes (Varin Bleuchêne)
				{ "Shield_Wooden", "Crate_Metal", "Anvil", false },        // armures
			};
			for (int k = 0; k < 3; ++k)
			{
				const double a = aMain - 0.07 - 0.048 * k;
				const Vec2 u = Dir(a), p = o + u * 227.0, f = u * -1.0, side{ -u.z, u.x };
				L.stallFronts.push_back(Stall(w, p, f, kGoods[k].cart));
				Prop(w, kGoods[k].a, p + side * 1.6 + u * 0.3, YawFacing(f), 1.0, 0, 1);
				Prop(w, kGoods[k].b, p - side * (kGoods[k].cart ? 2.8 : 1.5) - u * 0.2, YawFacing(f), 1.0, 0, 2);
				Prop(w, kGoods[k].c2, p + side * 2.4 - u * 0.6, YawFacing(f), 1.0, kSolid, 1, 0.5);
			}
		}
		// Torches du parvis.
		for (int side = -1; side <= 1; side += 2)
		{
			const Vec2 tp = parvis + swSide * (side * 7.0) + swDir * 3.0;
			Prop(w, "Torch_Metal", tp, YawFacing(swDir * -1.0), 1.0, 0, 1, 0.0, 1.4);
			Light(w, c.id + "_parvis_" + std::to_string(side > 0 ? 1 : 0), tp, S.plainY + 1.9, 8.0, 1.6, 1.0, 0.55, 0.18, "torch");
		}

		// --- 3. Chemin du sud-ouest bordé des échoppes des faux devins (8 étals, 2 tentes).
		{
			static const char* kProps[] = { "CandleStick_Triple", "Book_5", "Scroll_1", "Pot_1", "Coin_Pile", "BookStand", "Potion_2", "Chest_Wood" };
			for (int k = 0; k < 8; ++k)
			{
				const double along = 150.0 + 35.0 * k;
				const int side = (k % 2) ? 1 : -1;
				const Vec2 axis = swAt(along);
				const Vec2 f = swSide * static_cast<double>(-side); // comptoir vers le chemin
				const Vec2 p = axis + swSide * (side * 7.5);
				L.stallFronts.push_back(Stall(w, p, f, k % 3 == 2));
				Prop(w, "Table_Large", p + f * 0.2 + swDir * 2.4, YawFacing(f), 0.7, kSolid, 1, 0.8);
				Prop(w, kProps[k], p + f * 0.2 + swDir * 2.4, rng.Range(0.0, 360.0), 1.0, 0, 2, 0.0, 0.57);
				Prop(w, (k % 2) ? "Banner_2" : "Banner_1", p - f * 0.6, YawFacing(f), 1.0, 0, 1, 0.0, 3.4);
				if (k % 2 == 0) Prop(w, "CandleStick_Stand", p + f * 1.2 - swDir * 1.6, 0.0, 1.0, 0, 1);
			}
			for (int k = 0; k < 2; ++k)
			{
				const double along = 168.0 + 140.0 * k;
				const int side = k ? -1 : 1;
				const Vec2 p = swAt(along) + swSide * (side * 9.5);
				const Vec2 door = RidgeTent(w, p, swSide * static_cast<double>(side), 2.6, 1, 55.0, true);
				w.OccupyRect(p, 3.4, 3.6, YawFacing(swSide));
				L.homeDoors.push_back(door);
			}
		}
		// Graphe : chemin du sud-ouest jusqu'à la dernière échoppe.
		{
			std::vector<Vec2> pts;
			for (double a = 0.0; a <= 430.0; a += 9.0) pts.push_back(swAt(a));
			Chain(L, pts, 0);
		}

		// --- Cimetière de Ralilde Sombremorne : hors des tentes, à l'ouest du chemin d'arrivée.
		{
			Vec2 gy = o + Dir(aMain + 0.95) * 398.0;
			for (int attempt = 0; attempt < 40; ++attempt)
			{
				gy = o + Dir(aMain + 0.95 + 0.11 * attempt) * (398.0 + (attempt % 3) * 12.0);
				if (w.InPlayableBounds(gy, 60.0) && !w.IsWater(gy) && !w.IsOccupied(gy, 7.0) && RoadGap(w, gy) > 10.0) break;
			}
			L.hasGraveyard = true;
			L.graveyard = gy;
			w.Occupy(gy, 9.0); // marge : l'océan de tentes est posé après
			for (int k = 0; k < 6; ++k)
			{
				const Vec2 p = gy + Dir(k * kPi / 3.0 + 0.3) * 3.0;
				Prop(w, (k % 2) ? "Pebble_Square_3" : "Rock_Medium_2", p, rng.Range(0.0, 360.0), (k % 2) ? 2.2 : 0.45, 0, 1);
			}
			Prop(w, "CandleStick_Stand", gy, 0.0, 1.0, 0, 1);
		}

		// --- Cinq écuries : enclos aux sorties du camp (routes, chemin du sud-ouest), complétés
		//     dans les plus grands intervalles d'angle.
		{
			std::vector<double> sites;
			for (double a : exits)
			{
				bool dup = false;
				for (double b : sites) dup = dup || std::fabs(std::remainder(a - b, 2.0 * kPi)) < 0.45;
				if (!dup && sites.size() < 5) sites.push_back(a);
			}
			while (sites.size() < 5)
			{
				std::vector<double> sorted = sites;
				std::sort(sorted.begin(), sorted.end(), [](double x, double y) { return std::remainder(x, 2.0 * kPi) < std::remainder(y, 2.0 * kPi); });
				double bestMid = 0.0, bestGap = -1.0;
				for (size_t k = 0; k < sorted.size(); ++k)
				{
					const double a0 = std::remainder(sorted[k], 2.0 * kPi);
					double a1 = k + 1 < sorted.size() ? std::remainder(sorted[k + 1], 2.0 * kPi) : std::remainder(sorted[0], 2.0 * kPi) + 2.0 * kPi;
					if (a1 - a0 > bestGap) { bestGap = a1 - a0; bestMid = 0.5 * (a0 + a1); }
				}
				if (sorted.empty()) bestMid = 0.0;
				sites.push_back(bestMid);
			}
			int made = 0;
			for (double a : sites)
			{
				for (int attempt = 0; attempt < 12; ++attempt)
				{
					const double lat = (attempt % 2 ? -1.0 : 1.0) * (16.0 + 6.0 * (attempt / 2));
					const Vec2 az = Dir(a), ax{ -az.z, az.x };
					const Vec2 fc = o + az * 376.0 + ax * lat;
					const double yaw = YawFacing(az);
					if (!w.InPlayableBounds(fc, 60.0) || RoadGap(w, fc) < 9.0 || w.IsOccupiedRect(fc, 7.5, 5.5, yaw)) continue;
					w.OccupyRect(fc, 7.5, 5.5, yaw);
					const double yawAx = YawAlong(ax), yawAz = YawAlong(az);
					const Vec2 toRoad = ax * (lat > 0 ? -1.0 : 1.0);
					for (double uu = -5.0; uu < 6.0; uu += 2.06)
					{
						Prop(w, "Prop_WoodenFence_Single", fc + ax * uu + az * 4.0, yawAx, 1.0, static_cast<uint8_t>(kSolid | kPiece), 1);
						Prop(w, "Prop_WoodenFence_Single", fc + ax * uu - az * 4.0, yawAx, 1.0, static_cast<uint8_t>(kSolid | kPiece), 1);
					}
					for (double vv = -3.0; vv < 4.0; vv += 2.06)
					{
						if (std::fabs(vv) > 1.2) // entrée côté route
							Prop(w, "Prop_WoodenFence_Single", fc + az * vv + toRoad * 6.0, yawAz, 1.0, static_cast<uint8_t>(kSolid | kPiece), 1);
						Prop(w, "Prop_WoodenFence_Single", fc + az * vv - toRoad * 6.0, yawAz, 1.0, static_cast<uint8_t>(kSolid | kPiece), 1);
					}
					Prop(w, "Barrel_Holder", fc - toRoad * 4.2 + az * 2.2, yawAz, 1.0, kSolid, 1, 0.6);
					Prop(w, "Bucket_Wooden_1", fc - toRoad * 4.2 - az * 1.6, 0.0, 1.0, 0, 2);
					Prop(w, "Bucket_Wooden_1", fc - toRoad * 3.6 - az * 2.4, 70.0, 1.0, 0, 2);
					Prop(w, "Prop_Wagon", fc + az * 8.0, yawAx, 1.0, kSolid, 1, 1.4);
					++made;
					break;
				}
			}
			if (made < 5) w.Warn("camp de l'Oracle : " + std::to_string(made) + " ecurie(s) posee(s) sur 5");
		}

		// --- 11. v1.0.141 — Eau (demande du propriétaire du 25/11/2026) : bassins des pèlerins
		//     dans la plaine, hors de l'emprise du camp (roseaux et nénuphars posés par les rives),
		//     abreuvoirs de pierre le long de l'allée entre les anneaux de tentes. Posés avant
		//     l'océan de tentes, qui les contourne.
		int ponds = 0, troughs = 0;
		{
			// Place réservée au veilleur de la grande porte (PNJ posé avec les salles, plus bas).
			w.Occupy(o + FacetN(kMainFacet) * (S.r[kMainFacet] + kLandOut + kStairRun + 5.0) + FacetT(kMainFacet) * 7.0, 3.0);
			for (int k = 0; k < 4; ++k)
			{
				const double R = rng.Range(14.0, 19.0);
				const double water = S.plainY - 0.25;
				bool placed = false;
				for (int attempt = 0; attempt < 10 && !placed; ++attempt)
				{
					const double a = aMain + kPi * 0.25 + k * kPi * 0.5 + (attempt % 2 ? 1.0 : -1.0) * 0.09 * (attempt / 2 + 1) * (attempt > 0);
					const Vec2 cpt = o + Dir(a) * (kPondR + rng.Range(-4.0, 4.0));
					if (!w.InPlayableBounds(cpt, R + 30.0) || RoadGap(w, cpt) < R + 14.0 || w.IsWater(cpt) || w.IsOccupied(cpt, R + 6.0)) continue;
					// Cuvette : fond parabolique sous l'eau, berge raccordée à la plaine sur 8 m.
					const int i0 = std::max(0, w.height.IndexX(cpt.x - R - 10.0)), i1 = std::min(w.height.width - 1, w.height.IndexX(cpt.x + R + 10.0));
					const int j0 = std::max(0, w.height.IndexZ(cpt.z - R - 10.0)), j1 = std::min(w.height.height - 1, w.height.IndexZ(cpt.z + R + 10.0));
					for (int j = j0; j <= j1; ++j)
						for (int i = i0; i <= i1; ++i)
						{
							const double d = std::hypot(w.height.WorldX(i) - cpt.x, w.height.WorldZ(j) - cpt.z);
							double& h = w.height.At(i, j);
							if (d < R) h = water - 1.6 * (1.0 - (d / R) * (d / R)) - 0.3;
							else if (d < R + 8.0) h = Lerp(water + 0.35, h, SmoothStep(R, R + 8.0, d));
						}
					Lake l;
					l.name = c.id + "_bassin_" + std::to_string(k);
					l.center = cpt; l.radiusM = R; l.waterY = water;
					for (int q = 0; q < 20; ++q) // contour CCW (water.bin), bord légèrement irrégulier
					{
						const double aq = q * 2.0 * kPi / 20.0, rr = R * (0.9 + 0.1 * rng.Next());
						l.polygon.push_back(cpt + Dir(aq) * rr);
					}
					w.lakes.push_back(l);
					w.Occupy(cpt, R + 6.0);
					// Abreuvoirs et seaux côté camp, pierres au bord de l'eau.
					const Vec2 in = (o - cpt) * (1.0 / std::max(1e-6, Len(o - cpt))), sd{ -in.z, in.x };
					for (int side = -1; side <= 1; side += 2)
					{
						const Vec2 tp = cpt + in * (R + 3.5) + sd * (side * 4.0);
						Prop(w, "Trough_Stone_1", tp, YawAlong(sd), 1.0, kSolid, 1, 0.9);
						Prop(w, "Bucket_Wooden_1", tp + in * 1.4, rng.Range(0.0, 360.0), 1.0, 0, 2);
						++troughs;
					}
					for (int q = 0; q < 5; ++q)
					{
						const Vec2 rp = cpt + Dir(rng.Range(0.0, 2.0 * kPi)) * (R - 0.5);
						Prop(w, q % 2 ? "Rock_Water_1" : "Rock_Water_2", rp, rng.Range(0.0, 360.0), rng.Range(0.7, 1.1), 0, 1, 0.0, -0.4);
					}
					++ponds;
					placed = true;
				}
			}
			// Abreuvoirs de l'allée (une halte d'eau par traverse, côté gauche).
			for (int k = 0; k < 8; ++k)
			{
				const double a = (k + 0.5) * kPi / 4.0;
				const Vec2 u = Dir(a), sd{ -u.z, u.x };
				const Vec2 tp = o + u * kAlleyR + sd * 7.0 + u * 6.0;
				if (RoadGap(w, tp) < 2.0 || w.IsOccupied(tp, 2.5)) continue;
				Prop(w, "Trough_Stone_1", tp, YawAlong(u), 1.0, kSolid, 1, 0.9);
				Prop(w, "Barrel", tp + sd * 1.8, 0.0, 1.0, kSolid, 1, 0.4);
				Prop(w, "Bucket_Wooden_1", tp - sd * 1.6, rng.Range(0.0, 360.0), 1.0, 0, 2);
				w.Occupy(tp, 3.0);
				++troughs;
			}
			if (ponds < 3) w.Warn("camp de l'Oracle : seulement " + std::to_string(ponds) + " bassin(s)");
		}

		// --- 4. L'« océan de tentes » : rangs serrés en deux anneaux (chemin de ronde -> allée,
		//     allée -> avant les menhirs), entrées tournées vers le rocher ; posé après le
		//     cimetière et les écuries (même couronne de rayons), qu'il contourne. Retour propriétaire du
		//     23/11/2026 (« pas assez de tentes autour ») : quinze rangs au lieu de six, tentes de
		//     pèlerins modélisées (Tent_Pilgrim_*, faîtage selon X local, entrée en -X) au lieu
		//     des panneaux de toit ; tentes longues de familles avec leur feu.
		int tents = 0, pennants = 0, fireLights = 0;
		{
			// v1.0.141 : toiles de couleur (_4 rouge sombre, _5 bleu-gris, T2-P045) ; tentes
			// agrandies ×1,5 (×1,4 la longue) : emprises recalculées en conséquence.
			static const char* kTents[6] = { "Tent_Pilgrim_1", "Tent_Pilgrim_2", "Tent_Pilgrim_3", "Tent_Pilgrim_Long_1",
				"Tent_Pilgrim_4", "Tent_Pilgrim_5" };
			std::vector<double> rows;
			for (double r = kRingR + 14.0; r <= kAlleyR - 9.0; r += 12.5) rows.push_back(r);
			for (double r = kAlleyR + 9.0; r <= 425.0; r += 12.5) rows.push_back(r);
			for (double row : rows)
			{
				const int slots = static_cast<int>(2.0 * kPi * row / 7.5);
				const double phase = rng.Range(0.0, 1.0);
				for (int k = 0; k < slots; ++k)
				{
					if (!rng.Chance(0.82)) continue;
					const double a = (k + phase + rng.Range(-0.25, 0.25)) * 2.0 * kPi / slots;
					const Vec2 t = o + Dir(a) * (row + rng.Range(-2.0, 2.0));
					const Vec2 u = Dir(a + rng.Range(-0.25, 0.25)); // faîtage radial (vers l'extérieur)
					static const int kShort[5] = { 0, 1, 2, 4, 5 }; // 3 = tente longue (12 %)
					const int kind = rng.Chance(0.12) ? 3 : kShort[rng.Below(5)];
					const double hz = kind == 3 ? 4.4 : 3.0, hx = kind == 3 ? 2.6 : 2.4; // hz : le long du faîtage
					if (!w.InPlayableBounds(t, 40.0) || RoadGap(w, t) < hz + 1.5 || w.IsOccupiedRect(t, hx, hz, YawFacing(u))) continue;
					// X local du mesh le long de u : l'entrée (-X) regarde le rocher.
					Prop(w, kTents[kind], t, YawAlong(u), rng.Range(0.92, 1.08), kSolid, 0, kind == 3 ? 3.1 : 2.3);
					w.OccupyRect(t, hx, hz, YawFacing(u));
					L.homeDoors.push_back(t - u * (hz + 1.0));
					++tents;
					const Vec2 side{ -u.z, u.x };
					if (rng.Chance(0.16)) // fanion au mât arrière
						Prop(w, rng.Chance(0.5) ? "Banner_1" : "Banner_2", t + u * (hz - 0.4), YawFacing(side), 1.0, 0, 1, 0.0, 3.9);
					else if (rng.Chance(0.07)) // v1.0.141 : mât à long fanion jaune (T2-P045)
					{
						Prop(w, "Pennant_Pole_1", t + u * (hz + 0.8), rng.Range(0.0, 360.0), 1.0, 0, 0);
						++pennants;
					}
					if (rng.Chance(kind == 3 ? 0.6 : 0.14)) // feu (familles), bagages
					{
						static const char* kCamp[] = { "Bonfire_Stack_1", "Cauldron", "Barrel", "Crate_Wooden", "Bag", "Bucket_Wooden_1", "Pot_1", "PackSaddle_1" };
						const Vec2 q = t + side * (rng.Chance(0.5) ? 4.2 : -4.2);
						if (!w.IsOccupied(q, 0.6) && RoadGap(w, q) > 1.5)
						{
							const int pick = kind == 3 ? 0 : static_cast<int>(1 + rng.Below(7));
							Prop(w, kCamp[pick], q, rng.Range(0.0, 360.0), 1.0, kSolid, 1, 0.45);
							w.Occupy(q, 0.6);
							// v1.0.141 : feux éclairés la nuit (T3-P005 : l'océan de tentes piqueté de
							// feux) ; un feu de famille sur deux, au plus 90 (le client garde les 64
							// lumières les plus proches de la caméra).
							if (pick == 0 && fireLights < 90 && rng.Chance(0.5))
							{
								Light(w, c.id + "_feu_" + std::to_string(fireLights), q, w.HeightAt(q) + 1.1, 11.0, 1.5, 1.0, 0.55, 0.2, "torch");
								w.pointLights.back().autoNight = true; // feux visibles la nuit seulement
								++fireLights;
							}
						}
					}
				}
			}
		}

		// --- 6. Menhirs bleus sur le grand cercle (pierres dressées, lueur bleue d'un sur deux).
		{
			int stones = 0;
			for (int k = 0; k < 22; ++k)
			{
				double a = k * 2.0 * kPi / 22.0 + rng.Range(-0.06, 0.06);
				const double r = rng.Range(440.0, 540.0);
				Vec2 p = o + Dir(a) * r;
				for (int attempt = 0; attempt < 8 && (RoadGap(w, p) < 10.0 || w.IsOccupied(p, 4.0)); ++attempt)
				{
					a += 0.035;
					p = o + Dir(a) * r;
				}
				if (RoadGap(w, p) < 10.0 || w.IsOccupied(p, 4.0) || !w.InPlayableBounds(p, 60.0)) continue;
				// v1.0.141 : menhirs bleus de 3 à 5 m (T2-P044 : à peine deux hauteurs d'homme).
				Prop(w, (k % 3) ? "Oracle_Menhir_2" : "Oracle_Menhir_1", p, rng.Range(0.0, 360.0), rng.Range(0.95, 1.05), kSolid, 0, 0.9, -0.2);
				w.Occupy(p, 2.0);
				if (k % 2 == 0) BlueLight(w, c.id + "_menhir_" + std::to_string(k), p + Dir(a) * -2.0, w.HeightAt(p) + 2.5, 12.0, 1.4);
				++stones;
			}
			if (stones < 18) w.Warn("camp de l'Oracle : seulement " + std::to_string(stones) + " menhirs");
		}

		// --- 10. v1.0.141 — Salles intérieures, passages conditionnels, PNJ de quête, raid.
		{
			const double floorY = S.plainY + kDoorH;
			const Vec2 nSO = FacetN(kMainFacet), nE = FacetN(kEchoFacet);
			const Vec2 tSO = FacetT(kMainFacet);
			// Décor posé à \p yAbove mètres au-dessus du dallage (le sol des salles est plat).
			auto Inner = [&](const char* mesh, Vec2 p, double yaw, double scale, uint8_t flags, double colR, double yAbove) {
				Prop(w, mesh, p, yaw, scale, flags, 0, colR, floorY + yAbove - w.HeightAt(p));
			};
			/// Une salle ronde : 16 panneaux sculptés (centrés sur les directions k·22,5° : une
			/// porte peut s'y adosser), colonnes aux joints, voûte, porte intérieure vers \p doorDir.
			auto Hall = [&](Vec2 hc, double sc, double vaultSc, Vec2 doorDir, const std::string& tag) {
				for (int i = 0; i < kHallPanels; ++i)
				{
					const double a = i * 2.0 * kPi / kHallPanels;
					const Vec2 d = Dir(a);
					// Origine du panneau 0,79 m derrière sa face concave (centre de l'arc = hc).
					Inner("Oracle_Hall_Wall", hc + d * ((kHallWallR + 0.79) * sc), YawFacing(d * -1.0), sc,
						static_cast<uint8_t>(kSolid | kPiece), 0.0, 0.0);
					const Vec2 dc = Dir(a + kPi / kHallPanels);
					Inner("Oracle_Hall_Column", hc + dc * ((kHallWallR - 2.2) * sc), YawFacing(dc * -1.0), sc, kSolid, 1.8 * sc, 0.0);
				}
				Inner("Oracle_Vault", hc, rng.Range(0.0, 360.0), vaultSc, 0, 0.0, kHallVaultY * vaultSc);
				Inner("Oracle_Door_Grand", hc + doorDir * (kInDoorR * sc + (1.0 - sc) * 0.0), YawFacing(doorDir * -1.0), 1.0, kSolid, 4.6, 0.0);
				Light(w, c.id + "_" + tag + "_porte", hc + doorDir * (kInDoorR * sc - 4.0), floorY + 6.0, 16.0, 1.8, 0.45, 0.6, 1.0);
			};

			// Salle de l'Oracle (T1-P003..005, T3-P006..007).
			Hall(o, 1.0, 1.0, nSO, "salle");
			Inner("Oracle_Hall_Dais", o, 0.0, 1.0, kSolid, 13.0, 0.0);
			Inner("Oracle_Light_Pillar", o, rng.Range(0.0, 360.0), 1.0, 0, 0.0, 1.5);
			Light(w, c.id + "_oracle", o, floorY + 14.0, 60.0, 5.0, 0.75, 0.86, 1.0);
			for (int k = 0; k < 8; ++k)
			{
				const double a = (k + 0.25) * kPi / 4.0;
				// Chaînes : bout bas 21 m sous l'intrados de la voûte à ce rayon.
				const double r = rng.Range(18.0, 30.0);
				const double vault = kHallVaultY + 22.0 * std::sin(std::acos(std::min(1.0, r / 52.0)));
				Inner("Oracle_Chain_Hanging", o + Dir(a) * r, rng.Range(0.0, 360.0), 1.0, 0, 0.0, vault - 21.0);
				// Amas de cristaux contre les parois (hors de l'axe de la porte).
				const Vec2 cp = o + Dir(a + 0.2) * rng.Range(36.0, 39.0);
				if (Dot(Dir(a + 0.2), nSO) < 0.9)
				{
					Inner(k % 2 ? "Oracle_Crystal_Cluster_2" : "Oracle_Crystal_Cluster_1", cp, rng.Range(0.0, 360.0), 1.0, kSolid, 1.8, -0.2);
					Light(w, c.id + "_cristal_" + std::to_string(k), cp + (o - cp) * 0.1, floorY + 4.0, 14.0, 1.8, 0.35, 0.6, 1.0);
				}
			}
			// Salle des Échos (raid) sous la facette est.
			const Vec2 ec = o + nE * kEchoD;
			Hall(ec, kEchoScale, kEchoVaultScale, nE, "echos");
			Light(w, c.id + "_echos", ec, floorY + 10.0, 32.0, 2.6, 0.45, 0.65, 1.0);
			for (int k = 0; k < 6; ++k)
			{
				const Vec2 d = Dir(k * kPi / 3.0 + 0.5);
				const Vec2 cp = ec + d * rng.Range(17.0, 19.5);
				if (Dot(d, nE) > 0.8) continue; // couloir de la porte libre
				Inner(k % 2 ? "Oracle_Crystal_Cluster_1" : "Oracle_Crystal_Cluster_2", cp, rng.Range(0.0, 360.0), 1.0, kSolid, 1.8, -0.2);
				Light(w, c.id + "_echos_" + std::to_string(k), cp + (ec - cp) * 0.15, floorY + 3.5, 12.0, 1.6, 0.35, 0.6, 1.0);
			}
			// Ambiances : soleil éteint, lumière froide des cristaux.
			for (int q = 0; q < 2; ++q)
			{
				AmbianceZoneDef z;
				z.id = c.id + (q ? "_salle_echos" : "_salle_oracle");
				z.label = q ? "Salle des Échos" : "Salle de l'Oracle";
				z.center = q ? ec : o;
				z.radiusM = q ? kEchoR + 2.0 : kHallR + 2.0;
				z.floorY = floorY - 2.0;
				z.topY = floorY + 70.0;
				z.darkness = q ? 0.9 : 0.85;
				w.ambianceZones.push_back(z);
			}
			// Passages conditionnels (shard) : porte SO -> salle de l'Oracle, porte E -> salle des Échos.
			auto Pass = [&](const std::string& id, const std::string& name, Vec2 from, double r, Vec2 to, Vec2 face,
					std::vector<std::string> quests, uint32_t minGroup, const std::string& denied) {
				PassageDef p;
				p.id = id; p.name = name; p.position = from; p.y = floorY; p.radiusM = r;
				p.destination = to; p.destY = floorY + 0.05; p.destYawDeg = YawFacing(face);
				p.requiredQuests = std::move(quests); p.minGroup = minGroup; p.deniedMessage = denied;
				w.passages.push_back(std::move(p));
			};
			Pass(c.id + "_porte_so_entree", "Grande porte de l'Oracle (entree)", o + nSO * (S.r[kMainFacet] + kPassInS), 2.5,
				o + nSO * kInArriveR, nSO * -1.0, { "oracle_appel" }, 0,
				"La grande porte reste close : l'Oracle ne vous a pas appelé. Le veilleur, au pied de l'escalier, en sait davantage.");
			Pass(c.id + "_porte_so_sortie", "Grande porte de l'Oracle (sortie)", o + nSO * kInPassR, 2.0,
				o + nSO * (S.r[kMainFacet] + kPassOutS), nSO, {}, 0, "");
			Pass(c.id + "_porte_e_entree", "Porte de l'est (salle des Echos, entree)", o + nE * (S.r[kEchoFacet] + kPassInS), 2.5,
				ec + nE * (15.0), nE * -1.0, { "oracle_appel", "oracle_echos" }, 3,
				"La porte de l'est ne s'ouvre qu'à un groupe d'au moins trois compagnons engagés dans « L'Épreuve des Échos ».");
			Pass(c.id + "_porte_e_sortie", "Porte de l'est (salle des Echos, sortie)", ec + nE * (kInPassR * kEchoScale), 2.0,
				o + nE * (S.r[kEchoFacet] + kPassOutS), nE, {}, 0, "");
			// PNJ de quête : veilleur au pied de l'escalier SO, prêtresse devant le dais.
			auto Npc = [&](Vec2 p, Vec2 face, const char* label, const char* role, const char* msg, const char* mesh,
					const char* dialogue, const char* target) {
				InteractableDef d;
				d.x = p.x; d.z = p.z; d.radius = 2.0; d.npc = true;
				d.label = label; d.role = role; d.message = msg; d.mesh = mesh;
				d.scale = 95.0; d.rotXDeg = -90.0; d.yawDeg = YawFacing(face);
				d.dialogueId = dialogue; d.npcTargetId = target;
				w.interactables.push_back(std::move(d));
				w.Occupy(p, 1.5);
			};
			Npc(o + nSO * (S.r[kMainFacet] + kLandOut + kStairRun + 5.0) + tSO * 7.0, nSO,
				"Veilleur de la grande porte", "Veilleur", "Veilleur : Des milliers attendent l'appel de l'Oracle.",
				"models/characters/humains/Male_Peasant/Male_Peasant.glb", "oracle_veilleur", "npc:oracle_veilleur");
			Npc(o + nSO * 17.0, nSO, "Prêtresse du dais", "Prêtresse de l'Oracle", "Prêtresse : Tu es entré. Écoute.",
				"models/characters/humains/Female_Peasant/Female_Peasant.glb", "oracle_pretresse", "npc:oracle_pretresse");
			// Raid de la salle des Échos : le Gardien (170) au centre, quatre échos (171).
			{
				SpawnerDef g;
				// Préfixe « quete_ » : cible de quête sur la minicarte (quest_poi.json).
				g.id = "quete_" + c.id + "_gardien_echos"; g.archetypeId = 170; g.position = ec; g.y = floorY;
				g.count = 1; g.respawnSec = 1800; g.leashM = 14.0;
				w.spawners.push_back(g);
				for (int k = 0; k < 4; ++k)
				{
					SpawnerDef e;
					e.id = c.id + "_echo_" + std::to_string(k); e.archetypeId = 171;
					e.position = ec + Dir(k * kPi / 2.0 + kPi / 4.0) * 9.0; e.y = floorY;
					e.count = 1; e.respawnSec = 900; e.leashM = 10.0;
					w.spawners.push_back(e);
				}
			}
			// Rocher en cristal bleu au rendu (couche A du splat, client).
			w.crystalLayer = true;
		}

		// --- Fin : emprise du camp (créatures écartées, carte), départ éventuel au parvis.
		c.radiusM = kPlainR;
		L.startSpot = parvis + swDir * 4.0;
		L.startYawDeg = YawFacing(swDir * -1.0);
		if (tents < 1200) w.Warn("camp de l'Oracle : seulement " + std::to_string(tents) + " tentes");
		if (pennants < 20) w.Warn("camp de l'Oracle : seulement " + std::to_string(pennants) + " mats a fanion");
		(void)troughs;
	}
}
