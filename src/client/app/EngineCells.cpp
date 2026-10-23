// Engine — monde « Royaumes » en cellules de 10 km (étape 3, client).
//
// Le monde est continu en coordonnées GLOBALES (±200 km) ; le moteur, lui,
// travaille dans un repère de RENDU centré sur la cellule du joueur
// (m_renderFrame) pour garder la précision du float. Ce fichier gère :
//   - les réglages du monde en cellules (cellules générées, réglages client) ;
//   - le streaming : heightmaps CPU (collision, pose au sol) dans un grand
//     rayon, terrain GPU + décor instancié + bâtiments dans un rayon proche,
//     déchargement avec hystérésis et destruction GPU différée ;
//   - le contenu « léger » chargé une fois par cellule (PNJ / interactibles,
//     marqueurs de respawn, eau) en coordonnées de rendu ;
//   - le changement d'origine (« rebase ») quand le joueur change de cellule.
// Les conversions réseau (shard, master) sont faites aux frontières : UIModel
// (snapshots), Engine::UpdateGameplayNet (Input, ForcePosition), sauvegarde de
// position et EnterWorld.

#include "src/client/app/Engine.h"

#include "src/client/dialogue/DialogueConfigLoader.h"
#include "src/client/render/terrain/HeightmapLoader.h"
#include "src/client/world/places/WorldPlaces.h"
#include "src/client/world/water/WaterSurfaces.h"
#include "src/shared/core/Log.h"
#include "src/shared/platform/FileSystem.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <sstream>

namespace engine
{
	namespace cells = engine::world::cells;

	namespace
	{
		/// Nombre de frames avant de détruire une tuile retirée (frames en vol + marge).
		constexpr uint64_t kRetiredTileDelayFrames = 4u;

		/// Coin min GLOBAL du terrain d'une cellule : celui de sa heightmap CPU
		/// (terrain.origin_x/z du zone.json) si résidente, sinon le coin théorique.
		cells::CellPoint TerrainMinCorner(const cells::CellHeightFields* heights, cells::CellCoord cell)
		{
			if (heights != nullptr)
				if (const cells::CellHeightTile* t = heights->Find(cell))
					return { t->minX, t->minZ };
			return cells::CellMinCorner(cell);
		}

		/// Lit un fichier binaire du contenu (vide si absent).
		std::vector<uint8_t> ReadContentBytes(const engine::core::Config& cfg, const std::string& rel)
		{
			return engine::platform::FileSystem::ReadAllBytesContent(cfg, rel);
		}
	}

	bool Engine::LoadCellWorldSettings()
	{
		const std::string zone = m_cfg.GetString("world.active_zone", "");
		m_cellWorld = cells::IsCellWorld(zone);
		if (!m_cellWorld)
			return false;

		m_generatedCells.Clear();
		engine::client::WorldPlaces places;
		std::string err;
		if (places.LoadFromContent(m_cfg, zone, err))
		{
			for (const std::string& lore : places.GeneratedCells())
				if (const std::optional<cells::CellCoord> c = cells::ParseLoreCellId(lore))
					m_generatedCells.Add(*c);
		}
		if (m_generatedCells.Empty())
		{
			LOG_ERROR(Core, "[Cells] Monde en cellules '{}' sans cellule generee ({}) : lancer tools/world_gen", zone, err);
			return true;
		}

		// Réglages client du monde (brouillard, culling du décor, profondeur de champ…) :
		// identiques dans toutes les cellules (importés par le générateur) — on prend
		// ceux de la première, SANS son contenu (PNJ, spawn, chemins propres à la cellule).
		const cells::CellCoord first = m_generatedCells.Cells().front();
		const std::string content = m_cfg.GetString("paths.content", "game/data");
		engine::core::Config cellCfg;
		if (cellCfg.LoadFromFile(content + "/zones/" + cells::CellDirName(first) + "/zone.json"))
		{
			for (const char* prefix : { "world.interactables", "world.default_spawn", "world.cell",
					"world.scatter_path", "world.scenery", "world.test_water" })
				cellCfg.EraseUnderPrefix(prefix);
			const size_t merged = m_cfg.MergeFrom(cellCfg, "world.");
			LOG_INFO(Core, "[Cells] Reglages client de {} fusionnes ({} cle(s))", cells::CellDirName(first), merged);
		}
		// Le monde en cellules n'a pas de bassin de test.
		m_cfg.SetValue("world.test_water.enabled", false);

		// Rayons de streaming (m, distance au BORD de la cellule).
		m_cellRenderRadiusM = static_cast<float>(std::clamp(m_cfg.GetDouble("world.cells.render_radius_m", 2500.0), 200.0, 9000.0));
		m_cellUnloadRadiusM = std::max(m_cellRenderRadiusM + 500.0f,
			static_cast<float>(m_cfg.GetDouble("world.cells.unload_radius_m", 3500.0)));
		m_cellHeightRadiusM = std::max(m_cellUnloadRadiusM,
			static_cast<float>(m_cfg.GetDouble("world.cells.height_radius_m", 6000.0)));
		m_cellTerrainDrawDistanceM = static_cast<float>(std::clamp(m_cfg.GetDouble("world.cells.terrain_draw_distance_m", 2500.0), 200.0, 20000.0));

		// Point d'arrivée au boot (GLOBAL) : centre de la première cellule, ou
		// world.cells.boot_x / boot_z (tests, fond de l'écran de connexion).
		{
			const cells::CellPoint c = cells::CellCenter(first);
			m_cellBootGlobalX = m_cfg.GetDouble("world.cells.boot_x", c.x);
			m_cellBootGlobalZ = m_cfg.GetDouble("world.cells.boot_z", c.z);
		}
		m_renderFrame.SetOriginCell(cells::CellFromPosition(m_cellBootGlobalX, m_cellBootGlobalZ));
		LOG_INFO(Core, "[Cells] Monde en cellules '{}' : {} cellule(s) generee(s), rayons rendu/dechargement/hauteurs = {:.0f}/{:.0f}/{:.0f} m",
			zone, m_generatedCells.Cells().size(), m_cellRenderRadiusM, m_cellUnloadRadiusM, m_cellHeightRadiusM);
		return true;
	}

