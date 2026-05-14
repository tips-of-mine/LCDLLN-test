#pragma once

#include "src/client/world/water/WaterSurfaces.h"
#include "src/world_editor/core/CommandStack.h"
#include "src/world_editor/terrain/PolylineMacroCore.h"  // SparseChunkDeltas
#include "src/world_editor/water/OceanSettings.h"

#include <optional>
#include <string>

namespace engine::editor::world
{
	class TerrainDocument;
	class WaterDocument;

	/// Commande Coastline (M100.37). Apply :
	///   - écrit `OceanSettings` complètes dans `WaterDocument`,
	///   - insère (ou met à jour in-place) un `LakeInstance` `isOcean=true`,
	///   - applique les `SparseChunkDeltas` de smoothing + falaises au
	///     `TerrainDocument`,
	///   - marque les chunks touchés dirty et invoque `OnCommit` pour la
	///     régen LOD M100.8.
	///
	/// Undo strict :
	///   - restaure le snapshot `OceanSettings` précédent,
	///   - retire le `LakeInstance` océan inséré OU restaure le précédent,
	///   - inverse les deltas heightmap (delta × −1).
	///
	/// Note MVP (M100.37) : la passe "plage automatique" (splat sand) est
	/// volontairement non-câblée dans cette commande — elle nécessite un
	/// snapshot par cellule splat pour Undo et sera traitée par un follow-up.
	/// Le flag `beachSplatEnabled` est conservé pour traçabilité dans l'UI.
	class CoastlineCommand final : public ICommand
	{
	public:
		struct ApplyData
		{
			OceanSettings newOcean;
			OceanSettings previousOcean;
			/// LakeInstance océan à insérer si absent (rempli si la zone
			/// n'avait pas encore d'océan). Si présent, on met à jour
			/// in-place le lac existant via `m_existingOceanIndex`.
			std::optional<engine::world::water::LakeInstance> oceanToInsert;
			/// Index dans `scene.lakes` du lac océan existant. -1 si absent.
			int               existingOceanIndex = -1;
			/// Snapshot des paramètres du lac océan existant (waterLevelY,
			/// bottomColor, turbidity) pour pouvoir les restaurer à l'Undo
			/// quand on a fait une mise à jour in-place. Renommé pour ne
			/// pas masquer `previousOcean` au-dessus.
			std::optional<engine::world::water::LakeInstance> previousOceanLake;
			SparseChunkDeltas heightmapDeltas;  // smoothing + falaises cumulés
		};

		CoastlineCommand(TerrainDocument& terrain, WaterDocument& water,
			ApplyData data);

		const char* GetLabel()           const override { return "Coastline"; }
		size_t      GetMemoryFootprint() const override;

		void Execute() override;
		void Undo()    override;

		const ApplyData& Data() const { return m_data; }

	private:
		void ApplyHeightDeltas(float sign);

		TerrainDocument* m_terrain = nullptr;
		WaterDocument*   m_water   = nullptr;
		ApplyData        m_data;
		/// Index réel dans `scene.lakes` du lac océan qu'on a inséré à
		/// l'Apply (utilisé pour le retirer à l'Undo). -1 si aucun insert.
		int              m_insertedIndex = -1;
	};
}
