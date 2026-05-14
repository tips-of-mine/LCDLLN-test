#include "src/world_editor/water/WatershedSimulation.h"

#include "src/client/world/terrain/TerrainChunk.h"
#include "src/world_editor/terrain/TerrainDocument.h"
#include "src/world_editor/water/D8FlowDirection.h"
#include "src/world_editor/water/FlowAccumulation.h"
#include "src/world_editor/water/PathSimplifyDouglasPeucker.h"
#include "src/world_editor/water/WaterDocument.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <queue>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace engine::editor::world
{
	namespace
	{
		constexpr float kChunkSpanMeters =
			(engine::world::terrain::kTerrainResolution - 1u) *
			 engine::world::terrain::kTerrainCellSizeMeters;
		constexpr int kRes =
			static_cast<int>(engine::world::terrain::kTerrainResolution);

		/// Convertit une cellule (gridX, gridZ) en index linéaire grid.
		inline size_t CellIndex(int gridX, int gridZ, int width)
		{
			return static_cast<size_t>(gridZ) * width + gridX;
		}

		/// Convertit une cellule grid en coord monde XZ (mètres). La cellule
		/// `(0, 0)` est à `(originCellX * cellSize, originCellZ * cellSize)`.
		inline void CellToWorld(const ConsolidatedHeightGrid& g,
			int gx, int gz, float& outX, float& outZ)
		{
			outX = static_cast<float>(g.originCellX + gx) * g.cellSizeMeters;
			outZ = static_cast<float>(g.originCellZ + gz) * g.cellSizeMeters;
		}

		/// Convertit une coord monde XZ en cellule grid la plus proche.
		/// Retourne false si hors grid.
		bool WorldToCell(const ConsolidatedHeightGrid& g,
			float worldX, float worldZ, int& outX, int& outZ)
		{
			const float fx = worldX / g.cellSizeMeters - static_cast<float>(g.originCellX);
			const float fz = worldZ / g.cellSizeMeters - static_cast<float>(g.originCellZ);
			outX = static_cast<int>(std::round(fx));
			outZ = static_cast<int>(std::round(fz));
			return (outX >= 0 && outX < g.width && outZ >= 0 && outZ < g.height);
		}

		/// Pour un chemin de cellules, calcule la flow accumulation max.
		uint32_t MaxFlowAccOnPath(const std::vector<std::pair<int,int>>& cells,
			const std::vector<uint32_t>& flowAcc, int width)
		{
			uint32_t m = 0u;
			for (const auto& [x, z] : cells)
			{
				m = std::max(m, flowAcc[CellIndex(x, z, width)]);
			}
			return m;
		}

		/// Convertit un chemin de cellules en polyline 3D (XZ depuis grid,
		/// Y depuis heightmap), puis simplifie par Douglas-Peucker.
		std::vector<engine::math::Vec3> CellPathToSplinePoints(
			const std::vector<std::pair<int,int>>& cells,
			const ConsolidatedHeightGrid& grid, float toleranceMeters)
		{
			std::vector<engine::math::Vec3> raw;
			raw.reserve(cells.size());
			for (const auto& [x, z] : cells)
			{
				float wx, wz;
				CellToWorld(grid, x, z, wx, wz);
				engine::math::Vec3 v;
				v.x = wx;
				v.y = grid.Get(x, z);
				v.z = wz;
				raw.push_back(v);
			}
			return SimplifyPolylineDouglasPeucker(raw, toleranceMeters);
		}

		/// Profil radial gaussien pour le carving : amplitude max sur l'axe,
		/// décroît à 0 à `widthMeters`. Renvoie un facteur dans [0, 1].
		float GaussianFalloff(float distMeters, float widthMeters)
		{
			if (widthMeters <= 0.0f) return 0.0f;
			const float u = distMeters / widthMeters;
			return std::exp(-2.0f * u * u);
		}

		/// Distance perpendiculaire d'un point (px, pz) au segment
		/// (ax, az)-(bx, bz), uniquement composante XZ.
		float DistToSegmentXZ(float px, float pz,
			float ax, float az, float bx, float bz)
		{
			const float dx = bx - ax;
			const float dz = bz - az;
			const float len2 = dx * dx + dz * dz;
			if (len2 < 1e-12f)
			{
				const float ex = px - ax;
				const float ez = pz - az;
				return std::sqrt(ex * ex + ez * ez);
			}
			const float ex = px - ax;
			const float ez = pz - az;
			const float t = std::clamp((ex * dx + ez * dz) / len2, 0.0f, 1.0f);
			const float qx = ax + t * dx;
			const float qz = az + t * dz;
			const float dx2 = px - qx;
			const float dz2 = pz - qz;
			return std::sqrt(dx2 * dx2 + dz2 * dz2);
		}

		/// Pour chaque cellule du grid dans un BB autour d'une spline,
		/// calcule la distance minimale à la spline et écrit un delta négatif
		/// (creusement) selon profil gaussien. Ajoute aux deltas existants
		/// dans `out`.
		void EmitCarveDeltas(const std::vector<engine::math::Vec3>& spline,
			const ConsolidatedHeightGrid& grid,
			float depthMeters, float widthMeters,
			SparseChunkDeltas& out)
		{
			if (spline.size() < 2u || depthMeters <= 0.0f || widthMeters <= 0.0f)
				return;

			// Bounding box de la spline + padding.
			float minX = +1e30f, maxX = -1e30f, minZ = +1e30f, maxZ = -1e30f;
			for (const auto& p : spline)
			{
				minX = std::min(minX, p.x); maxX = std::max(maxX, p.x);
				minZ = std::min(minZ, p.z); maxZ = std::max(maxZ, p.z);
			}
			minX -= widthMeters; maxX += widthMeters;
			minZ -= widthMeters; maxZ += widthMeters;

			// Itération sur les chunks dans le BB.
			const int chunkXMin = static_cast<int>(std::floor(minX / kChunkSpanMeters));
			const int chunkXMax = static_cast<int>(std::floor(maxX / kChunkSpanMeters));
			const int chunkZMin = static_cast<int>(std::floor(minZ / kChunkSpanMeters));
			const int chunkZMax = static_cast<int>(std::floor(maxZ / kChunkSpanMeters));

			for (int cz = chunkZMin; cz <= chunkZMax; ++cz)
			{
				for (int cx = chunkXMin; cx <= chunkXMax; ++cx)
				{
					const float chunkOX = static_cast<float>(cx) * kChunkSpanMeters;
					const float chunkOZ = static_cast<float>(cz) * kChunkSpanMeters;

					for (int iz = 0; iz < kRes; ++iz)
					{
						const float wz = chunkOZ + static_cast<float>(iz);
						if (wz < minZ || wz > maxZ) continue;
						for (int ix = 0; ix < kRes; ++ix)
						{
							const float wx = chunkOX + static_cast<float>(ix);
							if (wx < minX || wx > maxX) continue;

							// Distance min à la spline.
							float bestDist = 1e30f;
							for (size_t i = 0; i + 1u < spline.size(); ++i)
							{
								const float d = DistToSegmentXZ(wx, wz,
									spline[i].x, spline[i].z,
									spline[i + 1u].x, spline[i + 1u].z);
								if (d < bestDist) bestDist = d;
							}
							if (bestDist > widthMeters) continue;

							const float w = GaussianFalloff(bestDist, widthMeters);
							const float delta = -depthMeters * w;
							if (delta == 0.0f) continue;

							const engine::world::GlobalChunkCoord coord{ cx, cz };
							const uint32_t cellIdx = static_cast<uint32_t>(iz * kRes + ix);
							out[coord][cellIdx] += delta;

							// Évite warning sur `grid` non utilisé pour le carving
							// (la grille a servi à la sim ; les deltas sont calculés
							// dans le système chunk natif).
							(void)grid;
						}
					}
				}
			}
		}

		/// Génère un lac auto autour d'une cellule sink. Flood-fill 8-conn
		/// pour trouver le bassin (cellules connectées avec altitude <=
		/// sinkAlt + maxDepth). Le polygone est ensuite construit comme
		/// l'enveloppe convexe XZ des cellules du bassin (approximation
		/// englobante simple, suffisante pour M100.36 MVP).
		engine::world::water::LakeInstance BuildAutoLakeAtSink(
			int sinkX, int sinkZ, const ConsolidatedHeightGrid& grid,
			float maxDepthMeters)
		{
			engine::world::water::LakeInstance lake;
			lake.name = "auto_lake_" +
				std::to_string(grid.originCellX + sinkX) + "_" +
				std::to_string(grid.originCellZ + sinkZ);

			const float sinkAlt = grid.Get(sinkX, sinkZ);
			const float maxAlt  = sinkAlt + maxDepthMeters;
			const int W = grid.width;
			const int H = grid.height;

			std::vector<uint8_t> inBasin(static_cast<size_t>(W) * H, 0u);
			std::queue<std::pair<int,int>> q;
			q.emplace(sinkX, sinkZ);
			inBasin[CellIndex(sinkX, sinkZ, W)] = 1u;
			std::vector<std::pair<int,int>> basinCells;
			float overflowAlt = maxAlt;
			while (!q.empty())
			{
				const auto [x, z] = q.front();
				q.pop();
				basinCells.emplace_back(x, z);
				for (int dz = -1; dz <= 1; ++dz)
				{
					for (int dx = -1; dx <= 1; ++dx)
					{
						if (dx == 0 && dz == 0) continue;
						const int nx = x + dx;
						const int nz = z + dz;
						if (nx < 0 || nx >= W || nz < 0 || nz >= H)
						{
							// Frontière du grid : enregistre la hauteur de la
							// cellule courante comme candidat overflow.
							overflowAlt = std::min(overflowAlt, grid.Get(x, z));
							continue;
						}
						if (inBasin[CellIndex(nx, nz, W)]) continue;
						const float h = grid.Get(nx, nz);
						if (h <= maxAlt)
						{
							inBasin[CellIndex(nx, nz, W)] = 1u;
							q.emplace(nx, nz);
						}
						else
						{
							// Cellule trop haute : candidat débordement.
							overflowAlt = std::min(overflowAlt, h);
						}
					}
				}
			}

			const float lakeY = std::min(maxAlt, overflowAlt);
			lake.waterLevelY = lakeY;
			lake.bottomColor = engine::math::Vec3{ 0.05f, 0.20f, 0.30f };
			lake.turbidity = 0.4f;

			if (basinCells.empty())
			{
				return lake;
			}

			// Enveloppe convexe (Graham scan) sur les cellules du bassin
			// projetées en monde XZ. Pour des bassins petits, c'est rapide.
			std::vector<engine::math::Vec3> pts;
			pts.reserve(basinCells.size());
			for (const auto& [x, z] : basinCells)
			{
				float wx, wz;
				CellToWorld(grid, x, z, wx, wz);
				engine::math::Vec3 v;
				v.x = wx;
				v.y = lakeY;
				v.z = wz;
				pts.push_back(v);
			}
			// Convex hull 2D (XZ) — algorithm Andrew monotone chain.
			std::sort(pts.begin(), pts.end(),
				[](const engine::math::Vec3& a, const engine::math::Vec3& b) {
					if (a.x != b.x) return a.x < b.x;
					return a.z < b.z;
				});
			auto cross = [](const engine::math::Vec3& O,
				const engine::math::Vec3& A, const engine::math::Vec3& B) {
				return (A.x - O.x) * (B.z - O.z) - (A.z - O.z) * (B.x - O.x);
			};
			std::vector<engine::math::Vec3> hull;
			hull.reserve(2u * pts.size());
			// Lower hull
			for (const auto& p : pts)
			{
				while (hull.size() >= 2u &&
					cross(hull[hull.size() - 2u], hull.back(), p) <= 0.0f)
				{
					hull.pop_back();
				}
				hull.push_back(p);
			}
			const size_t lowerSize = hull.size() + 1u;
			// Upper hull
			for (auto it = pts.rbegin(); it != pts.rend(); ++it)
			{
				const auto& p = *it;
				while (hull.size() >= lowerSize &&
					cross(hull[hull.size() - 2u], hull.back(), p) <= 0.0f)
				{
					hull.pop_back();
				}
				hull.push_back(p);
			}
			if (!hull.empty()) hull.pop_back();   // dernier == premier
			lake.polygon = std::move(hull);
			return lake;
		}
	}

	RiverNetworkResult RunWatershedOnGrid(
		const ConsolidatedHeightGrid& grid,
		float seaLevelMeters,
		const WatershedSimulationParams& params)
	{
		RiverNetworkResult result;
		if (grid.width < 2 || grid.height < 2) return result;

		const int W = grid.width;
		const int H = grid.height;

		const std::vector<uint8_t>  flowDirs = ComputeD8FlowDirection(grid);
		const std::vector<uint32_t> flowAcc  = ComputeFlowAccumulation(grid, flowDirs);

		// Set des cellules déjà visitées pour détecter les confluences
		// (deux sources qui convergent sur la même cellule en aval).
		std::vector<uint8_t> visited(static_cast<size_t>(W) * H, 0u);

		struct PathTracing
		{
			std::vector<std::pair<int,int>> cells;
			enum class End { Boundary, SeaLevel, Sink, Confluence } end = End::Boundary;
			int sinkX = -1, sinkZ = -1;   // valide si end == Sink
		};
		std::vector<PathTracing> paths;
		paths.reserve(params.springs.size());

		for (const auto& src : params.springs)
		{
			int x = 0, z = 0;
			if (!WorldToCell(grid, src.worldX, src.worldZ, x, z))
			{
				// Source hors grille : ignorée silencieusement.
				continue;
			}

			PathTracing path;
			while (true)
			{
				path.cells.emplace_back(x, z);
				const size_t idx = CellIndex(x, z, W);
				if (visited[idx] && path.cells.size() > 1u)
				{
					// On vient de toucher un chemin déjà tracé.
					path.end = PathTracing::End::Confluence;
					result.confluenceCount++;
					break;
				}
				visited[idx] = 1u;

				const float h = grid.Get(x, z);
				if (h <= seaLevelMeters)
				{
					path.end = PathTracing::End::SeaLevel;
					result.mouthCount++;
					break;
				}

				const uint8_t dir = flowDirs[idx];
				if (dir >= 8u)
				{
					path.end  = PathTracing::End::Sink;
					path.sinkX = x;
					path.sinkZ = z;
					result.sinkEndCount++;
					break;
				}
				const int nx = x + kD8Order[dir].dx;
				const int nz = z + kD8Order[dir].dz;
				if (nx < 0 || nx >= W || nz < 0 || nz >= H)
				{
					path.end = PathTracing::End::Boundary;
					result.boundaryEndCount++;
					break;
				}
				x = nx;
				z = nz;
			}
			if (path.cells.size() >= 2u) paths.push_back(std::move(path));
		}

		// Filtrage par seuil + simplification + construction RiverInstance.
		int riverIndex = 0;
		for (const auto& path : paths)
		{
			const uint32_t maxAcc = MaxFlowAccOnPath(path.cells, flowAcc, W);
			if (maxAcc < params.minFlowThresholdCells)
			{
				result.rejectedByThreshold++;
				continue;
			}
			const auto spline = CellPathToSplinePoints(path.cells, grid,
				params.simplificationToleranceMeters);
			if (spline.size() < 2u) continue;

			engine::world::water::RiverInstance river;
			river.name = "river_auto_" + std::to_string(riverIndex++);
			river.nodes.reserve(spline.size());
			for (size_t i = 0; i < spline.size(); ++i)
			{
				engine::world::water::RiverNode n;
				n.position    = spline[i];
				// Largeur indicative en log(flowAcc) du chemin. Cap min 2 m.
				const float widthBase =
					2.0f + 0.5f * std::log(static_cast<float>(std::max(1u, maxAcc)));
				n.widthMeters = std::clamp(widthBase, 2.0f, 24.0f);
				n.depthMeters = 1.0f;
				river.nodes.push_back(n);
			}
			result.rivers.push_back(std::move(river));

			if (params.carveHeightmap)
			{
				EmitCarveDeltas(spline, grid,
					params.carveDepthMeters, params.carveWidthMeters,
					result.carveDeltas);
			}
		}

		// Lacs auto pour les sinks.
		if (params.autoLakesAtSinks)
		{
			std::unordered_set<uint64_t> seenSinks;
			for (const auto& path : paths)
			{
				if (path.end != PathTracing::End::Sink) continue;
				if (path.sinkX < 0 || path.sinkZ < 0) continue;
				const uint64_t key =
					(static_cast<uint64_t>(static_cast<uint32_t>(path.sinkX)) << 32) |
					 static_cast<uint64_t>(static_cast<uint32_t>(path.sinkZ));
				if (!seenSinks.insert(key).second) continue;
				result.autoLakes.push_back(
					BuildAutoLakeAtSink(path.sinkX, path.sinkZ, grid,
						params.autoLakeMaxDepthMeters));
			}
		}

		return result;
	}

	RiverNetworkResult RunWatershedSimulation(
		TerrainDocument& terrain,
		const WaterDocument& water,
		const engine::core::Config& cfg,
		const WatershedSimulationParams& params)
	{
		RiverNetworkResult emptyResult;
		if (params.springs.empty()) return emptyResult;

		// 1) Calcule le bounding box des sources en coords monde, étend par
		//    une marge de sécurité (~256 m de chaque côté) pour couvrir les
		//    chemins descendants.
		float minX = +1e30f, maxX = -1e30f;
		float minZ = +1e30f, maxZ = -1e30f;
		for (const auto& s : params.springs)
		{
			minX = std::min(minX, s.worldX);
			maxX = std::max(maxX, s.worldX);
			minZ = std::min(minZ, s.worldZ);
			maxZ = std::max(maxZ, s.worldZ);
		}
		constexpr float kSafetyMarginMeters = 256.0f;
		minX -= kSafetyMarginMeters; maxX += kSafetyMarginMeters;
		minZ -= kSafetyMarginMeters; maxZ += kSafetyMarginMeters;

		// 2) Plage de chunks.
		const int chunkXMin = static_cast<int>(std::floor(minX / kChunkSpanMeters));
		const int chunkXMax = static_cast<int>(std::floor(maxX / kChunkSpanMeters));
		const int chunkZMin = static_cast<int>(std::floor(minZ / kChunkSpanMeters));
		const int chunkZMax = static_cast<int>(std::floor(maxZ / kChunkSpanMeters));

		// 3) Construit la ConsolidatedHeightGrid en assemblant les chunks.
		//    Width / Height : nombre de cellules totales sur les axes.
		//    Le bord partagé entre 2 chunks adjacents est échantillonné une
		//    seule fois (le côté droit du chunk N == côté gauche du chunk N+1).
		const int chunksX = chunkXMax - chunkXMin + 1;
		const int chunksZ = chunkZMax - chunkZMin + 1;
		const int gridW = chunksX * (kRes - 1) + 1;
		const int gridH = chunksZ * (kRes - 1) + 1;

		ConsolidatedHeightGrid grid;
		grid.width = gridW;
		grid.height = gridH;
		grid.cellSizeMeters = engine::world::terrain::kTerrainCellSizeMeters;
		grid.originCellX = chunkXMin * (kRes - 1);
		grid.originCellZ = chunkZMin * (kRes - 1);
		grid.heights.assign(
			static_cast<size_t>(gridW) * static_cast<size_t>(gridH), 0.0f);

		for (int cz = 0; cz < chunksZ; ++cz)
		{
			for (int cx = 0; cx < chunksX; ++cx)
			{
				const int chunkX = chunkXMin + cx;
				const int chunkZ = chunkZMin + cz;
				auto chunk = terrain.EnsureLoaded(cfg, chunkX, chunkZ);
				if (!chunk) continue;
				const int baseX = cx * (kRes - 1);
				const int baseZ = cz * (kRes - 1);
				for (int iz = 0; iz < kRes; ++iz)
				{
					for (int ix = 0; ix < kRes; ++ix)
					{
						const int gx = baseX + ix;
						const int gz = baseZ + iz;
						if (gx >= gridW || gz >= gridH) continue;
						grid.heights[CellIndex(gx, gz, gridW)] =
							chunk->heights[static_cast<size_t>(iz) * kRes + ix];
					}
				}
			}
		}

		return RunWatershedOnGrid(grid, water.GetOcean().seaLevelMeters, params);
	}
}
