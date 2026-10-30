// tools/world_gen — agglomérations : rues, lots, bâtiments, remparts,
// mobilier urbain, champs, cimetière et graphe de déplacement des PNJ.
//
// Disposition (pour chaque ville) :
//   - une PLACE centrale (rayon ~30 % du rayon urbain) avec étals de marché ;
//   - des RUES rayonnantes : d'abord les routes qui arrivent à la ville, puis
//     des rues complémentaires pour atteindre un nombre minimal selon la taille ;
//   - une RUE CIRCULAIRE pour les villes moyennes et grandes ;
//   - des LOTS de part et d'autre des rues, façade (porte) tournée vers la rue ;
//   - l'AUBERGE face à la place ; une FORGE en périphérie ;
//   - un REMPART (capitale, grande ville, forteresse) ou une PALISSADE (orcs,
//     camps), percé de portes là où passent les routes ;
//   - des CHAMPS clôturés hors les murs (hameaux et villages), un CIMETIÈRE.
// Le graphe de rues (noeuds tous les ~9 m) sert aux tournées des villageois et
// aux rondes des gardes (Population.cpp).

#include "WorldGenContext.h"
#include "HouseCatalog.h"
#include "Random.h"

#include <algorithm>
#include <cmath>

namespace tools::world_gen
{
	namespace
	{
		constexpr double kPi = 3.14159265358979323846;
		constexpr double kStreetHalf = 2.5;
		/// Bits ScatterInstance::flags (cf. src/client/world/instances/ScatterInstances.h).
		constexpr uint8_t kSolid = 1u, kPiece = 2u, kOccluder = 4u;

		/// Modèle de bâtiment candidat (référence de bibliothèque + encombrement).
		struct TemplateRef
		{
			std::string type;
			std::string variant;
			double halfW = 2.0;   ///< demi-largeur X (m)
			double halfD = 3.0;   ///< demi-profondeur Z hors perron (m)
			double weight = 1.0;
		};

		TemplateRef House(const char* id, double weight)
		{
			const HouseSpec* s = FindHouse(id);
			TemplateRef t;
			t.type = "maison"; t.variant = id; t.weight = weight;
			t.halfW = s ? s->widthM * 0.5 : 2.0;
			t.halfD = s ? s->depthM * 0.5 : 3.0;
			return t;
		}
		TemplateRef Hand(const char* type, const char* variant, double halfW, double halfD, double weight)
		{
			TemplateRef t; t.type = type; t.variant = variant; t.halfW = halfW; t.halfD = halfD; t.weight = weight;
			return t;
		}

		/// Palette d'habitations selon la taille, la race et l'état.
		std::vector<TemplateRef> Palette(const CityDef& c)
		{
			if (c.procedural == "ferme")
				return { House("longere_6x10", 2), House("chaumiere_4x4", 2), House("maison_4x6", 1) };
			if (c.procedural == "bucherons")
				return { House("chaumiere_4x4", 3), House("longere_6x10", 1) };
			if (c.procedural == "mine")
				return { House("chaumiere_4x4", 2), House("forge_6x6", 1) };
			if (c.procedural == "bandits")
				return { House("ruine_6x6", 2), House("chaumiere_4x4", 2) };
			if (c.state == "ruine")
				return { House("ruine_6x6", 3), House("ruine_6x10", 2), House("ruine_8x10", 2), House("chaumiere_4x4", 0.5) };
			if (c.race == "nains")
				return { House("maison_6x6", 3), House("manoir_8x12", 1), House("caserne_8x14", 0.6), House("forge_6x6", 0.8), House("echoppe_4x8", 1) };
			if (c.race == "orcs")
				return { House("chaumiere_4x4", 3), House("longere_6x10", 2), House("forge_6x6", 0.6) };
			if (c.race == "elfes")
				return { House("chaumiere_4x4", 2), House("maison_4x6", 2), House("longere_6x10", 1) };
			if (c.race == "demons" || c.kind == "camp")
				return { House("chaumiere_4x4", 2), House("ruine_6x6", 1), House("forge_6x6", 0.5) };
			if (c.kind == "capitale")
				return { House("maison_haute_6x8", 3), House("grande_maison_8x10", 2), House("manoir_8x12", 1.5),
					Hand("house", "house_001", 4.0, 7.4, 1.2), House("echoppe_4x8", 1.2), House("maison_6x6", 1) };
			if (c.kind == "grande_ville")
				return { House("maison_6x6", 2.5), House("maison_haute_6x8", 2), House("grande_maison_8x10", 1.5),
					House("echoppe_4x8", 1.5), House("manoir_8x12", 0.6), Hand("house", "house_001", 4.0, 7.4, 0.8) };
			if (c.kind == "forteresse")
				return { House("caserne_8x14", 1.5), House("maison_6x6", 2), House("manoir_8x12", 0.8), House("forge_6x6", 0.6) };
			if (c.kind == "ville")
				return { House("maison_4x6", 2), House("maison_6x6", 2), House("echoppe_4x8", 1.5), House("maison_haute_6x8", 1),
					House("grande_maison_8x10", 0.8) };
			if (c.kind == "village")
				return { House("chaumiere_4x4", 2), House("maison_4x6", 2), House("longere_6x10", 1.5), House("maison_6x6", 1),
					House("echoppe_4x8", 0.7) };
			return { House("chaumiere_4x4", 3), House("longere_6x10", 2), House("maison_4x6", 1.5) }; // hameau
		}

