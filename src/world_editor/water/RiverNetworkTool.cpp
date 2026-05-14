#include "src/world_editor/water/RiverNetworkTool.h"

#include "src/world_editor/terrain/TerrainDocument.h"
#include "src/world_editor/water/RiverNetworkCommand.h"
#include "src/world_editor/water/WaterDocument.h"
#include "src/world_editor/water/WatershedSimulation.h"

#include <memory>
#include <utility>

namespace engine::editor::world
{
	bool RiverNetworkTool::Init(CommandStack& stack, TerrainDocument& terrain,
		WaterDocument& water, const engine::core::Config& cfg)
	{
		m_stack   = &stack;
		m_terrain = &terrain;
		m_water   = &water;
		m_cfg     = &cfg;
		Reset();
		return true;
	}

	void RiverNetworkTool::Reset()
	{
		m_params.springs.clear();
		m_params.minFlowThresholdCells         = 200u;
		m_params.simplificationToleranceMeters = 5.0f;
		m_params.autoLakesAtSinks              = true;
		m_params.autoLakeMaxDepthMeters        = 10.0f;
		m_params.carveHeightmap                = true;
		m_params.carveDepthMeters              = 3.0f;
		m_params.carveWidthMeters              = 12.0f;
		// Réinitialise le slider à la valeur stockée dans le document.
		if (m_water != nullptr)
		{
			m_seaLevelBuffer = m_water->GetOcean().seaLevelMeters;
		}
		m_lastResult = RiverNetworkResult{};
		m_hasResult  = false;
	}

	bool RiverNetworkTool::AddSpring(float worldX, float worldZ, float worldY)
	{
		if (m_params.springs.size() >= kMaxSprings) return false;
		SpringSource s;
		s.worldX = worldX;
		s.worldZ = worldZ;
		s.worldY = worldY;
		m_params.springs.push_back(s);
		// Invalide le résultat précédent : il faut re-simuler.
		m_hasResult = false;
		return true;
	}

	void RiverNetworkTool::RemoveSpring(size_t idx)
	{
		if (idx >= m_params.springs.size()) return;
		m_params.springs.erase(m_params.springs.begin() +
			static_cast<std::ptrdiff_t>(idx));
		m_hasResult = false;
	}

	void RiverNetworkTool::MoveSpring(size_t idx, float worldX, float worldZ, float worldY)
	{
		if (idx >= m_params.springs.size()) return;
		m_params.springs[idx].worldX = worldX;
		m_params.springs[idx].worldZ = worldZ;
		m_params.springs[idx].worldY = worldY;
		m_hasResult = false;
	}

	bool RiverNetworkTool::Simulate()
	{
		if (m_terrain == nullptr || m_water == nullptr || m_cfg == nullptr) return false;
		if (m_params.springs.empty()) return false;
		// Le simulator lit le sea level depuis WaterDocument, donc on synchronise
		// temporairement la valeur du slider AVANT la sim. À l'Apply, la valeur
		// est commitée durablement ; à l'absence d'Apply, l'utilisateur peut
		// Cancel et la valeur sera réinitialisée à Reset.
		const OceanSettings previousOcean = m_water->GetOcean();
		OceanSettings tempOcean = previousOcean;
		tempOcean.seaLevelMeters = m_seaLevelBuffer;
		m_water->SetOceanSettings(tempOcean);
		m_lastResult = RunWatershedSimulation(*m_terrain, *m_water, *m_cfg, m_params);
		// Restaure la valeur du document à son état pré-simulation : l'Apply
		// est le seul moment où le slider est durablement persisté.
		m_water->SetOceanSettings(previousOcean);
		m_hasResult = true;
		return true;
	}

	bool RiverNetworkTool::Apply()
	{
		if (m_stack == nullptr || m_terrain == nullptr || m_water == nullptr) return false;
		if (!m_hasResult)
		{
			// Si l'utilisateur n'a pas cliqué Simulate, on tente une sim
			// implicite. No-op si pas de sources.
			if (!Simulate()) return false;
		}
		const OceanSettings previousOcean = m_water->GetOcean();
		OceanSettings newOcean = previousOcean;
		newOcean.seaLevelMeters = m_seaLevelBuffer;

		auto cmd = std::make_unique<RiverNetworkCommand>(*m_terrain, *m_water,
			std::move(m_lastResult), newOcean, previousOcean);
		m_stack->Push(std::move(cmd));
		Reset();
		return true;
	}

	void RiverNetworkTool::Cancel()
	{
		Reset();
	}
}
