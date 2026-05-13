#include "src/world_editor/water/CoastlineCommand.h"

#include "src/client/world/terrain/TerrainChunk.h"
#include "src/world_editor/terrain/TerrainDocument.h"
#include "src/world_editor/water/WaterDocument.h"

#include <algorithm>
#include <utility>

namespace engine::editor::world
{
	CoastlineCommand::CoastlineCommand(TerrainDocument& terrain,
		WaterDocument& water, ApplyData data)
		: m_terrain(&terrain)
		, m_water(&water)
		, m_data(std::move(data))
	{
	}

	size_t CoastlineCommand::GetMemoryFootprint() const
	{
		size_t bytes = sizeof(CoastlineCommand);
		for (const auto& kv : m_data.heightmapDeltas)
		{
			bytes += sizeof(kv.first);
			bytes += kv.second.size() * (sizeof(uint32_t) + sizeof(float));
		}
		if (m_data.oceanToInsert)
		{
			bytes += m_data.oceanToInsert->name.capacity();
			bytes += m_data.oceanToInsert->polygon.capacity() *
				sizeof(engine::math::Vec3);
		}
		return bytes;
	}

	void CoastlineCommand::Execute()
	{
		if (m_water == nullptr || m_terrain == nullptr) return;

		// 1) OceanSettings.
		m_water->SetOceanSettings(m_data.newOcean);

		// 2) Océan LakeInstance : insert OU update in-place.
		auto& scene = m_water->Mutable();
		if (m_data.existingOceanIndex >= 0 &&
			static_cast<size_t>(m_data.existingOceanIndex) < scene.lakes.size())
		{
			// Update in-place. Snapshot capturé à la construction de la
			// commande dans `m_data.previousOcean`.
			auto& lake = scene.lakes[static_cast<size_t>(m_data.existingOceanIndex)];
			lake.waterLevelY = m_data.newOcean.seaLevelMeters;
			lake.bottomColor = engine::math::Vec3{
				m_data.newOcean.bottomColor[0],
				m_data.newOcean.bottomColor[1],
				m_data.newOcean.bottomColor[2] };
			lake.turbidity   = m_data.newOcean.turbidity;
			lake.isOcean     = true;
			m_insertedIndex = -1;
		}
		else if (m_data.oceanToInsert)
		{
			scene.lakes.push_back(*m_data.oceanToInsert);
			m_insertedIndex = static_cast<int>(scene.lakes.size() - 1u);
		}
		m_water->MarkDirty();

		// 3) Heightmap deltas (smoothing + falaises).
		ApplyHeightDeltas(+1.0f);
	}

	void CoastlineCommand::Undo()
	{
		if (m_water == nullptr || m_terrain == nullptr) return;

		// Inverse order.
		ApplyHeightDeltas(-1.0f);

		auto& scene = m_water->Mutable();
		if (m_insertedIndex >= 0 &&
			static_cast<size_t>(m_insertedIndex) < scene.lakes.size())
		{
			scene.lakes.erase(scene.lakes.begin() + m_insertedIndex);
			m_insertedIndex = -1;
		}
		else if (m_data.previousOceanLake &&
			m_data.existingOceanIndex >= 0 &&
			static_cast<size_t>(m_data.existingOceanIndex) < scene.lakes.size())
		{
			scene.lakes[static_cast<size_t>(m_data.existingOceanIndex)] =
				*m_data.previousOceanLake;
		}
		m_water->MarkDirty();

		// Restore OceanSettings snapshot.
		m_water->SetOceanSettings(m_data.previousOcean);
	}

	void CoastlineCommand::ApplyHeightDeltas(float sign)
	{
		if (m_data.heightmapDeltas.empty()) return;
		for (auto& kv : m_data.heightmapDeltas)
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
