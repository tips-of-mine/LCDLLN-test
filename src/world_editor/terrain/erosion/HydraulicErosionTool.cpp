#include "src/world_editor/terrain/erosion/HydraulicErosionTool.h"

#include "src/world_editor/terrain/TerrainDocument.h"
#include "src/world_editor/terrain/erosion/HydraulicErosionCommand.h"
#include "src/world_editor/terrain/erosion/HydraulicSimulation.h"
#include "src/world_editor/water/WaterDocument.h"

#include <memory>
#include <utility>

namespace engine::editor::world::erosion
{
	bool HydraulicErosionTool::Init(engine::editor::world::CommandStack& stack,
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

	void HydraulicErosionTool::Reset()
	{
		m_params = HydraulicSimulationParams{};
		m_lastResult = HydraulicSimulationResult{};
		m_hasResult = false;
		m_previewEnabled = true;
	}

	bool HydraulicErosionTool::Simulate()
	{
		if (m_terrain == nullptr || m_water == nullptr || m_cfg == nullptr) return false;
		m_lastResult = RunHydraulicSimulation(*m_terrain, *m_water, *m_cfg, m_params);
		m_hasResult  = true;
		return true;
	}

	bool HydraulicErosionTool::Apply()
	{
		if (m_stack == nullptr || m_terrain == nullptr) return false;
		if (!m_hasResult) return false;
		auto cmd = std::make_unique<HydraulicErosionCommand>(
			*m_terrain, std::move(m_lastResult), m_params);
		m_stack->Push(std::move(cmd));
		m_lastResult = HydraulicSimulationResult{};
		m_hasResult = false;
		return true;
	}

	void HydraulicErosionTool::Cancel()
	{
		m_lastResult = HydraulicSimulationResult{};
		m_hasResult = false;
	}
}
