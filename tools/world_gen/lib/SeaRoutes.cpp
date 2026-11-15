// tools/world_gen — trajets des bateaux des lignes maritimes (v1.0.125, décision du
// propriétaire du 15/11/2026 : « à bord en temps réel », 2 bateaux par ligne, LORE_BD §16.51).
//
// Deux temps :
//   1. MACRO (avant les cellules) : pour chaque étape d'une ligne, plus court chemin en mer
//      sur la grille grossière du relief (250 m) entre les eaux les plus proches des deux
//      villes ; les cellules traversées (et leurs voisines à moins de 1,5 km) sont ajoutées
//      à la génération (génération complète seulement) — sans elles le client n'aurait ni
//      eau ni décor sous le bateau ;
//   2. APRÈS les cellules : trajet définitif de quai à quai (quai = 30 m au large du ponton,
//      dans le regard du ponton), restreint aux cellules générées, simplifié en lignes
//      droites qui restent en mer ; écrit dans world/<monde>/sea_lines.json (lu par le shard).
//
// Fonctions pures du master et des pontons (déterministes, sans aléa).

#include "WorldGenContext.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <limits>
#include <queue>
#include <set>

namespace tools::world_gen
{
	namespace
	{
		namespace wc = engine::world::cells;
		constexpr double kPi = 3.14159265358979323846;
		/// Profondeur minimale (m sous la mer) d'un nœud navigable : tirant d'eau du dromon 1,55 m.
		constexpr double kMinDepthM = 3.0;
		/// Marge (m) autour du trajet macro : cellules voisines ajoutées à la génération.
		constexpr double kRouteCellMarginM = 1500.0;
		/// Rayon de recherche (m) de l'eau navigable la plus proche d'une ville ou d'un quai.
		constexpr double kSnapRadiusM = 15000.0;

		/// Vrai si le point (x, z) est navigable (relief grossier sous la mer de kMinDepthM).
		bool Navigable(const GlobalWorld& g, double x, double z)
		{
			return SampleBilinear(g.coarse, x, z) < g.master.scale.seaLevelM - kMinDepthM;
		}

		/// Masque de la PLEINE MER sur la grille grossière : nœuds navigables des composantes
		/// connexes (8 voisins) d'au moins kOpenSeaMinNodes nœuds — les cuvettes du relief
		/// sous le niveau de la mer (mares de quelques centaines de mètres) en sont exclues.
		constexpr size_t kOpenSeaMinNodes = 500; // ≈ 31 km²
		std::vector<uint8_t> OpenSeaMask(const GlobalWorld& g)
		{
			const Grid<double>& c = g.coarse;
			const double sea = g.master.scale.seaLevelM - kMinDepthM;
			const int W = c.width, H = c.height;
			std::vector<int> comp(static_cast<size_t>(W) * H, -1);
			std::vector<size_t> size;
			std::vector<int> stack;
			for (int s = 0; s < W * H; ++s)
			{
				if (comp[static_cast<size_t>(s)] >= 0 || c.data[static_cast<size_t>(s)] >= sea) continue;
				const int id = static_cast<int>(size.size());
				size.push_back(0);
				comp[static_cast<size_t>(s)] = id;
				stack.push_back(s);
				while (!stack.empty())
				{
					const int u = stack.back();
					stack.pop_back();
					++size.back();
					const int ui = u % W, uj = u / W;
					for (int dj = -1; dj <= 1; ++dj)
						for (int di = -1; di <= 1; ++di)
						{
							const int vi = ui + di, vj = uj + dj;
							if (vi < 0 || vj < 0 || vi >= W || vj >= H) continue;
							const size_t v = static_cast<size_t>(vj) * W + vi;
							if (comp[v] < 0 && c.data[v] < sea) { comp[v] = id; stack.push_back(static_cast<int>(v)); }
						}
				}
			}
			std::vector<uint8_t> mask(comp.size(), 0);
			for (size_t k = 0; k < comp.size(); ++k)
				mask[k] = comp[k] >= 0 && size[static_cast<size_t>(comp[k])] >= kOpenSeaMinNodes;
			return mask;
		}

