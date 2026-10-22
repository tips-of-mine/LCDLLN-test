// tools/world_gen — réseau routier.
//
// MACRO (continent, une fois) :
// 1. Graphe : arbre couvrant minimal (Kruskal) des villes du lore + liaisons
//    vers les 2 plus proches voisines (réseau maillé crédible).
// 2. Tracé : A* sur la grille grossière de 250 m ; coût = distance × (pente,
//    eau, relief) ; les tronçons déjà tracés coûtent moins cher (les routes
//    fusionnent en grands axes au lieu de courir en parallèle).
// 3. Lissage (Chaikin) + profil en long GLOBAL (pente ≤ 16 %) calculé sur le
//    relief macro : chaque cellule aplanit sa portion de route au même profil,
//    les chaussées se raccordent donc exactement aux frontières.
//
// CELLULE :
// 4. Découpe des routes globales sur l'emprise de la cellule, aplanissement
//    de la chaussée et des accotements.
// 5. Chemins locaux (A* sur ≈ 10 m) reliant chaque lieu procédural au réseau.

#include "WorldGenContext.h"
#include "Parallel.h"
#include "Random.h"

#include <algorithm>
#include <cmath>
#include <queue>
#include <unordered_map>

namespace tools::world_gen
{
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

		/// Grille de coûts d'un A* : coût par mètre (≥ 1e29 = infranchissable)
		/// + marquage des cases déjà empruntées par une route.
		struct CostGrid
		{
			Grid<float> cost;
			Grid<uint8_t> onRoad;
		};

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

		/// Simplification de Ramer-Douglas-Peucker : supprime l'escalier du
		/// treillis A* (les routes ne suivent plus les axes de la grille).
		std::vector<Vec2> SimplifyRdp(const std::vector<Vec2>& in, double tol)
		{
			if (in.size() < 3) return in;
			std::vector<uint8_t> keep(in.size(), 0);
			keep.front() = keep.back() = 1;
			std::vector<std::pair<size_t, size_t>> stack{ { 0, in.size() - 1 } };
			while (!stack.empty())
			{
				const auto [a, b] = stack.back(); stack.pop_back();
				double best = -1.0; size_t bi = a;
				for (size_t k = a + 1; k < b; ++k)
				{
					const double d = DistPointSegment(in[k], in[a], in[b]);
					if (d > best) { best = d; bi = k; }
				}
				if (best > tol) { keep[bi] = 1; stack.push_back({ a, bi }); stack.push_back({ bi, b }); }
			}
			std::vector<Vec2> out;
			for (size_t k = 0; k < in.size(); ++k) if (keep[k]) out.push_back(in[k]);
			return out;
		}

		/// Index spatial (cases de 64 m) des points de rivière : franchissements.
		struct RiverIndex
		{
			struct Ref { const River* r; size_t k; };
			std::unordered_map<int64_t, std::vector<Ref>> cells;
			static int64_t Key(int x, int z) { return (static_cast<int64_t>(z) << 32) ^ static_cast<uint32_t>(x); }
			void Build(const std::vector<River>& rivers)
			{
				for (const River& r : rivers)
					for (size_t k = 0; k < r.points.size(); k += 2)
						cells[Key(static_cast<int>(std::floor(r.points[k].x / 64.0)), static_cast<int>(std::floor(r.points[k].z / 64.0)))].push_back({ &r, k });
			}
			/// Surface d'eau la plus haute à moins de \p radius du point, ou -1e9.
			double WaterYNear(Vec2 p, double radius) const
			{
				double best = -1e9;
				const int cx = static_cast<int>(std::floor(p.x / 64.0)), cz = static_cast<int>(std::floor(p.z / 64.0));
				for (int dz = -1; dz <= 1; ++dz)
					for (int dx = -1; dx <= 1; ++dx)
					{
						const auto it = cells.find(Key(cx + dx, cz + dz));
						if (it == cells.end()) continue;
						for (const Ref& ref : it->second)
						{
							const Vec2 q = ref.r->points[ref.k];
							if (std::hypot(q.x - p.x, q.z - p.z) < radius + ref.r->widthM[ref.k] * 0.5)
								best = std::max(best, ref.r->waterY[ref.k]);
						}
					}
				return best;
			}
		};

