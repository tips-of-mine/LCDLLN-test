#include "src/world_editor/terrain/erosion/HydraulicErosionCommand.h"

#include "src/client/world/terrain/TerrainChunk.h"
#include "src/world_editor/terrain/TerrainDocument.h"

#include <algorithm>
#include <utility>

namespace engine::editor::world::erosion
{
	HydraulicErosionCommand::HydraulicErosionCommand(
		engine::editor::world::TerrainDocument& terrain,
		HydraulicSimulationResult result,
		HydraulicSimulationParams paramsSnapshot)
		: m_terrain(&terrain)
		, m_result(std::move(result))
		, m_paramsSnapshot(paramsSnapshot)
	{
	}

	size_t HydraulicErosionCommand::GetMemoryFootprint() const
	{
		size_t bytes = sizeof(HydraulicErosionCommand);
		for (const auto& kv : m_result.deltas)
		{
			bytes += sizeof(kv.first);
			bytes += kv.second.size() * (sizeof(uint32_t) + sizeof(float));
		}
		return bytes;
	}

	void HydraulicErosionCommand::Execute()
	{
		ApplyDeltas(+1.0f);
	}

	void HydraulicErosionCommand::Undo()
	{
		ApplyDeltas(-1.0f);
	}

	void HydraulicErosionCommand::ApplyDeltas(float sign)
	{
		if (m_terrain == nullptr) return;
		for (auto& kv : m_result.deltas)
		{
			auto chunk = m_terrain->Find(kv.first);
			if (!chunk) continue;
			const size_t total = chunk->heights.size();
			for (const auto& cell : kv.second)
			{
				const size_t idx = static_cast<size_t>(cell.first);
				if (idx >= total) continue;
				const float oldH = chunk->heights[idx];
				const float newH = std::clamp(oldH + cell.second * sign,
					engine::world::terrain::kTerrainHeightMinMeters,
					engine::world::terrain::kTerrainHeightMaxMeters);
				chunk->heights[idx] = newH;
			}
			m_terrain->MarkDirty(kv.first);
			m_terrain->OnCommit(kv.first);
		}
	}
}
