#include "src/world_editor/terrain/erosion/ThermalWindErosionTool.h"

#include "src/world_editor/terrain/TerrainDocument.h"
#include "src/world_editor/terrain/erosion/ThermalWindErosionCommand.h"
#include "src/world_editor/water/HeightGridAssembly.h"
#include "src/world_editor/water/WaterDocument.h"

#include <memory>
#include <utility>

namespace engine::editor::world::erosion
{
	// `BuildGridFromLoadedChunks` vit maintenant dans
	// `water/HeightGridAssembly.{h,cpp}` (partagé avec CoastlineEditorTool
	// et OperationDispatcher). Voir l'include ci-dessus.

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

		auto grid = engine::editor::world::BuildGridFromLoadedChunks(*m_terrain, *m_cfg);
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