	void Engine::InitCellWorldBoot()
	{
		if (!m_cellWorld)
			return;
		m_cellHeights = std::make_unique<cells::CellHeightFields>(&m_renderFrame);
		m_cellLoader.Start();
		m_terrainCollider.BindHeightFields(nullptr, m_cellHeights.get());
		// Base des collisions : terrain des cellules (m_terrainCollider) + props des
		// interactibles, ajoutés au fil des cellules (AppendInteractableProps).
		m_worldCollider.SetTerrain(&m_terrainCollider);
		m_worldCollider.ClearCylinders();
		m_worldCollider.ClearBoxes();
		m_cellCollider.SetBase(&m_worldCollider);
		m_uiModelBinding.SetWorldOrigin(m_renderFrame.OriginX(), m_renderFrame.OriginZ());
		m_questPoiTable.SetOrigin(m_renderFrame.OriginX(), m_renderFrame.OriginZ());

		// Eau : TOUTES les cellules générées dès le boot (peu de données) — la passe
		// d'eau est choisie à la construction du FrameGraph, qui exige un mesh valide.
		m_cellWaterGlobal.clear();
		for (const cells::CellCoord& cell : m_generatedCells.Cells())
		{
			const std::string rel = "instances/zone_" + cells::CellDirName(cell) + "/water.bin";
			const std::vector<uint8_t> bytes = ReadContentBytes(m_cfg, rel);
			if (bytes.empty())
				continue;
			engine::world::water::WaterScene scene;
			engine::world::water::OceanSectionData oceanIgnored;
			std::string err;
			if (!engine::world::water::LoadWaterBin(std::span<const uint8_t>(bytes), scene, oceanIgnored, err))
			{
				LOG_WARN(Render, "[Cells] water.bin invalide ({}) : {}", rel, err);
				continue;
			}
			m_cellWaterGlobal[cells::CellZoneId(cell)] = std::move(scene);
		}
		RebuildCellWater();
		m_terrainCollider.BindWater(m_clientWaterScene.get());
		LOG_INFO(Render, "[Cells] Eau : {} cellule(s) avec eau", m_cellWaterGlobal.size());
	}

	void Engine::EnterCellWorldAt(double globalX, double globalZ)
	{
		if (!m_cellWorld || !m_cellHeights)
			return;
		const cells::CellCoord cell = cells::CellFromPosition(globalX, globalZ);
		if (cell != m_renderFrame.OriginCell())
			RebaseRenderOrigin(cell);
		// Chargement synchrone de tout ce qui entoure le point d'arrivée.
		UpdateCellStreamingAround(globalX, globalZ, /*synchronous*/ true);
	}

