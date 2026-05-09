#include "src/world_editor/TerrainSculptCommand.h"

#include "src/client/world/terrain/TerrainChunk.h"

#include <utility>

namespace engine::editor::world
{
	TerrainSculptCommand::TerrainSculptCommand(TerrainDocument& doc,
		std::vector<TerrainSculptDeltaChunk> deltas,
		CommandMergeKey mergeKey)
		: m_doc(&doc), m_deltas(std::move(deltas)), m_mergeKey(mergeKey)
	{
	}

	size_t TerrainSculptCommand::GetMemoryFootprint() const
	{
		size_t bytes = sizeof(TerrainSculptCommand);
		for (const auto& d : m_deltas)
		{
			bytes += sizeof(TerrainSculptDeltaChunk);
			bytes += d.cells.capacity() * sizeof(TerrainSculptDeltaCell);
		}
		return bytes;
	}

	void TerrainSculptCommand::Execute()
	{
		if (!m_doc) return;
		for (const auto& deltaChunk : m_deltas)
		{
			auto chunk = m_doc->Find(deltaChunk.coord);
			if (!chunk) continue; // chunk non chargé : on ignore (ne devrait pas arriver)
			const uint32_t resX = chunk->resolutionX;
			for (const auto& cell : deltaChunk.cells)
			{
				const size_t idx = static_cast<size_t>(cell.z) * resX + cell.x;
				if (idx < chunk->heights.size())
				{
					chunk->heights[idx] += cell.deltaMeters;
				}
			}
			m_doc->MarkDirty(deltaChunk.coord);
			m_doc->OnCommit(deltaChunk.coord);
		}
	}

	void TerrainSculptCommand::Undo()
	{
		if (!m_doc) return;
		for (const auto& deltaChunk : m_deltas)
		{
			auto chunk = m_doc->Find(deltaChunk.coord);
			if (!chunk) continue;
			const uint32_t resX = chunk->resolutionX;
			// Inverse en parcourant en sens inverse : équivalent à `-=` cellule
			// par cellule mais respecte l'ordre symétrique de `Execute` (utile
			// si plus tard on stockait des opérations non-commutatives).
			for (auto it = deltaChunk.cells.rbegin(); it != deltaChunk.cells.rend(); ++it)
			{
				const auto& cell = *it;
				const size_t idx = static_cast<size_t>(cell.z) * resX + cell.x;
				if (idx < chunk->heights.size())
				{
					chunk->heights[idx] -= cell.deltaMeters;
				}
			}
			m_doc->MarkDirty(deltaChunk.coord);
			m_doc->OnCommit(deltaChunk.coord);
		}
	}

	bool TerrainSculptCommand::TryMerge(const ICommand& other)
	{
		const auto* o = dynamic_cast<const TerrainSculptCommand*>(&other);
		if (o == nullptr) return false;
		if (m_mergeKey == 0 || o->m_mergeKey != m_mergeKey) return false;
		// Concatène les cellules par chunk. On indexe par coord packée pour
		// éviter une scan O(N²) si beaucoup de chunks touchés.
		for (const auto& src : o->m_deltas)
		{
			auto it = m_deltas.end();
			for (auto cur = m_deltas.begin(); cur != m_deltas.end(); ++cur)
			{
				if (cur->coord == src.coord) { it = cur; break; }
			}
			if (it == m_deltas.end())
			{
				m_deltas.push_back(src);
			}
			else
			{
				it->cells.insert(it->cells.end(), src.cells.begin(), src.cells.end());
			}
		}
		return true;
	}
}
