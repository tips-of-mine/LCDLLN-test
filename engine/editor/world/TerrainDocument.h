#pragma once

#include "engine/world/WorldModel.h"
#include "engine/world/terrain/TerrainChunk.h"

#include <memory>
#include <string>
#include <unordered_map>

namespace engine::core { class Config; }

namespace engine::editor::world
{
	/// Wrapper éditeur d'un ensemble de `TerrainChunk` mutables, indexé par
	/// `GlobalChunkCoord` (M100.5). Le document possède les chunks chargés en
	/// RAM et tracke ceux qui ont été modifiés depuis le dernier save.
	///
	/// M100.8 étendra cette classe avec un hook `OnCommit` qui enqueue le
	/// `TerrainLodWorker` à chaque commit d'`ICommand` terrain.
	///
	/// Contraintes thread/timing : toutes les méthodes publiques sont appelées
	/// depuis le main thread (mêmes contraintes que `CommandStack` /
	/// `WorldEditorShell`).
	class TerrainDocument
	{
	public:
		/// Charge le chunk `(chunkX, chunkZ)` depuis disque si présent dans
		/// `<paths.content>/chunks/chunk_<i>_<j>/terrain.bin`, sinon crée un
		/// chunk plat à 0 m. Met le chunk en cache RAM dans `m_chunks` et
		/// retourne le shared_ptr stable. Idempotent : appels successifs avec
		/// les mêmes coords retournent le même pointeur.
		/// \param config Source de la clé `paths.content` pour la lecture
		/// disque (défaut "game/data").
		std::shared_ptr<engine::world::terrain::TerrainChunk> EnsureLoaded(
			const engine::core::Config& config, int chunkX, int chunkZ);

		/// Marque le chunk `coord` comme modifié (dirty). Utilisé par les
		/// commandes terrain (sculpt, stamp, …) pour signaler qu'une écriture
		/// disque sera nécessaire au prochain `SaveDirtyToDisk`.
		void MarkDirty(engine::world::GlobalChunkCoord coord);

		/// Retourne true si au moins un chunk est dirty depuis le dernier save.
		bool HasDirtyChunks() const;

		/// Sauvegarde sur disque tous les chunks dirty, sous
		/// `<paths.content>/chunks/chunk_<i>_<j>/terrain.bin`. Crée le dossier
		/// parent au besoin.
		/// \param config Source de la clé `paths.content`.
		/// \return nombre de chunks effectivement écrits.
		size_t SaveDirtyToDisk(const engine::core::Config& config);

		/// Accès lecture seule pour les tests : retourne le chunk déjà chargé
		/// pour `coord`, ou nullptr si non chargé. Pas de chargement disque.
		std::shared_ptr<engine::world::terrain::TerrainChunk> Find(
			engine::world::GlobalChunkCoord coord) const;

		/// Nombre de chunks actuellement résidents en RAM.
		size_t LoadedChunkCount() const { return m_chunks.size(); }

	private:
		struct ChunkSlot
		{
			std::shared_ptr<engine::world::terrain::TerrainChunk> chunk;
			bool dirty = false;
		};

		/// Empaquette `(x, z)` int32 dans un uint64 pour servir de clé hash.
		static uint64_t PackCoord(engine::world::GlobalChunkCoord c);

		std::unordered_map<uint64_t, ChunkSlot> m_chunks;
	};
}
