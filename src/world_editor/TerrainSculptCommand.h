#pragma once

#include "src/world_editor/CommandStack.h"
#include "src/world_editor/TerrainBrush.h"
#include "src/world_editor/TerrainDocument.h"

#include <vector>

namespace engine::editor::world
{
	/// Commande undo/redo encapsulant un brushstroke complet (M100.6). Stocke
	/// un delta sparse par chunk touché. Le mergeKey est non-nul : tous les
	/// ticks d'un même geste (press → release du bouton) partagent la même
	/// valeur, ce qui permet à `CommandStack::Push` de fusionner les commandes
	/// successives via `TryMerge`.
	///
	/// Contraintes thread/timing : main thread (mute le `TerrainDocument`).
	class TerrainSculptCommand final : public ICommand
	{
	public:
		/// Construit la commande avec un lot de deltas et un mergeKey stable.
		/// \param doc       Document terrain (référence — durée de vie >
		///                  commande, garantie par l'éditeur shell).
		/// \param deltas    Cellules modifiées (groupées par chunk).
		/// \param mergeKey  Identifiant du brushstroke (0 = pas de coalescing).
		TerrainSculptCommand(TerrainDocument& doc,
			std::vector<TerrainSculptDeltaChunk> deltas,
			CommandMergeKey mergeKey);

		const char* GetLabel() const override { return "Sculpt brush stroke"; }
		size_t GetMemoryFootprint() const override;
		CommandMergeKey GetMergeKey() const override { return m_mergeKey; }

		/// Réapplique les deltas (`heights[idx] += delta`) puis appelle
		/// `MarkDirty` + `OnCommit` sur chaque chunk touché.
		void Execute() override;

		/// Inverse de `Execute` : `heights[idx] -= delta` + MarkDirty/OnCommit.
		void Undo() override;

		/// Fusionne `other` dans `*this` si c'est un `TerrainSculptCommand`
		/// avec le même `mergeKey`. La fusion concatène les cellules par
		/// chunk (les doublons sont conservés — `Execute`/`Undo` les
		/// recomposent par addition / soustraction).
		bool TryMerge(const ICommand& other) override;

		/// Accès lecture seule pour les tests (snapshot des deltas).
		const std::vector<TerrainSculptDeltaChunk>& Deltas() const { return m_deltas; }

	private:
		TerrainDocument*                        m_doc = nullptr;
		std::vector<TerrainSculptDeltaChunk>    m_deltas;
		CommandMergeKey                         m_mergeKey = 0;
	};
}
