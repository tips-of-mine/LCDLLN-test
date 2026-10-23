// tools/world_gen — orchestration du pipeline de génération.

#include "WorldGen.h"
#include "WorldGenContext.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <queue>
#include <set>

namespace tools::world_gen
{
	namespace
	{
		/// Découpe une polyligne en tronçons dont les points tombent dans la boîte.
		template <typename T, typename MakePiece>
		void ClipRuns(const std::vector<Vec2>& pts, double x0, double z0, double x1, double z1, MakePiece&& make)
		{
			size_t start = 0;
			bool in = false;
			for (size_t k = 0; k <= pts.size(); ++k)
			{
				const bool inside = k < pts.size() && pts[k].x >= x0 && pts[k].x <= x1 && pts[k].z >= z0 && pts[k].z <= z1;
				if (inside && !in) { start = k; in = true; }
				else if (!inside && in)
				{
					if (k - start >= 2) make(start, k);
					in = false;
				}
			}
		}

		void Accumulate(GenReport& dst, const GenReport& src)
		{
			dst.scatterInstances += src.scatterInstances;
			dst.scatterMeshes = std::max(dst.scatterMeshes, src.scatterMeshes);
			dst.buildings += src.buildings;
			dst.spawners += src.spawners;
			dst.ambientNpcs += src.ambientNpcs;
			dst.creatures += src.creatures;
			dst.respawnPoints += src.respawnPoints;
			dst.interactables += src.interactables;
			dst.proceduralPlaces += src.proceduralPlaces;
			dst.warnings.insert(dst.warnings.end(), src.warnings.begin(), src.warnings.end());
			dst.writtenFiles.insert(dst.writtenFiles.end(), src.writtenFiles.begin(), src.writtenFiles.end());
		}
	}

	void InitCell(World& w, const GlobalWorld& g, engine::world::cells::CellCoord c)
	{
		w.g = &g;
		w.master = g.master;
		w.contentRoot = g.contentRoot;
		w.outputRoot = g.outputRoot;
		w.cell = c;
		w.cellName = engine::world::cells::CellDirName(c);
		w.zoneId = engine::world::cells::CellZoneId(c);
		const engine::world::cells::CellPoint mn = engine::world::cells::CellMinCorner(c);
		w.terrainOriginX = mn.x;
		w.terrainOriginZ = mn.z;
		w.terrainSizeM = engine::world::cells::kCellSizeMeters;

		for (const CityDef& city : g.cities)
			if (city.n == c.n && city.e == c.e) w.cities.push_back(city);
		for (const PoiDef& p : g.pois)
			if (p.n == c.n && p.e == c.e) w.pois.push_back(p);

		// Éléments globaux recoupant la cellule (+ marge : creusement, accotements).
		const double margin = 60.0;
		const double x0 = w.terrainOriginX - margin, z0 = w.terrainOriginZ - margin;
		const double x1 = w.terrainOriginX + w.terrainSizeM + margin, z1 = w.terrainOriginZ + w.terrainSizeM + margin;
		for (const Lake& l : g.lakes)
			if (l.center.x + l.radiusM + 60.0 >= x0 && l.center.x - l.radiusM - 60.0 <= x1
				&& l.center.z + l.radiusM + 60.0 >= z0 && l.center.z - l.radiusM - 60.0 <= z1)
				w.lakes.push_back(l);
		for (const River& r : g.rivers)
			ClipRuns<River>(r.points, x0, z0, x1, z1, [&](size_t a, size_t b) {
				River piece;
				piece.name = r.name;
				piece.globalStart = a;
				piece.points.assign(r.points.begin() + static_cast<long>(a), r.points.begin() + static_cast<long>(b));
				piece.waterY.assign(r.waterY.begin() + static_cast<long>(a), r.waterY.begin() + static_cast<long>(b));
				piece.widthM.assign(r.widthM.begin() + static_cast<long>(a), r.widthM.begin() + static_cast<long>(b));
				w.rivers.push_back(std::move(piece));
			});
		for (const Road& r : g.roads)
			ClipRuns<Road>(r.points, x0, z0, x1, z1, [&](size_t a, size_t b) {
				Road piece = r;
				piece.points.assign(r.points.begin() + static_cast<long>(a), r.points.begin() + static_cast<long>(b));
				piece.profileY.assign(r.profileY.begin() + static_cast<long>(a), r.profileY.begin() + static_cast<long>(b));
				w.roads.push_back(std::move(piece));
			});
	}