	void Engine::UpdateCellStreaming(bool synchronous)
	{
		if (!m_cellWorld || !m_cellHeights)
			return;
		++m_cellFrameCounter;
		const engine::math::Vec3 p = m_characterController.GetPosition();
		double gx = m_renderFrame.ToGlobalX(p.x);
		double gz = m_renderFrame.ToGlobalZ(p.z);
		// Rebase quand le joueur est entré franchement (≥ 50 m) dans une autre cellule
		// générée : évite d'osciller en longeant la frontière.
		const cells::CellCoord here = cells::CellFromPosition(gx, gz);
		if ((m_cellFrameCounter % 600u) == 0u)
			LOG_DEBUG(Core, "[Cells] joueur rendu=({:.1f},{:.1f}) global=({:.1f},{:.1f}) cellule={} origine={} tuiles={}",
				p.x, p.z, gx, gz, cells::LoreCellId(here), cells::LoreCellId(m_renderFrame.OriginCell()), m_cellTiles.size());
		if (here != m_renderFrame.OriginCell() && m_generatedCells.Contains(here)
			&& cells::DistanceToCellBorder(gx, gz) >= 50.0)
		{
			RebaseRenderOrigin(here);
		}
		UpdateCellStreamingAround(gx, gz, synchronous);
		DestroyRetiredCellTiles(false);
	}

	void Engine::UpdateCellStreamingAround(double globalX, double globalZ, bool synchronous)
	{
		const auto isFailed = [this](cells::CellCoord c) { return m_failedCells.count(cells::CellZoneId(c)) != 0; };
		const auto hasTile = [this](cells::CellCoord c) {
			return std::any_of(m_cellTiles.begin(), m_cellTiles.end(),
				[&](const CellRenderTile& t) { return t.cell == c; });
		};
		bool changed = false;

		// 0) Résultats du thread de travail : heightmaps intégrées toutes, au plus UNE
		//    tuile de rendu par frame (envois GPU + pose des instances au main thread).
		if (!synchronous)
		{
			m_cellLoader.Poll(m_cellReady);
			bool renderBudget = true;
			for (size_t i = 0; i < m_cellReady.size();)
			{
				cells::CellAssets& a = m_cellReady[i];
				const double dist = cells::DistanceToCell(a.cell, globalX, globalZ);
				if (!a.heightsOk)
				{
					LOG_WARN(Render, "[Cells] {} : {} (cellule ignoree)", cells::CellDirName(a.cell), a.error);
					m_failedCells.insert(cells::CellZoneId(a.cell));
					m_cellReady.erase(m_cellReady.begin() + static_cast<std::ptrdiff_t>(i));
					continue;
				}
				const bool renderUseful = a.renderOk && !hasTile(a.cell) && dist <= m_cellUnloadRadiusM;
				if (renderUseful && !renderBudget)
				{
					++i; // tuile prête : intégrée à une frame suivante
					continue;
				}
				if (!m_cellHeights->Has(a.cell) && dist <= m_cellHeightRadiusM + 4000.0)
					IntegrateCellHeights(a);
				if (renderUseful && m_cellHeights->Has(a.cell))
				{
					if (LoadCellTile(a))
						changed = true;
					renderBudget = false;
				}
				m_cellReady.erase(m_cellReady.begin() + static_cast<std::ptrdiff_t>(i));
			}
		}
		const auto requested = [this](cells::CellCoord c, bool render) {
			if (m_cellLoader.IsPending(c, render))
				return true;
			return std::any_of(m_cellReady.begin(), m_cellReady.end(),
				[&](const cells::CellAssets& a) { return a.cell == c && (a.wantRender || !render); });
		};

		// 1) Tuiles de rendu : retrait au-delà du rayon de déchargement.
		for (size_t i = 0; i < m_cellTiles.size();)
		{
			if (cells::DistanceToCell(m_cellTiles[i].cell, globalX, globalZ) > m_cellUnloadRadiusM)
			{
				RetireCellTile(i);
				changed = true;
				continue;
			}
			++i;
		}

		// 2) Tuiles proches (la plus proche d'abord) : demandées au thread de travail,
		//    ou chargées tout de suite en mode synchrone (boot, téléportation).
		static thread_local std::vector<cells::CellCoord> wanted;
		cells::SelectCellsInRange(globalX, globalZ, m_cellRenderRadiusM, m_generatedCells, wanted);
		for (const cells::CellCoord& cell : wanted)
		{
			if (hasTile(cell) || isFailed(cell))
				continue;
			if (synchronous)
			{
				cells::CellAssets a;
				(void)cells::LoadCellAssets(MakeCellJob(cell, /*wantRender*/ true), a);
				if (!a.heightsOk)
				{
					LOG_WARN(Render, "[Cells] {} : {} (cellule ignoree)", cells::CellDirName(cell), a.error);
					m_failedCells.insert(cells::CellZoneId(cell));
					continue;
				}
				if (!m_cellHeights->Has(cell))
					IntegrateCellHeights(a);
				if (LoadCellTile(a))
					changed = true;
				continue;
			}
			if (!requested(cell, true) && m_cellLoader.InFlight() < kCellJobsInFlight)
				m_cellLoader.Request(MakeCellJob(cell, /*wantRender*/ true));
		}

		// 3) Heightmaps CPU (collision, pose au sol, PNJ) : grand rayon.
		cells::SelectCellsInRange(globalX, globalZ, m_cellHeightRadiusM, m_generatedCells, wanted);
		for (const cells::CellCoord& cell : wanted)
		{
			if (m_cellHeights->Has(cell) || isFailed(cell))
				continue;
			if (synchronous)
			{
				(void)EnsureCellHeights(cell);
				continue;
			}
			if (!requested(cell, false) && m_cellLoader.InFlight() < kCellJobsInFlight)
				m_cellLoader.Request(MakeCellJob(cell, /*wantRender*/ false));
		}
		// Heightmaps trop lointaines : libérées (hystérésis de 4 km).
		{
			static thread_local std::vector<cells::CellCoord> resident;
			m_cellHeights->ListCells(resident);
			for (const cells::CellCoord& cell : resident)
				if (cells::DistanceToCell(cell, globalX, globalZ) > m_cellHeightRadiusM + 4000.0)
					m_cellHeights->Remove(cell);
		}
		if (changed)
			RefreshCellOffsets();
	}

