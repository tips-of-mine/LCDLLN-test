#include "src/client/world/cells/CellAssetLoader.h"

#include "src/client/render/terrain/HeightmapLoader.h"
#include "src/client/render/terrain/TerrainGrassDetail.h"
#include "src/shared/core/Log.h"
#include "src/shared/platform/FileSystem.h"

#include <algorithm>
#include <span>
#include <utility>

namespace engine::world::cells
{
	bool LoadCellAssets(const CellAssetJob& job, CellAssets& out)
	{
		using engine::render::terrain::HeightmapLoader;
		out = CellAssets{};
		out.cell = job.cell;
		out.wantRender = job.wantRender;

		const std::string dir = CellDirName(job.cell);
		const std::string content = job.baseConfig.GetString("paths.content", "game/data");

		// zone.json SEUL (contenu propre à la cellule : PNJ, réglages) et fusionné
		// à la config de base (réglages du terrain, comme TerrainRenderer::Init).
		engine::core::Config zone;
		if (!zone.LoadFromFile(content + "/zones/" + dir + "/zone.json"))
		{
			out.error = "zone.json absent ou invalide";
			return false;
		}
		out.config = job.baseConfig;
		(void)out.config.LoadFromFile(content + "/zones/" + dir + "/zone.json");

		// Heightmap CPU (mêmes clés et défauts que l'ancien chargement synchrone).
		out.heights.cell = job.cell;
		const std::string hm = zone.GetString("render.terrain.heightmap", "zones/" + dir + "/terrain_height.r16h");
		if (!HeightmapLoader::LoadFromFile(content + "/" + hm, out.heights.data))
		{
			out.error = "heightmap '" + hm + "' illisible";
			return false;
		}
		const CellPoint minCorner = CellMinCorner(job.cell);
		out.heights.minX = zone.GetDouble("terrain.origin_x", minCorner.x);
		out.heights.minZ = zone.GetDouble("terrain.origin_z", minCorner.z);
		out.heights.worldSize = zone.GetDouble("terrain.world_size", kCellSizeMeters);
		out.heights.heightScale = zone.GetDouble("terrain.height_scale", 1200.0);
		out.heightsOk = true;
		// La config fusionnée sert aussi aux interactibles : on garde la zone seule
		// pour ceux-là (la config de base n'en contient pas, cf. LoadCellWorldSettings).
		out.zoneOnly = std::move(zone);

		if (!job.wantRender)
			return true;

		// ── Rendu : terrain ──────────────────────────────────────────────────────
		// Échelle et pas identiques à ceux que TerrainRenderer::Init calculera
		// (config fusionnée, mêmes défauts) : sinon la normal map est recalculée.
		const auto& hd = out.heights.data;
		const float heightScale = static_cast<float>(out.config.GetDouble("terrain.height_scale", 200.0));
		const float worldSize = static_cast<float>(out.config.GetDouble("terrain.world_size", 1024.0));
		if (hd.width >= 3u && hd.height >= 3u && worldSize > 0.0f)
		{
			const float vertStep = worldSize / static_cast<float>(hd.width - 1u);
			// Raccord des normales : l'échantillon de la voisine juste au-delà de chaque
			// bord (le premier/dernier échantillon est partagé, on prend le suivant).
			// Voisine absente ou de taille différente = bord répété (comme avant).
			engine::render::terrain::HeightmapBorders borders;
			const auto neighbourPath = [&](int dn, int de) {
				const CellCoord c{ job.cell.n + dn, job.cell.e + de };
				const std::string d = CellDirName(c);
				return content + "/zones/" + d + "/terrain_height.r16h";
			};
			uint32_t nw = 0, nh = 0;
			if (!HeightmapLoader::ReadColumn(neighbourPath(0, -1), hd.width - 2u, borders.west, nw, nh)
				|| nw != hd.width || nh != hd.height) borders.west.clear();
			if (!HeightmapLoader::ReadColumn(neighbourPath(0, +1), 1u, borders.east, nw, nh)
				|| nw != hd.width || nh != hd.height) borders.east.clear();
			// Nord = Z min = ligne 0 ; la voisine nord (n + 1) est au-dessus : sa ligne h − 2.
			if (!HeightmapLoader::ReadRow(neighbourPath(+1, 0), hd.height - 2u, borders.north, nw, nh)
				|| nw != hd.width || nh != hd.height) borders.north.clear();
			if (!HeightmapLoader::ReadRow(neighbourPath(-1, 0), 1u, borders.south, nw, nh)
				|| nw != hd.width || nh != hd.height) borders.south.clear();
			if (HeightmapLoader::ComputeNormalMap(hd, heightScale, vertStep, out.terrain.normalPixels, &borders))
			{
				out.terrain.normalHeightScale = heightScale;
				out.terrain.normalVertStep = vertStep;
			}
		}
		out.terrain.heightmap = hd; // copie : le CPU garde la sienne (collision)
		const std::string splat = out.config.GetString("render.terrain.splatmap", "");
		if (!splat.empty())
			out.terrain.splatFile = engine::platform::FileSystem::ReadAllBytesContent(out.config, splat);
		const std::string grass = out.config.GetString("render.terrain.grass_mask", "");
		if (!grass.empty()
			&& !engine::render::terrain::TerrainGrassDetail::LoadFromFile(content + "/" + grass, out.terrain.grassMask))
			out.terrain.grassMask = {};

		// ── Rendu : décor instancié ──────────────────────────────────────────────
		{
			const std::vector<uint8_t> bytes = engine::platform::FileSystem::ReadAllBytesContent(
				out.config, "instances/zone_" + dir + "/scatter.bin");
			std::string err;
			if (!bytes.empty())
			{
				if (engine::world::instances::LoadScatterBin(bytes, out.scatter, err))
					out.hasScatter = true;
				else
					LOG_WARN(Render, "[Cells] {} : scatter.bin invalide ({})", dir, err);
			}
		}

		// ── Rendu : bâtiments + bibliothèque des types ───────────────────────────
		{
			const std::vector<uint8_t> bytes = engine::platform::FileSystem::ReadAllBytes(
				content + "/instances/zone_" + dir + "/buildings.bin");
			std::string err;
			if (!bytes.empty())
			{
				if (!engine::world::instances::LoadBuildingsBin(std::span<const uint8_t>(bytes), out.buildings, err))
				{
					LOG_WARN(Render, "[Cells] {} : buildings.bin invalide ({})", dir, err);
					out.buildings.clear();
				}
			}
			if (!out.buildings.empty())
			{
				std::string libErr;
				if (!out.buildingLibrary.LoadFromContent(content, libErr) && !libErr.empty())
					LOG_WARN(Render, "[Cells] {} : bibliotheque de batiments partielle ({})", dir, libErr);
			}
		}
		out.renderOk = true;
		return true;
	}