	/// Cellules traversées par les routes qui relient les villes de départ.
	/// Arbre couvrant « au plus près » : en partant de la première ville de départ,
	/// on ajoute à chaque tour la ville de départ non reliée la plus proche PAR LA
	/// ROUTE (Dijkstra multi-sources depuis tout ce qui est déjà relié). Seules les
	/// routes inter-villes comptent (pas les chemins locaux). Une ville de départ
	/// injoignable (île) est signalée et laissée isolée. Les cellules ajoutées
	/// suivent celles du master, dans l'ordre de parcours des routes. Déterministe.
	void AddStartCorridors(GlobalWorld& g, std::vector<std::string>& ids)
	{
		namespace wc = engine::world::cells;
		// Graphe : villes (id → index) et arêtes = routes inter-villes.
		std::map<std::string, int> node;
		auto nodeOf = [&](const std::string& id) {
			auto it = node.find(id);
			if (it != node.end()) return it->second;
			const int k = static_cast<int>(node.size());
			node.emplace(id, k);
			return k;
		};
		struct Edge { int to; size_t road; double len; };
		std::vector<std::vector<Edge>> adj;
		for (size_t r = 0; r < g.roads.size(); ++r)
		{
			const Road& road = g.roads[r];
			if (road.path || road.points.size() < 2) continue;
			const int a = nodeOf(road.fromId), b = nodeOf(road.toId);
			if (static_cast<int>(adj.size()) <= std::max(a, b)) adj.resize(static_cast<size_t>(std::max(a, b)) + 1u);
			double len = 0.0;
			for (size_t i = 1; i < road.points.size(); ++i)
				len += std::hypot(road.points[i].x - road.points[i - 1].x, road.points[i].z - road.points[i - 1].z);
			adj[static_cast<size_t>(a)].push_back({ b, r, len });
			adj[static_cast<size_t>(b)].push_back({ a, r, len });
		}
		std::vector<int> starts;
		for (const CityDef& c : g.cities)
			if (!c.startFor.empty())
				starts.push_back(nodeOf(c.id));
		adj.resize(node.size());
		if (starts.size() < 2) return;

		std::vector<char> connected(node.size(), 0);
		std::vector<size_t> chosenRoads;
		connected[static_cast<size_t>(starts.front())] = 1;
		std::set<int> remaining(starts.begin() + 1, starts.end());
		while (!remaining.empty())
		{
			// Dijkstra depuis tous les nœuds déjà reliés.
			const double inf = std::numeric_limits<double>::infinity();
			std::vector<double> dist(node.size(), inf);
			std::vector<std::pair<int, size_t>> prev(node.size(), { -1, 0 });
			using QE = std::pair<double, int>;
			std::priority_queue<QE, std::vector<QE>, std::greater<QE>> pq;
			for (size_t i = 0; i < node.size(); ++i)
				if (connected[i]) { dist[i] = 0.0; pq.push({ 0.0, static_cast<int>(i) }); }
			while (!pq.empty())
			{
				const auto [d, u] = pq.top();
				pq.pop();
				if (d > dist[static_cast<size_t>(u)]) continue;
				for (const Edge& ed : adj[static_cast<size_t>(u)])
				{
					const double nd = d + ed.len;
					if (nd < dist[static_cast<size_t>(ed.to)])
					{
						dist[static_cast<size_t>(ed.to)] = nd;
						prev[static_cast<size_t>(ed.to)] = { u, ed.road };
						pq.push({ nd, ed.to });
					}
				}
			}
			int best = -1;
			for (int s : remaining)
				if (dist[static_cast<size_t>(s)] < inf && (best < 0 || dist[static_cast<size_t>(s)] < dist[static_cast<size_t>(best)]))
					best = s;
			if (best < 0)
			{
				for (int s : remaining)
					for (const auto& [id, k] : node)
						if (k == s) g.Warn("ville de depart sans route vers les autres : " + id + " (reste isolee)");
				break;
			}
			for (int v = best; !connected[static_cast<size_t>(v)]; v = prev[static_cast<size_t>(v)].first)
			{
				chosenRoads.push_back(prev[static_cast<size_t>(v)].second);
				connected[static_cast<size_t>(v)] = 1;
			}
			remaining.erase(best);
		}

		// Cellules de la chaussée (+ voisine quand la route longe une frontière).
		std::set<std::string> have(ids.begin(), ids.end());
		const double margin = g.master.corridorMarginM;
		size_t added = 0;
		auto addCell = [&](wc::CellCoord c) {
			if (!wc::IsInLoreGrid(c)) return; // hors de la carte du lore : jamais générée
			const std::string id = wc::LoreCellId(c);
			if (have.insert(id).second) { ids.push_back(id); ++added; }
		};
		for (size_t r : chosenRoads)
			for (const Vec2& p : g.roads[r].points)
			{
				const wc::CellCoord c = wc::CellFromPosition(p.x, p.z);
				addCell(c);
				if (margin <= 0.0) continue;
				const wc::CellPoint mn = wc::CellMinCorner(c);
				const double s = wc::kCellSizeMeters;
				// X = est ; Z = sud (nord = −Z, cellule n + 1).
				if (p.x - mn.x < margin)     addCell({ c.n, c.e - 1 });
				if (mn.x + s - p.x < margin) addCell({ c.n, c.e + 1 });
				if (p.z - mn.z < margin)     addCell({ c.n + 1, c.e });
				if (mn.z + s - p.z < margin) addCell({ c.n - 1, c.e });
			}
		std::printf("[world_gen] liaison des villes de depart : %zu route(s), %zu cellule(s) ajoutee(s) (%zu au total)\n",
			chosenRoads.size(), added, ids.size());
	}