	cells::CellAssetJob Engine::MakeCellJob(cells::CellCoord cell, bool wantRender) const
	{
		cells::CellAssetJob job;
		job.cell = cell;
		job.wantRender = wantRender;
		job.baseConfig = m_cfg; // copie : le thread de travail ne lit jamais m_cfg
		return job;
	}

	bool Engine::EnsureCellHeights(cells::CellCoord cell)
	{
		if (m_cellHeights->Has(cell))
			return true;
		cells::CellAssets a;
		if (!cells::LoadCellAssets(MakeCellJob(cell, /*wantRender*/ false), a))
		{
			LOG_WARN(Render, "[Cells] {} : {} (cellule ignoree)", cells::CellDirName(cell), a.error);
			m_failedCells.insert(cells::CellZoneId(cell));
			return false;
		}
		IntegrateCellHeights(a);
		return true;
	}

	void Engine::IntegrateCellHeights(cells::CellAssets& a)
	{
		const cells::CellCoord cell = a.cell;
		const std::string dir = cells::CellDirName(cell);
		const engine::core::Config& zcfg = a.zoneOnly;
		m_cellHeights->Put(std::move(a.heights));
		LOG_INFO(Render, "[Cells] {} : heightmap CPU residente", dir);

		const uint32_t id = cells::CellZoneId(cell);
		if (m_cellsWithStaticContent.insert(id).second)
		{
			// Interactibles (PNJ, coffres) et marqueurs de respawn, en coordonnées de rendu.
			const size_t before = m_interactables.size();
			AppendInteractables(zcfg, m_renderFrame.OriginX(), m_renderFrame.OriginZ());
			const std::string markersText = engine::platform::FileSystem::ReadAllTextContent(
				m_cfg, "zones/" + dir + "/respawn_points.txt");
			std::istringstream input(markersText);
			std::string line;
			size_t markers = 0;
			while (std::getline(input, line))
			{
				if (line.empty() || line[0] == '#')
					continue;
				std::istringstream ls(line);
				RespawnMarker marker{};
				std::string typeToken;
				double x = 0.0, y = 0.0, z = 0.0;
				if (!(ls >> marker.zoneId >> typeToken >> x >> y >> z))
					continue;
				if (typeToken == "graveyard") marker.destinationType = 0;
				else if (typeToken == "inn") marker.destinationType = 1;
				else continue;
				marker.x = m_renderFrame.ToRenderX(x);
				marker.z = m_renderFrame.ToRenderZ(z);
				m_respawnMarkers.push_back(marker);
				++markers;
			}
			if (m_interactables.size() != before)
			{
				// Props et collisions des SEULS nouveaux interactibles (les existants et le
				// cache de matériaux sont conservés) ; le coffre animé est chargé une fois
				// (premier coffre rencontré). Les matériaux créés réécrivent le set
				// bindless : on attend que le GPU n'utilise plus la frame précédente.
				if (m_pipeline && m_vkDeviceContext.IsValid())
					vkDeviceWaitIdle(m_vkDeviceContext.GetDevice());
				if (!m_chestLoaded)
					LoadAnimatedChest();
				AppendInteractableProps(before);
			}
			LOG_INFO(Render, "[Cells] {} : {} interactible(s), {} marqueur(s) de respawn",
				dir, m_interactables.size() - before, markers);
		}
	}

