#pragma once

#include "src/world_editor/world/CommandStack.h"
#include "src/world_editor/world/TerrainBrush.h"
#include "src/world_editor/world/TerrainDocument.h"

#include <vector>

namespace engine::editor::world
{
	/// Commande undo/redo encapsulant un stamp terrain (M100.7). Réutilise les
	/// types `TerrainSculptDeltaChunk` / `TerrainSculptDeltaCell` introduits par
	/// M100.6 — un stamp est sémantiquement un brushstroke ponctuel : la
	/// résolution de l'opérateur (Add/Replace/Max/Min) est faite au moment où
	/// `TerrainStampTool::OnClickAt` calcule le delta. La commande ne stocke que
	/// des deltas additifs, donc `Execute` est un simple `+= delta` (et `Undo`
	/// un `-= delta`), strictement symétrique.
	///
	/// Contrairement à `TerrainSculptCommand`, il n'y a pas de mergeKey : un
	/// stamp = une commande dans l'historique (un click = une entrée de pile,
	/// pas de coalescing).
	///
	/// Contraintes thread/timing : main thread (mute le `TerrainDocument`).
	class TerrainStampCommand final : public ICommand
	{
	public:
		/// Construit la commande avec le document cible et un lot de deltas
		/// déjà résolus (en mètres, signés selon le mode de stamp choisi).
		/// \param doc    Document terrain (référence — durée de vie >
		///               commande, garantie par le shell éditeur).
		/// \param deltas Cellules modifiées (groupées par chunk). Movés.
		TerrainStampCommand(TerrainDocument& doc,
			std::vector<TerrainSculptDeltaChunk> deltas);

		const char* GetLabel() const override { return "Stamp"; }
		size_t GetMemoryFootprint() const override;

		/// Applique `heights[idx] += delta` cellule par cellule, puis appelle
		/// `MarkDirty` + `OnCommit` sur chaque chunk touché. Idempotent par
		/// rapport à `Undo` : Execute → Undo restaure l'état initial.
		void Execute() override;

		/// Inverse de `Execute` : `heights[idx] -= delta` + MarkDirty/OnCommit.
		void Undo() override;

		/// Accès lecture seule pour les tests.
		const std::vector<TerrainSculptDeltaChunk>& Deltas() const { return m_deltas; }

	private:
		TerrainDocument*                     m_doc = nullptr;
		std::vector<TerrainSculptDeltaChunk> m_deltas;
		bool                                 m_applied = false;
	};
}