		/// Nœud de pleine mer (\p openSea) de la grille grossière le plus proche de \p p
		/// (≤ kSnapRadiusM), accepté par \p allowed. \return false si aucun.
		bool NearestWaterNode(const GlobalWorld& g, const std::vector<uint8_t>& openSea, Vec2 p,
			const std::function<bool(int, int)>& allowed, int& oi, int& oj)
		{
			const Grid<double>& c = g.coarse;
			const int ci = c.IndexX(p.x), cj = c.IndexZ(p.z);
			const int r = static_cast<int>(kSnapRadiusM / c.step) + 1;
			double best = std::numeric_limits<double>::infinity();
			for (int j = cj - r; j <= cj + r; ++j)
				for (int i = ci - r; i <= ci + r; ++i)
				{
					if (!c.InBounds(i, j) || !allowed(i, j)) continue;
					if (!openSea[static_cast<size_t>(j) * c.width + i]) continue;
					const double d = std::hypot(c.WorldX(i) - p.x, c.WorldZ(j) - p.z);
					if (d < best) { best = d; oi = i; oj = j; }
				}
			return best <= kSnapRadiusM;
		}

		/// Plus court chemin (A*, 8 voisins) entre deux nœuds navigables de la grille
		/// grossière, en restant sur les nœuds acceptés par \p allowed. Le coût d'un pas
		/// est majoré près des côtes (les bateaux gardent le large quand ils le peuvent).
		/// \return points monde du chemin (vide si aucun).
		std::vector<Vec2> WaterAStar(const GlobalWorld& g, int si, int sj, int ti, int tj,
			const std::function<bool(int, int)>& allowed)
		{
			const Grid<double>& c = g.coarse;
			const double sea = g.master.scale.seaLevelM - kMinDepthM;
			const int W = c.width, H = c.height;
			auto water = [&](int i, int j) { return c.InBounds(i, j) && c.At(i, j) < sea && allowed(i, j); };
			auto land = [&](int i, int j) { return c.InBounds(i, j) && c.At(i, j) >= sea; };
			// Proximité d'une côte : +3 si une terre touche le nœud, +1 à deux pas.
			auto shore = [&](int i, int j) {
				double s = 0.0;
				for (int dj = -2; dj <= 2; ++dj)
					for (int di = -2; di <= 2; ++di)
						if (land(i + di, j + dj)) s = std::max(s, (std::abs(di) <= 1 && std::abs(dj) <= 1) ? 3.0 : 1.0);
				return s;
			};
			const size_t N = static_cast<size_t>(W) * static_cast<size_t>(H);
			std::vector<double> dist(N, std::numeric_limits<double>::infinity());
			std::vector<int> prev(N, -1);
			using QE = std::pair<double, int>;
			std::priority_queue<QE, std::vector<QE>, std::greater<QE>> pq;
			auto idx = [&](int i, int j) { return j * W + i; };
			auto heur = [&](int i, int j) { return std::hypot(i - ti, j - tj); };
			dist[static_cast<size_t>(idx(si, sj))] = 0.0;
			pq.push({ heur(si, sj), idx(si, sj) });
			const int target = idx(ti, tj);
			while (!pq.empty())
			{
				const auto [f, u] = pq.top();
				pq.pop();
				if (u == target) break;
				const int ui = u % W, uj = u / W;
				const double du = dist[static_cast<size_t>(u)];
				if (f - heur(ui, uj) > du + 1e-6) continue;
				for (int dj = -1; dj <= 1; ++dj)
					for (int di = -1; di <= 1; ++di)
					{
						if (di == 0 && dj == 0) continue;
						const int vi = ui + di, vj = uj + dj;
						if (!water(vi, vj)) continue;
						const double step = (di != 0 && dj != 0) ? 1.41421356 : 1.0;
						const double nd = du + step * (1.0 + shore(vi, vj));
						const int v = idx(vi, vj);
						if (nd < dist[static_cast<size_t>(v)])
						{
							dist[static_cast<size_t>(v)] = nd;
							prev[static_cast<size_t>(v)] = u;
							pq.push({ nd + heur(vi, vj), v });
						}
					}
			}
			std::vector<Vec2> out;
			if (prev[static_cast<size_t>(target)] < 0 && target != idx(si, sj)) return out;
			for (int v = target; v >= 0; v = prev[static_cast<size_t>(v)])
				out.push_back({ c.WorldX(v % W), c.WorldZ(v / W) });
			std::reverse(out.begin(), out.end());
			return out;
		}