	bool Engine::LoadCellTile(cells::CellAssets& assets)
	{
		if (!m_vkDeviceContext.IsValid() || !m_pipeline || !assets.renderOk)
			return false;
		const cells::CellCoord cell = assets.cell;
		const std::string dir = cells::CellDirName(cell);
		const auto t0 = std::chrono::steady_clock::now();

		// Configuration du terrain : config moteur + zone.json de la cellule (préparée
		// par le thread de travail), origine remplacée par le coin min de la cellule
		// dans le repère de RENDU.
		engine::core::Config& tcfg = assets.config;
		// Même origine que la heightmap CPU (collision) : GPU et CPU ne divergent jamais.
		const cells::CellPoint minCorner = TerrainMinCorner(m_cellHeights.get(), cell);
		tcfg.SetValue("terrain.origin_x", minCorner.x - m_renderFrame.OriginX());
		tcfg.SetValue("terrain.origin_z", minCorner.z - m_renderFrame.OriginZ());
		// Les matériaux du décor de la cellule réécrivent le set bindless partagé :
		// la frame précédente ne doit plus être en vol (chargement déjà synchrone).
		vkDeviceWaitIdle(m_vkDeviceContext.GetDevice());

		CellRenderTile tile;
		tile.cell = cell;
		tile.terrain = std::make_unique<engine::render::terrain::TerrainRenderer>();
		auto loadFnTerrain = [this](const char* p) { return LoadTerrainSpirvWords(p); };
		const bool terrainOk = tile.terrain->Init(
			m_vkDeviceContext.GetDevice(),
			m_vkDeviceContext.GetPhysicalDevice(),
			tcfg,
			tcfg.GetString("render.terrain.heightmap", "zones/" + dir + "/terrain_height.r16h"),
			tcfg.GetString("render.terrain.splatmap", ""),
			tcfg.GetString("render.terrain.grass_mask", ""),
			/*holeMask*/ std::string(),
			/*cliffs*/ std::vector<std::string>{},
			VK_FORMAT_R8G8B8A8_SRGB,
			VK_FORMAT_A2B10G10R10_UNORM_PACK32,
			VK_FORMAT_R8G8B8A8_UNORM,
			VK_FORMAT_R16G16_SFLOAT,
			VK_FORMAT_D32_SFLOAT,
			m_vkDeviceContext.GetGraphicsQueue(),
			m_vkDeviceContext.GetGraphicsQueueFamilyIndex(),
			loadFnTerrain,
			std::nullopt,
			&assets.terrain);
		if (!terrainOk)
		{
			LOG_WARN(Render, "[Cells] {} : terrain GPU indisponible", dir);
			tile.terrain.reset();
		}
		else
		{
			tile.terrain->SetMaxDrawDistance(m_cellTerrainDrawDistanceM);
		}

		// Décor instancié + bâtiments, en coordonnées LOCALES à la cellule.
		tile.scatter = std::make_unique<engine::world::scatter::ScatterField>();
		tile.collider = std::make_unique<engine::gameplay::CompositeWorldCollider>(nullptr);
		const cells::CellPoint center = cells::CellCenter(cell);
		InstanceTarget target;
		target.field = tile.scatter.get();
		target.collider = tile.collider.get();
		target.originX = center.x;
		target.originZ = center.z;
		target.ground = [this, center](float lx, float lz) {
			float h = 0.0f;
			(void)m_cellHeights->SampleGlobal(center.x + lx, center.z + lz, h);
			return h;
		};
		const size_t instances = assets.hasScatter
			? PlaceScatterFile(assets.scatter, target, "instances/zone_" + dir + "/scatter.bin") : 0u;
		const int pieces = assets.buildings.empty() ? 0
			: PlaceBuildings(assets.buildings, assets.buildingLibrary, target, "instances/zone_" + dir + "/buildings.bin");
		LOG_INFO(Render, "[Cells] {} chargee en {} ms (main thread) : terrain={}, decor={} instance(s), batiments={} piece(s), collisions={} cyl / {} boites",
			dir, std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count(),
			tile.terrain ? "oui" : "non", instances, pieces,
			tile.collider->CylinderCount(), tile.collider->BoxCount());
		m_cellTiles.push_back(std::move(tile));
		return true;
	}

