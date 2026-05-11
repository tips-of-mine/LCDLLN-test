#include "src/world_editor/terrain/TerrainStampCommand.h"

#include "src/client/world/terrain/TerrainChunk.h"

#include <utility>

namespace engine::editor::world
{
	TerrainStampCommand::TerrainStampCommand(TerrainDocument& doc,
		std::vector<TerrainSculptDeltaChunk> deltas)
		: m_doc(&doc), m_deltas(std::move(deltas))
	{
	}

	size_t TerrainStampCommand::GetMemoryFootprint() const
	{
		size_t bytes = sizeof(TerrainStampCommand);
		for (const auto& d : m_deltas)
		{
			bytes += sizeof(TerrainSculptDeltaChunk);
			bytes += d.cells.capacity() * sizeof(TerrainSculptDeltaCell);
		}
		return bytes;
	}

	void TerrainStampCommand::Execute()
	{
		if (!m_doc) return;
		for (const auto& deltaChunk : m_deltas)
		{
			auto chunk = m_doc->Find(deltaChunk.coord);
			if (!chunk) continue; // chunk non chargé : ne devrait pas arriver
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
		m_applied = true;
	}

	void TerrainStampCommand::Undo()
	{
		if (!m_doc) return;
		for (const auto& deltaChunk : m_deltas)
		{
			auto chunk = m_doc->Find(deltaChunk.coord);
			if (!chunk) continue;
			const uint32_t resX = chunk->resolutionX;
			// Parcours en sens inverse pour symétrie avec Execute (utile si
			// plus tard on stockait des opérations non-commutatives).
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
		m_applied = false;
	}
}
