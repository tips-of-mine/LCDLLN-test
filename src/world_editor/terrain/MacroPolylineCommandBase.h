#pragma once

#include "src/world_editor/core/CommandStack.h"
#include "src/world_editor/terrain/PolylineMacroCore.h"

namespace engine::editor::world
{
	class TerrainDocument;

	/// Base commune Mountain Range / Valley Chain (M100.35). Porte un lot de
	/// `SparseChunkDeltas` (deltas par chunk indexés par cellIndex linéaire) et
	/// implémente `Execute` (delta += height) / `Undo` (delta -= height) de
	/// manière strictement symétrique.
	///
	/// La commande est immuable après construction (pas de coalescing : un
	/// `Apply` outil = une entrée d'historique).
	///
	/// Effets de bord à chaque Execute/Undo :
	///   - Mute `TerrainChunk::heights` via `TerrainDocument::Find` (sans
	///     `EnsureLoaded` : la commande suppose les chunks chargés au moment
	///     de la construction, ce qui est vrai puisque le rasterizer travaille
	///     sur les chunks que l'outil a déjà chargés).
	///   - Clamp les hauteurs dans `[kTerrainHeightMinMeters,
	///     kTerrainHeightMaxMeters]` et stocke le delta effectif (réversible).
	///   - Appelle `TerrainDocument::MarkDirty(coord)` pour chaque chunk touché.
	///   - Appelle `TerrainDocument::OnCommit(coord)` pour enqueue la
	///     régénération LOD (M100.8) en arrière-plan.
	///
	/// Contraintes thread/timing : main thread (mute le TerrainDocument et la
	/// pile undo via CommandStack).
	class MacroPolylineCommandBase : public ICommand
	{
	public:
		MacroPolylineCommandBase(TerrainDocument& doc,
			SparseChunkDeltas deltas,
			const char* label);

		~MacroPolylineCommandBase() override = default;

		const char* GetLabel()           const override { return m_label; }
		size_t      GetMemoryFootprint() const override;

		void Execute() override;
		void Undo()    override;

		/// Accès lecture seule pour les tests (compte de chunks, agrégats).
		const SparseChunkDeltas& Deltas() const { return m_deltas; }

	private:
		/// Applique tous les deltas multipliés par `sign` (+1 pour Execute,
		/// -1 pour Undo). Met à jour `MarkDirty` + `OnCommit` une seule fois
		/// par chunk touché.
		void ApplyDeltas(float sign);

		TerrainDocument*  m_doc = nullptr;
		SparseChunkDeltas m_deltas;
		const char*       m_label = "Macro Polyline";
	};
}