	CellAssetLoader::~CellAssetLoader()
	{
		Stop();
	}

	void CellAssetLoader::Start()
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		if (m_thread.joinable())
			return;
		m_stop = false;
		m_thread = std::thread([this] { WorkerLoop(); });
	}

	void CellAssetLoader::Stop()
	{
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			m_stop = true;
			m_queue.clear();
		}
		m_cv.notify_all();
		if (m_thread.joinable())
			m_thread.join();
		std::lock_guard<std::mutex> lock(m_mutex);
		m_done.clear();
		m_busy = false;
	}

	void CellAssetLoader::Request(CellAssetJob job)
	{
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			if (m_stop)
				return;
			for (CellAssetJob& q : m_queue)
			{
				if (q.cell == job.cell)
				{
					q.wantRender = q.wantRender || job.wantRender;
					return;
				}
			}
			if (m_busy && m_busyCell == job.cell && (m_busyRender || !job.wantRender))
				return;
			for (const CellAssets& d : m_done)
				if (d.cell == job.cell && (d.wantRender || !job.wantRender))
					return;
			m_queue.push_back(std::move(job));
		}
		m_cv.notify_one();
	}

	bool CellAssetLoader::IsPending(CellCoord cell, bool wantRender) const
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		for (const CellAssetJob& q : m_queue)
			if (q.cell == cell && (q.wantRender || !wantRender))
				return true;
		if (m_busy && m_busyCell == cell && (m_busyRender || !wantRender))
			return true;
		for (const CellAssets& d : m_done)
			if (d.cell == cell && (d.wantRender || !wantRender))
				return true;
		return false;
	}

	size_t CellAssetLoader::InFlight() const
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		return m_queue.size() + (m_busy ? 1u : 0u) + m_done.size();
	}

	void CellAssetLoader::Poll(std::vector<CellAssets>& out)
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		for (CellAssets& d : m_done)
			out.push_back(std::move(d));
		m_done.clear();
	}

	void CellAssetLoader::WorkerLoop()
	{
		for (;;)
		{
			CellAssetJob job;
			{
				std::unique_lock<std::mutex> lock(m_mutex);
				m_cv.wait(lock, [this] { return m_stop || !m_queue.empty(); });
				if (m_stop)
					return;
				job = std::move(m_queue.front());
				m_queue.pop_front();
				m_busy = true;
				m_busyCell = job.cell;
				m_busyRender = job.wantRender;
			}
			CellAssets result;
			(void)LoadCellAssets(job, result);
			{
				std::lock_guard<std::mutex> lock(m_mutex);
				m_busy = false;
				if (!m_stop)
					m_done.push_back(std::move(result));
			}
		}
	}
}
