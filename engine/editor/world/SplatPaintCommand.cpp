#include "engine/editor/world/SplatPaintCommand.h"

#include "engine/world/terrain/SplatMap.h"

#include <utility>

namespace engine::editor::world
{
	SplatPaintCommand::SplatPaintCommand(TerrainDocument& doc,
		std::vector<SplatDeltaChunk> deltas,
		CommandMergeKey strokeKey)
		: m_doc(&doc), m_deltas(std::move(deltas)), m_mergeKey(strokeKey)
	{
	}

	size_t SplatPaintCommand::GetMemoryFootprint() const
	{
		size_t bytes = sizeof(SplatPaintCommand);
		for (const auto& d : m_deltas)
		{
			bytes += sizeof(SplatDeltaChunk);
			bytes += d.cells.capacity() * sizeof(SplatDeltaCell);
		}
		return bytes;
	}

	void SplatPaintCommand::Execute()
	{
		if (!m_doc) return;
		for (const auto& deltaChunk : m_deltas)
		{
			auto splat = m_doc->FindSplat(deltaChunk.coord);
			if (!splat) continue; // splat non chargée : rien à faire ici (le
			                      // tool en assure le chargement avant le push).
			const uint32_t res = splat->resolution;
			const uint32_t lc  = splat->layerCount;
			for (const auto& cell : deltaChunk.cells)
			{
				const size_t base = (static_cast<size_t>(cell.z) * res + cell.x) * lc;
				if (base + lc > splat->weights.size()) continue;
				for (uint32_t l = 0; l < lc && l < cell.next.size(); ++l)
				{
					splat->weights[base + l] = cell.next[l];
				}
			}
			m_doc->MarkSplatDirty(deltaChunk.coord);
		}
	}

	void SplatPaintCommand::Undo()
	{
		if (!m_doc) return;
		for (const auto& deltaChunk : m_deltas)
		{
			auto splat = m_doc->FindSplat(deltaChunk.coord);
			if (!splat) continue;
			const uint32_t res = splat->resolution;
			const uint32_t lc  = splat->layerCount;
			// Parcours en sens inverse pour symétrie avec Execute (utile si
			// une future variante stocke des opérations non-commutatives ;
			// pour le pattern prev/next strict, l'ordre n'importe pas).
			for (auto it = deltaChunk.cells.rbegin(); it != deltaChunk.cells.rend(); ++it)
			{
				const auto& cell = *it;
				const size_t base = (static_cast<size_t>(cell.z) * res + cell.x) * lc;
				if (base + lc > splat->weights.size()) continue;
				for (uint32_t l = 0; l < lc && l < cell.prev.size(); ++l)
				{
					splat->weights[base + l] = cell.prev[l];
				}
			}
			m_doc->MarkSplatDirty(deltaChunk.coord);
		}
	}

	bool SplatPaintCommand::TryMerge(const ICommand& other)
	{
		const auto* o = dynamic_cast<const SplatPaintCommand*>(&other);
		if (o == nullptr) return false;
		if (m_mergeKey == 0 || o->m_mergeKey != m_mergeKey) return false;

		// Pour chaque chunk source, on cherche le chunk de même coord dans
		// `*this`. Si trouvé, on fusionne par cellule (last-write-wins sur
		// `next` ; `prev` du chunk courant reste celui d'origine — c'est la
		// pré-condition de la fusion : la première occurrence de la cellule
		// dans le stroke porte le `prev` correct, et les ticks suivants ne
		// font que repropager le `next`). Si pas trouvé, on push le chunk
		// entier sans fusion fine (le mergeKey garantit qu'aucune autre
		// commande externe ne s'intercale).
		for (const auto& src : o->m_deltas)
		{
			SplatDeltaChunk* target = nullptr;
			for (auto& cur : m_deltas)
			{
				if (cur.coord == src.coord) { target = &cur; break; }
			}
			if (target == nullptr)
			{
				m_deltas.push_back(src);
				continue;
			}
			// Fusion fine cellule par cellule. On indexe par (x,z) pour éviter
			// un scan O(N²) si beaucoup de cellules. Comme les `uint16_t x,z`
			// sont bornés à `kSplatResolution-1`, la clé tient sur 32 bits.
			for (const auto& srcCell : src.cells)
			{
				bool merged = false;
				for (auto& dstCell : target->cells)
				{
					if (dstCell.x == srcCell.x && dstCell.z == srcCell.z)
					{
						// Last-write-wins sur next ; prev intact.
						dstCell.next = srcCell.next;
						merged = true;
						break;
					}
				}
				if (!merged)
				{
					target->cells.push_back(srcCell);
				}
			}
		}
		return true;
	}
}
