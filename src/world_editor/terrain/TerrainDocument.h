#pragma once

#include "src/client/world/WorldModel.h"
#include "src/client/world/terrain/SplatMap.h"
#include "src/client/world/terrain/TerrainChunk.h"
#include "src/client/world/terrain/TerrainLodWorker.h"

#include <functional>
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

		/// M100.46 — Vide intégralement le cache RAM (chunks + splats).
		/// Les prochains `EnsureLoaded` / `EnsureSplatLoaded` reconstruiront
		/// des chunks plats à 0 m. Utilisé par `WorldMapEditDocumentReset`
		/// avant l'exécution d'un zone preset sur une zone non vide.
		void Reset() noexcept
		{
			m_chunks.clear();
			m_splats.clear();
		}

		/// Initialise une zone neuve : vide le cache puis alloue
		/// `chunksPerAxis * chunksPerAxis` chunks plats (257×257, 1 m/cellule)
		/// à `flatHeightMeters`, indexés de (0,0) à (chunksPerAxis-1,
		/// chunksPerAxis-1). **Tous marqués dirty** pour qu'un
		/// `SaveDirtyToDisk` ultérieur les persiste sur disque — les chunks
		/// sont la source de vérité de la zone (le `r16h` n'en est qu'un cache
		/// GPU reconstruit). No-op si `chunksPerAxis <= 0`.
		/// \param chunksPerAxis nombre de chunks par axe (empreinte d'édition).
		/// \param flatHeightMeters hauteur uniforme initiale, en mètres monde.
		/// Contrainte thread : main thread (comme le reste de la classe).
		void InitFlatZone(int chunksPerAxis, float flatHeightMeters);

		/// Attache un `TerrainLodWorker` pour la régénération asynchrone des
		/// LODs (M100.8). Le caller possède le worker (ne le détruit pas pendant
		/// la durée de vie du document). `contentRoot` est mémorisé pour que le
		/// callback worker puisse écrire `terrain_lods.bin` au bon emplacement.
		void AttachLodWorker(engine::world::terrain::TerrainLodWorker* worker,
			std::string contentRoot);

		/// À appeler après chaque commit d'`ICommand` qui modifie le chunk
		/// `coord` (M100.8). Si un worker est attaché, enqueue une demande de
		/// régénération LOD asynchrone ; sinon no-op.
		/// Effet de bord : incrémente la génération atomique du chunk dans le
		/// worker, garantissant que les jobs périmés sont jetés.
		void OnCommit(engine::world::GlobalChunkCoord coord);

		/// Charge la `SplatMap` du chunk `(chunkX, chunkZ)` depuis disque
		/// (`<paths.content>/chunks/chunk_<i>_<j>/splat.bin`) si présente,
		/// sinon crée une splat-map uniforme layer 0 (= "dirt"). Met en cache
		/// dans `m_splats`. Idempotent (M100.9).
		std::shared_ptr<engine::world::terrain::SplatMap> EnsureSplatLoaded(
			const engine::core::Config& config, int chunkX, int chunkZ);

		/// Marque la splat-map du chunk `coord` comme modifiée (M100.9).
		void MarkSplatDirty(engine::world::GlobalChunkCoord coord);

		/// Sauvegarde sur disque toutes les splat-maps dirty (M100.9).
		/// \return nombre de splat-maps effectivement écrites.
		size_t SaveDirtySplatToDisk(const engine::core::Config& config);

		/// Accès lecture seule à la splat-map déjà chargée, ou nullptr (M100.9).
		std::shared_ptr<engine::world::terrain::SplatMap> FindSplat(
			engine::world::GlobalChunkCoord coord) const;

		/// Callback invoqué par `OnCommit(coord)` à chaque mutation
		/// committée d'un chunk (M100.46+ pont CPU → GPU). Permet à un
		/// observer (Engine) de marquer la heightmap GPU à re-synchroniser.
		///
		/// Effet de bord : le callback est invoqué **synchrone** depuis le
		/// thread qui appelle `OnCommit`. Doit donc être très court (set un
		/// flag, ne pas faire d'IO ni de Vulkan call) — la synchro lourde
		/// est repoussée au prochain tick Engine.
		using OnChunkChangedCallback = std::function<void(engine::world::GlobalChunkCoord)>;
		void SetOnChunkChanged(OnChunkChangedCallback cb) { m_onChunkChanged = std::move(cb); }

		/// Itère **tous les chunks actuellement chargés** en RAM, appelant
		/// `visitor(coord, chunk_ptr)` pour chaque. Utilisé par
		/// `Engine::SyncWorldEditorHeightmapFromDocument` pour pousser tous
		/// les chunks au GPU (et pas seulement les 2×2 du coin SW).
		using ChunkVisitor = std::function<void(engine::world::GlobalChunkCoord,
			const std::shared_ptr<engine::world::terrain::TerrainChunk>&)>;
		void ForEachLoadedChunk(const ChunkVisitor& visitor) const;

	private:
		struct ChunkSlot
		{
			std::shared_ptr<engine::world::terrain::TerrainChunk> chunk;
			bool dirty = false;
		};

		/// Empaquette `(x, z)` int32 dans un uint64 pour servir de clé hash.
		static uint64_t PackCoord(engine::world::GlobalChunkCoord c);

		std::unordered_map<uint64_t, ChunkSlot> m_chunks;
		engine::world::terrain::TerrainLodWorker* m_lodWorker = nullptr;
		std::string m_contentRootForLods;
		OnChunkChangedCallback m_onChunkChanged;

		/// Slot splat-map (M100.9) parallèle à `ChunkSlot` mais sur un map
		/// distinct pour éviter de réécrire EnsureLoaded/MarkDirty/Save.
		struct SplatSlot
		{
			std::shared_ptr<engine::world::terrain::SplatMap> splat;
			bool dirty = false;
		};
		std::unordered_map<uint64_t, SplatSlot> m_splats;
	};
}