	void Engine::RetireCellTile(size_t index)
	{
		if (index >= m_cellTiles.size())
			return;
		LOG_INFO(Render, "[Cells] {} dechargee", cells::CellDirName(m_cellTiles[index].cell));
		RetiredCellTile retired;
		retired.tile = std::move(m_cellTiles[index]);
		retired.retiredAtFrame = m_cellFrameCounter;
		m_retiredCellTiles.push_back(std::move(retired));
		m_cellTiles.erase(m_cellTiles.begin() + static_cast<std::ptrdiff_t>(index));
	}

	void Engine::DestroyRetiredCellTiles(bool all)
	{
		if (m_retiredCellTiles.empty())
			return;
		VkDevice device = m_vkDeviceContext.IsValid() ? m_vkDeviceContext.GetDevice() : VK_NULL_HANDLE;
		bool waited = false;
		for (size_t i = 0; i < m_retiredCellTiles.size();)
		{
			RetiredCellTile& r = m_retiredCellTiles[i];
			if (!all && m_cellFrameCounter < r.retiredAtFrame + kRetiredTileDelayFrames)
			{
				++i;
				continue;
			}
			// Le compteur suit les mises à jour, pas les frames présentées : on attend
			// explicitement le GPU (rare : un déchargement de cellule) avant de détruire.
			if (!waited && device != VK_NULL_HANDLE)
			{
				vkDeviceWaitIdle(device);
				waited = true;
			}
			if (r.tile.terrain && device != VK_NULL_HANDLE)
				r.tile.terrain->Destroy(device);
			m_retiredCellTiles.erase(m_retiredCellTiles.begin() + static_cast<std::ptrdiff_t>(i));
		}
	}

	void Engine::RefreshCellOffsets()
	{
		std::vector<engine::gameplay::CellWorldCollider::CellEntry> entries;
		entries.reserve(m_cellTiles.size());
		for (CellRenderTile& tile : m_cellTiles)
		{
			const cells::CellPoint center = m_renderFrame.CellCenterRender(tile.cell);
			if (tile.terrain)
			{
				const cells::CellPoint minCorner = TerrainMinCorner(m_cellHeights.get(), tile.cell);
				tile.terrain->SetTerrainOrigin(
					static_cast<float>(minCorner.x - m_renderFrame.OriginX()),
					static_cast<float>(minCorner.z - m_renderFrame.OriginZ()));
			}
			entries.push_back({ tile.collider.get(), static_cast<float>(center.x), static_cast<float>(center.z) });
		}
		m_cellCollider.SetCells(std::move(entries));
	}

	void Engine::RebaseRenderOrigin(cells::CellCoord newOrigin)
	{
		const cells::CellPoint d = m_renderFrame.RebaseDelta(newOrigin);
		const float dx = static_cast<float>(d.x);
		const float dz = static_cast<float>(d.z);
		LOG_INFO(Core, "[Cells] Origine de rendu : {} -> {} (decalage {:.0f}, {:.0f} m)",
			cells::LoreCellId(m_renderFrame.OriginCell()), cells::LoreCellId(newOrigin), dx, dz);
		m_renderFrame.SetOriginCell(newOrigin);
		m_uiModelBinding.SetWorldOrigin(m_renderFrame.OriginX(), m_renderFrame.OriginZ());
		m_questPoiTable.SetOrigin(m_renderFrame.OriginX(), m_renderFrame.OriginZ());

		// Joueur, caméra, dernière position synchronisée.
		m_characterController.Translate(dx, dz);
		{
			engine::math::Vec3 target = m_orbitalCameraController.GetTargetPosition();
			target.x += dx;
			target.z += dz;
			m_orbitalCameraController.SetTargetPosition(target);
		}
		m_lastSyncedPosition.x += dx;
		m_lastSyncedPosition.z += dz;

		// Entités distantes lissées (les positions du modèle UI sont décalées par SetWorldOrigin).
		for (auto& [id, smoothed] : m_remoteSmoothed)
		{
			(void)id;
			smoothed.x += dx;
			smoothed.z += dz;
		}

		// Interactibles, leurs props (matrice décalée, pas de rechargement) et
		// collisions, coffre animé.
		for (InteractableEntity& e : m_interactables)
		{
			e.position.x += dx;
			e.position.z += dz;
		}
		m_chestPos.x += dx;
		m_chestPos.z += dz;
		TranslateStaticProps(dx, dz);
		for (RespawnMarker& m : m_respawnMarkers)
		{
			m.x += dx;
			m.z += dz;
		}

		// Eau (scène reconstruite depuis les positions globales), terrains et collisions.
		// Le buffer d'eau est réécrit : la frame précédente ne doit plus le lire
		// (rebase rare : un changement de cellule).
		if (m_vkDeviceContext.IsValid())
			vkDeviceWaitIdle(m_vkDeviceContext.GetDevice());
		RebuildCellWater();
		RefreshCellOffsets();
		// Les vecteurs de mouvement de cette frame compareraient deux repères
		// différents (~10 km) : historique TAA invalidé.
		m_taaHistoryInvalid = true;
	}

