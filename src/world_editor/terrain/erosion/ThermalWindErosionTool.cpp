#include "src/world_editor/terrain/erosion/ThermalWindErosionTool.h"

#include "src/client/world/terrain/TerrainChunk.h"
#include "src/world_editor/terrain/TerrainDocument.h"
#include "src/world_editor/terrain/erosion/ThermalWindErosionCommand.h"
#include "src/world_editor/water/WaterDocument.h"

#include <memory>
#include <utility>

namespace engine::editor::world::erosion
{
	namespace
	{
		constexpr int kRes =
			static_cast<int>(engine::world::terrain::kTerrainResolution);

		/// Assemble un grid 2×2 chunks autour de l'origine (même périmètre
		/// MVP que les autres outils Phase 2.5).
		engine::editor::world::ConsolidatedHeightGrid BuildGrid(
			engine::editor::world::TerrainDocument& terrain,
			const engine::core::Config& cfg)
		{
			constexpr int kChunksDim = 2;
			engine::editor::world::ConsolidatedHeightGrid grid;
			grid.cellSizeMeters = engine::world::terrain::kTerrainCellSizeMeters;
			grid.originCellX = 0;
			grid.originCellZ = 0;
			const int W = kChunksDim * (kRes - 1) + 1;
			const int H = kChunksDim * (kRes - 1) + 1;
			grid.width = W;
			grid.height = H;
			grid.heights.assign(static_cast<size_t>(W) * H, 0.0f);
			for (int cz = 0; cz < kChunksDim; ++cz)
			{
				for (int cx = 0; cx < kChunksDim; ++cx)
				{
					auto chunk = terrain.EnsureLoaded(cfg, cx, cz);
					if (!chunk) continue;
					const int baseX = cx * (kRes - 1);
					const int baseZ = cz * (kRes - 1);
					for (int iz = 0; iz < kRes; ++iz)
					{
						for (int ix = 0; ix < kRes; ++ix)
						{
							const int gx = baseX + ix;
							const int gz = baseZ + iz;
							if (gx >= W || gz >= H) continue;
							grid.heights[static_cast<size_t>(gz) * W + gx] =
								chunk->heights[static_cast<size_t>(iz) * kRes + ix];
						}
					}
				}
			}
			return grid;
		}
	}

	bool ThermalWindErosionTool::Init(engine::editor::world::CommandStack& stack,
		engine::editor::world::TerrainDocument& terrain,
		engine::editor::world::WaterDocument& water,
		const engine::core::Config& cfg)
	{
		m_stack   = &stack;
		m_terrain = &terrain;
		m_water   = &water;
		m_cfg     = &cfg;
		Reset();
		return true;
	}

	void ThermalWindErosionTool::Reset()
	{
		m_params = ThermalWindErosionParams{};
		m_thermalResult = ThermalSimulationResult{};
		m_windResult    = WindSimulationResult{};
		m_thermalDeltas.clear();
		m_windDeltas.clear();
		m_hasResult = false;
	}

	bool ThermalWindErosionTool::Simulate()
	{
		if (m_terrain == nullptr || m_water == nullptr || m_cfg == nullptr) return false;

		auto grid = BuildGrid(*m_terrain, *m_cfg);
		const float seaLevel = m_water->GetOcean().seaLevelMeters;

		m_thermalDeltas.clear();
		m_windDeltas.clear();
		m_thermalResult = ThermalSimulationResult{};
		m_windResult    = WindSimulationResult{};

		const bool runThermal = (m_params.subMode == ErosionSubMode::Thermal
			|| m_params.subMode == ErosionSubMode::Both);
		const bool runWind = (m_params.subMode == ErosionSubMode::Wind
			|| m_params.subMode == ErosionSubMode::Both);

		if (runThermal)
		{
			m_thermalResult = RunThermalSimulation(grid, seaLevel, m_params.thermal);
			m_thermalDeltas = m_thermalResult.deltas;
		}

		if (runWind)
		{
			// En mode Both, `grid` a été muté par Thermal et sert d'input
			// au Wind (l'ordre est strictement respecté). En Wind seul, le
			// grid reste pristine.
			m_windResult = RunWindSimulation(grid, seaLevel, m_params.wind);
			m_windDeltas = m_windResult.deltas;
		}

		m_hasResult = true;
		return true;
	}

	bool ThermalWindErosionTool::Apply()
	{
		if (m_stack == nullptr || m_terrain == nullptr) return false;
		if (!m_hasResult) return false;

		ThermalWindErosionCommand::Data data;
		data.thermalDeltas = std::move(m_thermalDeltas);
		data.windDeltas    = std::move(m_windDeltas);
		data.thermalStats  = m_thermalResult;
		data.windStats     = m_windResult;

		auto cmd = std::make_unique<ThermalWindErosionCommand>(*m_terrain,
			std::move(data));
		m_stack->Push(std::move(cmd));
		Reset();
		return true;
	}

	void ThermalWindErosionTool::Cancel()
	{
		Reset();
	}
}
