// tools/world_gen — réseau routier.
//
// 1. Graphe : arbre couvrant minimal (Kruskal) des agglomérations + liaisons
//    supplémentaires vers les 2 plus proches voisins (réseau maillé crédible).
// 2. Tracé : A* sur une grille de 10 m ; coût = distance × (pente, eau, relief),
//    les tronçons déjà tracés coûtent moins cher (les routes fusionnent en
//    grands axes au lieu de courir en parallèle).
// 3. Lissage (Chaikin) + profil en long lissé (pente ≤ 16 %), puis
//    aplanissement de la chaussée et des accotements dans la heightmap.

#include "WorldGenContext.h"
#include "Random.h"

#include <algorithm>
#include <cmath>
#include <queue>

namespace tools::world_gen
{
	std::vector<Vec2> SmoothPolyline(const std::vector<Vec2>& in, int iterations);
	std::vector<Vec2> ResamplePolyline(const std::vector<Vec2>& in, double stepM);

	namespace
	{
		bool IsMajor(const std::string& kind)
		{
			return kind == "capitale" || kind == "grande_ville" || kind == "forteresse";
		}

		struct Dsu
		{
			std::vector<int> p;
			explicit Dsu(size_t n) : p(n) { for (size_t i = 0; i < n; ++i) p[i] = static_cast<int>(i); }
			int Find(int x) { while (p[static_cast<size_t>(x)] != x) { p[static_cast<size_t>(x)] = p[static_cast<size_t>(p[static_cast<size_t>(x)])]; x = p[static_cast<size_t>(x)]; } return x; }
			bool Unite(int a, int b) { a = Find(a); b = Find(b); if (a == b) return false; p[static_cast<size_t>(std::max(a, b))] = std::min(a, b); return true; }
		};

		/// Grille de coûts du A* (10 m), recalculée une fois ; `roadBonus`
		/// est mis à jour après chaque route tracée.
		struct CostGrid
		{
			Grid<float> cost;      ///< coût par mètre (inf = infranchissable)
			Grid<uint8_t> onRoad;  ///< 1 si une route passe déjà là
		};

		CostGrid BuildCostGrid(const World& w)
		{
			CostGrid cg;
			const int factor = 2;
			const int n = (w.height.width - 1) / factor + 1;
			cg.cost.Init(n, n, w.height.originX, w.height.originZ, w.height.step * factor, 1.0f);
			cg.onRoad.Init(n, n, w.height.originX, w.height.originZ, w.height.step * factor, 0);
			const double sea = w.master.scale.seaLevelM;
			for (int j = 0; j < n; ++j)
				for (int i = 0; i < n; ++i)
				{
					const Vec2 p{ cg.cost.WorldX(i), cg.cost.WorldZ(j) };
					const double h = w.HeightAt(p);
					float c = 1.0f;
					if (h < sea + 0.8 || !w.InPlayableBounds(p, 40.0)) { cg.cost.At(i, j) = 1e30f; continue; }
					for (const Lake& l : w.lakes)
						if (std::hypot(p.x - l.center.x, p.z - l.center.z) < l.radiusM + 6.0) c = 1e30f;
					if (c > 1e29f) { cg.cost.At(i, j) = c; continue; }
					const double slope = w.SlopeDegAt(p);
					// Falaises infranchissables ; au-delà de ~25° le coût explose (lacets).
					if (slope > 46.0) { cg.cost.At(i, j) = 1e30f; continue; }
					c += static_cast<float>(std::pow(slope / 7.0, 2.0));
					// Préfère les basses terres (évite de franchir les cols inutilement).
					c += static_cast<float>(std::max(0.0, h - 70.0) * 0.03);
					// Franchir une rivière coûte (gué) : on évite de la longer dedans.
					const int ri = w.riverDist.IndexX(p.x), rj = w.riverDist.IndexZ(p.z);
					if (w.riverDist.InBounds(ri, rj) && w.riverDist.At(ri, rj) < 4.0f) c += 6.0f;
					// Marais : boueux.
					const char b = w.biome.At(w.biome.IndexX(p.x), w.biome.IndexZ(p.z));
					if (b == '%' || b == '&') c += 1.5f;
					if (b == 'T' || b == 'R' || b == 'L') c += 0.4f;
					cg.cost.At(i, j) = c;
				}
			return cg;
		}

