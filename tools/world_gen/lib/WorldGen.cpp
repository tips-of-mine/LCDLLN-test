// tools/world_gen — orchestration du pipeline de génération.

#include "WorldGen.h"
#include "WorldGenContext.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <queue>
#include <set>
#include <thread>

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
			dst.seaPocketSamples += src.seaPocketSamples;
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
		// Dossier et identifiant de zone propres à la carte (cell_* / enfers_*).
		w.cellName = engine::world::cells::CellDirName(c, g.master.cellMap);
		w.zoneId = engine::world::cells::CellZoneId(c, g.master.cellMap);
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

	/// Routes qui relient les villes de départ.
	/// Arbre couvrant « au plus près » : en partant de la première ville de départ,
	/// on ajoute à chaque tour la ville de départ non reliée la plus proche PAR LA
	/// ROUTE (Dijkstra multi-sources depuis tout ce qui est déjà relié). Seules les
	/// routes inter-villes comptent (pas les chemins locaux). Une ville de départ
	/// injoignable (île) est signalée (si \p warn) et laissée isolée. Déterministe.
	std::vector<size_t> StartCorridorRoads(GlobalWorld& g, const std::vector<Road>& roads, bool warn)
	{
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
		for (size_t r = 0; r < roads.size(); ++r)
		{
			const Road& road = roads[r];
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
		std::vector<size_t> chosenRoads;
		if (starts.size() < 2) return chosenRoads;

		std::vector<char> connected(node.size(), 0);
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
				if (warn)
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
		return chosenRoads;
	}

	/// Cellules de la chaussée des routes choisies (+ voisine quand la route longe
	/// une frontière), ajoutées à \p ids dans l'ordre de parcours des routes.
	size_t AddCorridorCells(const GlobalWorld& g, const std::vector<Road>& roads,
		const std::vector<size_t>& chosen, std::vector<std::string>& ids)
	{
		namespace wc = engine::world::cells;
		std::set<std::string> have(ids.begin(), ids.end());
		const double margin = g.master.corridorMarginM;
		size_t added = 0;
		auto addCell = [&](wc::CellCoord c) {
			if (!wc::IsInLoreGrid(c)) return; // hors de la carte du lore : jamais générée
			const std::string id = wc::LoreCellId(c);
			if (have.insert(id).second) { ids.push_back(id); ++added; }
		};
		for (size_t r : chosen)
			for (const Vec2& p : roads[r].points)
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
		return added;
	}

	/// Cellules traversées par les routes qui relient les villes de départ, sur le
	/// réseau du GÉNÉRATEUR (g.generatorRoads, v1.0.105 ; g.roads s'il est vide) :
	/// les cellules jouables ne changent pas quand les routes du lore sont tracées.
	/// Les cellules ajoutées suivent celles du master, dans l'ordre de parcours.
	void AddStartCorridors(GlobalWorld& g, std::vector<std::string>& ids)
	{
		const std::vector<Road>& network = g.generatorRoads.empty() ? g.roads : g.generatorRoads;
		const std::vector<size_t> chosenRoads = StartCorridorRoads(g, network, true);
		const size_t added = AddCorridorCells(g, network, chosenRoads, ids);
		std::printf("[world_gen] liaison des villes de depart : %zu route(s), %zu cellule(s) ajoutee(s) (%zu au total)\n",
			chosenRoads.size(), added, ids.size());
	}

	bool RunWorldGen(const GenOptions& opt, GenReport& outReport, std::string& err)
	{
		GlobalWorld g;
		g.contentRoot = opt.contentRoot;
		g.outputRoot = opt.outputRoot.empty() ? opt.contentRoot : opt.outputRoot;
		if (!LoadWorldMaster(opt.contentRoot, opt.worldId, g.master, err)) return false;

		// Mesure du temps par étape (secondes cumulées), affichée en fin de génération.
		using Clock = std::chrono::steady_clock;
		std::map<std::string, double> stepSec;
		std::vector<std::string> stepOrder;
		// Exécute fn et ajoute sa durée à l'étape name (appelable depuis plusieurs fils :
		// avec des cellules en parallèle, les durées des cellules s'additionnent).
		std::mutex stepMutex;
		auto timed = [&](const char* name, auto&& fn) {
			const auto t0 = Clock::now();
			fn();
			const double dt = std::chrono::duration<double>(Clock::now() - t0).count();
			std::lock_guard<std::mutex> lock(stepMutex);
			if (!stepSec.count(name)) stepOrder.push_back(name);
			stepSec[name] += dt;
		};

		// Couche macro : tout ce qui traverse les frontières de cellules.
		timed("macro : relief", [&] { BuildMacroTerrain(g); });   // relief grossier, sites des villes, lacs
		timed("macro : rivieres", [&] { BuildMacroRivers(g); });  // rivières (contournent les villes)
		timed("macro : routes", [&] { BuildMacroRoads(g); });     // routes du lore + corridors du générateur, profils en long

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
		std::vector<std::unique_ptr<World>> worlds(ids.size());
		std::vector<World*> cells;
		GenReport total;
		// Cellules valides (avant tout calcul).
		std::vector<engine::world::cells::CellCoord> coords;
		for (const std::string& id : ids)
		{
			int n = 0, e = 0;
			if (!ParseCellId(id, n, e)) { err = "cellule invalide : " + id; return false; }
			if (!engine::world::cells::IsInLoreGrid({ n, e }, g.master.cellMap))
			{
				err = "cellule hors de la grille de la carte '" + g.master.worldId + "' : " + id; return false;
			}
			coords.push_back({ n, e });
		}
		// Génère la cellule k (pipeline complet, écriture comprise) dans worlds[k].
		// Ne lit que g (constant) et n'écrit que dans sa cellule et ses fichiers :
		// plusieurs cellules peuvent tourner en même temps. \return message d'erreur
		// (vide si succès).
		auto genCell = [&](size_t k) -> std::string {
			std::string cerr;
			auto w = std::make_unique<World>();
			timed("cellule : init", [&] { InitCell(*w, g, coords[k]); });
			// L'ordre compte : chaque étape s'appuie sur les champs des précédentes.
			timed("cellule : lieux", [&] { BuildPlaces(*w); });            // lieux procéduraux (avant le terrain : leurs plateaux)
			timed("cellule : terrain", [&] { BuildCellTerrain(*w); });     // altitude + biomes (fonctions macro)
			timed("cellule : plateaux", [&] { FlattenSettlements(*w); });  // plateaux urbains
			timed("cellule : eau", [&] { CarveWater(*w); });               // lits de rivière, lacs
			timed("cellule : routes", [&] { ApplyRoads(*w); });            // routes globales + chemins locaux
			timed("cellule : poches de mer", [&] { FillSeaPockets(*w); }); // EAU.5 : un déblai de route ne crée pas de « mare de mer »
			bool imported = true;
			timed("cellule : import", [&] { imported = ImportLegacyContent(*w, cerr); }); // occupe la place des objets historiques
			if (!imported) return cerr.empty() ? std::string("import du contenu historique impossible") : cerr;
			timed("cellule : agglomerations", [&] { BuildSettlements(*w); }); // rues, bâtiments, remparts, mobilier, graphe PNJ
			timed("cellule : foret", [&] { BuildForestMask(*w); });           // FORET.1 : densité des forêts (arbres posés par le client)
			timed("cellule : vegetation", [&] { BuildVegetation(*w); });      // décor naturel (respecte l'occupation)
			timed("cellule : population", [&] { BuildPopulation(*w); });      // spawners, respawns, départs
			w->report.scatterInstances = w->scatter.size();
			w->report.buildings = w->buildings.size();
			w->report.spawners = w->spawners.size();

			for (const CityDef& c : w->cities)
				if (w->HeightAt(c.center) < w->master.scale.seaLevelM + 1.0)
					return "agglomeration sous le niveau de la mer : " + c.id;
			bool written = true;
			if (!opt.dryRun) timed("cellule : ecriture", [&] { written = WriteCellOutputs(*w, cerr); });
			if (!written) return cerr.empty() ? std::string("ecriture de la cellule impossible") : cerr;
			// Altitude du sol au centre des lieux remarquables (portails : portals.json),
			// relevée AVANT la libération de la heightmap.
			for (PoiDef& p : w->pois)
				p.y = w->HeightAt(p.center);
			// Libère les champs lourds (≈ 100 Mo par cellule) : seules les listes
			// servent encore aux sorties globales.
			w->height = {}; w->biome = {}; w->roadDist = {}; w->riverDist = {}; w->urban = {};
			w->occupied.Clear();
			w->scatter.clear(); w->scatter.shrink_to_fit();
			worlds[k] = std::move(w);
			return {};
		};
		// Plusieurs cellules en même temps (opt.jobs, 4 au plus par défaut : ≈ 350 Mo
		// de mémoire par cellule en cours). Chaque fil prend la cellule suivante ; une
		// erreur arrête la distribution, et c'est la PREMIÈRE cellule en erreur (ordre
		// de la liste) qui est rapportée, comme en génération séquentielle.
		{
			const unsigned hw = std::max(1u, std::thread::hardware_concurrency());
			const size_t jobs = std::min<size_t>(ids.size(), opt.jobs > 0 ? static_cast<size_t>(opt.jobs) : std::min(4u, hw));
			std::vector<std::string> errs(ids.size());
			std::atomic<size_t> next{ 0 };
			std::atomic<bool> stop{ false };
			auto worker = [&]() {
				for (size_t k = next.fetch_add(1); k < ids.size() && !stop.load(); k = next.fetch_add(1))
				{
					errs[k] = genCell(k);
					if (!errs[k].empty()) stop.store(true);
				}
			};
			std::printf("[world_gen] %zu cellule(s) a generer, %zu en parallele\n", ids.size(), jobs);
			if (jobs <= 1) worker();
			else
			{
				std::vector<std::thread> pool;
				for (size_t t = 0; t < jobs; ++t) pool.emplace_back(worker);
				for (std::thread& th : pool) th.join();
			}
			for (size_t k = 0; k < ids.size(); ++k)
				if (!errs[k].empty()) { err = errs[k]; return false; }
		}
		// Bilans fusionnés dans l'ordre des cellules (indépendant du parallélisme).
		for (std::unique_ptr<World>& w : worlds)
		{
			Accumulate(total, w->report);
			++total.cells;
			cells.push_back(w.get());
		}
		// v1.0.121 — recensement des cimetières par cellule (--graveyard-report), écrit
		// avant le contrôle du total pour aider à le corriger.
		if (!opt.graveyardReport.empty())
		{
			std::FILE* f = std::fopen(opt.graveyardReport.c_str(), "wb");
			if (!f) { err = "rapport des cimetieres impossible a ecrire : " + opt.graveyardReport; return false; }
			for (const World* w : cells)
			{
				// Jeton « id@x:z » (position monde en m, pour choisir les hameaux les plus
				// proches d'un autre cimetière) ; « id@? » pour une ligne importée.
				std::vector<std::string> gy;
				char pos[64];
				for (const RespawnDef& r : w->respawns)
					if (r.type == "graveyard")
					{
						std::snprintf(pos, sizeof(pos), "@%.0f:%.0f", r.position.x, r.position.z);
						gy.push_back(r.graveyardId + pos);
					}
				for (const std::string& l : w->importedRespawnLines)
					if (l.find(" graveyard ") != std::string::npos)
					{
						const size_t at = l.rfind(" gy=");
						gy.push_back((at == std::string::npos ? std::string("?") : l.substr(at + 4)) + "@?");
					}
				std::fprintf(f, "%s %zu", w->cellName.c_str(), gy.size());
				for (const std::string& id : gy) std::fprintf(f, " %s", id.c_str());
				std::fprintf(f, "\n");
			}
			std::fclose(f);
		}
		// v1.0.111 — nombre exact de cimetières (graveyards.target du master, 666 aux
		// Royaumes) : contrôlé seulement sur une génération complète (sans --cells).
		if (opt.cells.empty() && g.master.graveyardTarget > 0)
		{
			int graves = 0;
			for (const World* w : cells)
			{
				for (const RespawnDef& r : w->respawns) graves += r.type == "graveyard" ? 1 : 0;
				for (const std::string& l : w->importedRespawnLines) graves += l.find(" graveyard ") != std::string::npos ? 1 : 0;
			}
			std::printf("[world_gen] cimetieres : %d (attendus : %d)\n", graves, g.master.graveyardTarget);
			if (graves != g.master.graveyardTarget)
			{
				err = "cimetieres : " + std::to_string(graves) + " generes, " + std::to_string(g.master.graveyardTarget) +
					" attendus (graveyards.target du master ; ajuster graveyards.without_graveyard)";
				return false;
			}
		}
		// v1.0.114 — identifiant stable de chaque cimetière (gy=<id>) : présent et unique
		// dans le monde (visites mémorisées par personnage, exploit des 666 cimetières).
		{
			std::set<std::string> ids;
			for (const World* w : cells)
			{
				std::vector<std::string> cellIds;
				for (const RespawnDef& r : w->respawns)
					if (r.type == "graveyard") cellIds.push_back(r.graveyardId);
				for (const std::string& l : w->importedRespawnLines)
					if (l.find(" graveyard ") != std::string::npos)
					{
						const size_t at = l.rfind(" gy=");
						cellIds.push_back(at == std::string::npos ? std::string() : l.substr(at + 4));
					}
				for (const std::string& id : cellIds)
					if (id.empty() || !ids.insert(id).second)
					{
						err = "cimetiere de " + w->cellName + (id.empty() ? std::string(" sans identifiant") : " : identifiant en double '" + id + "'");
						return false;
					}
			}
		}
		bool anyStart = false;
		for (const World* w : cells) anyStart = anyStart || !w->starts.empty();
		if (!anyStart && opt.cells.empty()) { err = "aucun point de depart genere"; return false; }

		if (!opt.dryRun)
		{
			if (!WriteHouseTemplates(g.outputRoot, g.report, err)) return false;
			bool globalOk = true;
			if (writeGlobal) timed("sorties globales", [&] { globalOk = WriteGlobalOutputs(g, cells, err); });
			if (!globalOk) return false;
		}
		// Temps par étape (cumul sur toutes les cellules) : repère pour les optimisations.
		{
			double sum = 0.0;
			for (const auto& kv : stepSec) sum += kv.second;
			std::printf("[world_gen] temps par etape (%zu cellule(s), %.1f s cumules sur tous les fils) :\n", cells.size(), sum);
			for (const std::string& name : stepOrder)
				std::printf("  %-26s %8.1f s  %5.1f %%\n", name.c_str(), stepSec[name], sum > 0.0 ? 100.0 * stepSec[name] / sum : 0.0);
		}
		total.roads = g.report.roads;
		total.roadLengthM = g.report.roadLengthM;
		total.rivers = g.report.rivers;
		total.lakes = g.report.lakes;
		total.houseTemplates = g.report.houseTemplates;
		total.portals = g.report.portals;
		total.warnings.insert(total.warnings.begin(), g.report.warnings.begin(), g.report.warnings.end());
		total.writtenFiles.insert(total.writtenFiles.end(), g.report.writtenFiles.begin(), g.report.writtenFiles.end());
		outReport = std::move(total);
		return true;
	}
}