		TemplateRef InnFor(const CityDef& c)
		{
			if (c.kind == "capitale") return Hand("tavern", "auberge_cite", 4.0, 7.4, 1);
			if (c.kind == "grande_ville") return Hand("tavern", "auberge_grande_ville", 4.0, 5.4, 1);
			if (c.kind == "forteresse") return Hand("tavern", "auberge_forteresse", 4.0, 6.4, 1);
			if (c.kind == "ville") return Hand("tavern", "auberge_terrasse", 3.0, 4.4, 1);
			return Hand("tavern", "auberge_hameau", 2.0, 4.4, 1);
		}

		int MinSpokes(const std::string& kind, const std::string& procedural)
		{
			if (procedural == "ferme" || procedural == "mine") return 1;
			if (kind == "capitale") return 6;
			if (kind == "grande_ville") return 5;
			if (kind == "ville" || kind == "forteresse") return 4;
			if (kind == "village" || kind == "camp") return 3;
			return 2;
		}

		bool HasWalls(const CityDef& c)
		{
			return c.state != "ruine" && (c.kind == "capitale" || c.kind == "grande_ville" || c.kind == "forteresse");
		}
		bool HasPalisade(const CityDef& c)
		{
			return c.state != "ruine" && !HasWalls(c) && (c.race == "orcs" || c.kind == "camp" || c.kind == "ville");
		}

		double YawFacing(Vec2 f) { return std::atan2(-f.x, -f.z) * 180.0 / kPi; }
		Vec2 Dir(double angleRad) { return { std::cos(angleRad), std::sin(angleRad) }; }

		void AddProp(World& w, const std::string& mesh, Vec2 p, double yaw, double scale, uint8_t flags,
			uint8_t cull, double collisionRadius = 0.0, double yOffset = 0.0)
		{
			ScatterDef s;
			s.mesh = "meshes/props/" + mesh + ".gltf";
			s.position = p; s.yawDeg = yaw; s.scale = scale; s.flags = flags; s.cullClass = cull;
			s.collisionRadius = collisionRadius; s.yOffset = yOffset;
			w.scatter.push_back(std::move(s));
		}

		/// Vrai si un disque reste exploitable (hors eau, hors rivière, pente faible).
		bool GroundOk(const World& w, Vec2 c, double r)
		{
			if (!w.InPlayableBounds(c, 48.0)) return false;
			for (int k = 0; k < 8; ++k)
			{
				const Vec2 p = c + Dir(k * kPi / 4.0) * r;
				if (w.IsWater(p)) return false;
				const int ri = w.riverDist.IndexX(p.x), rj = w.riverDist.IndexZ(p.z);
				if (w.riverDist.InBounds(ri, rj) && w.riverDist.At(ri, rj) < 4.0f) return false;
			}
			if (w.IsWater(c)) return false;
			// Dénivelé sur l'emprise : un bâtiment ne se pose pas à flanc de colline.
			double lo = 1e9, hi = -1e9;
			for (int k = 0; k < 9; ++k)
			{
				const Vec2 p = (k == 8) ? c : c + Dir(k * kPi / 4.0) * r;
				const double h = w.HeightAt(p);
				lo = std::min(lo, h); hi = std::max(hi, h);
			}
			return hi - lo < 1.6;
		}

		int AddNode(SettlementLayout& L, Vec2 p)
		{
			L.nodes.push_back({ p, {} });
			return static_cast<int>(L.nodes.size()) - 1;
		}
		void Link(SettlementLayout& L, int a, int b)
		{
			if (a == b || a < 0 || b < 0) return;
			auto& la = L.nodes[static_cast<size_t>(a)].links;
			if (std::find(la.begin(), la.end(), b) == la.end()) la.push_back(b);
			auto& lb = L.nodes[static_cast<size_t>(b)].links;
			if (std::find(lb.begin(), lb.end(), a) == lb.end()) lb.push_back(a);
		}
		int Nearest(const SettlementLayout& L, Vec2 p, int skip = -1)
		{
			int best = -1; double bd = 1e18;
			for (size_t i = 0; i < L.nodes.size(); ++i)
			{
				if (static_cast<int>(i) == skip) continue;
				const double d = std::hypot(L.nodes[i].p.x - p.x, L.nodes[i].p.z - p.z);
				if (d < bd) { bd = d; best = static_cast<int>(i); }
			}
			return best;
		}