		/// Profil en long : moyenne glissante, plateaux urbains, pente ≤ \p maxGrade,
		/// au-dessus de la mer et des rivières franchies (gué empierré).
		void ComputeProfile(Road& r, const std::vector<double>& raw, const std::vector<CityDef>& cities,
			const RiverIndex& rivers, double sea, double maxGrade)
		{
			const size_t np = r.points.size();
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
			// Dans les agglomérations : plateau exact, raccord progressif.
			for (const CityDef& c : cities)
				for (size_t k = 0; k < np; ++k)
				{
					const double d = std::hypot(r.points[k].x - c.center.x, r.points[k].z - c.center.z);
					const double blend = std::max(40.0, c.radiusM * 0.6);
					if (d < c.radiusM + 6.0) r.profileY[k] = c.plateauY;
					else if (d < c.radiusM + 6.0 + blend)
						r.profileY[k] = Lerp(c.plateauY, r.profileY[k], SmoothStep(c.radiusM + 6.0, c.radiusM + 6.0 + blend, d));
				}
			const double maxDy = maxGrade * 4.0; // points tous les 4 m
			for (int pass = 0; pass < 2; ++pass)
			{
				for (size_t k = 1; k < np; ++k)
					r.profileY[k] = Clamp(r.profileY[k], r.profileY[k - 1] - maxDy, r.profileY[k - 1] + maxDy);
				for (size_t k = np - 1; k-- > 0;)
					r.profileY[k] = Clamp(r.profileY[k], r.profileY[k + 1] - maxDy, r.profileY[k + 1] + maxDy);
			}
			for (size_t k = 0; k < np; ++k)
				r.profileY[k] = std::max({ r.profileY[k], sea + 0.8, rivers.WaterYNear(r.points[k], 14.0) + 0.5 });
		}

