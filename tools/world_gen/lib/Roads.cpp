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
// 3 bis. Routes SOUDÉES (v1.0.134) : plus aucune route en double. Tracé par importance
//    (voie impériale, grandes routes, routes, sentiers ; embranchements en dernier) ; une
//    portion déjà reliée par le réseau tracé n'est pas retracée ; un corridor commun est
//    emprunté (jonctions exactes sur la route hôte, qui reçoit l'identifiant de la route
//    dans Road::alsoLoreIds) ; les méandres ne rapprochent jamais une route d'une autre.
//    Même soudure pour les routes du générateur gardées (et le réseau des Enfers).
// 4. Lissage (RDP, méandres, Chaikin) + profil en long GLOBAL (pente ≤ 16 %)
//    calculé sur le relief macro : chaque cellule aplanit sa portion de route au
//    même profil, les chaussées se raccordent donc exactement aux frontières.
//
// CELLULE :
// 5. Découpe des routes globales sur l'emprise de la cellule, aplanissement
//    de la chaussée et des accotements.
// 6. Chemins locaux (A* sur ≈ 10 m) reliant chaque lieu procédural au réseau
//    (v1.0.134 : arrêtés à la première chaussée atteinte).

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
				r.profileY[k] = std::max({ r.profileY[k], sea + 0.8, rivers.WaterYNear(r.points[k], 14.0) + (r.path ? 0.9 : 1.6) }); // v1.0.129 : tablier de pont au-dessus de l'eau
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
			const bool rivers = w.riverDist.data.size() == w.height.data.size();
			// v1.0.140 — villes en gradins (acropole) : la chaussée suit les gradins de la ville et
			// de la butte (aucun déblai : le profil en long reste celui du plateau de la ville basse).
			std::vector<const CityDef*> acropoles;
			for (const CityDef& c : w.cities)
				if (c.acropole.enabled) acropoles.push_back(&c);
			auto onAcropole = [&](double x, double z) {
				for (const CityDef* c : acropoles)
					if (std::hypot(x - c->center.x, z - c->center.z) < c->radiusM + 6.0
						|| std::hypot(x - c->acropole.summit.x, z - c->acropole.summit.z) < c->acropole.FootRadiusM() + 20.0)
						return true;
				return false;
			};
			for (size_t q = 0; q < w.height.data.size(); ++q)
			{
				const float rd = dist.data[q];
				if (rd > 9.0f) continue;
				w.roadDist.data[q] = std::min(w.roadDist.data[q], rd);
				if (!acropoles.empty())
				{
					const int i = static_cast<int>(q % static_cast<size_t>(w.height.width)), j = static_cast<int>(q / static_cast<size_t>(w.height.width));
					if (onAcropole(w.height.WorldX(i), w.height.WorldZ(j))) continue;
				}
				// Lot carte v1.0.129 : le lit d'une rivière (et 1 m de berge) n'est plus comblé
				// par la chaussée : un pont le franchit (BuildBridges), l'eau passe dessous.
				if (rivers && w.riverDist.data[q] < 1.0f) continue;
				const double t = SmoothStep(0.0, 9.0, rd);
				w.height.data[q] = Lerp(target.data[q] - 0.05, w.height.data[q], t);
			}
		}
		/// Longueur (m) d'une polyligne.
		double PolylineLength(const std::vector<Vec2>& p)
		{
			double len = 0.0;
			for (size_t k = 1; k < p.size(); ++k) len += std::hypot(p[k].x - p[k - 1].x, p[k].z - p[k - 1].z);
			return len;
		}

		/// Index spatial (v1.0.134, routes soudées) des points des tronçons déjà tracés
		/// (cases de 250 m) : détection des corridors communs, points de raccord exacts sur
		/// la route hôte et garde des méandres (pas de nouvelle parallèle le long d'une route).
		struct RoadIndex
		{
			struct Ref { int piece = -1; size_t k = 0; };
			static constexpr double kCell = 250.0;
			const std::vector<Road>* roads = nullptr; ///< tronçons indexés (référencés par indice)
			std::unordered_map<int64_t, std::vector<Ref>> cells;
			static int64_t Key(int x, int z) { return (static_cast<int64_t>(z) << 32) ^ static_cast<uint32_t>(x); }
			/// Indexe le tronçon \p pi de *roads (un point sur deux, ≈ 8 m, et le dernier).
			void Add(int pi)
			{
				const std::vector<Vec2>& pts = (*roads)[static_cast<size_t>(pi)].points;
				for (size_t k = 0; k < pts.size(); k += (k + 2 < pts.size() ? 2 : 1))
					cells[Key(static_cast<int>(std::floor(pts[k].x / kCell)), static_cast<int>(std::floor(pts[k].z / kCell)))].push_back({ pi, k });
			}
			/// Point indexé le plus proche de \p p à moins de \p radius (m) ; faux si aucun.
			/// \param out  tronçon et indice du point trouvé ; \param dist distance (m), optionnelle.
			/// Déterministe : à distance égale, le premier indexé l'emporte.
			bool Nearest(Vec2 p, double radius, Ref& out, double* dist = nullptr) const
			{
				const int cx = static_cast<int>(std::floor(p.x / kCell)), cz = static_cast<int>(std::floor(p.z / kCell));
				const int r = static_cast<int>(std::ceil(radius / kCell));
				double best = radius;
				bool found = false;
				for (int dz = -r; dz <= r; ++dz)
					for (int dx = -r; dx <= r; ++dx)
					{
						const auto it = cells.find(Key(cx + dx, cz + dz));
						if (it == cells.end()) continue;
						for (const Ref& ref : it->second)
						{
							const Vec2 q = (*roads)[static_cast<size_t>(ref.piece)].points[ref.k];
							const double d = std::hypot(q.x - p.x, q.z - p.z);
							if (d < best || (!found && d <= best)) { best = d; out = ref; found = true; }
						}
					}
				if (found && dist) *dist = best;
				return found;
			}
			/// Distance (m) de \p p au tronçon indexé le plus proche, plafonnée à \p radius.
			double Distance(Vec2 p, double radius) const
			{
				Ref r;
				double d = radius;
				return Nearest(p, radius, r, &d) ? d : radius;
			}
		};

		/// Soudure des routes (v1.0.134) : un nœud du chemin brut A* est « sur » une route
		/// déjà tracée à moins de kWeldRadiusM ; une portion qui la longe sur au moins
		/// kWeldMinRunM n'est pas retracée (la route emprunte la chaussée existante, raccordée
		/// à l'entrée et à la sortie par une jonction exacte sur l'hôte).
		constexpr double kWeldRadiusM = 300.0;
		constexpr double kWeldMinRunM = 600.0;
		/// Garde des méandres : un écart latéral n'est pas appliqué s'il rapproche la route à
		/// moins de kMeanderClearM d'une autre route (il en ferait une parallèle).
		constexpr double kMeanderClearM = 400.0;
		/// Approche d'une jonction : le tronçon quitte (ou rejoint) le chemin brut au dernier
		/// nœud situé à au moins kApproachM de toute route, puis va droit sur le point de l'hôte
		/// le plus proche : il aborde l'hôte franchement, sans longer la route en biais.
		constexpr double kApproachM = 450.0;

		/// Indice du point du tronçon \p r le plus proche de \p p (recherche exhaustive). Pur.
		size_t NearestOnPiece(const Road& r, Vec2 p)
		{
			size_t best = 0;
			double bd = 1e300;
			for (size_t k = 0; k < r.points.size(); ++k)
			{
				const double d = std::hypot(r.points[k].x - p.x, r.points[k].z - p.z);
				if (d < bd) { bd = d; best = k; }
			}
			return best;
		}

		/// Nœud d'approche (v1.0.134) d'une jonction sur le chemin brut \p raw : en partant de
		/// \p start et en avançant de \p step (−1 vers l'amont, +1 vers l'aval) jusqu'à \p limit
		/// compris, premier nœud à au moins kApproachM de toute route de \p idx ; \p limit si
		/// aucun. Pur.
		size_t ApproachNode(const std::vector<Vec2>& raw, const RoadIndex& idx, size_t start, size_t limit, int step)
		{
			size_t k = start;
			while (k != limit)
			{
				if (idx.Distance(raw[k], kApproachM + 1.0) >= kApproachM) return k;
				k = static_cast<size_t>(static_cast<long>(k) + step);
			}
			return limit;
		}

		/// Portion [s, e] (indices de nœuds) d'un chemin brut qui longe une route déjà tracée.
		struct WeldRun { size_t s = 0, e = 0; };

		/// Portions (v1.0.134) du chemin brut \p raw qui longent un tronçon de \p idx : nœuds à
		/// moins de kWeldRadiusM d'un point indexé, trous de un ou deux nœuds comblés (croisement
		/// en biais, méandre de l'hôte), longueur ≥ kWeldMinRunM ; deux portions séparées de moins
		/// de kWeldMinRunM sont fusionnées (pas de court tronçon collé à l'hôte entre deux
		/// jonctions). Triées, disjointes. Pur.
		std::vector<WeldRun> FindWeldRuns(const std::vector<Vec2>& raw, const RoadIndex& idx)
		{
			std::vector<WeldRun> runs;
			if (raw.size() < 2 || idx.cells.empty()) return runs;
			std::vector<uint8_t> near(raw.size(), 0);
			for (size_t k = 0; k < raw.size(); ++k) near[k] = idx.Distance(raw[k], kWeldRadiusM + 1.0) <= kWeldRadiusM;
			std::vector<double> along(raw.size(), 0.0);
			for (size_t k = 1; k < raw.size(); ++k) along[k] = along[k - 1] + std::hypot(raw[k].x - raw[k - 1].x, raw[k].z - raw[k - 1].z);
			std::vector<WeldRun> all;
			for (size_t k = 0; k < raw.size();)
			{
				if (!near[k]) { ++k; continue; }
				size_t e = k;
				while (e + 1 < raw.size() && near[e + 1]) ++e;
				// Trou d'un ou deux nœuds : la portion continue.
				if (!all.empty() && k - all.back().e <= 3) all.back().e = e;
				else all.push_back({ k, e });
				k = e + 1;
			}
			for (const WeldRun& r : all)
			{
				if (along[r.e] - along[r.s] < kWeldMinRunM) continue;
				if (!runs.empty() && along[r.s] - along[runs.back().e] < kWeldMinRunM) runs.back().e = r.e;
				else runs.push_back(r);
			}
			return runs;
		}

		/// Garde des méandres (v1.0.134) : annule l'écart latéral \p offs[k] (m, le long de la
		/// normale \p nrm[k]) des points de \p dense qu'il rapprocherait à moins de kMeanderClearM
		/// d'une route de \p avoid, avec une rampe de 4 points de part et d'autre (pas de cran) :
		/// l'ondulation ne fabrique pas de parallèle le long d'une route existante.
		/// Effet de bord : modifie \p offs.
		void GateMeanders(const std::vector<Vec2>& dense, const std::vector<Vec2>& nrm, std::vector<double>& offs, const RoadIndex& avoid)
		{
			std::vector<double> gate(dense.size(), 1.0);
			for (size_t k = 1; k + 1 < dense.size(); ++k)
			{
				if (offs[k] == 0.0) continue;
				const double dq = avoid.Distance(dense[k] + nrm[k] * offs[k], kMeanderClearM);
				if (dq < std::min(kMeanderClearM, avoid.Distance(dense[k], kMeanderClearM))) gate[k] = 0.0;
			}
			std::vector<double> ramp = gate;
			for (size_t k = 0; k < dense.size(); ++k)
				if (gate[k] == 0.0)
					for (long o = -4; o <= 4; ++o)
					{
						const long q = static_cast<long>(k) + o;
						if (q >= 0 && q < static_cast<long>(dense.size())) ramp[static_cast<size_t>(q)] = std::min(ramp[static_cast<size_t>(q)], std::abs(o) * 0.25);
					}
			for (size_t k = 0; k < dense.size(); ++k) offs[k] *= ramp[k];
		}

		/// Tracé naturel (v1.0.105) d'une route du lore à partir des chemins bruts de
		/// l'A* entre ses ANCRES successives (cases du lore) : chaque segment est
		/// simplifié séparément (RDP, l'ancre reste un sommet), subdivisé tous les
		/// 150 m, écarté en méandres bruités (v1.0.131 : ≈ ±240 m sur ≈ 3,4 km plus ±50 m sur
		/// ≈ 900 m) nuls à moins de 900 m d'une ancre et sur l'eau, puis
		/// lissé (Chaikin) et rééchantillonné tous les 4 m. Les deux extrémités sont
		/// exactes ; une ancre intermédiaire bouge de quelques dizaines de mètres au
		/// plus (coin arrondi) : elle reste dans sa case.
		/// \param segments chemins bruts successifs ; segments[i].front() est l'ancre i,
		///        segments.back().back() la dernière ancre.
		/// \param allowed  masque des nœuds du tracé (voir AStar) : un méandre ne fait pas
		///        sortir la route du masque.
		/// \param avoid    (v1.0.134) routes déjà tracées : un méandre qui rapprocherait la route
		///        à moins de kMeanderClearM de l'une d'elles n'est pas appliqué (pas de parallèle
		///        créée par l'ondulation, en particulier à l'approche d'une jonction). nullptr : aucune.
		/// Pur (lit le relief macro, les lacs, la graine et l'index).
		std::vector<Vec2> NaturalizeThroughAnchors(const GlobalWorld& g, const std::vector<std::vector<Vec2>>& segments,
			const Grid<uint8_t>& allowed, const RoadIndex* avoid = nullptr)
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
			// Écart latéral brut de chaque point (nul aux extrémités) et normale du tracé.
			std::vector<double> offs(dense.size(), 0.0);
			std::vector<Vec2> nrm(dense.size(), Vec2{ 0.0, 0.0 });
			for (size_t k = 1; k + 1 < dense.size(); ++k)
			{
				const Vec2 d = dense[k + 1] - dense[k - 1];
				const double l = std::hypot(d.x, d.z);
				if (l < 1e-6) continue;
				double nearest = 1e18;
				for (size_t ai : denseAnchor) nearest = std::min(nearest, std::fabs(along[k] - along[ai]));
				const double taper = Clamp(nearest / 900.0, 0.0, 1.0);
				// v1.0.131 : méandres plus amples (≈ ±240 m, longueur d'onde ≈ 3,4 km) et ondulation
				// fine (≈ ±50 m, ≈ 900 m) : la route épouse le pays au lieu de filer droit.
				offs[k] = (Fbm(dense[k].x, dense[k].z, 3400.0, 2, m.seed + 4747) * 240.0
					+ Fbm(dense[k].x, dense[k].z, 900.0, 2, m.seed + 4749) * 50.0) * taper;
				nrm[k] = Vec2{ -d.z / l, d.x / l };
			}
			if (avoid) GateMeanders(dense, nrm, offs, *avoid);
			for (size_t k = 1; k + 1 < dense.size(); ++k)
			{
				if (offs[k] == 0.0) continue;
				const Vec2 q = dense[k] + nrm[k] * offs[k];
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

		/// Tracé naturel d'une route du GÉNÉRATEUR (historique, extrait en v1.0.134) à partir
		/// de son chemin brut A* : (1) suppression de l'escalier du treillis (RDP 180 m),
		/// (2) subdivision tous les 150 m, (3) méandres latéraux bruités (longueur d'onde
		/// ≈ 2,5 km, ±110 m, nuls à moins de 600 m des extrémités et sur l'eau), (4) lissage
		/// de Chaikin, rééchantillonnage tous les 4 m. Extrémités exactes (raw.front/back).
		/// \param avoid (v1.0.134) routes déjà tracées : garde des méandres (GateMeanders) pour
		///        les tronçons soudés ; nullptr pour le réseau historique (tracé inchangé).
		/// Pur (lit le relief macro, les lacs, la graine et l'index).
		std::vector<Vec2> NaturalizeGeneratorRoad(const GlobalWorld& g, const std::vector<Vec2>& raw, const RoadIndex* avoid)
		{
			const WorldMaster& m = g.master;
			const double sea = m.scale.seaLevelM;
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
			std::vector<double> offs(dense.size(), 0.0);
			std::vector<Vec2> nrm(dense.size(), Vec2{ 0.0, 0.0 });
			for (size_t k = 1; k + 1 < dense.size(); ++k)
			{
				const Vec2 d = dense[k + 1] - dense[k - 1];
				const double l = std::hypot(d.x, d.z);
				if (l < 1e-6) continue;
				const double taper = Clamp(std::min(along[k], total - along[k]) / 600.0, 0.0, 1.0);
				offs[k] = Fbm(dense[k].x, dense[k].z, 2500.0, 2, m.seed + 4747) * 110.0 * taper;
				nrm[k] = Vec2{ -d.z / l, d.x / l };
			}
			if (avoid) GateMeanders(dense, nrm, offs, *avoid);
			std::vector<Vec2> meander = dense;
			for (size_t k = 1; k + 1 < dense.size(); ++k)
			{
				if (nrm[k].x == 0.0 && nrm[k].z == 0.0) continue;
				const Vec2 q = dense[k] + nrm[k] * offs[k];
				bool wet = SampleBilinear(g.coarse, q.x, q.z) < sea + 1.5;
				for (const Lake& lk : g.lakes)
					if (std::hypot(q.x - lk.center.x, q.z - lk.center.z) < lk.radiusM + 30.0) wet = true;
				if (!wet) meander[k] = q;
			}
			std::vector<Vec2> out = ResamplePolyline(SmoothPolyline(meander, 4), 4.0);
			out.front() = raw.front();
			out.back() = raw.back();
			return out;
		}

		/// Raccord du profil en long à une jonction (v1.0.134) : décale l'altitude des 30
		/// premiers (\p atStart) ou derniers points (≈ 120 m) de \p r, en rampe linéaire, pour
		/// que la chaussée arrive exactement à l'altitude \p hostY de la route hôte.
		/// Effet de bord : modifie r.profileY.
		void BlendJunctionProfile(Road& r, bool atStart, double hostY)
		{
			const size_t n = r.profileY.size();
			if (n == 0) return;
			const size_t span = std::min<size_t>(30, n);
			const double delta = hostY - (atStart ? r.profileY.front() : r.profileY.back());
			for (size_t q = 0; q < span; ++q)
			{
				const double w = 1.0 - static_cast<double>(q) / static_cast<double>(span);
				r.profileY[atStart ? q : n - 1 - q] += delta * w;
			}
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
			// Nœud franchissable (coût fini) de la grille 250 m le plus proche de \p p,
			// cherché à moins de 1 500 m ; \p p lui-même si aucun.
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

		/// Soudure du réseau du GÉNÉRATEUR (v1.0.134) : chaque route \p which (indices dans
		/// \p network, dans l'ordre) est ajoutée à \p out ; la portion de son chemin brut qui longe
		/// un tronçon déjà présent dans \p out (FindWeldRuns) n'est pas retracée : la route
		/// s'arrête sur une jonction exacte de l'hôte et repart de la jonction de sortie. Une
		/// extrémité (ville) déjà desservie par l'hôte n'a pas de raccord. Une route sans portion
		/// commune est recopiée telle quelle (tracé historique). Les tronçons soudés ont un
		/// fromId / toId vide côté jonction.
		/// \param raws  chemins bruts A* des routes de \p network (même indice).
		/// \param idx   index de \p out (idx.roads == &out), mis à jour.
		/// Effet de bord : \p out, \p idx, cg.onRoad. Renvoie le nombre de routes soudées.
		size_t WeldGeneratorRoads(const GlobalWorld& g, CostGrid& cg, const RiverIndex& rivers, const std::vector<Road>& network,
			const std::vector<std::vector<Vec2>>& raws, const std::vector<size_t>& which, std::vector<Road>& out, RoadIndex& idx)
		{
			const WorldMaster& m = g.master;
			const double sea = m.scale.seaLevelM;
			size_t weldedRoads = 0;
			/// Extrémité d'une portion : ville (id) ou jonction sur un tronçon (snap ≥ 0).
			struct End { Vec2 p; std::string id; int snap = -1; size_t k = 0; };
			for (size_t r : which)
			{
				const Road& src = network[r];
				const std::vector<Vec2>& raw = raws[r];
				const std::vector<WeldRun> runs = FindWeldRuns(raw, idx);
				if (runs.empty())
				{
					out.push_back(src);
					idx.Add(static_cast<int>(out.size() - 1));
					continue;
				}
				++weldedRoads;
				// Tronçon le plus proche de p (−1 si aucun à portée de soudure).
				auto hostOf = [&](Vec2 p) {
					RoadIndex::Ref ref;
					return idx.Nearest(p, kWeldRadiusM * 1.5, ref) ? ref.piece : -1;
				};
				// Jonction : point exact de l'hôte le plus proche de p.
				auto junctionAt = [&](Vec2 p, End& e) {
					RoadIndex::Ref ref;
					if (!idx.Nearest(p, kWeldRadiusM * 1.5, ref)) return false;
					e = End{ out[static_cast<size_t>(ref.piece)].points[ref.k], std::string(), ref.piece, ref.k };
					return true;
				};
				// Jonction sur le point du tronçon host le plus proche de p (faux si host < 0).
				auto pieceEnd = [&](int host, Vec2 p, End& e) {
					if (host < 0) return false;
					const size_t k = NearestOnPiece(out[static_cast<size_t>(host)], p);
					e = End{ out[static_cast<size_t>(host)].points[k], std::string(), host, k };
					return true;
				};
				// Vrai si la ville id est une extrémité du tronçon host.
				auto attached = [&](const std::string& id, int host) {
					return host >= 0 && !id.empty() && (out[static_cast<size_t>(host)].fromId == id || out[static_cast<size_t>(host)].toId == id);
				};
				struct Portion { std::vector<Vec2> raw; End a, b; };
				std::vector<Portion> portions;
				End start{ raw.front(), src.fromId };
				size_t from = 0;
				bool welded = false;
				// Portion ouverte : start, nœuds bruts from+1 .. upto-1, puis endP.
				auto open = [&](size_t upto, Vec2 endP) {
					std::vector<Vec2> p{ start.p };
					for (size_t k = from + 1; k < upto; ++k) p.push_back(raw[k]);
					p.push_back(endP);
					return p;
				};
				for (size_t ri = 0; ri < runs.size(); ++ri)
				{
					const WeldRun& run = runs[ri];
					const int host = hostOf(raw[std::min(run.s + 1, run.e)]);
					if (!(run.s == 0 && attached(src.fromId, host)))
					{
						// Entrée par le dernier nœud d'approche (voir kApproachM).
						const size_t t = run.s > from ? ApproachNode(raw, idx, run.s - 1, from, -1) : from;
						End j;
						if (pieceEnd(hostOf(raw[run.s]), t == from ? start.p : raw[t], j) && std::hypot(j.p.x - start.p.x, j.p.z - start.p.z) >= 1.0)
							portions.push_back({ open(t + 1, j.p), start, j });
					}
					welded = true;
					if (run.e + 1 < raw.size())
					{
						// Sortie au droit du premier nœud d'approche.
						const size_t lim = ri + 1 < runs.size() ? runs[ri + 1].s - 1 : raw.size() - 1;
						const size_t t = ApproachNode(raw, idx, run.e + 1, lim, +1);
						if (pieceEnd(hostOf(raw[run.e]), raw[t], start)) { from = t - 1; welded = false; }
					}
				}
				const End B{ raw.back(), src.toId };
				if (!welded)
					portions.push_back({ open(raw.size() - 1, B.p), start, B });
				else if (!attached(src.toId, hostOf(raw[raw.size() - 2])))
				{
					End j;
					if (junctionAt(raw.back(), j)) portions.push_back({ { j.p, B.p }, j, B });
				}
				for (Portion& pr : portions)
				{
					if (PolylineLength(pr.raw) < 1.0) continue;
					Road road;
					road.fromId = pr.a.id; road.toId = pr.b.id;
					road.major = src.major;
					road.widthM = src.widthM;
					road.points = NaturalizeGeneratorRoad(g, pr.raw, &idx);
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
					if (pr.a.snap >= 0) BlendJunctionProfile(road, true, out[static_cast<size_t>(pr.a.snap)].profileY[pr.a.k]);
					if (pr.b.snap >= 0) BlendJunctionProfile(road, false, out[static_cast<size_t>(pr.b.snap)].profileY[pr.b.k]);
					out.push_back(std::move(road));
					idx.Add(static_cast<int>(out.size() - 1));
				}
			}
			return weldedRoads;
		}

		/// Routes du lore (v1.0.105) : tracé case par case, puis tri du réseau du
		/// générateur (voir l'en-tête du fichier, étapes 2 et 3).
		/// v1.0.134 (routes soudées) : ordre de tracé par importance (voie impériale, grandes
		/// routes, routes, sentiers ; embranchements en dernier) ; une portion déjà reliée par
		/// le réseau tracé (même paire de villes, détour ≤ 1,5 ×) n'est pas retracée (alias) ;
		/// un corridor commun (FindWeldRuns) est emprunté, pas doublé : le tronçon s'arrête sur
		/// une jonction exacte de la route hôte et repart de la jonction de sortie ; l'hôte
		/// reçoit l'identifiant de la route (Road::alsoLoreIds).
		/// \param cg         grille de coûts du continent (250 m) ; son marquage des routes
		///                   (onRoad) est remis à zéro puis rempli par les routes du lore.
		/// \param network    réseau du générateur déjà tracé (devient g.generatorRoads).
		/// \param networkRaw chemins bruts A* de \p network (même indice) : soudure des routes
		///                   du générateur gardées sur les routes du lore.
		/// Effet de bord : remplit g.roads (routes du lore, puis routes du générateur
		/// gardées, soudées) et g.generatorRoads ; avertissements dans g.report.
		void BuildLoreRoads(GlobalWorld& g, CostGrid& cg, const RiverIndex& rivers, std::vector<Road> network,
			const std::vector<std::vector<Vec2>>& networkRaw)
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
			// v1.0.134 : index des tronçons tracés (soudure, jonctions, garde des méandres).
			RoadIndex index;
			index.roads = &pieces;
			/// Arête du graphe des tronçons tracés : nœud d'arrivée, longueur (m), tronçon porteur.
			struct GEdge { std::string to; double len; size_t piece; };
			std::map<std::string, std::vector<GEdge>> graph;
			// Grille de coûts sans attrait des routes (créée au premier détour absurde, v1.0.134).
			CostGrid bareCg;

			// Position d'une extrémité nommée (ville, sinon POI) ; faux si inconnue.
			auto entityAnchor = [&](const std::string& id, Anchor& a) {
				if (id.empty()) return false;
				// v1.0.142 : une ville-tour (au milieu d'un lac) est reliée à la rive (CityRoadAnchor).
				if (const CityDef* c = g.FindCity(id)) { a.p = CityRoadAnchor(*c); a.id = id; a.city = true; a.ok = true; return true; }
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
					if (c.n == n && c.e == e && c.procedural.empty() && !c.secret) // ville secrète (v1.0.107) : jamais ancre
					{
						const double d = std::hypot(c.center.x - ctr.x, c.center.z - ctr.z);
						if (d < bd) { bd = d; best = &c; }
					}
				if (best) { a.p = CityRoadAnchor(*best); a.id = best->id; a.city = true; a.ok = true; return true; } // v1.0.142 : rive d'une ville-tour
				a.ok = PracticablePointInCell(cg, n, e, JitteredCellPoint(m, n, e), a.p); // v1.0.131 : pas le centre
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
				a.ok = PracticablePointInCell(cg, n, e, JitteredCellPoint(m, n, e), a.p); // v1.0.131 : pas le centre
				return a.ok;
			};
			// Plus court chemin (m) entre deux nœuds du graphe des tronçons tracés (∞ si aucun) ;
			// \p via reçoit les tronçons empruntés (pour les alias).
			auto shortest = [&](const std::string& a, const std::string& b, std::vector<size_t>& via) {
				via.clear();
				std::map<std::string, double> dist;
				std::map<std::string, std::pair<std::string, size_t>> prev;
				using QE = std::pair<double, std::string>;
				std::priority_queue<QE, std::vector<QE>, std::greater<QE>> pq;
				dist[a] = 0.0; pq.push({ 0.0, a });
				while (!pq.empty())
				{
					const auto [du, u] = pq.top(); pq.pop();
					if (du > dist[u]) continue;
					if (u == b)
					{
						for (std::string v = b; v != a;) { const auto& pv = prev[v]; via.push_back(pv.second); v = pv.first; }
						return du;
					}
					const auto it = graph.find(u);
					if (it == graph.end()) continue;
					for (const GEdge& ed : it->second)
					{
						const auto dv = dist.find(ed.to);
						if (dv == dist.end() || du + ed.len < dv->second) { dist[ed.to] = du + ed.len; prev[ed.to] = { u, ed.piece }; pq.push({ du + ed.len, ed.to }); }
					}
				}
				return std::numeric_limits<double>::infinity();
			};

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
			// Ordre (v1.0.134) : routes aux deux extrémités nommées d'abord, embranchements
			// ensuite (ils se raccordent aux routes déjà tracées) ; dans chaque groupe, voie
			// impériale, grandes routes (deux grandes villes), routes, puis sentiers : les routes
			// importantes sont tracées d'abord et deviennent les hôtes des soudures. Ordre du
			// master à rang égal (tri stable).
			auto rankOf = [&](const LoreRoadDef& d) {
				if (d.type == "imperial_road") return 0;
				if (d.type == "path") return 3;
				return IsMajorId(g, d.fromId) && IsMajorId(g, d.toId) ? 1 : 2;
			};
			std::vector<size_t> order(m.loreRoads.size());
			for (size_t li = 0; li < order.size(); ++li) order[li] = li;
			std::stable_sort(order.begin(), order.end(), [&](size_t x, size_t y) {
				const LoreRoadDef& a = m.loreRoads[x];
				const LoreRoadDef& b = m.loreRoads[y];
				const int na = (!a.fromId.empty() && !a.toId.empty()) ? 0 : 1, nb = (!b.fromId.empty() && !b.toId.empty()) ? 0 : 1;
				if (na != nb) return na < nb;
				return rankOf(a) < rankOf(b);
			});

			size_t tracedRoads = 0, failedSegments = 0, skippedCells = 0;
			size_t weldRuns = 0, aliasSpans = 0, aliasOnly = 0, upgradedHosts = 0;
			for (size_t li : order)
			{
				const LoreRoadDef& d = m.loreRoads[li];
				const size_t n = d.waypoints.size();
				const bool isPath = d.type == "path";
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
						// Vrai si la case \p c est jouable (generated_cells + corridors de départ).
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

				// Tronçons : coupés aux villes traversées, aux segments sans chemin et (v1.0.134)
				// aux corridors communs soudés. pa : ancres du tronçon ouvert (pa[0] = départ,
				// éventuellement une jonction) ; segs[i] : chemin brut de pa[i] à pa[i + 1].
				bool any = false, present = false;
				std::vector<Anchor> pa;
				std::vector<std::vector<Vec2>> segs;
				bool welded = false; // la route emprunte en ce moment un tronçon existant
				// Ajoute la route à la liste des routes qui empruntent le tronçon \p pi (alias) ;
				// une route (non sentier) qui emprunte un sentier ou une grande route qui emprunte
				// une route plus étroite l'élargit (la chaussée commune prend la classe la plus haute).
				// Effet de bord : pieces[pi], present, upgradedHosts.
				auto alias = [&](int pi) {
					if (pi < 0) return;
					Road& h = pieces[static_cast<size_t>(pi)];
					present = true;
					if (h.loreId == d.id) return;
					if (std::find(h.alsoLoreIds.begin(), h.alsoLoreIds.end(), d.id) == h.alsoLoreIds.end()) h.alsoLoreIds.push_back(d.id);
					const bool major = d.type == "imperial_road";
					if ((h.path && !isPath) || (major && !h.major))
					{
						++upgradedHosts;
						h.path = h.path && isPath;
						h.major = h.major || major;
						h.widthM = std::max(h.widthM, h.path ? 3.2 : (h.major ? 7.0 : 5.0));
					}
				};
				// Tronçon le plus proche de p, à portée de soudure (−1 si aucun).
				auto hostOf = [&](Vec2 p) {
					RoadIndex::Ref ref;
					return index.Nearest(p, kWeldRadiusM * 1.5, ref) ? ref.piece : -1;
				};
				// Jonction (ancre sans id) sur le point exact du tronçon le plus proche de p.
				auto junctionAt = [&](Vec2 p, Anchor& j) {
					RoadIndex::Ref ref;
					if (!index.Nearest(p, kWeldRadiusM * 1.5, ref)) return false;
					j = Anchor{};
					j.p = pieces[static_cast<size_t>(ref.piece)].points[ref.k];
					j.ok = true;
					j.snapPiece = ref.piece;
					j.snapK = ref.k;
					return true;
				};
				// Jonction sur le point du tronçon \p host le plus proche de p (faux si host < 0).
				auto pieceJunction = [&](int host, Vec2 p, Anchor& j) {
					if (host < 0) return false;
					j = Anchor{};
					j.snapPiece = host;
					j.snapK = NearestOnPiece(pieces[static_cast<size_t>(host)], p);
					j.p = pieces[static_cast<size_t>(host)].points[j.snapK];
					j.ok = true;
					return true;
				};
				// Vrai si l'ancre \p a est déjà sur le tronçon \p host : jonction sur un tronçon,
				// ville / POI extrémité de l'hôte, ou point confondu avec l'une de ses extrémités.
				auto attached = [&](const Anchor& a, int host) {
					if (a.snapPiece >= 0) return true;
					if (host < 0) return false;
					const Road& h = pieces[static_cast<size_t>(host)];
					if (!a.id.empty() && (pieceFrom[static_cast<size_t>(host)] == a.id || pieceTo[static_cast<size_t>(host)] == a.id)) return true;
					for (const Vec2& e : { h.points.front(), h.points.back() })
						if (std::hypot(e.x - a.p.x, e.z - a.p.z) < 1.0) return true;
					return false;
				};
				// Ferme le tronçon ouvert (ancres pa, segments segs) : assemble les segments A*
				// en une Road (id du lore, extrémités, type, largeur), la naturalise par ses ancres
				// (garde des méandres près des routes existantes ; coins diagonaux ramenés dans les
				// cases reliées), calcule son profil en long (pente ≤ 16 %, sentier ≤ 18 % ; raccord
				// exact à l'altitude de l'hôte aux jonctions) et l'indexe. Sans segment : vide pa.
				// Effet de bord : pieces, pieceFrom / pieceTo, snaps, graph, index, cg.onRoad, any ;
				// pa et segs vidés.
				auto finish = [&]() {
					if (segs.empty() || pa.size() != segs.size() + 1) { pa.clear(); segs.clear(); return; }
					const Anchor A = pa.front();
					const Anchor B = pa.back();
					Road road;
					road.loreId = d.id;
					road.fromId = A.id; road.toId = B.id;
					road.path = isPath;
					road.major = d.type == "imperial_road" || (d.type == "road" && IsMajorId(g, A.id) && IsMajorId(g, B.id));
					road.widthM = road.path ? 3.2 : (road.major ? 7.0 : 5.0);
					road.points = NaturalizeThroughAnchors(g, segs, allowed, &index);
					// Le lissage arrondit le coin d'un pas en diagonale : les points à moins de
					// 50 m du coin qui tombent dans une autre case sont ramenés (au mètre près)
					// dans la plus proche des deux cases reliées.
					for (const Anchor& C : pa)
					{
						if (!C.corner) continue;
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
					if (A.snapPiece >= 0) BlendJunctionProfile(road, true, pieces[static_cast<size_t>(A.snapPiece)].profileY[A.snapK]);
					if (B.snapPiece >= 0) BlendJunctionProfile(road, false, pieces[static_cast<size_t>(B.snapPiece)].profileY[B.snapK]);
					const size_t pi = pieces.size();
					const std::string nodeA = A.id.empty() ? "#" + d.id + ":" + std::to_string(pi) + ":a" : A.id;
					const std::string nodeB = B.id.empty() ? "#" + d.id + ":" + std::to_string(pi) + ":b" : B.id;
					// Graphe des tronçons tracés : arête du tronçon, raccords aux hôtes.
					const double len = PolylineLength(road.points);
					graph[nodeA].push_back({ nodeB, len, pi });
					graph[nodeB].push_back({ nodeA, len, pi });
					for (const auto& [J, node] : { std::pair<const Anchor*, const std::string*>{ &A, &nodeA }, { &B, &nodeB } })
					{
						if (J->snapPiece < 0) continue;
						const size_t host = static_cast<size_t>(J->snapPiece);
						snaps.push_back({ *node, host, J->snapK });
						const std::vector<Vec2>& hp = pieces[host].points;
						const double toA = PolylineLength(std::vector<Vec2>(hp.begin(), hp.begin() + static_cast<long>(J->snapK) + 1));
						const double toB = PolylineLength(hp) - toA;
						graph[*node].push_back({ pieceFrom[host], toA, host }); graph[pieceFrom[host]].push_back({ *node, toA, host });
						graph[*node].push_back({ pieceTo[host], toB, host }); graph[pieceTo[host]].push_back({ *node, toB, host });
					}
					pieceFrom.push_back(nodeA); pieceTo.push_back(nodeB);
					pieces.push_back(std::move(road));
					index.Add(static_cast<int>(pi));
					pa.clear();
					segs.clear();
					any = true;
					present = true;
				};
				// Ajoute au tronçon ouvert le segment pa.back() → nœuds bruts raw[from+1 .. upto-1]
				// → \p end (nouvelle ancre). Ancre confondue avec pa.back() (< 1 m) : pa.back() en
				// prend la jonction, sans segment.
				auto extend = [&](const std::vector<Vec2>& raw, size_t from, size_t upto, const Anchor& end) {
					if (pa.empty()) { pa.push_back(end); return; }
					if (std::hypot(end.p.x - pa.back().p.x, end.p.z - pa.back().p.z) < 1.0)
					{
						if (end.snapPiece >= 0 && pa.back().snapPiece < 0)
						{
							pa.back().snapPiece = end.snapPiece; pa.back().snapK = end.snapK; pa.back().p = end.p;
							if (!segs.empty()) segs.back().back() = end.p;
						}
						return;
					}
					std::vector<Vec2> seg{ pa.back().p };
					for (size_t k = from + 1; k < upto; ++k) seg.push_back(raw[k]);
					seg.push_back(end.p);
					segs.push_back(std::move(seg));
					pa.push_back(end);
				};
				for (size_t i = 0; i + 1 < pts.size(); ++i)
				{
					// v1.0.134 — portion entre deux extrémités nommées déjà reliée par le réseau
					// tracé (détour ≤ 1,5 × la ligne brisée de ses ancres, cases intermédiaires
					// desservies) : pas de second tracé, la route emprunte ces tronçons (alias).
					if (!welded && segs.empty() && !pts[i].id.empty())
					{
						size_t j = i + 1;
						while (j < pts.size() && pts[j].id.empty()) ++j;
						if (j < pts.size())
						{
							double chain = 0.0;
							for (size_t k = i; k < j; ++k) chain += std::hypot(pts[k + 1].p.x - pts[k].p.x, pts[k + 1].p.z - pts[k].p.z);
							std::vector<size_t> via;
							const double dist = shortest(pts[i].id, pts[j].id, via);
							bool ok = pts[i].id != pts[j].id && !via.empty() && dist <= 1.5 * chain;
							std::set<std::string> cells;
							for (size_t pi : via)
							{
								ok = ok && (isPath || !pieces[pi].path);
								for (const Vec2& q : pieces[pi].points) cells.insert(wc::LoreCellId(wc::CellFromPosition(q.x, q.z)));
							}
							for (size_t k = i + 1; ok && k < j; ++k)
								ok = pts[k].corner || cells.count(wc::LoreCellId(wc::CellFromPosition(pts[k].p.x, pts[k].p.z))) > 0;
							if (ok)
							{
								for (size_t pi : via) alias(static_cast<int>(pi));
								++aliasSpans;
								pa.clear();
								i = j - 1;
								continue;
							}
						}
					}
					std::vector<Vec2> raw = AStarLoose(cg, pts[i].p, pts[i + 1].p, &allowed);
					// Détour absurde (case enclavée dans un massif : le chemin ferait le tour
					// du continent) : traité comme une absence de chemin.
					const double straight = std::hypot(pts[i + 1].p.x - pts[i].p.x, pts[i + 1].p.z - pts[i].p.z);
					if (raw.size() >= 2 && PolylineLength(raw) > 3.0 * straight + 3000.0)
					{
						// v1.0.134 : le détour peut venir de l'attrait des routes déjà tracées (A* qui
						// fait le tour par le réseau plutôt que de couper hors de son tracé) : nouvel
						// essai sans cet attrait avant de conclure à une absence de chemin.
						if (bareCg.cost.data.empty())
						{
							bareCg.cost = cg.cost;
							bareCg.onRoad.Init(cg.onRoad.width, cg.onRoad.height, cg.onRoad.originX, cg.onRoad.originZ, cg.onRoad.step, 0);
						}
						raw = AStarLoose(bareCg, pts[i].p, pts[i + 1].p, &allowed);
						if (raw.size() >= 2 && PolylineLength(raw) > 3.0 * straight + 3000.0) raw.clear();
					}
					if (raw.size() < 2)
					{
						++failedSegments;
						const wc::CellCoord ca = wc::CellFromPosition(pts[i].p.x, pts[i].p.z), cb = wc::CellFromPosition(pts[i + 1].p.x, pts[i + 1].p.z);
						g.Warn("route du lore " + d.id + " : pas de chemin " + wc::LoreCellId(ca) + " -> " + wc::LoreCellId(cb) + " (relief/eau ou detour > 3x, troncon coupe)");
						finish();
						welded = false;
						continue;
					}
					raw.front() = pts[i].p;
					raw.back() = pts[i + 1].p;
					if (!welded && pa.empty()) pa.push_back(pts[i]);
					const std::vector<WeldRun> runs = FindWeldRuns(raw, index);
					size_t from = 0; // nœud brut représenté par pa.back() (portion ouverte)
					// Soudure héritée du segment précédent qui ne se prolonge pas : sortie au départ,
					// par le premier nœud d'approche (à kApproachM des routes).
					if (welded && (runs.empty() || runs.front().s > 0))
					{
						const size_t lim = runs.empty() ? raw.size() - 1 : runs.front().s - 1;
						const size_t t = ApproachNode(raw, index, 0, lim, +1);
						Anchor J;
						pa.clear();
						if (pieceJunction(hostOf(raw.front()), raw[t], J)) { pa.push_back(J); from = t == 0 ? 0 : t - 1; }
						else pa.push_back(pts[i]);
						welded = false;
					}
					for (size_t ri = 0; ri < runs.size(); ++ri)
					{
						const WeldRun& run = runs[ri];
						if (!welded)
						{
							// Entrée : le tronçon ouvert quitte le chemin brut au dernier nœud d'approche
							// et s'arrête sur l'hôte (jonction exacte), sauf si le départ même du segment
							// est déjà sur l'hôte (rien à tracer).
							const int host = hostOf(raw[std::min(run.s + 1, run.e)]);
							const bool onHost = run.s == 0 && segs.empty() && pa.size() == 1 && attached(pa.front(), host);
							if (!onHost)
							{
								const size_t t = run.s > from ? ApproachNode(raw, index, run.s - 1, from, -1) : from;
								Anchor J;
								if (pieceJunction(hostOf(raw[run.s]), t == from ? pa.back().p : raw[t], J)) extend(raw, from, t + 1, J);
							}
							finish();
							pa.clear();
							welded = true;
							++weldRuns;
						}
						for (size_t k = run.s; k <= run.e; ++k) alias(hostOf(raw[k]));
						// Sortie au milieu du segment : nouveau tronçon depuis la jonction de sortie,
						// au droit du premier nœud d'approche.
						if (run.e + 1 < raw.size())
						{
							const size_t lim = ri + 1 < runs.size() ? runs[ri + 1].s - 1 : raw.size() - 1;
							const size_t t = ApproachNode(raw, index, run.e + 1, lim, +1);
							Anchor J;
							if (pieceJunction(hostOf(raw[run.e]), raw[t], J))
							{
								pa.clear();
								pa.push_back(J);
								from = t - 1;
								welded = false;
							}
						}
					}
					const Anchor& T = pts[i + 1];
					const bool last = i + 2 == pts.size();
					if (!welded)
						extend(raw, from, raw.size() - 1, T);
					else if (!attached(T, hostOf(raw[raw.size() - 2])) && (last || !T.id.empty()))
					{
						// Soudé jusqu'à une extrémité nommée (ou la fin) que l'hôte ne dessert pas :
						// court raccord depuis la jonction la plus proche.
						Anchor J;
						if (junctionAt(raw.back(), J))
						{
							pa.clear();
							pa.push_back(J);
							extend(raw, raw.size() - 1, raw.size() - 1, T);
							welded = false;
						}
					}
					else if (T.city || last)
						welded = false; // extrémité déjà desservie par l'hôte : rien à tracer
					// (sinon : ancre de case intermédiaire, la soudure continue au segment suivant)
					if (T.city && !last) { finish(); welded = false; }
				}
				finish();
				if (any) ++tracedRoads;
				else if (present) { ++tracedRoads; ++aliasOnly; }
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
			const size_t lorePieces = pieces.size();
			size_t kept = 0;
			std::vector<size_t> keptIdx;
			for (size_t r = 0; r < network.size(); ++r)
				if (keep[r]) { keptIdx.push_back(r); ++kept; }
			// v1.0.134 : les routes du générateur gardées sont soudées sur les routes du lore.
			const size_t weldedGen = WeldGeneratorRoads(g, cg, rivers, network, networkRaw, keptIdx, pieces, index);
			g.roads = std::move(pieces);
			std::printf("[world_gen] routes du lore : %zu/%zu tracee(s) en %zu troncon(s) (%zu case(s) sans point praticable, %zu segment(s) sans chemin) ; "
				"reseau du generateur : %zu route(s) gardee(s) pour les departs, %zu remplacee(s) par le lore, %zu abandonnee(s)\n",
				tracedRoads, m.loreRoads.size(), lorePieces, skippedCells, failedSegments,
				kept, replaced, network.size() - kept - replaced);
			std::printf("[world_gen] routes soudees (v1.0.134) : %zu corridor(s) commun(s) emprunte(s), %zu portion(s) deja reliee(s) (alias), "
				"%zu route(s) sans troncon propre, %zu troncon(s) hote(s) elargi(s), %zu route(s) du generateur soudee(s)\n",
				weldRuns, aliasSpans, aliasOnly, upgradedHosts, weldedGen);
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
				// v1.0.131 — préférences de terrain bruitées (± 35 %, longueur d'onde ≈ 3,5 km) :
				// en plaine, le plus court chemin n'est plus la ligne droite ; les routes ondulent.
				c *= static_cast<float>(0.65 + 0.7 * Clamp(0.5 + 1.6 * Fbm(g.coarse.WorldX(i), g.coarse.WorldZ(j), 3500.0, 3, m.seed + 4801), 0.0, 1.0));
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
		// Gouffre (v1.0.107) : fosse et galerie fermées aux routes (pas de route ni de
		// sentier : on n'y entre que par la galerie dissimulée). v1.0.130 : disques relevés au
		// chargement du master (`roadBlocks` : fosse et abords de l'entrée cachée), pour que
		// le réseau soit le même avec ou sans le relief.
		for (const WorldMaster::CalmZone& b : m.roadBlocks)
		{
			const double R = b.radiusM;
			for (int j = std::max(0, cg.cost.IndexZ(b.center.z - R)); j <= std::min(cg.cost.height - 1, cg.cost.IndexZ(b.center.z + R)); ++j)
				for (int i = std::max(0, cg.cost.IndexX(b.center.x - R)); i <= std::min(cg.cost.width - 1, cg.cost.IndexX(b.center.x + R)); ++i)
					if (std::hypot(cg.cost.WorldX(i) - b.center.x, cg.cost.WorldZ(j) - b.center.z) < R) cg.cost.At(i, j) = 1e30f;
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
		// Villes secrètes (v1.0.107) : hors du réseau (ni Kruskal, ni maillage) ; les
		// indices des autres villes ne changent pas (ordre du tri identique).
		for (size_t a = 0; a < n; ++a)
			for (size_t b = a + 1; b < n; ++b)
				if (!cities[a].secret && !cities[b].secret)
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
		std::vector<std::vector<Vec2>> networkRaw; // v1.0.134 : chemins bruts (soudure)
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
			// Tracé naturel (RDP, méandres ±110 m, Chaikin) : NaturalizeGeneratorRoad.
			road.points = NaturalizeGeneratorRoad(g, raw, nullptr);
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
			networkRaw.push_back(std::move(raw));
			network.push_back(std::move(road));
		}
		if (m.loreRoads.empty())
		{
			// Pas de routes du lore (ex. Enfers) : le réseau du générateur est le réseau.
			// v1.0.134 : g.roads en est la version SOUDÉE (corridors communs tracés une seule
			// fois) ; le réseau historique reste dans g.generatorRoads (corridors des départs).
			std::vector<Road> welded;
			RoadIndex idx;
			idx.roads = &welded;
			std::vector<size_t> all(network.size());
			for (size_t r = 0; r < all.size(); ++r) all[r] = r;
			const size_t n = WeldGeneratorRoads(g, cg, rivers, network, networkRaw, all, welded, idx);
			std::printf("[world_gen] routes soudees (v1.0.134) : %zu route(s) du generateur soudee(s), %zu troncon(s)\n", n, welded.size());
			g.roads = std::move(welded);
			g.generatorRoads = std::move(network);
		}
		else
			BuildLoreRoads(g, cg, rivers, std::move(network), networkRaw);
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
		//    route ou agglomération déjà reliée), A* sur ≈ 10 m. v1.0.134 : un point de route
		//    à moins de 1,3 × la distance du centre est préféré au centre d'une agglomération, et
		//    le chemin s'arrête dès qu'il atteint une chaussée (il ne la longe pas).
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
				if (!w.InPlayableBounds(p, 20.0)) { cg.cost.At(i, j) = 1e30f; continue; }
				// Lot carte v1.0.129 : un cours d'eau (pas la mer ni un lac) se franchit, à coût
				// élevé (passerelle de bois posée par BuildBridges) ; la mer et les lacs restent fermés.
				if (w.IsWater(p))
				{
					const int ri = w.riverDist.IndexX(p.x), rj = w.riverDist.IndexZ(p.z);
					const bool riverOnly = w.riverDist.InBounds(ri, rj) && w.riverDist.At(ri, rj) < 3.0f; // lit (même creusé sous la mer)
					bool lake = false;
					for (const Lake& l : w.lakes) lake = lake || std::hypot(p.x - l.center.x, p.z - l.center.z) < l.radiusM + 4.0;
					if (!riverOnly || lake) { cg.cost.At(i, j) = 1e30f; continue; }
					cg.cost.At(i, j) = 25.0f;
					continue;
				}
				const double slope = w.SlopeDegAt(p);
				// v1.0.129 : les berges creusées d'un ruisseau sont raides sur quelques mètres ;
				// elles ne ferment pas le passage (la passerelle les enjambe).
				const int bi = w.riverDist.IndexX(p.x), bj = w.riverDist.IndexZ(p.z);
				const bool bank = w.riverDist.InBounds(bi, bj) && w.riverDist.At(bi, bj) < 8.0f;
				if (slope > 40.0 && !bank) { cg.cost.At(i, j) = 1e30f; continue; }
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
			if (c.procedural.empty()) targets.push_back({ CityRoadAnchor(c), c.id, c.tower.enabled ? w.HeightAt(CityRoadAnchor(c)) : c.plateauY }); // v1.0.142 : rive d'une ville-tour

		std::vector<Road> paths;
		for (const CityDef* pl : places)
		{
			if (targets.empty())
			{
				// Cellule sans route ni ville : le premier lieu devient le noeud du réseau.
				targets.push_back({ pl->center, pl->id, pl->plateauY });
				continue;
			}
			// v1.0.134 : un point de route l'emporte sur un centre d'agglomération s'il est à moins
			// de 1,3 × sa distance (le chemin rejoint la route, il ne la longe pas jusqu'au centre).
			const Target* best = nullptr;
			const Target* bestRoad = nullptr;
			double bd = 1e18, bdRoad = 1e18;
			for (const Target& t : targets)
			{
				const double d = std::hypot(t.p.x - pl->center.x, t.p.z - pl->center.z);
				if (d <= 1.0) continue;
				if (d < bd) { bd = d; best = &t; }
				if (t.id.empty() && d < bdRoad) { bdRoad = d; bestRoad = &t; }
			}
			if (best && !best->id.empty() && bestRoad && bdRoad <= 1.3 * bd) best = bestRoad;
			if (!best) continue;
			std::vector<Vec2> raw = AStar(cg, pl->center, best->p);
			if (raw.size() < 2) { w.Warn("chemin impossible vers " + pl->id); continue; }
			raw.front() = pl->center;
			raw.back() = best->p;
			// v1.0.134 : le chemin s'arrête dès qu'il atteint la chaussée d'une route globale
			// (bord à moins de 1 m) : raccord au point de l'axe le plus proche, à son altitude.
			Vec2 endP = best->p;
			double endY = best->y;
			std::string endId = best->id;
			for (size_t k = 1; k < raw.size(); ++k)
			{
				const int ri = w.roadDist.IndexX(raw[k].x), rj = w.roadDist.IndexZ(raw[k].z);
				if (!w.roadDist.InBounds(ri, rj) || w.roadDist.At(ri, rj) >= 1.0f) continue;
				double dmin = 1e18;
				for (const Road& r : w.roads)
					for (size_t q = 0; q < r.points.size(); ++q)
					{
						const double d = std::hypot(r.points[q].x - raw[k].x, r.points[q].z - raw[k].z);
						if (d < dmin) { dmin = d; endP = r.points[q]; endY = r.profileY[q]; }
					}
				if (dmin > 30.0) { endP = best->p; endY = best->y; continue; } // chaussée d'une route hors emprise
				raw.resize(k + 1);
				raw.back() = endP;
				endId.clear();
				break;
			}
			// Lieu posé sur la chaussée : déjà relié, pas de chemin (il reste une cible).
			if (std::hypot(endP.x - pl->center.x, endP.z - pl->center.z) < 1.0) { targets.push_back({ pl->center, pl->id, pl->plateauY }); continue; }
			Road road;
			road.fromId = pl->id; road.toId = endId;
			road.path = true;
			road.widthM = 3.2;
			road.points = ResamplePolyline(SmoothPolyline(raw, 3), 4.0);
			road.points.front() = pl->center;
			road.points.back() = endP;
			std::vector<double> ground(road.points.size());
			for (size_t k = 0; k < road.points.size(); ++k) ground[k] = w.HeightAt(road.points[k]);
			RiverIndex rivers;
			rivers.Build(w.rivers);
			std::vector<CityDef> ends;
			for (const CityDef& c : w.cities) if (c.id == pl->id || (!endId.empty() && c.id == endId)) ends.push_back(c);
			ComputeProfile(road, ground, ends, rivers, sea, 0.18);
			road.profileY.back() = endY;
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

	// ------------------------------------------------------------------- ponts

	namespace
	{
		/// Lacet (degrés) qui aligne l'axe X local d'un mesh sur la direction \p d (XZ) :
		/// même convention que YawFacing (rotation autour de Y, -Z local vers l'avant).
		double YawAlong(Vec2 d) { return std::atan2(-d.z, d.x) * 57.29577951308232; }

		/// Ajoute une instance de décor (mesh de meshes/props/), hauteur ABSOLUE \p y (m) :
		/// yOffset = y - sol à la position (le client pose l'instance sur sa heightmap).
		/// \p solid : pièce de bâtiment solide (bits solide + pièce du catalogue de collision).
		void AddBridgeProp(World& w, const char* mesh, Vec2 p, double y, double yaw, double scale, bool solid)
		{
			ScatterDef s;
			s.mesh = std::string("meshes/props/") + mesh + ".gltf";
			s.position = p; s.yOffset = y - w.HeightAt(p); s.yawDeg = yaw; s.scale = scale;
			// Pièce solide : collision par le catalogue de pièces (bit 1 : murs = barrières,
			// dalles = dessus marchable, building_pieces.json), sinon cylindre englobant.
			s.flags = solid ? 3u : 0u; s.cullClass = 1;
			w.scatter.push_back(std::move(s));
		}
	}

	void BuildBridges(World& w)
	{
		if (w.riverDist.data.empty()) return;
		RiverIndex rivers;
		rivers.Build(w.rivers);
		auto inBed = [&](Vec2 p) {
			const int i = w.riverDist.IndexX(p.x), j = w.riverDist.IndexZ(p.z);
			return w.riverDist.InBounds(i, j) && w.riverDist.At(i, j) < 0.5f;
		};
		int count = 0;
		for (const Road& r : w.roads) // routes globales et chemins locaux (passerelles de bois)
		{
			const size_t n = r.points.size();
			size_t k = 0;
			while (k < n)
			{
				if (!inBed(r.points[k])) { ++k; continue; }
				size_t a = k, b = k;
				while (b + 1 < n && inBed(r.points[b + 1])) ++b;
				k = b + 1;
				// Culées : un point (≈ 4 m) de chaque côté, sur la berge aplanie.
				a = a > 0 ? a - 1 : a;
				b = std::min(n - 1, b + 1);
				const Vec2 A = r.points[a], B = r.points[b];
				const Vec2 mid = (A + B) * 0.5;
				if (!w.OwnsPoint(mid)) continue; // le pont appartient à la cellule de son milieu
				const double len = std::hypot(B.x - A.x, B.z - A.z);
				if (len < 3.0) continue;
				const Vec2 d = (B - A) * (1.0 / len);
				const Vec2 nrm{ -d.z, d.x };
				const double yaw = YawAlong(d);
				const bool stone = !r.path;
				// Atelier décors T2 (n° 82) : chemin de montagne = pont suspendu de cordes,
				// une seule travée de 1,5 m ; ailleurs passerelle de planches sur pilotis (n° 83).
				const char biome = MacroBiome(w.master, mid.x, mid.z);
				const bool rope = !stone && (biome == '^' || biome == 'M' || biome == '*');
				const double half = rope ? 0.75 : std::max(1.6, r.widthM * 0.5);
				const int across = rope ? 1 : std::max(1, static_cast<int>(std::ceil(2.0 * half / 2.0)));
				const double water = rivers.WaterYNear(mid, 40.0);
				// Atelier décors T3 (n° 125) : ruisseau étroit franchi par une route = pont de
				// pierre en dos d'âne d'une seule arche (portée 8 m, tablier 3,5 m).
				if (stone && len <= 9.0 && r.widthM <= 6.0)
				{
					const double deckY = Lerp(r.profileY[a], r.profileY[b], 0.5);
					AddBridgeProp(w, "Bridge_Stone_Hump_1", mid, deckY, yaw, std::max(1.0, len / 8.5), true);
					w.Occupy(mid, len * 0.5 + 2.0);
					++count;
					continue;
				}
				// Tablier : dalles de 2 m le long de la corde A-B, à l'altitude du profil.
				const int along = std::max(1, static_cast<int>(std::ceil(len / 2.0)));
				for (int u = 0; u < along; ++u)
				{
					const double t = (u + 0.5) / along;
					const double y = Lerp(r.profileY[a], r.profileY[b], t);
					const Vec2 c = A + (B - A) * t;
					if (rope)
					{
						// Module de 2 m (planches et mains courantes de corde), dans l'axe.
						AddBridgeProp(w, "Bridge_Rope_Deck_1", c, y, yaw, 1.0, true);
						continue;
					}
					for (int v = 0; v < across; ++v)
					{
						const double off = (v + 0.5 - across * 0.5) * 2.0;
						AddBridgeProp(w, stone ? "Bridge_Stone_Deck_1" : "Boardwalk_Deck_1", c + nrm * off, y, yaw, 1.0, true);
					}
					// Garde-corps des deux côtés : parapet de pierre (n° 81) ou barrière de bois.
					for (int side = -1; side <= 1; side += 2)
					{
						const Vec2 e = c + nrm * (side * (across * 1.0 + 0.2));
						AddBridgeProp(w, stone ? "Bridge_Stone_Parapet_1" : "Prop_WoodenFence_Single", e, y, yaw, 1.0, true);
					}
				}
				// Arches de pierre sous le tablier (route) : du niveau de l'eau au tablier.
				if (stone && water > -1e8)
				{
					const double deck = Lerp(r.profileY[a], r.profileY[b], 0.5);
					const double h = deck - (water - 0.6);
					if (h > 0.8)
					{
						const double sc = Clamp(h / 3.0, 0.5, 3.0);
						const int arches = std::max(1, static_cast<int>(std::ceil(len / (2.0 * sc))));
						for (int u = 0; u < arches; ++u)
						{
							const Vec2 c = A + d * ((u + 0.5) * len / arches);
							for (int side = -1; side <= 1; side += 2)
								AddBridgeProp(w, "Bridge_Stone_Arch_1", c + nrm * (side * (across * 1.0 + 0.05)), deck - 3.0 * sc - 0.35, yaw, sc, false);
						}
					}
				}
				w.Occupy(mid, len * 0.5 + 2.0);
				++count;
			}
		}
		w.report.bridges += static_cast<size_t>(count);
	}

	/// Poteaux indicateurs (atelier décors T2, pl.15 ; flèches sans inscription) : un par
	/// carrefour, c.-à-d. bout de route sans ville ni lieu (fromId / toId vide) situé dans
	/// la zone jouable de la cellule, sur le bas-côté (largeur/2 + 2 m), côté sec et libre.
	/// Les chemins locaux (path) n'en ont pas. Déterministe. Effet de bord : w.scatter,
	/// occupation fine (0,6 m).
	void PlaceSignposts(World& w)
	{
		std::vector<Vec2> done;
		for (const Road& r : w.roads)
		{
			if (r.path || r.points.size() < 3) continue;
			for (int end = 0; end < 2; ++end)
			{
				if (!(end == 0 ? r.fromId : r.toId).empty()) continue;
				const Vec2 p = end == 0 ? r.points.front() : r.points.back();
				const Vec2 q = end == 0 ? r.points[2] : r.points[r.points.size() - 3];
				if (!w.InPlayableBounds(p, 64.0)) continue;
				bool near = false;
				for (const Vec2& d : done)
					if (std::hypot(d.x - p.x, d.z - p.z) < 25.0) near = true;
				if (near) continue;
				Vec2 dir{ q.x - p.x, q.z - p.z };
				const double l = std::hypot(dir.x, dir.z);
				if (l < 1e-6) continue;
				dir = dir * (1.0 / l);
				const Vec2 nrm{ -dir.z, dir.x };
				for (const double side : { 1.0, -1.0 })
				{
					const Vec2 s = p + nrm * (side * (r.widthM * 0.5 + 2.0)) + dir * 3.0;
					if (w.IsWater(s) || w.IsOccupied(s, 0.6)) continue;
					ScatterDef sd;
					sd.mesh = "meshes/props/Signpost_1.gltf";
					sd.position = s;
					sd.yawDeg = std::atan2(-dir.z, dir.x) * 180.0 / 3.14159265358979;   // flèche principale le long de la route
					sd.flags = 1u; sd.cullClass = 1; sd.collisionRadius = 0.15;
					w.scatter.push_back(std::move(sd));
					w.Occupy(s, 0.6);
					done.push_back(p);
					break;
				}
			}
		}
	}
}
