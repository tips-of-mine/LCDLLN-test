#include "src/world_editor/terrain/erosion/ThermalWindErosionCommand.h"

#include "src/client/world/terrain/TerrainChunk.h"
#include "src/world_editor/terrain/TerrainDocument.h"

#include <algorithm>
#include <unordered_set>
#include <utility>

namespace engine::editor::world::erosion
{
	ThermalWindErosionCommand::ThermalWindErosionCommand(
		engine::editor::world::TerrainDocument& terrain, Data data)
		: m_terrain(&terrain)
		, m_data(std::move(data))
	{
	}

	size_t ThermalWindErosionCommand::GetMemoryFootprint() const
	{
		size_t bytes = sizeof(ThermalWindErosionCommand);
		auto accumulate = [&bytes](const engine::editor::world::SparseChunkDeltas& d) {
			for (const auto& kv : d)
			{
				bytes += sizeof(kv.first);
				bytes += kv.second.size() * (sizeof(uint32_t) + sizeof(float));
			}
		};
		accumulate(m_data.thermalDeltas);
		accumulate(m_data.windDeltas);
		return bytes;
	}

	void ThermalWindErosionCommand::Execute()
	{
		ApplyDeltas(m_data.thermalDeltas, +1.0f);
		ApplyDeltas(m_data.windDeltas,    +1.0f);
	}

	void ThermalWindErosionCommand::Undo()
	{
		// Inverse de l'ordre Apply : wind d'abord, puis thermal.
		ApplyDeltas(m_data.windDeltas,    -1.0f);
		ApplyDeltas(m_data.thermalDeltas, -1.0f);
	}

	void ThermalWindErosionCommand::ApplyDeltas(
		const engine::editor::world::SparseChunkDeltas& deltas, float sign)
	{
		if (m_terrain == nullptr) return;
		for (const auto& kv : deltas)
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