		/// A* 8-connexe. Renvoie le chemin (centres de cases) ou vide.
		std::vector<Vec2> AStar(const CostGrid& cg, Vec2 from, Vec2 to)
		{
			const Grid<float>& g = cg.cost;
			const int si = g.IndexX(from.x), sj = g.IndexZ(from.z);
			const int ti = g.IndexX(to.x), tj = g.IndexZ(to.z);
			if (!g.InBounds(si, sj) || !g.InBounds(ti, tj)) return {};
			const size_t N = g.data.size();
			std::vector<float> gs(N, 1e30f);
			std::vector<int> parent(N, -1);
			std::vector<uint8_t> closed(N, 0);
			struct Node { float f; int idx; bool operator>(const Node& o) const { return f > o.f || (f == o.f && idx > o.idx); } };
			std::priority_queue<Node, std::vector<Node>, std::greater<Node>> open;
			auto idxOf = [&](int i, int j) { return j * g.width + i; };
			auto heur = [&](int i, int j) { return static_cast<float>(std::hypot(i - ti, j - tj) * g.step * 0.9); };
			const int s0 = idxOf(si, sj);
			gs[static_cast<size_t>(s0)] = 0.0f;
			open.push({ heur(si, sj), s0 });
			const int goal = idxOf(ti, tj);
			while (!open.empty())
			{
				const Node cur = open.top(); open.pop();
				if (closed[static_cast<size_t>(cur.idx)]) continue;
				closed[static_cast<size_t>(cur.idx)] = 1;
				if (cur.idx == goal) break;
				const int ci = cur.idx % g.width, cj = cur.idx / g.width;
				for (int dj = -1; dj <= 1; ++dj)
					for (int di = -1; di <= 1; ++di)
					{
						if (!di && !dj) continue;
						const int ni = ci + di, nj = cj + dj;
						if (!g.InBounds(ni, nj)) continue;
						const int nidx = idxOf(ni, nj);
						if (closed[static_cast<size_t>(nidx)]) continue;
						float c = g.At(ni, nj);
						// Les extrémités (dans les villes) restent toujours franchissables.
						if (c > 1e29f && nidx != goal) continue;
						if (c > 1e29f) c = 1.0f;
						if (cg.onRoad.At(ni, nj)) c *= 0.35f;
						const float len = static_cast<float>(((di && dj) ? 1.41421356 : 1.0) * g.step);
						const float ng = gs[static_cast<size_t>(cur.idx)] + c * len;
						if (ng < gs[static_cast<size_t>(nidx)])
						{
							gs[static_cast<size_t>(nidx)] = ng;
							parent[static_cast<size_t>(nidx)] = cur.idx;
							open.push({ ng + heur(ni, nj), nidx });
						}
					}
			}
			if (parent[static_cast<size_t>(goal)] < 0 && goal != s0) return {};
			std::vector<Vec2> path;
			for (int k = goal; k >= 0; k = parent[static_cast<size_t>(k)])
			{
				path.push_back({ g.WorldX(k % g.width), g.WorldZ(k / g.width) });
				if (k == s0) break;
			}
			std::reverse(path.begin(), path.end());
			return path;
		}
	}