	void Engine::AppendInteractableProps(size_t firstIndex)
	{
		if (!m_pipeline)
			return;
		const size_t propsBefore = m_props.size();
		for (size_t ii = firstIndex; ii < m_interactables.size(); ++ii)
		{
			const InteractableEntity& e = m_interactables[ii];
			if (e.meshPath.empty())
				continue;
			// Coffre animé (pipeline skinné) : seulement son cylindre de collision.
			if (m_chestLoaded && static_cast<int>(ii) == m_chestInteractableIndex)
			{
				const float gy = m_terrainCollider.GroundHeightAt(e.position.x, e.position.z);
				m_worldCollider.AddCylinder(engine::gameplay::PropCylinder{
					e.position.x, e.position.z, 0.6f, gy, gy + 1.0f });
				continue;
			}
			BuildPropFromMesh(e.meshPath, e.position.x, e.position.z, e.meshYawDeg,
				e.meshRotXDeg, e.meshScale, static_cast<int>(ii),
				/*solid*/ true, /*collisionRadius*/ 0.0f);
		}
		LOG_INFO(Render, "[Cells] {} prop(s) interactif(s) ajoute(s) ({} au total)",
			m_props.size() - propsBefore, m_props.size());
	}

	void Engine::TranslateStaticProps(float dx, float dz)
	{
		for (PropRenderable& prop : m_props)
		{
			// Translation pré-multipliée (matrice affine, column-major : colonne 3).
			prop.modelMatrix.m[12] += dx;
			prop.modelMatrix.m[14] += dz;
			prop.worldPos.x += dx;
			prop.worldPos.z += dz;
			prop.impostorCenter.x += dx;
			prop.impostorCenter.z += dz;
			prop.occluderCenter.x += dx;
			prop.occluderCenter.z += dz;
		}
		// Cylindres et boîtes des props (le terrain du collisionneur suit le repère seul).
		m_worldCollider.Translate(dx, dz);
	}

	void Engine::RebuildCellWater()
	{
		if (!m_clientWaterScene)
			m_clientWaterScene = std::make_shared<engine::world::water::WaterScene>();
		engine::world::water::WaterScene& scene = *m_clientWaterScene;
		scene.lakes.clear();
		scene.rivers.clear();
		const double ox = m_renderFrame.OriginX();
		const double oz = m_renderFrame.OriginZ();
		auto toRender = [ox, oz](engine::math::Vec3 p) {
			p.x = static_cast<float>(static_cast<double>(p.x) - ox);
			p.z = static_cast<float>(static_cast<double>(p.z) - oz);
			return p;
		};
		for (const auto& [id, global] : m_cellWaterGlobal)
		{
			(void)id;
			for (engine::world::water::LakeInstance lake : global.lakes)
			{
				for (engine::math::Vec3& v : lake.polygon)
					v = toRender(v);
				scene.lakes.push_back(std::move(lake));
			}
			for (engine::world::water::RiverInstance river : global.rivers)
			{
				for (engine::world::water::RiverNode& n : river.nodes)
					n.position = toRender(n.position);
				scene.rivers.push_back(std::move(river));
			}
		}
		m_waterClientSceneDirty = true;
	}