		/// Vrai si le segment [a, b] reste navigable (échantillons tous les 40 m) et, si
		/// \p allowedPoint est donné, dans les cellules qu'il accepte.
		bool SegmentNavigable(const GlobalWorld& g, Vec2 a, Vec2 b, const std::function<bool(Vec2)>& allowedPoint)
		{
			const double len = std::hypot(b.x - a.x, b.z - a.z);
			const int n = std::max(1, static_cast<int>(std::ceil(len / 40.0)));
			for (int k = 0; k <= n; ++k)
			{
				const double t = static_cast<double>(k) / n;
				const Vec2 p{ a.x + (b.x - a.x) * t, a.z + (b.z - a.z) * t };
				if (!Navigable(g, p.x, p.z)) return false;
				if (allowedPoint && !allowedPoint(p)) return false;
			}
			return true;
		}

		/// Simplifie \p pts en lignes droites navigables (glouton : du point courant au plus
		/// lointain encore visible, segments de 6 km au plus). Extrémités conservées.
		std::vector<Vec2> Simplify(const GlobalWorld& g, const std::vector<Vec2>& pts, const std::function<bool(Vec2)>& allowedPoint)
		{
			if (pts.size() <= 2) return pts;
			std::vector<Vec2> out{ pts.front() };
			size_t i = 0;
			while (i + 1 < pts.size())
			{
				size_t best = i + 1;
				for (size_t j = i + 2; j < pts.size(); ++j)
				{
					if (std::hypot(pts[j].x - pts[i].x, pts[j].z - pts[i].z) > 6000.0) break;
					if (SegmentNavigable(g, pts[i], pts[j], allowedPoint)) best = j;
				}
				out.push_back(pts[best]);
				i = best;
			}
			return out;
		}

		/// Étapes d'une ligne : (k, k+1) ; plus (n-1, 0) si elle boucle (l'aller-retour
		/// repasse les mêmes étapes à l'envers).
		std::vector<std::pair<size_t, size_t>> Legs(const SeaLineDef& line)
		{
			std::vector<std::pair<size_t, size_t>> legs;
			for (size_t k = 0; k + 1 < line.stops.size(); ++k) legs.push_back({ k, k + 1 });
			if (line.loop && line.stops.size() > 2) legs.push_back({ line.stops.size() - 1, 0 });
			return legs;
		}

		/// Centre de la ville d'une escale, sinon centre de sa case.
		Vec2 StopAnchor(const GlobalWorld& g, const SeaLineStopDef& s)
		{
			if (const CityDef* c = g.FindCity(s.cityId)) return c->center;
			const wc::CellPoint mn = wc::CellMinCorner({ s.n, s.e });
			return { mn.x + wc::kCellSizeMeters * 0.5, mn.z + wc::kCellSizeMeters * 0.5 };
		}
	}

