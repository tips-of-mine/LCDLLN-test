#include "src/world_editor/water/RiverNetworkCommand.h"

#include "src/client/world/terrain/TerrainChunk.h"
#include "src/world_editor/terrain/TerrainDocument.h"
#include "src/world_editor/water/WaterDocument.h"

#include <algorithm>
#include <utility>

namespace engine::editor::world
{
	RiverNetworkCommand::RiverNetworkCommand(TerrainDocument& terrain,
		WaterDocument& water,
		RiverNetworkResult result,
		OceanSettings newOcean,
		OceanSettings previousOcean)
		: m_terrain(&terrain)
		, m_water(&water)
		, m_result(std::move(result))
		, m_newOcean(newOcean)
		, m_previousOcean(previousOcean)
	{
	}

	size_t RiverNetworkCommand::GetMemoryFootprint() const
	{
		size_t bytes = sizeof(RiverNetworkCommand);
		for (const auto& r : m_result.rivers)
		{
			bytes += r.name.capacity();
			bytes += r.nodes.capacity() * sizeof(engine::world::water::RiverNode);
		}
		for (const auto& l : m_result.autoLakes)
		{
			bytes += l.name.capacity();
			bytes += l.polygon.capacity() * sizeof(engine::math::Vec3);
		}
		for (const auto& kv : m_result.carveDeltas)
		{
			bytes += sizeof(kv.first) +
				kv.second.size() * (sizeof(uint32_t) + sizeof(float));
		}
		return bytes;
	}

	void RiverNetworkCommand::Execute()
	{
		if (m_water == nullptr || m_terrain == nullptr) return;

		// 1) OceanSettings : applique la nouvelle valeur (snapshot précédent
		//    capturé à la construction).
		m_water->SetOceanSettings(m_newOcean);

		// 2) Rivières + lacs auto. On capture les noms effectivement insérés
		//    pour les retirer à l'Undo sans casser les autres instances.
		auto& scene = m_water->Mutable();
		m_insertedRiverNames.clear();
		m_insertedRiverNames.reserve(m_result.rivers.size());
		for (const auto& r : m_result.rivers)
		{
			m_insertedRiverNames.push_back(r.name);
			scene.rivers.push_back(r);
		}
		m_insertedLakeNames.clear();
		m_insertedLakeNames.reserve(m_result.autoLakes.size());
		for (const auto& l : m_result.autoLakes)
		{
			m_insertedLakeNames.push_back(l.name);
			scene.lakes.push_back(l);
		}
		m_water->MarkDirty();

		// 3) Carving deltas.
		ApplyCarveDeltas(+1.0f);
	}

	void RiverNetworkCommand::Undo()
	{
		if (m_water == nullptr || m_terrain == nullptr) return;

		// Inverse des étapes Execute, dans l'ordre inverse.
		ApplyCarveDeltas(-1.0f);

		auto& scene = m_water->Mutable();
		// Retire les rivières / lacs par nom (ordre inverse pour pop_back).
		for (auto it = m_insertedRiverNames.rbegin(); it != m_insertedRiverNames.rend(); ++it)
		{
			auto found = std::find_if(scene.rivers.begin(), scene.rivers.end(),
				[&](const engine::world::water::RiverInstance& r) { return r.name == *it; });
			if (found != scene.rivers.end()) scene.rivers.erase(found);
		}
		for (auto it = m_insertedLakeNames.rbegin(); it != m_insertedLakeNames.rend(); ++it)
		{
			auto found = std::find_if(scene.lakes.begin(), scene.lakes.end(),
				[&](const engine::world::water::LakeInstance& l) { return l.name == *it; });
			if (found != scene.lakes.end()) scene.lakes.erase(found);
		}
		m_insertedRiverNames.clear();
		m_insertedLakeNames.clear();
		m_water->MarkDirty();

		// OceanSettings : restaure le snapshot précédent.
		m_water->SetOceanSettings(m_previousOcean);
	}

	void RiverNetworkCommand::ApplyCarveDeltas(float sign)
	{
		if (m_terrain == nullptr) return;
		if (m_result.carveDeltas.empty()) return;
		for (auto& kv : m_result.carveDeltas)
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
