// tools/world_gen — réseau routier.
//
// MACRO (continent, une fois) :
// 1. Réseau du GÉNÉRATEUR (historique) : arbre couvrant minimal (Kruskal) des
//    villes du lore + liaisons vers les 2 plus proches voisines ; A* sur la grille
//    grossière de 250 m ; coût = distance × (pente, eau, relief) ; les tronçons
//    déjà tracés coûtent moins cher (les routes fusionnent en grands axes).
//    Il sert toujours aux corridors des villes de départ (cellules jouables).
// 2. Routes du LORE (v1.0.105, `lore_roads` du master, recopiées par
//    tools/lore/sync_routes.py) : chaque route passe par chaque case de son tracé
//    (la ville du lore de la case, sinon le point praticable le plus proche du
//    centre) ; entre deux cases, le même A* suit le terrain. Une route coupe aux
//    villes qu'elle traverse (un tronçon par paire de villes : rues et approches
//    pavées des deux côtés). Embranchement (extrémité sans ville ni POI) : la route
//    part du point le plus proche d'une route du lore déjà tracée dans la case.
//    Les routes maritimes ne sont pas recopiées (aucun ponton dans le générateur).
// 3. Tri du réseau du générateur : une route n'est gardée que si elle fait partie
//    des corridors des villes de départ ET que les routes du lore ne relient pas
//    déjà ses deux villes à pied (cases jouables, détour ≤ 1,5 ×). Les autres
//    sont abandonnées : hors corridors, une case sans route du lore n'a pas de route.
// 4. Lissage (RDP, méandres, Chaikin) + profil en long GLOBAL (pente ≤ 16 %)
//    calculé sur le relief macro : chaque cellule aplanit sa portion de route au
//    même profil, les chaussées se raccordent donc exactement aux frontières.
//
// CELLULE :
// 5. Découpe des routes globales sur l'emprise de la cellule, aplanissement
//    de la chaussée et des accotements.
// 6. Chemins locaux (A* sur ≈ 10 m) reliant chaque lieu procédural au réseau.