	bool NearestOpenSea(const GlobalWorld& g, Vec2 p, double radiusM, Vec2& out)
	{
		const Grid<double>& c = g.coarse;
		if (g.openSea.size() != c.data.size() || c.data.empty()) return false;
		const int ci = c.IndexX(p.x), cj = c.IndexZ(p.z);
		const int r = static_cast<int>(radiusM / c.step) + 1;
		double best = std::numeric_limits<double>::infinity();
		for (int j = cj - r; j <= cj + r; ++j)
			for (int i = ci - r; i <= ci + r; ++i)
			{
				if (!c.InBounds(i, j) || !g.openSea[static_cast<size_t>(j) * c.width + i]) continue;
				const double d = std::hypot(c.WorldX(i) - p.x, c.WorldZ(j) - p.z);
				if (d < best) { best = d; out = { c.WorldX(i), c.WorldZ(j) }; }
			}
		return best <= radiusM;
	}

	void BuildMacroSeaRoutes(GlobalWorld& g)
	{
		g.seaRoutes.clear();
		const auto any = [](int, int) { return true; };
		g.openSea = OpenSeaMask(g);
		const std::vector<uint8_t>& openSea = g.openSea;
		for (const SeaLineDef& line : g.master.seaLines)
			for (const auto& [a, b] : Legs(line))
			{
				GlobalWorld::SeaRoute r;
				r.lineId = line.id; r.from = a; r.to = b;
				// Ancre : pleine mer la plus proche de la ville DANS la case du ponton (le ponton y
				// est posé au plus près de la ville), sinon n'importe où.
				auto snap = [&](const SeaLineStopDef& st, int& oi, int& oj) {
					const auto inCell = [&](int i, int j) {
						const wc::CellCoord cc = wc::CellFromPosition(g.coarse.WorldX(i), g.coarse.WorldZ(j));
						return cc.n == st.n && cc.e == st.e;
					};
					return NearestWaterNode(g, openSea, StopAnchor(g, st), inCell, oi, oj)
						|| NearestWaterNode(g, openSea, StopAnchor(g, st), any, oi, oj);
				};
				int si = 0, sj = 0, ti = 0, tj = 0;
				if (snap(line.stops[a], si, sj) && snap(line.stops[b], ti, tj))
					r.points = WaterAStar(g, si, sj, ti, tj, any);
				if (r.points.empty())
					g.Warn("ligne maritime " + line.id + " : pas de trajet en mer de " + line.stops[a].cityId + " a " + line.stops[b].cityId);
				g.seaRoutes.push_back(std::move(r));
			}
	}

	size_t AddSeaRouteCells(GlobalWorld& g, std::vector<std::string>& ids)
	{
		std::set<std::string> have(ids.begin(), ids.end());
		size_t added = 0;
		auto addCell = [&](wc::CellCoord c) {
			if (!wc::IsInLoreGrid(c, g.master.cellMap)) return;
			const std::string id = wc::LoreCellId(c);
			if (!have.insert(id).second) return;
			ids.push_back(id);
			// Cellule de mer ouverte pour un bateau : villages seulement (aucun cimetière
			// isolé ni de hameau, le total de 666 ne bouge pas).
			g.master.graveyardVillageOnlyCells.insert({ c.n, c.e });
			++added;
		};
		for (const GlobalWorld::SeaRoute& r : g.seaRoutes)
			for (size_t k = 0; k + 1 < r.points.size(); ++k)
			{
				const Vec2 a = r.points[k], b = r.points[k + 1];
				const int n = std::max(1, static_cast<int>(std::ceil(std::hypot(b.x - a.x, b.z - a.z) / 100.0)));
				for (int s = 0; s <= n; ++s)
				{
					const double t = static_cast<double>(s) / n;
					const double x = a.x + (b.x - a.x) * t, z = a.z + (b.z - a.z) * t;
					for (int dz = -1; dz <= 1; ++dz)
						for (int dx = -1; dx <= 1; ++dx)
							addCell(wc::CellFromPosition(x + dx * kRouteCellMarginM, z + dz * kRouteCellMarginM));
				}
			}
		std::printf("[world_gen] lignes maritimes : %zu trajet(s), %zu cellule(s) de mer ajoutee(s) (%zu au total)\n",
			g.seaRoutes.size(), added, ids.size());
		return added;
	}

