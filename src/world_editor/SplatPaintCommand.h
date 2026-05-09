#pragma once

#include "src/world_editor/world/CommandStack.h"
#include "src/world_editor/world/TerrainDocument.h"

#include <array>
#include <cstdint>
#include <vector>

namespace engine::editor::world
{
	/// Cellule modifiée par un brushstroke splat (M100.10). Contrairement au
	/// `TerrainSculptDeltaCell` qui stocke un simple `delta`, le splat impose
	/// l'invariant somme=255 par cellule sur 8 layers : on ne peut donc pas
	/// décrire un changement uniquement par addition. On stocke explicitement
	/// les 8 poids `prev` (avant brushstroke) et les 8 poids `next` (après) ;
	/// l'`Undo` recopie `prev`, l'`Execute` recopie `next`.
	struct SplatDeltaCell
	{
		uint16_t x = 0;
		uint16_t z = 0;
		std::array<uint8_t, 8> prev{}; ///< Poids avant brushstroke (somme=255).
		std::array<uint8_t, 8> next{}; ///< Poids après brushstroke (somme=255).
	};

	/// Lot de cellules splat modifiées dans un même chunk. Plusieurs lots
	/// cohabitent dans une `SplatPaintCommand` quand le brushstroke chevauche
	/// plusieurs chunks (couture inter-chunks identique au sculpt M100.6).
	struct SplatDeltaChunk
	{
		engine::world::GlobalChunkCoord coord{0, 0};
		std::vector<SplatDeltaCell> cells;
	};

	/// `ICommand` qui applique un delta sparse multi-chunk sur la splat-map
	/// (M100.10). Maintient l'invariant somme=255 par cellule (le `SplatPaintTool`
	/// précalcule les `next[8]` avec la renormalisation). `mergeKey` non-nul
	/// stable durée du brushstroke ; deux ticks consécutifs partagent la même
	/// clé et sont fusionnés via `TryMerge` (par chunk : pour la même cellule,
	/// `next` prend la valeur la plus récente, `prev` reste celui d'origine).
	///
	/// Contraintes thread/timing : main thread (mute le `TerrainDocument`).
	class SplatPaintCommand final : public ICommand
	{
	public:
		/// Construit la commande avec un lot de deltas et un mergeKey stable.
		/// \param doc       Document terrain (référence — durée de vie >
		///                  commande, garantie par l'éditeur shell).
		/// \param deltas    Cellules modifiées (groupées par chunk).
		/// \param strokeKey Identifiant du brushstroke (0 = pas de coalescing).
		SplatPaintCommand(TerrainDocument& doc,
			std::vector<SplatDeltaChunk> deltas,
			CommandMergeKey strokeKey);

		const char* GetLabel() const override { return "Splat paint"; }
		size_t GetMemoryFootprint() const override;
		CommandMergeKey GetMergeKey() const override { return m_mergeKey; }

		/// Réapplique les deltas (`weights[idx*8 + l] = next[l]`) puis appelle
		/// `MarkSplatDirty` sur chaque chunk touché. Force `EnsureSplatLoaded`
		/// au cas où la commande aurait été mise sur la pile redo et que le
		/// chunk n'est plus en RAM (rare mais possible si l'utilisateur
		/// décharge un chunk entre Undo et Redo).
		void Execute() override;

		/// Inverse de `Execute` : `weights[idx*8 + l] = prev[l]` + MarkSplatDirty.
		void Undo() override;

		/// Fusionne `other` dans `*this` si c'est une `SplatPaintCommand` avec
		/// le même `mergeKey` non-nul. Pour chaque chunk de `other`, on cherche
		/// le chunk correspondant dans `*this` ; pour chaque cellule, si elle
		/// existe déjà on met à jour `next` (last-write-wins), sinon on append.
		/// `prev` reste celui d'origine (la valeur d'avant le tout premier tick).
		bool TryMerge(const ICommand& other) override;

		/// Accès lecture seule pour les tests (snapshot des deltas).
		const std::vector<SplatDeltaChunk>& Deltas() const { return m_deltas; }

	private:
		TerrainDocument*               m_doc = nullptr;
		std::vector<SplatDeltaChunk>   m_deltas;
		CommandMergeKey                m_mergeKey = 0;
	};
}