#include "WorldGenContext.h"
#include "Parallel.h"
#include "Random.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <map>
#include <queue>
#include <set>
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
		/// \param partial  (v1.0.105) si le but est injoignable, renvoie le chemin vers le
		///                 nœud atteint le plus proche du but au lieu d'un chemin vide.
		/// \param allowed  (v1.0.105) masque des nœuds « dans le tracé » (même cadrage que
		///                 cg.cost) : hors masque, le coût est multiplié par 8 (la route du
		///                 lore reste dans ses cases et les cellules jouables). nullptr : aucun.
		std::vector<Vec2> AStar(const CostGrid& cg, Vec2 from, Vec2 to, bool partial = false, const Grid<uint8_t>* allowed = nullptr)
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
						if (allowed && !allowed->At(ni, nj)) c *= 8.0f;
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
			int end = goal;
			if (parent[static_cast<size_t>(goal)] < 0 && goal != s0)
			{
				if (!partial) return {};
				// Nœud fermé (atteint) le plus proche du but.
				double bd = 1e300;
				for (size_t q = 0; q < N; ++q)
					if (closed[q])
					{
						const double d = std::hypot(static_cast<int>(q % static_cast<size_t>(g.width)) - ti, static_cast<int>(q / static_cast<size_t>(g.width)) - tj);
						if (d < bd) { bd = d; end = static_cast<int>(q); }
					}
				if (end == goal) return {};
			}
			std::vector<Vec2> path;
			for (int k = end; k >= 0; k = parent[static_cast<size_t>(k)])
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
		/// Longueur (m) d'une polyligne.
		double PolylineLength(const std::vector<Vec2>& p)
		{
			double len = 0.0;
			for (size_t k = 1; k < p.size(); ++k) len += std::hypot(p[k].x - p[k - 1].x, p[k].z - p[k - 1].z);
			return len;
		}

		/// Tracé naturel (v1.0.105) d'une route du lore à partir des chemins bruts de
		/// l'A* entre ses ANCRES successives (cases du lore) : chaque segment est
		/// simplifié séparément (RDP, l'ancre reste un sommet), subdivisé tous les
		/// 150 m, écarté en méandres bruités (±110 m, longueur d'onde ≈ 2,5 km, comme le
		/// réseau du générateur) nuls à moins de 600 m d'une ancre et sur l'eau, puis
		/// lissé (Chaikin) et rééchantillonné tous les 4 m. Les deux extrémités sont
		/// exactes ; une ancre intermédiaire bouge de quelques dizaines de mètres au
		/// plus (coin arrondi) : elle reste dans sa case.
		/// \param segments chemins bruts successifs ; segments[i].front() est l'ancre i,
		///        segments.back().back() la dernière ancre.
		/// \param allowed  masque des nœuds du tracé (voir AStar) : un méandre ne fait pas
		///        sortir la route du masque.
		/// Pur (lit le relief macro, les lacs et la graine).
		std::vector<Vec2> NaturalizeThroughAnchors(const GlobalWorld& g, const std::vector<std::vector<Vec2>>& segments,
			const Grid<uint8_t>& allowed)
		{
			const WorldMaster& m = g.master;
			const double sea = m.scale.seaLevelM;
			std::vector<Vec2> simple;
			std::vector<size_t> anchorIdx;
			for (const std::vector<Vec2>& seg : segments)
			{
				const std::vector<Vec2> s = SimplifyRdp(seg, 180.0);
				if (simple.empty()) { simple = s; anchorIdx.push_back(0); continue; }
				anchorIdx.push_back(simple.size() - 1);
				simple.insert(simple.end(), s.begin() + 1, s.end());
			}
			anchorIdx.push_back(simple.size() - 1);
			std::vector<Vec2> dense;
			std::vector<size_t> denseAnchor;
			size_t nextAnchor = 0;
			for (size_t k = 0; k + 1 < simple.size(); ++k)
			{
				if (nextAnchor < anchorIdx.size() && anchorIdx[nextAnchor] == k) { denseAnchor.push_back(dense.size()); ++nextAnchor; }
				const Vec2 a = simple[k], b = simple[k + 1];
				const int parts = std::max(1, static_cast<int>(std::ceil(std::hypot(b.x - a.x, b.z - a.z) / 150.0)));
				for (int q = 0; q < parts; ++q) dense.push_back(a + (b - a) * (static_cast<double>(q) / parts));
			}
			denseAnchor.push_back(dense.size());
			dense.push_back(simple.back());
			std::vector<double> along(dense.size(), 0.0);
			for (size_t k = 1; k < dense.size(); ++k) along[k] = along[k - 1] + std::hypot(dense[k].x - dense[k - 1].x, dense[k].z - dense[k - 1].z);
			std::vector<Vec2> meander = dense;
			for (size_t k = 1; k + 1 < dense.size(); ++k)
			{
				const Vec2 d = dense[k + 1] - dense[k - 1];
				const double l = std::hypot(d.x, d.z);
				if (l < 1e-6) continue;
				double nearest = 1e18;
				for (size_t ai : denseAnchor) nearest = std::min(nearest, std::fabs(along[k] - along[ai]));
				const double taper = Clamp(nearest / 600.0, 0.0, 1.0);
				const double off = Fbm(dense[k].x, dense[k].z, 2500.0, 2, m.seed + 4747) * 110.0 * taper;
				const Vec2 q = dense[k] + Vec2{ -d.z / l, d.x / l } * off;
				bool wet = SampleBilinear(g.coarse, q.x, q.z) < sea + 1.5;
				for (const Lake& lk : g.lakes)
					if (std::hypot(q.x - lk.center.x, q.z - lk.center.z) < lk.radiusM + 30.0) wet = true;
				const int ai = allowed.IndexX(dense[k].x), aj = allowed.IndexZ(dense[k].z);
				const int qi = allowed.IndexX(q.x), qj = allowed.IndexZ(q.z);
				const bool leaves = allowed.InBounds(ai, aj) && allowed.At(ai, aj) && (!allowed.InBounds(qi, qj) || !allowed.At(qi, qj));
				if (!wet && !leaves) meander[k] = q;
			}
			std::vector<Vec2> out = ResamplePolyline(SmoothPolyline(meander, 4), 4.0);
			out.front() = segments.front().front();
			out.back() = segments.back().back();
			return out;
		}

		/// A* entre deux ancres d'une route du lore (v1.0.105). Si l'une d'elles est
		/// enclavée dans un relief infranchissable à 250 m (POI au pied d'une falaise,
		/// grotte côtière…), la recherche part du nœud franchissable le plus proche à
		/// moins de 1 500 m et va au plus près du but ; le chemin rejoint ensuite
		/// l'ancre exacte en ligne droite si elle est à moins de 3 km. Vide sinon.
		/// \param allowed masque du tracé (voir AStar).
		std::vector<Vec2> AStarLoose(const CostGrid& cg, Vec2 from, Vec2 to, const Grid<uint8_t>* allowed)
		{
			std::vector<Vec2> raw = AStar(cg, from, to, false, allowed);
			if (raw.size() >= 2) return raw;
			const Grid<float>& gr = cg.cost;
			auto nearestOpen = [&](Vec2 p) {
				const int ci = gr.IndexX(p.x), cj = gr.IndexZ(p.z);
				const int r = static_cast<int>(std::ceil(1500.0 / gr.step));
				Vec2 best = p;
				double bd = 1e18;
				for (int j = cj - r; j <= cj + r; ++j)
					for (int i = ci - r; i <= ci + r; ++i)
					{
						if (!gr.InBounds(i, j) || gr.At(i, j) > 1e29f) continue;
						const double d = std::hypot(gr.WorldX(i) - p.x, gr.WorldZ(j) - p.z);
						if (d < bd && d <= 1500.0) { bd = d; best = { gr.WorldX(i), gr.WorldZ(j) }; }
					}
				return best;
			};
			const Vec2 a = nearestOpen(from), b = nearestOpen(to);
			raw = AStar(cg, a, b, true, allowed);
			// But séparé du réseau franchissable par une lagune ou un bras de mer de moins
			// de 3 km (POI côtier) : le dernier tronçon y va tout droit, en chaussée (le
			// profil en long reste au-dessus de la mer).
			if (raw.size() < 2 || std::hypot(raw.back().x - to.x, raw.back().z - to.z) > 3000.0) return {};
			raw.insert(raw.begin(), from);
			const Vec2 last = raw.back();
			const int parts = std::max(1, static_cast<int>(std::ceil(std::hypot(to.x - last.x, to.z - last.z) / gr.step)));
			for (int q = 1; q <= parts; ++q) raw.push_back(last + (to - last) * (static_cast<double>(q) / parts));
			return raw;
		}

		/// Point praticable (v1.0.105) d'une case du lore pour une route : nœud de la
		/// grille de coûts franchissable le plus proche de \p wanted, à plus de 600 m des
		/// bords de la case (la route lissée y reste). Faux si la case n'en a aucun
		/// (mer, lac, crevasse…).
		bool PracticablePointInCell(const CostGrid& cg, int n, int e, Vec2 wanted, Vec2& out)
		{
			namespace wc = engine::world::cells;
			const wc::CellPoint mn = wc::CellMinCorner({ n, e });
			const double inset = 600.0, size = wc::kCellSizeMeters;
			const Grid<float>& gr = cg.cost;
			const int i0 = std::max(0, gr.IndexX(mn.x + inset)), i1 = std::min(gr.width - 1, gr.IndexX(mn.x + size - inset));
			const int j0 = std::max(0, gr.IndexZ(mn.z + inset)), j1 = std::min(gr.height - 1, gr.IndexZ(mn.z + size - inset));
			double best = 1e18;
			for (int j = j0; j <= j1; ++j)
				for (int i = i0; i <= i1; ++i)
				{
					const Vec2 p{ gr.WorldX(i), gr.WorldZ(j) };
					if (p.x < mn.x + inset || p.x > mn.x + size - inset || p.z < mn.z + inset || p.z > mn.z + size - inset) continue;
					if (gr.At(i, j) > 1e29f) continue;
					const double d = std::hypot(p.x - wanted.x, p.z - wanted.z);
					if (d < best) { best = d; out = p; }
				}
			return best < 1e17;
		}

		/// Est-ce une grande agglomération (routes larges, approches pavées) ?
		bool IsMajorId(const GlobalWorld& g, const std::string& id)
		{
			const CityDef* c = id.empty() ? nullptr : g.FindCity(id);
			return c && IsMajor(c->kind);
		}

		/// Routes du lore (v1.0.105) : tracé case par case, puis tri du réseau du
		/// générateur (voir l'en-tête du fichier, étapes 2 et 3).
		/// \param cg       grille de coûts du continent (250 m) ; son marquage des routes
		///                 (onRoad) est remis à zéro puis rempli par les routes du lore.
		/// \param network  réseau du générateur déjà tracé (devient g.generatorRoads).
		/// Effet de bord : remplit g.roads (routes du lore, puis routes du générateur
		/// gardées) et g.generatorRoads ; avertissements dans g.report.
		void BuildLoreRoads(GlobalWorld& g, CostGrid& cg, const RiverIndex& rivers, std::vector<Road> network)
		{
			namespace wc = engine::world::cells;
			const WorldMaster& m = g.master;
			const double sea = m.scale.seaLevelM;
			std::fill(cg.onRoad.data.begin(), cg.onRoad.data.end(), static_cast<uint8_t>(0));

			/// Ancre d'une route : point imposé (ville, POI, point praticable d'une case).
			struct Anchor
			{
				Vec2 p;
				std::string id;       ///< ville ou POI du master (vide : case seule / jonction)
				bool city = false;    ///< ancre sur une ville (la route y est coupée)
				bool ok = false;      ///< ancre résolue
				int snapPiece = -1;   ///< jonction : tronçon hôte (index dans pieces)
				size_t snapK = 0;     ///< jonction : point de raccord sur l'hôte
				bool corner = false;  ///< coin commun de deux cases en diagonale
				wc::CellCoord ca{}, cb{}; ///< coin : les deux cases qu'il relie
			};
			/// Raccord d'un embranchement sur sa route hôte (graphe de connexité).
			struct Snap { std::string node; size_t host; size_t k; };
			std::vector<Road> pieces;
			std::vector<Snap> snaps;
			std::vector<std::string> pieceFrom, pieceTo; // nœuds du graphe (ids ou jonctions)

			// Position d'une extrémité nommée (ville, sinon POI) ; faux si inconnue.
			auto entityAnchor = [&](const std::string& id, Anchor& a) {
				if (id.empty()) return false;
				if (const CityDef* c = g.FindCity(id)) { a.p = c->center; a.id = id; a.city = true; a.ok = true; return true; }
				for (const PoiDef& poi : g.pois)
					if (poi.id == id) { a.p = poi.center; a.id = id; a.ok = true; return true; }
				return false;
			};
			// Ancre d'une case de passage : ville du lore de la case (la plus proche du
			// centre), sinon point praticable le plus proche du centre.
			auto cellAnchor = [&](int n, int e, Anchor& a) {
				const wc::CellPoint ctr = wc::CellCenter({ n, e });
				const CityDef* best = nullptr;
				double bd = 1e18;
				for (const CityDef& c : g.cities)
					if (c.n == n && c.e == e && c.procedural.empty())
					{
						const double d = std::hypot(c.center.x - ctr.x, c.center.z - ctr.z);
						if (d < bd) { bd = d; best = &c; }
					}
				if (best) { a.p = best->center; a.id = best->id; a.city = true; a.ok = true; return true; }
				a.ok = PracticablePointInCell(cg, n, e, { ctr.x, ctr.z }, a.p);
				return a.ok;
			};
			// Extrémité sans ville ni POI : jonction sur une route du lore déjà tracée
			// dans la case (point le plus proche de l'ancre voisine), sinon point
			// praticable de la case.
			auto bareAnchor = [&](int n, int e, Vec2 toward, Anchor& a) {
				double bd = 1e18;
				for (size_t pi = 0; pi < pieces.size(); ++pi)
					for (size_t k = 0; k < pieces[pi].points.size(); ++k)
					{
						const Vec2 q = pieces[pi].points[k];
						const wc::CellCoord c = wc::CellFromPosition(q.x, q.z);
						if (c.n != n || c.e != e) continue;
						const double d = std::hypot(q.x - toward.x, q.z - toward.z);
						if (d < bd) { bd = d; a.p = q; a.snapPiece = static_cast<int>(pi); a.snapK = k; }
					}
				if (a.snapPiece >= 0) { a.ok = true; return true; }
				const wc::CellPoint ctr = wc::CellCenter({ n, e });
				a.ok = PracticablePointInCell(cg, n, e, { ctr.x, ctr.z }, a.p);
				return a.ok;
			};

			// Ordre : routes aux deux extrémités nommées d'abord, embranchements ensuite
			// (ils se raccordent aux routes déjà tracées) ; ordre du master sinon.
			std::vector<size_t> order;
			// Cellules jouables : generated_cells + corridors du réseau du GÉNÉRATEUR
			// (mêmes cellules qu'avant v1.0.105, cf. AddStartCorridors).
			std::set<std::string> playable(m.generatedCells.begin(), m.generatedCells.end());
			std::vector<size_t> corridor;
			if (m.connectStartCities)
			{
				corridor = StartCorridorRoads(g, network, false);
				std::vector<std::string> ids = m.generatedCells;
				AddCorridorCells(g, network, corridor, ids);
				playable.insert(ids.begin(), ids.end());
			}
			// Masque des nœuds de la grille de coûts dans une cellule jouable (base du
			// masque « dans le tracé » de chaque route, cf. AStar).
			Grid<uint8_t> playableMask;
			playableMask.Init(cg.cost.width, cg.cost.height, cg.cost.originX, cg.cost.originZ, cg.cost.step, 0);
			for (int j = 0; j < playableMask.height; ++j)
				for (int i = 0; i < playableMask.width; ++i)
					playableMask.At(i, j) = playable.count(wc::LoreCellId(wc::CellFromPosition(playableMask.WorldX(i), playableMask.WorldZ(j)))) ? 1 : 0;
			// Ajoute au masque \p mask les nœuds de la case (n, e).
			auto markCell = [&](Grid<uint8_t>& mask, int n, int e) {
				const wc::CellPoint mn = wc::CellMinCorner({ n, e });
				const double size = wc::kCellSizeMeters;
				for (int j = std::max(0, mask.IndexZ(mn.z)); j <= std::min(mask.height - 1, mask.IndexZ(mn.z + size)); ++j)
					for (int i = std::max(0, mask.IndexX(mn.x)); i <= std::min(mask.width - 1, mask.IndexX(mn.x + size)); ++i)
					{
						const wc::CellCoord c = wc::CellFromPosition(mask.WorldX(i), mask.WorldZ(j));
						if (c.n == n && c.e == e) mask.At(i, j) = 1;
					}
			};
			for (int pass = 0; pass < 2; ++pass)
				for (size_t li = 0; li < m.loreRoads.size(); ++li)
				{
					const LoreRoadDef& d = m.loreRoads[li];
					const bool named = !d.fromId.empty() && !d.toId.empty();
					if (named == (pass == 0)) order.push_back(li);
				}

			size_t tracedRoads = 0, failedSegments = 0, skippedCells = 0;
			for (size_t li : order)
			{
				const LoreRoadDef& d = m.loreRoads[li];
				const size_t n = d.waypoints.size();
				// Masque du tracé : cellules jouables + cases de la route.
				Grid<uint8_t> allowed = playableMask;
				for (const auto& [cn, ce] : d.cellNE) markCell(allowed, cn, ce);
				std::vector<Anchor> an(n);
				entityAnchor(d.fromId, an.front());
				entityAnchor(d.toId, an.back());
				for (size_t k = 1; k + 1 < n; ++k)
					if (!cellAnchor(d.cellNE[k].first, d.cellNE[k].second, an[k]))
					{
						++skippedCells;
						g.Warn("route du lore " + d.id + " : case " + d.waypoints[k] + " sans point praticable (ignoree)");
					}
				// Extrémités sans ville ni POI : vers l'ancre résolue la plus proche.
				auto neighbour = [&](size_t from, int step) {
					for (long k = static_cast<long>(from) + step; k >= 0 && k < static_cast<long>(n); k += step)
						if (an[static_cast<size_t>(k)].ok) return an[static_cast<size_t>(k)].p;
					const wc::CellPoint c = wc::CellCenter({ d.cellNE[n - 1 - from].first, d.cellNE[n - 1 - from].second });
					return Vec2{ c.x, c.z };
				};
				if (!an.front().ok && !bareAnchor(d.cellNE.front().first, d.cellNE.front().second, neighbour(0, 1), an.front()))
					g.Warn("route du lore " + d.id + " : depart " + d.waypoints.front() + " sans point praticable");
				if (!an.back().ok && !bareAnchor(d.cellNE.back().first, d.cellNE.back().second, neighbour(n - 1, -1), an.back()))
					g.Warn("route du lore " + d.id + " : arrivee " + d.waypoints.back() + " sans point praticable");
				// Ancres retenues (résolues, sans doublon consécutif).
				std::vector<Anchor> pts;
				for (const Anchor& a : an)
				{
					if (!a.ok) continue;
					if (!pts.empty() && std::hypot(a.p.x - pts.back().p.x, a.p.z - pts.back().p.z) < 1.0)
					{
						if (pts.back().id.empty()) pts.back() = a; // garder l'ancre nommée
						continue;
					}
					pts.push_back(a);
				}
				if (pts.size() < 2) { g.Warn("route du lore " + d.id + " : moins de deux points praticables (non tracee)"); continue; }
				// Pas en diagonale entre deux cellules jouables dont une des deux voisines
				// communes ne l'est pas : la route passe par le coin commun (sinon elle
				// pourrait couper le coin d'une cellule non générée, derrière le mur invisible).
				{
					std::vector<Anchor> withCorners;
					for (size_t i = 0; i < pts.size(); ++i)
					{
						withCorners.push_back(pts[i]);
						if (i + 1 >= pts.size()) break;
						const wc::CellCoord ca = wc::CellFromPosition(pts[i].p.x, pts[i].p.z);
						const wc::CellCoord cb = wc::CellFromPosition(pts[i + 1].p.x, pts[i + 1].p.z);
						if (std::abs(ca.n - cb.n) != 1 || std::abs(ca.e - cb.e) != 1) continue;
						auto isPlayable = [&](wc::CellCoord c) { return playable.count(wc::LoreCellId(c)) > 0; };
						if (!isPlayable(ca) || !isPlayable(cb) || (isPlayable({ ca.n, cb.e }) && isPlayable({ cb.n, ca.e }))) continue;
						const wc::CellPoint ma = wc::CellMinCorner(ca), mb = wc::CellMinCorner(cb);
						Anchor corner;
						corner.p = { std::max(ma.x, mb.x), std::max(ma.z, mb.z) };
						const int ci = cg.cost.IndexX(corner.p.x), cj = cg.cost.IndexZ(corner.p.z);
						if (!cg.cost.InBounds(ci, cj) || cg.cost.At(ci, cj) > 1e29f) continue;
						corner.ok = true;
						corner.corner = true;
						corner.ca = ca; corner.cb = cb;
						withCorners.push_back(corner);
					}
					pts.swap(withCorners);
				}

				// Tronçons : coupés aux villes traversées et aux segments sans chemin.
				bool any = false;
				size_t pieceStart = 0;
				std::vector<std::vector<Vec2>> segs;
				auto finish = [&](size_t endIdx) {
					if (segs.empty()) return;
					const Anchor& A = pts[pieceStart];
					const Anchor& B = pts[endIdx];
					Road road;
					road.loreId = d.id;
					road.fromId = A.id; road.toId = B.id;
					road.path = d.type == "path";
					road.major = d.type == "imperial_road" || (d.type == "road" && IsMajorId(g, A.id) && IsMajorId(g, B.id));
					road.widthM = road.path ? 3.2 : (road.major ? 7.0 : 5.0);
					road.points = NaturalizeThroughAnchors(g, segs, allowed);
					// Le lissage arrondit le coin d'un pas en diagonale : les points à moins de
					// 50 m du coin qui tombent dans une autre case sont ramenés (au mètre près)
					// dans la plus proche des deux cases reliées.
					for (size_t ai = pieceStart; ai <= endIdx; ++ai)
					{
						if (!pts[ai].corner) continue;
						const Anchor& C = pts[ai];
						for (Vec2& q : road.points)
						{
							if (std::hypot(q.x - C.p.x, q.z - C.p.z) > 50.0) continue;
							const wc::CellCoord cq = wc::CellFromPosition(q.x, q.z);
							if ((cq.n == C.ca.n && cq.e == C.ca.e) || (cq.n == C.cb.n && cq.e == C.cb.e)) continue;
							Vec2 best = q;
							double bd = 1e18;
							for (const wc::CellCoord& c : { C.ca, C.cb })
							{
								const wc::CellPoint mn = wc::CellMinCorner(c);
								const Vec2 in{ Clamp(q.x, mn.x + 0.5, mn.x + wc::kCellSizeMeters - 0.5), Clamp(q.z, mn.z + 0.5, mn.z + wc::kCellSizeMeters - 0.5) };
								const double dd = std::hypot(in.x - q.x, in.z - q.z);
								if (dd < bd) { bd = dd; best = in; }
							}
							q = best;
						}
					}
					for (const Vec2& p : road.points)
					{
						const int i = cg.onRoad.IndexX(p.x), j = cg.onRoad.IndexZ(p.z);
						if (cg.onRoad.InBounds(i, j)) cg.onRoad.At(i, j) = 1;
					}
					std::vector<double> ground(road.points.size());
					ParallelRows(static_cast<int>(road.points.size()), [&](int k) {
						ground[static_cast<size_t>(k)] = MacroHeight(m, road.points[static_cast<size_t>(k)].x, road.points[static_cast<size_t>(k)].z);
					});
					ComputeProfile(road, ground, g.cities, rivers, sea, road.path ? 0.18 : 0.16);
					const size_t pi = pieces.size();
					const std::string nodeA = A.id.empty() ? "#" + d.id + ":" + std::to_string(pi) + ":a" : A.id;
					const std::string nodeB = B.id.empty() ? "#" + d.id + ":" + std::to_string(pi) + ":b" : B.id;
					if (A.snapPiece >= 0) snaps.push_back({ nodeA, static_cast<size_t>(A.snapPiece), A.snapK });
					if (B.snapPiece >= 0) snaps.push_back({ nodeB, static_cast<size_t>(B.snapPiece), B.snapK });
					pieceFrom.push_back(nodeA); pieceTo.push_back(nodeB);
					pieces.push_back(std::move(road));
					segs.clear();
					any = true;
				};
				for (size_t i = 0; i + 1 < pts.size(); ++i)
				{
					std::vector<Vec2> raw = AStarLoose(cg, pts[i].p, pts[i + 1].p, &allowed);
					// Détour absurde (case enclavée dans un massif : le chemin ferait le tour
					// du continent) : traité comme une absence de chemin.
					const double straight = std::hypot(pts[i + 1].p.x - pts[i].p.x, pts[i + 1].p.z - pts[i].p.z);
					if (raw.size() >= 2 && PolylineLength(raw) > 3.0 * straight + 3000.0) raw.clear();
					if (raw.size() < 2)
					{
						++failedSegments;
						const wc::CellCoord ca = wc::CellFromPosition(pts[i].p.x, pts[i].p.z), cb = wc::CellFromPosition(pts[i + 1].p.x, pts[i + 1].p.z);
						g.Warn("route du lore " + d.id + " : pas de chemin " + wc::LoreCellId(ca) + " -> " + wc::LoreCellId(cb) + " (relief/eau ou detour > 3x, troncon coupe)");
						finish(i);
						pieceStart = i + 1;
						continue;
					}
					raw.front() = pts[i].p;
					raw.back() = pts[i + 1].p;
					segs.push_back(std::move(raw));
					if (pts[i + 1].city && i + 2 < pts.size()) { finish(i + 1); pieceStart = i + 1; }
				}
				finish(pts.size() - 1);
				if (any) ++tracedRoads;
			}

			// Graphe des routes du lore praticables à pied (tronçons entièrement dans des
			// cellules jouables) : nœuds = villes, POI, jonctions ; arêtes = longueurs.
			std::map<std::string, std::vector<std::pair<std::string, double>>> adj;
			std::vector<char> walkable(pieces.size(), 0);
			for (size_t pi = 0; pi < pieces.size(); ++pi)
			{
				const std::vector<Vec2>& pts = pieces[pi].points;
				bool in = true;
				for (size_t k = 0; in && k < pts.size(); k += (k + 8 < pts.size() ? 8 : 1))
					in = playable.count(wc::LoreCellId(wc::CellFromPosition(pts[k].x, pts[k].z))) > 0;
				walkable[pi] = in;
				if (!in) continue;
				const double len = PolylineLength(pts);
				adj[pieceFrom[pi]].push_back({ pieceTo[pi], len });
				adj[pieceTo[pi]].push_back({ pieceFrom[pi], len });
			}
			for (const Snap& sn : snaps)
			{
				if (!walkable[sn.host]) continue;
				const std::vector<Vec2>& hp = pieces[sn.host].points;
				const std::vector<Vec2> head(hp.begin(), hp.begin() + static_cast<long>(sn.k) + 1);
				const double toA = PolylineLength(head), toB = PolylineLength(hp) - toA;
				adj[sn.node].push_back({ pieceFrom[sn.host], toA }); adj[pieceFrom[sn.host]].push_back({ sn.node, toA });
				adj[sn.node].push_back({ pieceTo[sn.host], toB }); adj[pieceTo[sn.host]].push_back({ sn.node, toB });
			}
			// Distance à pied (m) entre deux villes par les routes du lore (∞ si aucune).
			auto loreDistance = [&](const std::string& a, const std::string& b) {
				std::map<std::string, double> dist;
				using QE = std::pair<double, std::string>;
				std::priority_queue<QE, std::vector<QE>, std::greater<QE>> pq;
				dist[a] = 0.0; pq.push({ 0.0, a });
				while (!pq.empty())
				{
					const auto [du, u] = pq.top(); pq.pop();
					if (u == b) return du;
					if (du > dist[u]) continue;
					const auto it = adj.find(u);
					if (it == adj.end()) continue;
					for (const auto& [v, w] : it->second)
					{
						const auto dv = dist.find(v);
						if (dv == dist.end() || du + w < dv->second) { dist[v] = du + w; pq.push({ du + w, v }); }
					}
				}
				return std::numeric_limits<double>::infinity();
			};
			// Tri du réseau du générateur : gardée = corridor de départ sans équivalent
			// au lore (le lore ne relie pas ses deux villes à pied en ≤ 1,5 × sa longueur).
			std::vector<char> keep(network.size(), 0);
			size_t replaced = 0;
			for (size_t r : corridor)
			{
				if (keep[r]) continue;
				const Road& gr = network[r];
				if (loreDistance(gr.fromId, gr.toId) <= 1.5 * PolylineLength(gr.points)) { ++replaced; continue; }
				keep[r] = 1;
			}
			g.roads = std::move(pieces);
			size_t kept = 0;
			for (size_t r = 0; r < network.size(); ++r)
				if (keep[r]) { g.roads.push_back(network[r]); ++kept; }
			std::printf("[world_gen] routes du lore : %zu/%zu tracee(s) en %zu troncon(s) (%zu case(s) sans point praticable, %zu segment(s) sans chemin) ; "
				"reseau du generateur : %zu route(s) gardee(s) pour les departs, %zu remplacee(s) par le lore, %zu abandonnee(s)\n",
				tracedRoads, m.loreRoads.size(), g.roads.size() - kept, skippedCells, failedSegments,
				kept, replaced, network.size() - kept - replaced);
			g.generatorRoads = std::move(network);
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
		// Crevasse (v1.0.72) : à 250 m, la grille lisse ses parois ; le massif qui
		// l'entoure est donc fermé explicitement aux routes, qui n'entrent et ne
		// sortent que par l'ouverture de la fente (cul-de-sac).
		for (const ReliefDef& r : m.reliefs)
		{
			if (r.type != "crevasse") continue;
			const double R = r.footRadiusM * 1.16 + CrevasseOuterSlopeM(r);
			for (int j = std::max(0, cg.cost.IndexZ(r.center.z - R)); j <= std::min(cg.cost.height - 1, cg.cost.IndexZ(r.center.z + R)); ++j)
				for (int i = std::max(0, cg.cost.IndexX(r.center.x - R)); i <= std::min(cg.cost.width - 1, cg.cost.IndexX(r.center.x + R)); ++i)
					if (ReliefContribution(r, cg.cost.WorldX(i), cg.cost.WorldZ(j)) > 0.35 * r.heightM) cg.cost.At(i, j) = 1e30f;
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
		// 1) Réseau du générateur (historique, inchangé) : corridors des départs.
		std::vector<Road> network;
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
			network.push_back(std::move(road));
		}
		if (m.loreRoads.empty())
		{
			// Pas de routes du lore (ex. Enfers) : le réseau du générateur est le réseau.
			g.roads = std::move(network);
			g.generatorRoads.clear();
		}
		else
			BuildLoreRoads(g, cg, rivers, std::move(network));
		g.report.roadLengthM = 0.0;
		for (const Road& r : g.roads) g.report.roadLengthM += PolylineLength(r.points);
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