		/// Pose un bâtiment s'il tient ; renvoie l'indice ou -1. L'emprise testée
		/// est le RECTANGLE orienté du bâtiment (+0,6 m de marge), pas un disque :
		/// les maisons s'alignent en front de rue sans gaspiller les lots.
		int TryPlaceBuilding(World& w, const CityDef& c, const TemplateRef& t, Vec2 center, Vec2 facing,
			const char* label, bool requireInside = true)
		{
			const double yaw = YawFacing(facing);
			const double hx = t.halfW + 0.6, hz = t.halfD + 0.6;
			const double footR = std::sqrt(hx * hx + hz * hz);
			if (requireInside && std::hypot(center.x - c.center.x, center.z - c.center.z) + footR * 0.5 > c.radiusM + 4.0)
				return -1;
			if (w.IsOccupiedRect(center, hx, hz, yaw) || !GroundOk(w, center, footR)) return -1;
			// Hors des routes inter-villes (la chaussée reste dégagée).
			for (int k = 0; k < 5; ++k)
			{
				const Vec2 corner = (k == 4) ? center : center + Vec2{ (k & 1 ? 1 : -1) * t.halfW, (k & 2 ? 1 : -1) * t.halfD };
				const int ri = w.roadDist.IndexX(corner.x), rj = w.roadDist.IndexZ(corner.z);
				if (w.roadDist.InBounds(ri, rj) && w.roadDist.At(ri, rj) < 1.0f) return -1;
			}
			w.OccupyRect(center, hx, hz, yaw);
			BuildingPlacementDef b;
			b.templateType = t.type;
			b.variantId = t.variant;
			b.displayName = std::string(label) + " (" + c.name + ")";
			b.position = center;
			b.yawDeg = yaw;
			b.footprintRadius = footR;
			b.halfW = t.halfW;
			b.halfD = t.halfD;
			b.cityId = c.id;
			b.doorPoint = center + facing * (t.halfD + 2.2);
			w.buildings.push_back(std::move(b));
			return static_cast<int>(w.buildings.size()) - 1;
		}

		/// Pave une polyligne de dalles (2 m) : `across` dalles de front, suivant
		/// la pente (inclinaison autour de l'axe X local).
		void PaveAlong(World& w, const std::vector<Vec2>& pts, int across, const char* mesh, double clipRadius, Vec2 clipCenter)
		{
			const std::vector<Vec2> rs = ResamplePolyline(pts, 2.0);
			for (size_t k = 0; k + 1 < rs.size(); ++k)
			{
				const Vec2 a = rs[k], b = rs[k + 1];
				if (clipRadius > 0.0 && std::hypot(a.x - clipCenter.x, a.z - clipCenter.z) > clipRadius) continue;
				Vec2 d{ b.x - a.x, b.z - a.z };
				const double l = std::hypot(d.x, d.z);
				if (l < 1e-6) continue;
				d = d * (1.0 / l);
				const Vec2 nrm{ -d.z, d.x };
				const Vec2 mid = (a + b) * 0.5;
				const double dh = w.HeightAt(b) - w.HeightAt(a);
				const double tilt = std::atan2(dh, l) * 180.0 / kPi;
				for (int t = 0; t < across; ++t)
				{
					const double off = (t - (across - 1) * 0.5) * 2.0;
					const Vec2 p = mid + nrm * off;
					if (w.IsWater(p)) continue;
					ScatterDef s;
					s.mesh = std::string("meshes/props/") + mesh + ".gltf";
					s.position = p;
					s.yawDeg = YawFacing(d);
					s.tiltDeg = tilt;
					s.yOffset = -0.06;
					s.cullClass = 1;
					w.scatter.push_back(std::move(s));
				}
			}
		}

		const TemplateRef& PickWeighted(const std::vector<TemplateRef>& pal, double u)
		{
			double total = 0.0;
			for (const auto& t : pal) total += t.weight;
			double acc = 0.0;
			for (const auto& t : pal) { acc += t.weight / total; if (u <= acc) return t; }
			return pal.back();
		}
	}