	bool BuildSeaLegPaths(GlobalWorld& g, const std::vector<World*>& cells, bool complete, std::string& err)
	{
		g.seaDocks.clear();
		g.seaLegs.clear();
		std::set<std::pair<int, int>> generated;
		for (const World* w : cells) generated.insert({ w->cell.n, w->cell.e });
		const Grid<double>& c = g.coarse;
		const auto allowedPoint = [&](Vec2 p) {
			const wc::CellCoord cc = wc::CellFromPosition(p.x, p.z);
			return generated.count({ cc.n, cc.e }) != 0;
		};
		const auto allowedNode = [&](int i, int j) { return c.InBounds(i, j) && allowedPoint({ c.WorldX(i), c.WorldZ(j) }); };
		for (const SeaLineDef& line : g.master.seaLines)
		{
			// Quais (calculés au ponton, BuildSeaPontons) : bateau parallèle à la côte.
			std::vector<const World::SeaPonton*> pontons(line.stops.size(), nullptr);
			for (const World* w : cells)
				for (const World::SeaPonton& sp : w->seaPontons)
					if (sp.lineId == line.id && sp.stopIndex < pontons.size()) pontons[sp.stopIndex] = &sp;
			bool allDocks = true;
			for (size_t k = 0; k < line.stops.size(); ++k)
			{
				if (!pontons[k]) { allDocks = false; continue; }
				GlobalWorld::SeaDock d;
				d.lineId = line.id; d.stop = k;
				d.position = pontons[k]->dock;
				d.yawDeg = std::fmod(pontons[k]->yawDeg + 90.0 + 360.0, 360.0);
				g.seaDocks.push_back(d);
			}
			if (!allDocks) continue; // sous-ensemble de cellules : pas de trajet (pas de sea_lines.json non plus)
			for (const auto& [a, b] : Legs(line))
			{
				GlobalWorld::SeaRoute leg;
				leg.lineId = line.id; leg.from = a; leg.to = b;
				// Quai → pleine mer (ligne droite vérifiée au ponton) → chemin en mer → quai.
				const World::SeaPonton& A = *pontons[a];
				const World::SeaPonton& B = *pontons[b];
				const Grid<double>& cg = g.coarse;
				const int si = cg.IndexX(A.openSea.x), sj = cg.IndexZ(A.openSea.z);
				const int ti = cg.IndexX(B.openSea.x), tj = cg.IndexZ(B.openSea.z);
				std::vector<Vec2> nodes;
				if (allowedNode(si, sj) && allowedNode(ti, tj))
					nodes = WaterAStar(g, si, sj, ti, tj, allowedNode);
				if (nodes.empty())
				{
					const std::string msg = "ligne maritime " + line.id + " : aucun trajet en mer (cellules generees) de "
						+ line.stops[a].cityId + " a " + line.stops[b].cityId;
					if (complete) { err = msg; return false; }
					g.Warn(msg);
					continue;
				}
				leg.points = { A.dock };
				for (const Vec2& p : Simplify(g, nodes, allowedPoint)) leg.points.push_back(p);
				leg.points.push_back(B.dock);
				g.seaLegs.push_back(std::move(leg));
			}
		}
		double total = 0.0;
		for (const GlobalWorld::SeaRoute& r : g.seaLegs)
			for (size_t k = 0; k + 1 < r.points.size(); ++k)
				total += std::hypot(r.points[k + 1].x - r.points[k].x, r.points[k + 1].z - r.points[k].z);
		if (!g.seaLegs.empty())
			std::printf("[world_gen] trajets des bateaux : %zu etape(s), %.1f km au total\n", g.seaLegs.size(), total / 1000.0);
		return true;
	}
}