	bool RunWorldGen(const GenOptions& opt, GenReport& outReport, std::string& err)
	{
		GlobalWorld g;
		g.contentRoot = opt.contentRoot;
		g.outputRoot = opt.outputRoot.empty() ? opt.contentRoot : opt.outputRoot;
		if (!LoadWorldMaster(opt.contentRoot, opt.worldId, g.master, err)) return false;

		// Couche macro : tout ce qui traverse les frontières de cellules.
		BuildMacroTerrain(g);   // relief grossier, sites des villes, lacs
		BuildMacroRivers(g);    // rivières (contournent les villes)
		BuildMacroRoads(g);     // routes inter-villes + profils en long

		std::vector<std::string> ids = opt.cells.empty() ? g.master.generatedCells : opt.cells;
		// Monde complet : cellules des routes reliant les villes de départ.
		if (opt.cells.empty() && g.master.connectStartCities)
			AddStartCorridors(g, ids);
		// Doublons retirés (ordre conservé) : une cellule n'est générée qu'une fois.
		{
			std::vector<std::string> uniq;
			for (const std::string& id : ids)
				if (std::find(uniq.begin(), uniq.end(), id) == uniq.end()) uniq.push_back(id);
			ids.swap(uniq);
		}
		if (ids.empty()) { err = "aucune cellule a generer (generated_cells vide)"; return false; }
		// Un sous-ensemble (--cells) ne réécrit PAS les sorties globales : elles
		// seraient incomplètes (départs, lieux et carte des autres cellules perdus).
		const bool writeGlobal = opt.cells.empty();
		std::vector<std::unique_ptr<World>> worlds;
		std::vector<World*> cells;
		GenReport total;
		for (const std::string& id : ids)
		{
			int n = 0, e = 0;
			if (!ParseCellId(id, n, e)) { err = "cellule invalide : " + id; return false; }
			auto w = std::make_unique<World>();
			InitCell(*w, g, { n, e });
			// L'ordre compte : chaque étape s'appuie sur les champs des précédentes.
			BuildPlaces(*w);        // lieux procéduraux (avant le terrain : leurs plateaux)
			BuildCellTerrain(*w);   // altitude + biomes (fonctions macro)
			FlattenSettlements(*w); // plateaux urbains
			CarveWater(*w);         // lits de rivière, lacs
			ApplyRoads(*w);         // routes globales + chemins locaux
			if (!ImportLegacyContent(*w, err)) return false; // occupe la place des objets historiques
			BuildSettlements(*w);   // rues, bâtiments, remparts, mobilier, graphe PNJ
			BuildVegetation(*w);    // décor naturel (respecte l'occupation)
			BuildPopulation(*w);    // spawners, respawns, départs
			w->report.scatterInstances = w->scatter.size();
			w->report.buildings = w->buildings.size();
			w->report.spawners = w->spawners.size();

			for (const CityDef& c : w->cities)
				if (w->HeightAt(c.center) < w->master.scale.seaLevelM + 1.0)
				{
					err = "agglomeration sous le niveau de la mer : " + c.id;
					return false;
				}
			if (!opt.dryRun && !WriteCellOutputs(*w, err)) return false;
			Accumulate(total, w->report);
			++total.cells;
			// Libère les champs lourds (≈ 100 Mo par cellule) : seules les listes
			// servent encore aux sorties globales.
			w->height = {}; w->biome = {}; w->roadDist = {}; w->riverDist = {}; w->urban = {};
			w->occupied.Clear();
			w->scatter.clear(); w->scatter.shrink_to_fit();
			cells.push_back(w.get());
			worlds.push_back(std::move(w));
		}
		bool anyStart = false;
		for (const World* w : cells) anyStart = anyStart || !w->starts.empty();
		if (!anyStart && opt.cells.empty()) { err = "aucun point de depart genere"; return false; }

		if (!opt.dryRun)
		{
			if (!WriteHouseTemplates(g.outputRoot, g.report, err)) return false;
			if (writeGlobal && !WriteGlobalOutputs(g, cells, err)) return false;
		}
		total.roads = g.report.roads;
		total.roadLengthM = g.report.roadLengthM;
		total.rivers = g.report.rivers;
		total.lakes = g.report.lakes;
		total.houseTemplates = g.report.houseTemplates;
		total.warnings.insert(total.warnings.begin(), g.report.warnings.begin(), g.report.warnings.end());
		total.writtenFiles.insert(total.writtenFiles.end(), g.report.writtenFiles.begin(), g.report.writtenFiles.end());
		outReport = std::move(total);
		return true;
	}
}