		/// Aplanit la chaussée (+ accotements de 9 m) des routes dans la heightmap
		/// et met à jour la carte de distance à la chaussée.
		void FlattenRoads(World& w, const std::vector<const Road*>& roads)
		{
			Grid<double> target;
			target.Init(w.height.width, w.height.height, w.height.originX, w.height.originZ, w.height.step, 0.0);
			Grid<float> dist;
			dist.Init(w.height.width, w.height.height, w.height.originX, w.height.originZ, w.height.step, 1e9f);
			for (const Road* r : roads)
			{
				const double half = r->widthM * 0.5;
				for (size_t k = 0; k + 1 < r->points.size(); ++k)
				{
					const Vec2 a = r->points[k], b = r->points[k + 1];
					const double reach = half + 9.0;
					const int i0 = std::max(0, w.height.IndexX(std::min(a.x, b.x) - reach)), i1 = std::min(w.height.width - 1, w.height.IndexX(std::max(a.x, b.x) + reach));
					const int j0 = std::max(0, w.height.IndexZ(std::min(a.z, b.z) - reach)), j1 = std::min(w.height.height - 1, w.height.IndexZ(std::max(a.z, b.z) + reach));
					for (int j = j0; j <= j1; ++j)
						for (int i = i0; i <= i1; ++i)
						{
							double t = 0.0;
							const double d = DistPointSegment({ w.height.WorldX(i), w.height.WorldZ(j) }, a, b, &t);
							if (d > reach) continue;
							float& rd = dist.At(i, j);
							if (d - half < rd)
							{
								rd = static_cast<float>(d - half);
								target.At(i, j) = Lerp(r->profileY[k], r->profileY[k + 1], t);
							}
						}
				}
			}
			for (size_t q = 0; q < w.height.data.size(); ++q)
			{
				const float rd = dist.data[q];
				if (rd > 9.0f) continue;
				const double t = SmoothStep(0.0, 9.0, rd);
				w.height.data[q] = Lerp(target.data[q] - 0.05, w.height.data[q], t);
				w.roadDist.data[q] = std::min(w.roadDist.data[q], rd);
			}
		}
	}

	// ----------------------------------------------------------------- macro

	void BuildMacroRoads(GlobalWorld& g)
	{
		const WorldMaster& m = g.master;
		const double sea = m.scale.seaLevelM;
		auto& cities = g.cities;
		const size_t n = cities.size();

		// Grille de coûts (250 m) : relief macro + biome + rivières + lacs.
		CostGrid cg;
		cg.cost.Init(g.coarse.width, g.coarse.height, g.coarse.originX, g.coarse.originZ, g.coarse.step, 1.0f);
		cg.onRoad.Init(g.coarse.width, g.coarse.height, g.coarse.originX, g.coarse.originZ, g.coarse.step, 0);
		const double st = g.coarse.step;
		ParallelRows(g.coarse.height, [&](int j) {
			for (int i = 0; i < g.coarse.width; ++i)
			{
				const double h = g.coarse.At(i, j);
				if (h < sea + 0.8) { cg.cost.At(i, j) = 1e30f; continue; }
				const double hx = g.coarse.At(std::min(i + 1, g.coarse.width - 1), j) - g.coarse.At(std::max(i - 1, 0), j);
				const double hz = g.coarse.At(i, std::min(j + 1, g.coarse.height - 1)) - g.coarse.At(i, std::max(j - 1, 0));
				const double slope = std::atan(std::sqrt(hx * hx + hz * hz) / (2.0 * st)) * 57.29577951308232;
				// À 250 m, la pente est lissée : au-delà de ~24° le relief réel est infranchissable.
				if (slope > 24.0) { cg.cost.At(i, j) = 1e30f; continue; }
				float c = 1.0f + static_cast<float>(std::pow(slope / 4.0, 2.0));
				c += static_cast<float>(std::max(0.0, h - 160.0) * 0.004); // préfère les basses terres
				const char b = MacroBiome(m, g.coarse.WorldX(i), g.coarse.WorldZ(j));
				if (b == '%' || b == '&') c += 1.5f;
				if (b == 'T' || b == 'R' || b == 'L') c += 0.4f;
				cg.cost.At(i, j) = c;
			}
		});
		for (const Lake& l : g.lakes)
		{
			const double R = l.radiusM + 60.0;
			for (int j = std::max(0, cg.cost.IndexZ(l.center.z - R)); j <= std::min(cg.cost.height - 1, cg.cost.IndexZ(l.center.z + R)); ++j)
				for (int i = std::max(0, cg.cost.IndexX(l.center.x - R)); i <= std::min(cg.cost.width - 1, cg.cost.IndexX(l.center.x + R)); ++i)
					if (std::hypot(cg.cost.WorldX(i) - l.center.x, cg.cost.WorldZ(j) - l.center.z) < R) cg.cost.At(i, j) = 1e30f;
		}
		// Franchir une rivière coûte (gué) : on évite de la longer dedans.
		for (const River& r : g.rivers)
			for (size_t k = 0; k < r.points.size(); k += 20)
			{
				const int i = cg.cost.IndexX(r.points[k].x), j = cg.cost.IndexZ(r.points[k].z);
				if (cg.cost.InBounds(i, j) && cg.cost.At(i, j) < 1e29f) cg.cost.At(i, j) += 6.0f;
			}

		struct Edge { double d; int a, b; };
		std::vector<Edge> all;
		for (size_t a = 0; a < n; ++a)
			for (size_t b = a + 1; b < n; ++b)
				all.push_back({ std::hypot(cities[a].center.x - cities[b].center.x, cities[a].center.z - cities[b].center.z), static_cast<int>(a), static_cast<int>(b) });
		std::sort(all.begin(), all.end(), [](const Edge& x, const Edge& y) {
			return x.d < y.d || (x.d == y.d && (x.a < y.a || (x.a == y.a && x.b < y.b)));
		});
		Dsu dsu(n);
		std::vector<std::pair<int, int>> chosen;
		auto has = [&](int a, int b) {
			for (auto& e : chosen) if ((e.first == a && e.second == b) || (e.first == b && e.second == a)) return true;
			return false;
		};
		// Kruskal : liaisons < 14 cellules (au-delà : régions isolées par la mer).
		for (const Edge& e : all)
			if (e.d < 14.0 * engine::world::cells::kCellSizeMeters && dsu.Unite(e.a, e.b)) chosen.push_back({ e.a, e.b });
		// Maillage : 2 plus proches voisines de chaque ville (si < 7 cellules).
		for (size_t a = 0; a < n; ++a)
		{
			int added = 0;
			for (const Edge& e : all)
			{
				if (added >= 2) break;
				if (e.a != static_cast<int>(a) && e.b != static_cast<int>(a)) continue;
				if (e.d > 7.0 * engine::world::cells::kCellSizeMeters) break;
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

		RiverIndex rivers;
		rivers.Build(g.rivers);
		for (const auto& [ia, ib] : chosen)
		{
			const CityDef& A = cities[static_cast<size_t>(ia)];
			const CityDef& B = cities[static_cast<size_t>(ib)];
			std::vector<Vec2> raw = AStar(cg, A.center, B.center);
			if (raw.size() < 2)
			{
				g.Warn("route impossible : " + A.id + " -> " + B.id + " (relief/eau)");
				continue;
			}
			Road road;
			road.fromId = A.id; road.toId = B.id;
			road.major = IsMajor(A.kind) && IsMajor(B.kind);
			road.widthM = road.major ? 7.0 : 5.0;
			raw.front() = A.center;
			raw.back() = B.center;
			// Tracé naturel : (1) suppression de l'escalier du treillis (RDP),
			// (2) subdivision tous les 150 m, (3) méandres latéraux bruités
			// (longueur d'onde ≈ 2,5 km, ±110 m, nuls aux extrémités et sur l'eau),
			// (4) lissage de Chaikin.
			const std::vector<Vec2> simple = SimplifyRdp(raw, 180.0);
			std::vector<Vec2> dense;
			for (size_t k = 0; k + 1 < simple.size(); ++k)
			{
				const Vec2 a = simple[k], b = simple[k + 1];
				const int parts = std::max(1, static_cast<int>(std::ceil(std::hypot(b.x - a.x, b.z - a.z) / 150.0)));
				for (int q = 0; q < parts; ++q) dense.push_back(a + (b - a) * (static_cast<double>(q) / parts));
			}
			dense.push_back(simple.back());
			double total = 0.0;
			std::vector<double> along(dense.size(), 0.0);
			for (size_t k = 1; k < dense.size(); ++k) { total += std::hypot(dense[k].x - dense[k - 1].x, dense[k].z - dense[k - 1].z); along[k] = total; }
			std::vector<Vec2> meander = dense;
			for (size_t k = 1; k + 1 < dense.size(); ++k)
			{
				const Vec2 d = dense[k + 1] - dense[k - 1];
				const double l = std::hypot(d.x, d.z);
				if (l < 1e-6) continue;
				const double taper = Clamp(std::min(along[k], total - along[k]) / 600.0, 0.0, 1.0);
				const double off = Fbm(dense[k].x, dense[k].z, 2500.0, 2, m.seed + 4747) * 110.0 * taper;
				const Vec2 q = dense[k] + Vec2{ -d.z / l, d.x / l } * off;
				bool wet = SampleBilinear(g.coarse, q.x, q.z) < sea + 1.5;
				for (const Lake& lk : g.lakes)
					if (std::hypot(q.x - lk.center.x, q.z - lk.center.z) < lk.radiusM + 30.0) wet = true;
				if (!wet) meander[k] = q;
			}
			road.points = ResamplePolyline(SmoothPolyline(meander, 4), 4.0);
			road.points.front() = A.center;
			road.points.back() = B.center;
			for (const Vec2& p : road.points)
			{
				const int i = cg.onRoad.IndexX(p.x), j = cg.onRoad.IndexZ(p.z);
				if (cg.onRoad.InBounds(i, j)) cg.onRoad.At(i, j) = 1;
			}
			std::vector<double> ground(road.points.size());
			ParallelRows(static_cast<int>(road.points.size()), [&](int k) {
				ground[static_cast<size_t>(k)] = MacroHeight(m, road.points[static_cast<size_t>(k)].x, road.points[static_cast<size_t>(k)].z);
			});
			ComputeProfile(road, ground, g.cities, rivers, sea, 0.16);
			for (size_t k = 1; k < road.points.size(); ++k)
				g.report.roadLengthM += std::hypot(road.points[k].x - road.points[k - 1].x, road.points[k].z - road.points[k - 1].z);
			g.roads.push_back(std::move(road));
		}
		g.report.roads = g.roads.size();
	}

	// ----------------------------------------------------------------- cellule

	void ApplyRoads(World& w)
	{
		const double sea = w.master.scale.seaLevelM;
		w.roadDist.Init(w.height.width, w.height.height, w.height.originX, w.height.originZ, w.height.step, 1e9f);

		// 1) Routes globales (déjà découpées sur l'emprise par InitCell).
		{
			std::vector<const Road*> global;
			for (const Road& r : w.roads) global.push_back(&r);
			FlattenRoads(w, global);
		}

		// 2) Chemins locaux : chaque lieu procédural rejoint le réseau (point de
		//    route ou agglomération déjà reliée), A* sur ≈ 10 m.
		std::vector<const CityDef*> places;
		for (const CityDef& c : w.cities) if (!c.procedural.empty()) places.push_back(&c);
		if (places.empty()) return;

		CostGrid cg;
		const int factor = 2;
		const int n = (w.height.width - 1) / factor + 1;
		cg.cost.Init(n, n, w.height.originX, w.height.originZ, w.height.step * factor, 1.0f);
		cg.onRoad.Init(n, n, w.height.originX, w.height.originZ, w.height.step * factor, 0);
		ParallelRows(n, [&](int j) {
			for (int i = 0; i < n; ++i)
			{
				const Vec2 p{ cg.cost.WorldX(i), cg.cost.WorldZ(j) };
				if (w.IsWater(p) || !w.InPlayableBounds(p, 20.0)) { cg.cost.At(i, j) = 1e30f; continue; }
				const double slope = w.SlopeDegAt(p);
				if (slope > 40.0) { cg.cost.At(i, j) = 1e30f; continue; }
				float c = 1.0f + static_cast<float>(std::pow(slope / 6.0, 2.0));
				const int ri = w.roadDist.IndexX(p.x), rj = w.roadDist.IndexZ(p.z);
				if (w.roadDist.InBounds(ri, rj) && w.roadDist.At(ri, rj) < 2.0f) c *= 0.3f; // rejoindre la route
				cg.cost.At(i, j) = c;
			}
		});

		// Cibles : points des routes (1 sur 25) et centres des agglomérations du lore.
		struct Target { Vec2 p; std::string id; double y; };
		std::vector<Target> targets;
		for (const Road& r : w.roads)
			for (size_t k = 0; k < r.points.size(); k += 25)
				if (w.InPlayableBounds(r.points[k], 30.0)) targets.push_back({ r.points[k], std::string(), r.profileY[k] });
		for (const CityDef& c : w.cities)
			if (c.procedural.empty()) targets.push_back({ c.center, c.id, c.plateauY });

		std::vector<Road> paths;
		for (const CityDef* pl : places)
		{
			if (targets.empty())
			{
				// Cellule sans route ni ville : le premier lieu devient le noeud du réseau.
				targets.push_back({ pl->center, pl->id, pl->plateauY });
				continue;
			}
			const Target* best = nullptr;
			double bd = 1e18;
			for (const Target& t : targets)
			{
				const double d = std::hypot(t.p.x - pl->center.x, t.p.z - pl->center.z);
				if (d < bd && d > 1.0) { bd = d; best = &t; }
			}
			if (!best) continue;
			std::vector<Vec2> raw = AStar(cg, pl->center, best->p);
			if (raw.size() < 2) { w.Warn("chemin impossible vers " + pl->id); continue; }
			raw.front() = pl->center;
			raw.back() = best->p;
			Road road;
			road.fromId = pl->id; road.toId = best->id;
			road.path = true;
			road.widthM = 3.2;
			road.points = ResamplePolyline(SmoothPolyline(raw, 3), 4.0);
			road.points.front() = pl->center;
			road.points.back() = best->p;
			std::vector<double> ground(road.points.size());
			for (size_t k = 0; k < road.points.size(); ++k) ground[k] = w.HeightAt(road.points[k]);
			RiverIndex rivers;
			rivers.Build(w.rivers);
			std::vector<CityDef> ends;
			for (const CityDef& c : w.cities) if (c.id == pl->id || c.id == best->id) ends.push_back(c);
			ComputeProfile(road, ground, ends, rivers, sea, 0.18);
			road.profileY.back() = best->y;
			for (const Vec2& p : road.points)
			{
				const int i = cg.onRoad.IndexX(p.x), j = cg.onRoad.IndexZ(p.z);
				if (cg.onRoad.InBounds(i, j)) cg.onRoad.At(i, j) = 1;
			}
			// Le lieu relié devient une cible pour les suivants (réseau en arbre).
			// (id vide : un point de chemin n'est pas l'entrée d'une agglomération).
			for (size_t k = 25; k < road.points.size(); k += 25) targets.push_back({ road.points[k], std::string(), road.profileY[k] });
			targets.push_back({ pl->center, pl->id, pl->plateauY });
			paths.push_back(std::move(road));
		}
		std::vector<const Road*> local;
		for (const Road& r : paths) local.push_back(&r);
		FlattenRoads(w, local);
		for (Road& r : paths) w.roads.push_back(std::move(r));
	}
}