	void BuildSettlements(World& w)
	{
		w.urban.Init(w.height.width, w.height.height, w.height.originX, w.height.originZ, w.height.step, w.urban.data.empty() ? uint8_t(0) : uint8_t(0));
		for (CityDef& c : w.cities)
		{
			SettlementLayout L;
			L.cityId = c.id;
			const double R = c.radiusM;
			const double Rp = Clamp(R * 0.3, 7.0, 16.0);
			Rng rng(Hash(w.master.seed, HashString(c.id.c_str()), 4242));

			// --- Rues rayonnantes : directions des routes qui arrivent ici.
			std::vector<double> angles;
			for (const Road& r : w.roads)
			{
				if (r.points.size() < 4 || (r.fromId != c.id && r.toId != c.id)) continue;
				// Premier point de la route à plus de R du centre (sortie de ville).
				const bool fromHere = r.fromId == c.id;
				Vec2 exitP = fromHere ? r.points.back() : r.points.front();
				for (size_t k = 0; k < r.points.size(); ++k)
				{
					const Vec2 p = fromHere ? r.points[k] : r.points[r.points.size() - 1 - k];
					if (std::hypot(p.x - c.center.x, p.z - c.center.z) > R * 0.85) { exitP = p; break; }
				}
				const double a = std::atan2(exitP.z - c.center.z, exitP.x - c.center.x);
				bool dup = false;
				for (double b : angles) if (std::fabs(std::remainder(a - b, 2.0 * kPi)) < 0.45) dup = true;
				if (!dup) angles.push_back(a);
			}
			const size_t roadSpokes = angles.size();
			// Complète jusqu'au minimum avec des rues aux angles les plus libres.
			while (static_cast<int>(angles.size()) < MinSpokes(c.kind, c.procedural))
			{
				double bestA = rng.Range(0.0, 2.0 * kPi), bestGap = -1.0;
				for (int k = 0; k < 24; ++k)
				{
					const double a = k * kPi / 12.0 + rng.Range(-0.08, 0.08);
					double gap = 1e9;
					for (double b : angles) gap = std::min(gap, std::fabs(std::remainder(a - b, 2.0 * kPi)));
					// La rue ne doit pas plonger dans l'eau.
					const Vec2 tip = c.center + Dir(a) * (R - 2.0);
					if (w.IsWater(tip) || w.IsWater(c.center + Dir(a) * (R * 0.6))) gap *= 0.1;
					if (angles.empty()) gap = 1.0 + rng.Next();
					if (gap > bestGap) { bestGap = gap; bestA = a; }
				}
				angles.push_back(bestA);
			}

			// --- Graphe : place (noeud 0) + anneau de ronde.
			AddNode(L, c.center);
			for (int k = 0; k < 8; ++k)
			{
				const int id = AddNode(L, c.center + Dir(k * kPi / 4.0 + 0.2) * (Rp * 0.8));
				L.plazaRing.push_back(id);
				Link(L, 0, id);
			}
			for (size_t k = 0; k < L.plazaRing.size(); ++k)
				Link(L, L.plazaRing[k], L.plazaRing[(k + 1) % L.plazaRing.size()]);

			// Place : marque urbaine + occupation (dégagée).
			w.Occupy(c.center, Rp + 1.0);

			// --- Rues (polylignes) et noeuds.
			std::vector<std::vector<Vec2>> streets;
			for (size_t si = 0; si < angles.size(); ++si)
			{
				const double a = angles[si];
				// Légère courbure pour les rues complémentaires (aspect organique).
				const double bend = (si >= roadSpokes) ? rng.Range(-0.18, 0.18) : 0.0;
				std::vector<Vec2> st;
				const double len = R + (si < roadSpokes ? 6.0 : -1.0);
				for (double d = Rp; d <= len; d += 3.0)
				{
					const double t = (d - Rp) / std::max(1.0, len - Rp);
					st.push_back(c.center + Dir(a + bend * t) * d);
				}
				if (st.size() < 2) continue;
				int prev = Nearest(L, st.front());
				for (size_t k = 0; k < st.size(); k += 3)
				{
					const int id = AddNode(L, st[k]);
					Link(L, prev, id);
					prev = id;
				}
				if (si < roadSpokes) L.gatePoints.push_back(st.back());
				streets.push_back(st);
			}
			// Rue circulaire (villes moyennes+).
			const bool ring = (c.kind == "ville" || c.kind == "grande_ville" || c.kind == "capitale" || c.kind == "forteresse");
			if (ring)
			{
				const double rr = R * 0.62;
				std::vector<Vec2> st;
				std::vector<int> ringIds;
				for (int k = 0; k <= 48; ++k) st.push_back(c.center + Dir(k * 2.0 * kPi / 48.0) * rr);
				for (int k = 0; k < 48; k += 3) ringIds.push_back(AddNode(L, st[static_cast<size_t>(k)]));
				for (size_t k = 0; k < ringIds.size(); ++k) Link(L, ringIds[k], ringIds[(k + 1) % ringIds.size()]);
				// Raccorde l'anneau aux rues rayonnantes les plus proches.
				for (int id : ringIds)
				{
					const int nb = Nearest(L, L.nodes[static_cast<size_t>(id)].p, id);
					if (nb >= 0 && std::find(ringIds.begin(), ringIds.end(), nb) == ringIds.end()
						&& std::hypot(L.nodes[static_cast<size_t>(nb)].p.x - L.nodes[static_cast<size_t>(id)].p.x,
							L.nodes[static_cast<size_t>(nb)].p.z - L.nodes[static_cast<size_t>(id)].p.z) < 9.0)
						Link(L, id, nb);
				}
				streets.push_back(st);
			}
			// Dégage les rues (occupation) et marque les rues dans la carte urbaine.
			for (const auto& st : streets)
				for (const Vec2& p : st)
				{
					w.Occupy(p, kStreetHalf + 0.4);
					const int i = w.urban.IndexX(p.x), j = w.urban.IndexZ(p.z);
					for (int dj = -1; dj <= 1; ++dj)
						for (int di = -1; di <= 1; ++di)
							if (w.urban.InBounds(i + di, j + dj)) w.urban.At(i + di, j + dj) = 2;
				}
			// Place pavée : marque 2 (terre battue au splat).
			{
				const int i0 = w.urban.IndexX(c.center.x - Rp), i1 = w.urban.IndexX(c.center.x + Rp);
				const int j0 = w.urban.IndexZ(c.center.z - Rp), j1 = w.urban.IndexZ(c.center.z + Rp);
				for (int j = j0; j <= j1; ++j)
					for (int i = i0; i <= i1; ++i)
						if (w.urban.InBounds(i, j) && std::hypot(w.urban.WorldX(i) - c.center.x, w.urban.WorldZ(j) - c.center.z) <= Rp)
							w.urban.At(i, j) = 2;
				// Emprise urbaine (1) jusqu'au rayon.
				const int k0 = w.urban.IndexX(c.center.x - R), k1 = w.urban.IndexX(c.center.x + R);
				const int l0 = w.urban.IndexZ(c.center.z - R), l1 = w.urban.IndexZ(c.center.z + R);
				for (int j = l0; j <= l1; ++j)
					for (int i = k0; i <= k1; ++i)
						if (w.urban.InBounds(i, j) && w.urban.At(i, j) == 0
							&& std::hypot(w.urban.WorldX(i) - c.center.x, w.urban.WorldZ(j) - c.center.z) <= R)
							w.urban.At(i, j) = 1;
			}

			// --- Auberge face à la place (entre deux rues).
			{
				std::vector<double> sorted = angles;
				std::sort(sorted.begin(), sorted.end());
				double bestMid = 0.0, bestGap = -1.0;
				for (size_t k = 0; k < sorted.size(); ++k)
				{
					const double a0 = sorted[k], a1 = (k + 1 < sorted.size()) ? sorted[k + 1] : sorted[0] + 2.0 * kPi;
					if (a1 - a0 > bestGap) { bestGap = a1 - a0; bestMid = 0.5 * (a0 + a1); }
				}
				// Ville d'ancrage du contenu importé : l'auberge historique est déjà posée.
				const bool wantsInn = c.procedural.empty() || c.procedural == "hameau";
				if (wantsInn && c.state != "ruine" && c.kind != "camp" && c.id != w.master.importAnchorCity)
				{
					const TemplateRef inn = InnFor(c);
					int placed = -1;
					for (int attempt = 0; attempt < 10 && placed < 0; ++attempt)
					{
						const double a = bestMid + (attempt % 2 ? 1 : -1) * 0.12 * (attempt / 2);
						const Vec2 dir = Dir(a);
						placed = TryPlaceBuilding(w, c, inn, c.center + dir * (Rp + 2.2 + inn.halfD), dir * -1.0, "Auberge");
					}
					if (placed >= 0)
					{
						w.buildings[static_cast<size_t>(placed)].isInn = true;
						L.innDoor = w.buildings[static_cast<size_t>(placed)].doorPoint;
						L.buildingIndices.push_back(placed);
					}
					else L.innDoor = c.center;
				}
				else L.innDoor = c.center;
			}

			// --- Lots le long des rues.
			const std::vector<TemplateRef> pal = Palette(c);
			int lotCount = 0;
			for (const auto& st : streets)
			{
				const std::vector<Vec2> pts = ResamplePolyline(st, 1.0);
				double along = rng.Range(0.0, 4.0);
				while (along < static_cast<double>(pts.size()) - 2.0)
				{
					const size_t k = static_cast<size_t>(along);
					const Vec2 p = pts[k], q = pts[std::min(pts.size() - 1, k + 1)];
					Vec2 tdir{ q.x - p.x, q.z - p.z };
					const double tl = std::hypot(tdir.x, tdir.z);
					if (tl < 1e-6) { along += 1.0; continue; }
					tdir = tdir * (1.0 / tl);
					const TemplateRef& t = PickWeighted(pal, rng.Next());
					bool any = false;
					for (int side = -1; side <= 1; side += 2)
					{
						if (rng.Chance(c.kind == "hameau" ? 0.35 : 0.12)) continue; // jardins / vides
						const Vec2 nrm{ -tdir.z * side, tdir.x * side };
						const Vec2 center = p + nrm * (kStreetHalf + 1.2 + t.halfD);
						const int b = TryPlaceBuilding(w, c, t, center, nrm * -1.0, "Maison");
						if (b >= 0) { L.buildingIndices.push_back(b); any = true; ++lotCount; }
					}
					along += any ? (2.0 * t.halfW + 1.6 + rng.Range(0.0, 1.8)) : 2.0;
				}
			}
			// --- Remplissage des îlots : maisons supplémentaires dans l'espace resté
			//     libre, porte tournée vers le noeud de rue le plus proche.
			{
				int attempts = 30;
				if (c.kind == "capitale") attempts = 260;
				else if (c.kind == "grande_ville") attempts = 160;
				else if (c.kind == "ville" || c.kind == "forteresse") attempts = 90;
				else if (c.kind == "village") attempts = 50;
				for (int a = 0; a < attempts; ++a)
				{
					const double ang = rng.Range(0.0, 2.0 * kPi);
					const double rad = std::sqrt(rng.Range(0.08, 1.0)) * (R - 3.0);
					const Vec2 center = c.center + Dir(ang) * rad;
					const int nn = Nearest(L, center);
					if (nn < 0) break;
					Vec2 face{ L.nodes[static_cast<size_t>(nn)].p.x - center.x, L.nodes[static_cast<size_t>(nn)].p.z - center.z };
					const double fl = std::hypot(face.x, face.z);
					if (fl < 4.0 || fl > 22.0) continue; // trop collé à la rue ou sans accès
					face = face * (1.0 / fl);
					const TemplateRef& t = PickWeighted(pal, rng.Next());
					const int b = TryPlaceBuilding(w, c, t, center, face, "Maison");
					if (b >= 0) { L.buildingIndices.push_back(b); ++lotCount; }
				}
			}

			// Forge en périphérie (villages et plus, hors ruines) et à la mine.
			if (c.state != "ruine" && ((c.kind != "hameau" && c.procedural.empty()) || c.procedural == "mine"))
			{
				const TemplateRef forge = House("forge_6x6", 1);
				for (int attempt = 0; attempt < 24; ++attempt)
				{
					const double a = rng.Range(0.0, 2.0 * kPi);
					const Vec2 dir = Dir(a);
					const Vec2 center = c.center + dir * (R * rng.Range(0.55, 0.85));
					const int b = TryPlaceBuilding(w, c, forge, center, dir * -1.0, "Forge");
					if (b >= 0)
					{
						L.buildingIndices.push_back(b);
						const BuildingPlacementDef& bp = w.buildings[static_cast<size_t>(b)];
						const Vec2 side{ -dir.z, dir.x };
						AddProp(w, "Anvil", bp.doorPoint + side * 2.2, YawFacing(dir), 1.0, kSolid, 1, 0.6);
						AddProp(w, "Workbench", bp.doorPoint - side * 2.4, YawFacing(side), 1.0, kSolid, 1, 0.9);
						AddProp(w, "Whetstone", bp.doorPoint + side * 3.6 - dir * 0.8, rng.Range(0, 360), 1.0, 0, 2);
						break;
					}
				}
			}

			// --- Marché : étals autour de la place (sauf ville d'ancrage : sa place
			//     accueille déjà les PNJ et objets historiques importés).
			if (c.state != "ruine" && c.id != w.master.importAnchorCity && c.procedural.empty())
			{
				const int stalls = (c.kind == "capitale") ? 8 : (c.kind == "grande_ville" ? 6 : (c.kind == "hameau" ? 1 : 3));
				for (int k = 0; k < stalls; ++k)
				{
					const double a = 2.0 * kPi * (k + 0.5) / stalls + 0.31;
					const Vec2 dir = Dir(a);
					const Vec2 p = c.center + dir * (Rp - 1.8);
					const bool cart = (k % 3) == 2;
					AddProp(w, cart ? "Stall_Cart_Empty" : "Stall_Empty", p, YawFacing(dir * -1.0) + 180.0, 1.0, kSolid, 0, 1.0);
					L.stallFronts.push_back(c.center + dir * (Rp - 3.4));
					// Marchandises au pied de l'étal.
					const char* goods[] = { "Barrel_Apples", "FarmCrate_Apple", "FarmCrate_Carrot", "Crate_Wooden", "Bag", "Barrel" };
					const Vec2 side{ -dir.z, dir.x };
					AddProp(w, goods[rng.Below(6)], p + side * 1.4, rng.Range(0, 360), 1.0, kSolid, 1, 0.4);
				}
				// Bancs et fontaine symbolique (chaudron + seaux) au centre.
				AddProp(w, "Cauldron", c.center, 0.0, 1.6, kSolid, 0, 0.9);
				for (int k = 0; k < 3; ++k)
				{
					const Vec2 dir = Dir(k * 2.0 * kPi / 3.0 + 1.0);
					AddProp(w, "Bench", c.center + dir * (Rp * 0.5), YawFacing(dir) + 90.0, 1.0, kSolid, 1, 0.5);
				}
			}

			// --- Mobilier devant les maisons (tonneaux, caisses, seaux).
			for (int bi : L.buildingIndices)
			{
				const BuildingPlacementDef& b = w.buildings[static_cast<size_t>(bi)];
				if (rng.Chance(0.55))
				{
					const double yawRad = b.yawDeg * kPi / 180.0;
					const Vec2 front{ -std::sin(yawRad), -std::cos(yawRad) };
					const Vec2 side{ -front.z, front.x };
					const char* items[] = { "Barrel", "Crate_Wooden", "Bucket_Wooden_1", "Barrel_Holder", "FarmCrate_Empty", "Bag" };
					const Vec2 p = b.doorPoint - front * 1.0 + side * (rng.Chance(0.5) ? 2.4 : -2.4);
					if (!w.IsOccupied(p, 0.5)) AddProp(w, items[rng.Below(6)], p, rng.Range(0, 360), 1.0, kSolid, 1, 0.4);
				}
			}

			// --- Remparts / palissade, percés aux portes.
			const bool walls = HasWalls(c), palisade = HasPalisade(c);
			if (walls || palisade)
			{
				const double rw = R + 3.0;
				const double segLen = 2.0;
				const int segs = static_cast<int>(std::floor(2.0 * kPi * rw / segLen));
				for (int k = 0; k < segs; ++k)
				{
					const double a = 2.0 * kPi * (k + 0.5) / segs;
					const Vec2 p = c.center + Dir(a) * rw;
					bool gate = false;
					for (double ga : angles)
						if (std::fabs(std::remainder(a - ga, 2.0 * kPi)) * rw < 4.2) gate = true;
					if (gate) continue;
					if (w.IsWater(p) || !w.InPlayableBounds(p, 40.0)) continue;
					// Tangente : le mur s'étend le long du cercle (axe X local).
					const Vec2 tan{ -std::sin(a), std::cos(a) };
					const double yaw = std::atan2(tan.z, tan.x) * -180.0 / kPi; // X local -> tangente
					if (walls)
						AddProp(w, "Wall_UnevenBrick_Straight", p, yaw, 1.0, static_cast<uint8_t>(kSolid | kPiece | kOccluder), 0);
					else
						AddProp(w, "Prop_WoodenFence_Single", p, yaw, 1.0, static_cast<uint8_t>(kSolid | kPiece), 1);
					w.Occupy(p, 1.2);
				}
				// Portes : arches + torches + bannières.
				for (double ga : angles)
				{
					const Vec2 dir = Dir(ga);
					const Vec2 tan{ -dir.z, dir.x };
					const Vec2 g = c.center + dir * rw;
					if (w.IsWater(g)) continue;
					const double yaw = std::atan2(tan.z, tan.x) * -180.0 / kPi;
					if (walls)
					{
						AddProp(w, "Wall_Arch", g + tan * 3.2, yaw, 1.0, static_cast<uint8_t>(kSolid | kPiece), 0);
						AddProp(w, "Wall_Arch", g - tan * 3.2, yaw, 1.0, static_cast<uint8_t>(kSolid | kPiece), 0);
						AddProp(w, "Banner_1", g + tan * 3.2 + dir * 0.35, YawFacing(dir * -1.0), 1.0, 0, 0, 0.0, 2.6);
						AddProp(w, "Banner_2", g - tan * 3.2 + dir * 0.35, YawFacing(dir * -1.0), 1.0, 0, 0, 0.0, 2.6);
						// Torches fixées sur la face extérieure des arches.
						AddProp(w, "Torch_Metal", g + tan * 3.2 + dir * 0.3, YawFacing(dir * -1.0), 1.0, 0, 1, 0.0, 2.0);
						AddProp(w, "Torch_Metal", g - tan * 3.2 + dir * 0.3, YawFacing(dir * -1.0), 1.0, 0, 1, 0.0, 2.0);
					}
				}
			}

			// --- Champs clôturés hors les murs (hameaux, villages, fiefs).
			if ((c.kind == "hameau" || c.kind == "village") && c.state != "ruine" && c.race != "demons")
			{
				const int fields = c.procedural == "ferme" ? 4 : (c.kind == "hameau" ? 2 : 3);
				int made = 0;
				for (int attempt = 0; attempt < 30 && made < fields; ++attempt)
				{
					const double a = rng.Range(0.0, 2.0 * kPi);
					const Vec2 dir = Dir(a);
					const Vec2 fc = c.center + dir * (R + rng.Range(14.0, 30.0));
					const double hw = 8.0, hd = 5.0;
					const double fr = std::hypot(hw, hd) + 1.0;
					const int ri = w.roadDist.IndexX(fc.x), rj = w.roadDist.IndexZ(fc.z);
					if (w.roadDist.InBounds(ri, rj) && w.roadDist.At(ri, rj) < fr) continue;
					if (w.IsOccupied(fc, fr) || !GroundOk(w, fc, fr)) continue;
					w.Occupy(fc, fr);
					++made;
					const Vec2 ax = Dir(a + kPi / 2.0), az = dir;
					const double yawAx = std::atan2(ax.z, ax.x) * -180.0 / kPi;
					const double yawAz = std::atan2(az.z, az.x) * -180.0 / kPi;
					// Clôture (segments de ~2 m) sur les quatre côtés, avec une entrée.
					for (double u = -hw + 1.0; u < hw; u += 2.06)
					{
						AddProp(w, "Prop_WoodenFence_Single", fc + ax * u + az * hd, yawAx, 1.0, static_cast<uint8_t>(kSolid | kPiece), 1);
						if (std::fabs(u) > 1.5)
							AddProp(w, "Prop_WoodenFence_Single", fc + ax * u - az * hd, yawAx, 1.0, static_cast<uint8_t>(kSolid | kPiece), 1);
					}
					for (double v = -hd + 1.0; v < hd; v += 2.06)
					{
						AddProp(w, "Prop_WoodenFence_Single", fc + az * v + ax * hw, yawAz, 1.0, static_cast<uint8_t>(kSolid | kPiece), 1);
						AddProp(w, "Prop_WoodenFence_Single", fc + az * v - ax * hw, yawAz, 1.0, static_cast<uint8_t>(kSolid | kPiece), 1);
					}
					// Rangs de cultures.
					const char* crop = (attempt % 2) ? "Plant_7" : "Carrot";
					for (double u = -hw + 1.5; u < hw - 1.0; u += 1.4)
						for (double v = -hd + 1.2; v < hd - 1.0; v += 1.1)
							AddProp(w, crop, fc + ax * u + az * v, rng.Range(0, 360), rng.Range(0.9, 1.4), 0, 2);
					AddProp(w, "FarmCrate_Carrot", fc - az * (hd + 1.2) + ax * 2.0, rng.Range(0, 360), 1.0, kSolid, 1, 0.4);
				}
			}

			// --- Cimetière / point de résurrection : hors les murs, près d'une route
			//     (villes du lore et hameaux ; pas pour les fermes, camps et ruines).
			if (c.procedural.empty() || c.procedural == "hameau")
			{
				const double a = angles.empty() ? 0.0 : angles[0] + 0.5;
				Vec2 gy = c.center + Dir(a) * (R + 12.0);
				for (int attempt = 0; attempt < 16; ++attempt)
				{
					if (!w.IsWater(gy) && !w.IsOccupied(gy, 4.0) && w.InPlayableBounds(gy, 60.0)) break;
					gy = c.center + Dir(a + attempt * 0.4) * (R + 12.0 + attempt);
				}
				L.graveyard = gy;
				w.Occupy(gy, 4.0);
				for (int k = 0; k < 6; ++k)
				{
					const Vec2 p = gy + Dir(k * kPi / 3.0 + 0.3) * 3.0;
					AddProp(w, (k % 2) ? "Pebble_Square_3" : "Rock_Medium_2", p, rng.Range(0, 360), (k % 2) ? 2.2 : 0.45, 0, 1);
				}
				AddProp(w, "CandleStick_Stand", gy, 0.0, 1.0, 0, 1);
			}
			else L.graveyard = c.center;

			// --- Accessoires de métier des lieux procéduraux.
			if (c.procedural == "bucherons" || c.procedural == "mine" || c.procedural == "bandits")
			{
				const char* bucheron[] = { "Anvil_Log", "Axe_Bronze", "Prop_Wagon", "Barrel", "Crate_Wooden" };
				const char* mine[] = { "Pickaxe_Bronze", "Prop_Wagon", "Crate_Wooden", "Bucket_Metal", "Barrel" };
				const char* bandit[] = { "Prop_Wagon", "Chest_Wood", "Cage_Small", "Barrel", "Crate_Wooden" };
				const char** set = c.procedural == "mine" ? mine : (c.procedural == "bandits" ? bandit : bucheron);
				for (int k = 0; k < 10; ++k)
				{
					const Vec2 p = c.center + Dir(rng.Range(0.0, 2.0 * kPi)) * rng.Range(R * 0.25, R * 0.8);
					if (w.IsOccupied(p, 1.4) || w.IsWater(p)) continue;
					const char* mesh = set[rng.Below(5)];
					const bool big = std::string(mesh) == "Prop_Wagon";
					AddProp(w, mesh, p, rng.Range(0, 360), 1.0, kSolid, 1, big ? 1.4 : 0.5);
					w.Occupy(p, big ? 2.0 : 0.8);
				}
				// Feu de camp (chaudron sur la place).
				AddProp(w, "Cauldron", c.center, 0.0, 1.2, kSolid, 0, 0.8);
			}

			// --- Point de départ (villes de départ) : sur la place, face à l'auberge.
			{
				const Vec2 toInn{ L.innDoor.x - c.center.x, L.innDoor.z - c.center.z };
				const double l = std::hypot(toInn.x, toInn.z);
				const Vec2 dir = l > 1e-3 ? toInn * (1.0 / l) : Vec2{ 0.0, -1.0 };
				L.startSpot = c.center - dir * 3.5;
				L.startYawDeg = YawFacing(dir);
			}

			// --- Pavage : rues et place (villes moyennes+ et forteresses), dalles rondes
			//     pour les villages ; les hameaux, camps et ruines restent en terre.
			if (c.state == "intact" && c.kind != "hameau" && c.kind != "camp" && c.procedural.empty())
			{
				const bool cobble = (c.kind != "village");
				const char* tile = cobble ? "RockPath_Square_Wide" : "RockPath_Round_Wide";
				for (const auto& st : streets) PaveAlong(w, st, 2, tile, R + 4.0, c.center);
				// Place : anneaux concentriques de dalles.
				for (double rr = 1.5; rr < Rp - 0.5; rr += 2.0)
				{
					std::vector<Vec2> circle;
					const int segs = std::max(6, static_cast<int>(2.0 * kPi * rr / 2.0));
					for (int k = 0; k <= segs; ++k) circle.push_back(c.center + Dir(k * 2.0 * kPi / segs) * rr);
					PaveAlong(w, circle, 1, tile, 0.0, c.center);
				}
				// Approches pavées des grandes routes (90 m hors les murs).
				if (cobble)
					for (const Road& r : w.roads)
						if (r.major && (r.fromId == c.id || r.toId == c.id))
						{
							std::vector<Vec2> seg;
							for (const Vec2& p : r.points)
							{
								const double d = std::hypot(p.x - c.center.x, p.z - c.center.z);
								if (d > R + 2.0 && d < R + 90.0) seg.push_back(p);
							}
							if (seg.size() > 2) PaveAlong(w, seg, 3, tile, 0.0, c.center);
						}
			}

			if (lotCount == 0 && c.state != "ruine" && c.procedural.empty())
				w.Warn("aucun lot bati pour " + c.id + " (terrain contraint)");
			w.layouts.push_back(std::move(L));
		}
		w.report.buildings = w.buildings.size();
	}
}
