#include "src/world_editor/terrain/MacroPolylineCommandBase.h"

#include "src/client/world/terrain/TerrainChunk.h"
#include "src/world_editor/terrain/TerrainDocument.h"

#include <algorithm>
#include <utility>

namespace engine::editor::world
{
	MacroPolylineCommandBase::MacroPolylineCommandBase(TerrainDocument& doc,
		SparseChunkDeltas deltas, const char* label)
		: m_doc(&doc)
		, m_deltas(std::move(deltas))
		, m_label(label != nullptr ? label : "Macro Polyline")
	{
	}

	size_t MacroPolylineCommandBase::GetMemoryFootprint() const
	{
		size_t bytes = sizeof(MacroPolylineCommandBase);
		// Pour chaque chunk, taille approximative : header de bucket +
		// (cellIndex + delta) par cellule.
		for (const auto& kv : m_deltas)
		{
			bytes += sizeof(kv.first);
			bytes += kv.second.size() * (sizeof(uint32_t) + sizeof(float));
		}
		return bytes;
	}

	void MacroPolylineCommandBase::Execute()
	{
		ApplyDeltas(+1.0f);
	}

	void MacroPolylineCommandBase::Undo()
	{
		ApplyDeltas(-1.0f);
	}

	void MacroPolylineCommandBase::ApplyDeltas(float sign)
	{
		if (m_doc == nullptr) return;

		for (auto& kv : m_deltas)
		{
			const engine::world::GlobalChunkCoord coord = kv.first;
			auto chunk = m_doc->Find(coord);
			if (!chunk) continue;
			const uint32_t resX = chunk->resolutionX;
			const size_t   total = chunk->heights.size();

			for (const auto& cell : kv.second)
			{
				const uint32_t cellIndex = cell.first;
				const float    delta     = cell.second * sign;
				const size_t   idx       = static_cast<size_t>(cellIndex);
				if (idx >= total) continue;
				// Clamp anti-overflow. Le delta stocké est calculé en
				// monde, mais une cellule peut déjà être proche des
				// bornes — on clamp pour garantir un état terrain valide
				// post-Execute/Undo. La symétrie est conservée tant que
				// le delta initial n'a pas saturé sur Execute (les valeurs
				// d'usage typique sont à 2-3 ordres de grandeur des bornes).
				const float oldH = chunk->heights[idx];
				const float newH = std::clamp(oldH + delta,
					engine::world::terrain::kTerrainHeightMinMeters,
					engine::world::terrain::kTerrainHeightMaxMeters);
				chunk->heights[idx] = newH;
				(void)resX;
			}

			m_doc->MarkDirty(coord);
			m_doc->OnCommit(coord);
		}
	}
}