	bool Engine::RecordCellTerrains(VkCommandBuffer cmd, engine::render::Registry& reg, const engine::RenderState& rs)
	{
		bool first = true;
		for (CellRenderTile& tile : m_cellTiles)
		{
			if (!tile.terrain || !tile.terrain->IsValid())
				continue;
			// Le premier terrain efface le GBuffer, les suivants dessinent par-dessus.
			tile.terrain->SetClearAttachments(first);
			tile.terrain->Record(
				m_vkDeviceContext.GetDevice(), cmd, reg,
				m_vkSwapchain.GetExtent(),
				m_fgGBufferAId, m_fgGBufferBId, m_fgGBufferCId,
				m_fgGBufferVelocityId, m_fgDepthId,
				rs.prevViewProjMatrix.m, rs.viewProjMatrix.m,
				rs.camera.position, rs.frustum);
			first = false;
		}
		return !first;
	}

	bool Engine::HasCellTerrainToDraw() const
	{
		return std::any_of(m_cellTiles.begin(), m_cellTiles.end(),
			[](const CellRenderTile& t) { return t.terrain && t.terrain->IsValid(); });
	}

	float Engine::SampleGroundHeight(float x, float z) const
	{
		if (m_cellWorld)
			return m_terrainCollider.GroundHeightAt(x, z);
		return m_terrain.SampleHeightAtWorldXZ(x, z);
	}

	void Engine::ShutdownCellWorld()
	{
		if (!m_cellWorld)
			return;
		// Thread de travail arrêté d'abord (aucun résultat ne doit arriver après).
		m_cellLoader.Stop();
		m_cellReady.clear();
		VkDevice device = m_vkDeviceContext.IsValid() ? m_vkDeviceContext.GetDevice() : VK_NULL_HANDLE;
		for (CellRenderTile& tile : m_cellTiles)
			if (tile.terrain && device != VK_NULL_HANDLE)
				tile.terrain->Destroy(device);
		m_cellTiles.clear();
		DestroyRetiredCellTiles(true);
		m_cellCollider.SetCells({});
	}

	void Engine::InvalidateCellTerrainFramebuffers()
	{
		if (!m_vkDeviceContext.IsValid())
			return;
		for (CellRenderTile& tile : m_cellTiles)
			if (tile.terrain && tile.terrain->IsValid())
				tile.terrain->InvalidateFramebufferCache(m_vkDeviceContext.GetDevice());
		for (RetiredCellTile& r : m_retiredCellTiles)
			if (r.tile.terrain && r.tile.terrain->IsValid())
				r.tile.terrain->InvalidateFramebufferCache(m_vkDeviceContext.GetDevice());
	}

	void Engine::AppendInteractables(const engine::core::Config& cfg, double originX, double originZ)
	{
		const int icount = static_cast<int>(cfg.GetInt("world.interactables.count", 0));
		for (int ii = 0; ii < icount; ++ii)
		{
			const std::string base = "world.interactables." + std::to_string(ii) + ".";
			InteractableEntity e;
			e.position = engine::math::Vec3{
				static_cast<float>(cfg.GetDouble(base + "x", 0.0) - originX), 0.0f,
				static_cast<float>(cfg.GetDouble(base + "z", 0.0) - originZ) };
			e.radius = static_cast<float>(cfg.GetDouble(base + "radius", 2.5));
			e.isNpc = cfg.GetBool(base + "npc", false);
			e.label = cfg.GetString(base + "label", "?");
			e.message = cfg.GetString(base + "message", "");
			e.meshPath = cfg.GetString(base + "mesh", "");
			e.meshScale = static_cast<float>(cfg.GetDouble(base + "scale", 1.0));
			e.meshYawDeg = static_cast<float>(cfg.GetDouble(base + "yaw_deg", 0.0));
			e.meshRotXDeg = static_cast<float>(cfg.GetDouble(base + "rot_x_deg", 0.0));
			const int dcount = static_cast<int>(cfg.GetInt(base + "dialogue.count", 0));
			for (int dj = 0; dj < dcount; ++dj)
				e.dialogue.push_back(cfg.GetString(base + "dialogue." + std::to_string(dj), ""));
			// Cellule de dialogue PNJ : sous-titre (role) + arbre moderne
			// (fallback legacy depuis e.dialogue si pas de dialogue_tree).
			e.role = cfg.GetString(base + "role", "");
			e.dialogueTree = engine::client::LoadDialogueTree(cfg, base, e.dialogue);
			// SP2 — id réseau du PNJ (ex. "npc:elder_marn"), utilisé pour envoyer un
			// Talk à l'ouverture du dialogue (\see OpenDialogue) puis comme cible
			// des QuestAccept/TurnInRequest. Vide = pas de PNJ quête (aucun Talk envoyé).
			e.npcTargetId = cfg.GetString(base + "npc_target_id", "");
			m_interactables.push_back(e);
		}
	}
}
