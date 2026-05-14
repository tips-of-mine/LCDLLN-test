#include "src/world_editor/volumes/caves/PlaceCaveCommand.h"

#include "src/client/world/terrain/SplatMap.h"
#include "src/world_editor/terrain/TerrainDocument.h"
#include "src/world_editor/volumes/MeshInsertDocument.h"

#include <algorithm>
#include <utility>

namespace engine::editor::world::volumes::caves
{
	PlaceCaveCommand::PlaceCaveCommand(MeshInsertDocument& meshDoc,
		engine::editor::world::TerrainDocument& terrain, Data data)
		: m_meshDoc(&meshDoc)
		, m_terrain(&terrain)
		, m_data(std::move(data))
	{
	}

	size_t PlaceCaveCommand::GetMemoryFootprint() const
	{
		size_t bytes = sizeof(PlaceCaveCommand);
		bytes += m_data.instance.gltfRelativePath.capacity();
		bytes += m_data.instance.insertCategory.capacity();
		bytes += m_data.instance.displayName.capacity();
		bytes += m_splatSnapshot.capacity() * sizeof(SplatSnapshotCell);
		return bytes;
	}

	void PlaceCaveCommand::Execute()
	{
		if (m_meshDoc == nullptr || m_terrain == nullptr) return;

		// 1) Mesh insert.
		m_insertedGuid = m_meshDoc->Add(m_data.instance);

		// 2) Splat camouflage (optionnel). Le caller (CaveTool::Apply)
		// charge les splatmaps via TerrainDocument::EnsureSplatLoaded
		// avant de pousser la commande, on suppose donc qu'elles sont
		// résidentes (Find retourne non-null).
		if (m_data.splatPatch)
		{
			const auto weights = ComputeCaveSplatWeights(*m_data.splatPatch);
			for (const auto& kv : weights)
			{
				auto splat = m_terrain->FindSplat(kv.first);
				if (!splat) continue; // nécessite EnsureSplatLoaded en amont par l'outil
				for (const auto& cell : kv.second)
				{
					const uint32_t cellIndex = cell.first;
					const float weight       = cell.second; // [0..1]
					const size_t base = static_cast<size_t>(cellIndex) * splat->layerCount;
					if (base + splat->layerCount > splat->weights.size()) continue;
					const uint8_t target = m_data.splatPatch->splatLayer;
					if (target >= splat->layerCount) continue;
					const uint8_t prevTarget = splat->weights[base + target];
					// Capture le snapshot pour Undo.
					SplatSnapshotCell snap;
					snap.chunkCoord  = kv.first;
					snap.cellIndex   = cellIndex;
					snap.layer       = target;
					snap.prevWeight  = prevTarget;
					snap.prevOtherSum = static_cast<uint8_t>(255 - prevTarget);
					m_splatSnapshot.push_back(snap);
					// Calcule le nouveau poids et re-normalise pour maintenir
					// la somme = 255 sur la cellule.
					const float deltaW   = weight * 255.0f;
					const float newTargetF = std::clamp(
						static_cast<float>(prevTarget) + deltaW, 0.0f, 255.0f);
					const uint8_t newTarget = static_cast<uint8_t>(newTargetF + 0.5f);
					splat->weights[base + target] = newTarget;
					// Ré-équilibre les autres layers : ils se partagent
					// (255 - newTarget) proportionnellement à leur poids
					// précédent.
					const int othersOld = 255 - prevTarget;
					const int othersNew = 255 - newTarget;
					if (othersOld > 0)
					{
						for (uint32_t L = 0; L < splat->layerCount; ++L)
						{
							if (L == target) continue;
							const int oldW = splat->weights[base + L];
							const int newW = static_cast<int>(
								static_cast<float>(oldW * othersNew) /
								static_cast<float>(othersOld) + 0.5f);
							splat->weights[base + L] = static_cast<uint8_t>(
								std::clamp(newW, 0, 255));
						}
					}
					// Force somme = 255 exactement (arrondi peut décaler de
					// ±1 ; on injecte le résidu sur la couche target).
					int sum = 0;
					for (uint32_t L = 0; L < splat->layerCount; ++L)
						sum += splat->weights[base + L];
					const int residual = 255 - sum;
					const int adj = static_cast<int>(splat->weights[base + target]) + residual;
					splat->weights[base + target] = static_cast<uint8_t>(
						std::clamp(adj, 0, 255));
				}
				m_terrain->MarkSplatDirty(kv.first);
			}
		}
	}

	void PlaceCaveCommand::Undo()
	{
		if (m_meshDoc == nullptr || m_terrain == nullptr) return;
		// Restore splat snapshots (inverse de l'écriture).
		for (auto it = m_splatSnapshot.rbegin(); it != m_splatSnapshot.rend(); ++it)
		{
			auto splat = m_terrain->FindSplat(it->chunkCoord);
			if (!splat) continue;
			const size_t base = static_cast<size_t>(it->cellIndex) * splat->layerCount;
			if (base + splat->layerCount > splat->weights.size()) continue;
			// Restaure le poids exact de la layer cible. Les autres layers
			// sont restaurées proportionnellement (le ratio entre elles
			// est conservé depuis l'état pré-Execute).
			const uint8_t newTarget = splat->weights[base + it->layer];
			const int    newOthers  = 255 - newTarget;
			const int    oldOthers  = it->prevOtherSum;
			splat->weights[base + it->layer] = it->prevWeight;
			if (newOthers > 0 && oldOthers >= 0)
			{
				for (uint32_t L = 0; L < splat->layerCount; ++L)
				{
					if (L == it->layer) continue;
					const int curW = splat->weights[base + L];
					const int origW = static_cast<int>(
						static_cast<float>(curW * oldOthers) /
						static_cast<float>(newOthers) + 0.5f);
					splat->weights[base + L] = static_cast<uint8_t>(
						std::clamp(origW, 0, 255));
				}
			}
			// Re-clamp la somme.
			int sum = 0;
			for (uint32_t L = 0; L < splat->layerCount; ++L)
				sum += splat->weights[base + L];
			const int residual = 255 - sum;
			const int adj = static_cast<int>(splat->weights[base + it->layer]) + residual;
			splat->weights[base + it->layer] = static_cast<uint8_t>(
				std::clamp(adj, 0, 255));
			m_terrain->MarkSplatDirty(it->chunkCoord);
		}
		m_splatSnapshot.clear();

		// Retire le mesh insert.
		if (m_insertedGuid != kInvalidMeshInsertGuid)
		{
			m_meshDoc->Remove(m_insertedGuid);
			m_insertedGuid = kInvalidMeshInsertGuid;
		}
	}
}
