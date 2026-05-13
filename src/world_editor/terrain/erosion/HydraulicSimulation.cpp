#include "src/world_editor/terrain/erosion/HydraulicSimulation.h"

#include "src/client/world/terrain/TerrainChunk.h"
#include "src/world_editor/terrain/TerrainDocument.h"
#include "src/world_editor/terrain/erosion/DropletInitDistribution.h"
#include "src/world_editor/terrain/erosion/DropletKernel.h"
#include "src/world_editor/water/WaterDocument.h"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace engine::editor::world::erosion
{
	namespace
	{
		constexpr int kRes =
			static_cast<int>(engine::world::terrain::kTerrainResolution);
	}

	HydraulicSimulationResult RunHydraulicOnGrid(
		const engine::editor::world::ConsolidatedHeightGrid& grid,
		float seaLevelMeters,
		const HydraulicSimulationParams& params)
	{
		HydraulicSimulationResult result;
		if (params.numDroplets == 0u || grid.width < 2 || grid.height < 2) return result;

		const auto t0 = std::chrono::steady_clock::now();

		DropletInitDistribution sampler;
		sampler.Init(grid, params.distribution, params.rngSeed);

		uint64_t stepsAccum = 0;
		for (uint32_t i = 0; i < params.numDroplets; ++i)
		{
			const auto [px, pz] = sampler.Sample();
			DropletKernelResult dr = RunSingleDroplet(grid, params,
				seaLevelMeters, px, pz, result.deltas);
			stepsAccum            += dr.stepsTaken;
			result.cellsEroded    += dr.cellsEroded;
			result.cellsDeposited += dr.cellsDeposited;
		}
		result.dropletsSimulated = params.numDroplets;
		result.totalSteps        = stepsAccum;

		// Clamp final + statistiques min/max delta.
		for (auto& kv : result.deltas)
		{
			for (auto& cell : kv.second)
			{
				cell.second = std::clamp(cell.second,
					-params.maxDeltaPerCellMeters,
					+params.maxDeltaPerCellMeters);
				result.minDelta = std::min(result.minDelta, cell.second);
				result.maxDelta = std::max(result.maxDelta, cell.second);
			}
		}

		const auto t1 = std::chrono::steady_clock::now();
		const auto deltaMicros = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
		result.wallTimeMillis = static_cast<double>(deltaMicros) / 1000.0;
		return result;
	}

	HydraulicSimulationResult RunHydraulicSimulation(
		engine::editor::world::TerrainDocument& terrain,
		const engine::editor::world::WaterDocument& water,
		const engine::core::Config& cfg,
		const HydraulicSimulationParams& params)
	{
		HydraulicSimulationResult empty;
		if (params.numDroplets == 0u) return empty;

		// Pour M100.38 MVP, on assemble une grille 2×2 chunks autour de
		// l'origine — même périmètre que CoastlineEditorTool::BuildGrid pour
		// la cohérence du test interactif. Un follow-up étendra à la zone
		// complète quand le streaming sera pertinent.
		constexpr int kChunksDim = 2;
		constexpr int kBaseX = 0;
		constexpr int kBaseZ = 0;
		const int gridW = kChunksDim * (kRes - 1) + 1;
		const int gridH = kChunksDim * (kRes - 1) + 1;
		engine::editor::world::ConsolidatedHeightGrid grid;
		grid.width = gridW;
		grid.height = gridH;
		grid.cellSizeMeters = engine::world::terrain::kTerrainCellSizeMeters;
		grid.originCellX = kBaseX * (kRes - 1);
		grid.originCellZ = kBaseZ * (kRes - 1);
		grid.heights.assign(static_cast<size_t>(gridW) * gridH, 0.0f);
		for (int cz = 0; cz < kChunksDim; ++cz)
		{
			for (int cx = 0; cx < kChunksDim; ++cx)
			{
				auto chunk = terrain.EnsureLoaded(cfg, kBaseX + cx, kBaseZ + cz);
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
						grid.heights[static_cast<size_t>(gz) * gridW + gx] =
							chunk->heights[static_cast<size_t>(iz) * kRes + ix];
					}
				}
			}
		}

		return RunHydraulicOnGrid(grid, water.GetOcean().seaLevelMeters, params);
	}
}