	void BuildRoads(World& w)
	{
		auto& cities = w.master.cities;
		const size_t n = cities.size();
		struct Edge { double d; int a, b; };
		std::vector<Edge> all;
		for (size_t a = 0; a < n; ++a)
			for (size_t b = a + 1; b < n; ++b)
			{
				const double d = std::hypot(cities[a].center.x - cities[b].center.x, cities[a].center.z - cities[b].center.z);
				all.push_back({ d, static_cast<int>(a), static_cast<int>(b) });
			}
		std::sort(all.begin(), all.end(), [](const Edge& x, const Edge& y) {
			return x.d < y.d || (x.d == y.d && (x.a < y.a || (x.a == y.a && x.b < y.b)));
		});

		CostGrid cg = BuildCostGrid(w);
		Dsu dsu(n);
		std::vector<std::pair<int, int>> chosen;
		auto has = [&](int a, int b) {
			for (auto& e : chosen) if ((e.first == a && e.second == b) || (e.first == b && e.second == a)) return true;
			return false;
		};
		// Kruskal : on ne garde que les liaisons < 1800 m (au-delà : régions isolées).
		for (const Edge& e : all)
			if (e.d < 1800.0 && dsu.Unite(e.a, e.b)) chosen.push_back({ e.a, e.b });
		// Maillage : 2 plus proches voisins de chaque ville (si < 900 m).
		for (size_t a = 0; a < n; ++a)
		{
			int added = 0;
			for (const Edge& e : all)
			{
				if (added >= 2) break;
				if (e.a != static_cast<int>(a) && e.b != static_cast<int>(a)) continue;
				if (e.d > 900.0) break;
				if (!has(e.a, e.b)) chosen.push_back({ e.a, e.b });
				++added;
			}
		}
		// Les grandes routes d'abord (elles deviennent les axes que les autres rejoignent).
		std::stable_sort(chosen.begin(), chosen.end(), [&](const auto& x, const auto& y) {
			const int mx = IsMajor(cities[static_cast<size_t>(x.first)].kind) + IsMajor(cities[static_cast<size_t>(x.second)].kind);
			const int my = IsMajor(cities[static_cast<size_t>(y.first)].kind) + IsMajor(cities[static_cast<size_t>(y.second)].kind);
			return mx > my;
		});

		for (const auto& [ia, ib] : chosen)
		{
			const CityDef& A = cities[static_cast<size_t>(ia)];
			const CityDef& B = cities[static_cast<size_t>(ib)];
			std::vector<Vec2> raw = AStar(cg, A.center, B.center);
			if (raw.size() < 2)
			{
				w.Warn("route impossible : " + A.id + " -> " + B.id + " (relief/eau)");
				continue;
			}
			Road road;
			road.fromId = A.id; road.toId = B.id;
			road.major = IsMajor(A.kind) && IsMajor(B.kind);
			road.widthM = road.major ? 7.0 : 5.0;
			road.points = ResamplePolyline(SmoothPolyline(raw, 3), 4.0);
			road.points.front() = A.center;
			road.points.back() = B.center;
			// Marque les cases pour la réutilisation par les routes suivantes.
			for (const Vec2& p : road.points)
			{
				const int i = cg.onRoad.IndexX(p.x), j = cg.onRoad.IndexZ(p.z);
				for (int dj = -1; dj <= 1; ++dj)
					for (int di = -1; di <= 1; ++di)
						if (cg.onRoad.InBounds(i + di, j + dj)) cg.onRoad.At(i + di, j + dj) = 1;
			}
			w.roads.push_back(std::move(road));
		}

		// Profil en long : moyenne glissante puis limitation de pente.
		const double sea = w.master.scale.seaLevelM;
		for (Road& r : w.roads)
		{
			const size_t np = r.points.size();
			std::vector<double> raw(np);
			for (size_t k = 0; k < np; ++k) raw[k] = w.HeightAt(r.points[k]);
			r.profileY.assign(np, 0.0);
			const int win = 9;
			for (size_t k = 0; k < np; ++k)
			{
				double s = 0.0; int c = 0;
				for (int o = -win; o <= win; ++o)
				{
					const long q = static_cast<long>(k) + o;
					if (q < 0 || q >= static_cast<long>(np)) continue;
					s += raw[static_cast<size_t>(q)]; ++c;
				}
				r.profileY[k] = s / c;
			}
			// Dans les villes : plateau exact.
			for (const CityDef& c : w.master.cities)
				for (size_t k = 0; k < np; ++k)
				{
					const double d = std::hypot(r.points[k].x - c.center.x, r.points[k].z - c.center.z);
					if (d < c.radiusM + 6.0) r.profileY[k] = c.plateauY;
					else if (d < c.radiusM + 40.0)
						r.profileY[k] = Lerp(c.plateauY, r.profileY[k], SmoothStep(c.radiusM + 6.0, c.radiusM + 40.0, d));
				}
			// Pente max 16 % (4 m entre points → 0,64 m).
			const double maxDy = 0.64;
			for (int pass = 0; pass < 2; ++pass)
			{
				for (size_t k = 1; k < np; ++k)
					r.profileY[k] = Clamp(r.profileY[k], r.profileY[k - 1] - maxDy, r.profileY[k - 1] + maxDy);
				for (size_t k = np - 1; k-- > 0;)
					r.profileY[k] = Clamp(r.profileY[k], r.profileY[k + 1] - maxDy, r.profileY[k + 1] + maxDy);
			}
			for (size_t k = 0; k < np; ++k)
			{
				// Au-dessus de la mer et des rivières franchies (gué empierré).
				double floorY = sea + 0.8;
				const int ri = w.riverDist.IndexX(r.points[k].x), rj = w.riverDist.IndexZ(r.points[k].z);
				if (w.riverDist.InBounds(ri, rj) && w.riverDist.At(ri, rj) < 10.0f)
					for (const River& rv : w.rivers)
						for (size_t q = 0; q < rv.points.size(); q += 2)
							if (std::hypot(rv.points[q].x - r.points[k].x, rv.points[q].z - r.points[k].z) < 14.0)
								floorY = std::max(floorY, rv.waterY[q] + 0.5);
				r.profileY[k] = std::max(r.profileY[k], floorY);
			}
			for (size_t k = 1; k < np; ++k)
				w.report.roadLengthM += std::hypot(r.points[k].x - r.points[k - 1].x, r.points[k].z - r.points[k - 1].z);
		}

		// Aplanissement : chaque échantillon prend le profil du point de route le
		// plus proche, avec accotements en pente douce.
		w.roadDist.Init(w.height.width, w.height.height, w.height.originX, w.height.originZ, w.height.step, 1e9f);
		Grid<double> target;
		target.Init(w.height.width, w.height.height, w.height.originX, w.height.originZ, w.height.step, 0.0);
		Grid<float> halfW;
		halfW.Init(w.height.width, w.height.height, w.height.originX, w.height.originZ, w.height.step, 0.0f);
		for (const Road& r : w.roads)
		{
			const double half = r.widthM * 0.5;
			for (size_t k = 0; k + 1 < r.points.size(); ++k)
			{
				const Vec2 a = r.points[k], b = r.points[k + 1];
				const double reach = half + 9.0;
				const int i0 = std::max(0, w.height.IndexX(std::min(a.x, b.x) - reach)), i1 = std::min(w.height.width - 1, w.height.IndexX(std::max(a.x, b.x) + reach));
				const int j0 = std::max(0, w.height.IndexZ(std::min(a.z, b.z) - reach)), j1 = std::min(w.height.height - 1, w.height.IndexZ(std::max(a.z, b.z) + reach));
				for (int j = j0; j <= j1; ++j)
					for (int i = i0; i <= i1; ++i)
					{
						double t = 0.0;
						const double d = DistPointSegment({ w.height.WorldX(i), w.height.WorldZ(j) }, a, b, &t);
						if (d > reach) continue;
						float& rd = w.roadDist.At(i, j);
						if (d - half < rd)
						{
							rd = static_cast<float>(d - half);
							target.At(i, j) = Lerp(r.profileY[k], r.profileY[k + 1], t);
							halfW.At(i, j) = static_cast<float>(half);
						}
					}
			}
		}
		for (int j = 0; j < w.height.height; ++j)
			for (int i = 0; i < w.height.width; ++i)
			{
				const float rd = w.roadDist.At(i, j);
				if (rd > 9.0f) continue;
				const double t = SmoothStep(0.0, 9.0, rd);
				double& h = w.height.At(i, j);
				h = Lerp(target.At(i, j) - 0.05, h, t);
			}
		w.report.roads = w.roads.size();
	}
}
