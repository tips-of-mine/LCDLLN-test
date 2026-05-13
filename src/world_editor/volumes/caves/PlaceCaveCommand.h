#pragma once

#include "src/world_editor/core/CommandStack.h"
#include "src/world_editor/terrain/PolylineMacroCore.h"  // SparseChunkDeltas
#include "src/world_editor/volumes/MeshInsertInstance.h"
#include "src/world_editor/volumes/caves/CaveCamouflage.h"

#include <cstdint>
#include <cstddef>
#include <optional>

namespace engine::editor::world
{
	class TerrainDocument;
}

namespace engine::editor::world::volumes
{
	class MeshInsertDocument;
}

namespace engine::editor::world::volumes::caves
{
	/// Commande "Place Cave" (M100.40 MVP). Regroupe :
	///   - l'insertion d'une `MeshInsertInstance` dans le `MeshInsertDocument`,
	///   - optionnellement, un patch splat de camouflage « Rocher » autour
	///     de l'entrée (poids ajoutés à la couche cible, ré-équilibrés avec
	///     les autres couches pour préserver la somme=255 du splat M100.9).
	///
	/// Note MVP : les "props auto rochers" (M100.40 spec §B.2) demandent
	/// `InstanceDocument` de M100.17 qui n'est pas codé ; on les omet ici
	/// avec un TODO dans le tool.
	///
	/// Undo strict :
	///   - retire l'instance (par guid),
	///   - restaure les poids splat précédents (snapshot capturé à
	///     `Execute` pour chaque cellule touchée).
	class PlaceCaveCommand final : public ICommand
	{
	public:
		struct Data
		{
			MeshInsertInstance instance;
			std::optional<CaveSplatPatch> splatPatch;
		};

		PlaceCaveCommand(MeshInsertDocument& meshDoc,
			engine::editor::world::TerrainDocument& terrain,
			Data data);

		const char* GetLabel()           const override { return "Place Cave"; }
		size_t      GetMemoryFootprint() const override;

		void Execute() override;
		void Undo()    override;

		const Data& Get() const { return m_data; }

	private:
		struct SplatSnapshotCell
		{
			engine::world::GlobalChunkCoord chunkCoord;
			uint32_t cellIndex;
			uint8_t  layer;
			uint8_t  prevWeight;     // poids avant Execute
			uint8_t  prevOtherSum;   // 255 - prevWeight (pour restoration)
		};

		MeshInsertDocument*                     m_meshDoc = nullptr;
		engine::editor::world::TerrainDocument* m_terrain = nullptr;
		Data                                    m_data;
		uint64_t                                m_insertedGuid = 0u;
		std::vector<SplatSnapshotCell>          m_splatSnapshot;
	};
}
