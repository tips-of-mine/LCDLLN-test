#pragma once

// src/client/render/terrain_chunk/ChunkRuntime.h
//
// Cache LRU global du runtime terrain (M100 — PR Terrain Chunk Runtime).
// Tracke les chunks visibles, leur résidence GPU, et applique une politique
// d'éviction LRU bornée par un budget GPU configurable. Les chunks dans
// `Active` ou `Visible` ring ne sont jamais évincés (seuls les chunks `Far`
// peuvent l'être).
//
// Pure CPU : aucune dépendance Vulkan. Les caches concrets (mesh, splat)
// stockent leurs ressources GPU séparément et déclarent leur taille via
// `AddResidentBytes`.

#include "src/client/world/WorldModel.h" // GlobalChunkCoord, ChunkRing

#include <cstdint>
#include <list>
#include <unordered_map>
#include <vector>

namespace engine::render::terrain_chunk
{
	/// Identifiant opaque d'une entrée résidente dans `ChunkRuntime`. Utilisé
	/// par les caches concrets (mesh, splat) pour lier leur ressource GPU au
	/// chunk. Stable tant que le slot existe ; invalidé à l'éviction.
	using ChunkSlotId = uint64_t;

	/// État d'un chunk vu par le runtime : `Resident` (mesh+splat uploadés),
	/// `Pending` (lecture disque ou upload en cours), `Skipped` (fichiers
	/// `terrain.bin`/`splat.bin` absents — legacy le dessinera).
	enum class ChunkResidency : uint8_t { Resident, Pending, Skipped };

	/// Cache LRU global du runtime terrain. Voir docstring fichier.
	///
	/// Contraintes thread/timing : appelé uniquement depuis le main thread (le
	/// rendu Vulkan n'est pas thread-safe à ce niveau).
	class ChunkRuntime
	{
	public:
		struct Config
		{
			/// Budget GPU total (mesh + splat-maps) en octets. Au-delà, eviction
			/// LRU des chunks Far. Default 256 MB.
			size_t gpuBudgetBytes = 256ull * 1024ull * 1024ull;
		};

		/// Initialise le runtime avec la config donnée. Vide tout état précédent.
		void Init(const Config& cfg);

		/// Retourne le slot pour `coord`. Si pas encore résident, alloue un
		/// nouveau slot (état `Pending`) que les caches concrets devront
		/// remplir via `AddResidentBytes(slot, sizeBytes)`. Idempotent.
		/// \return slot stable pour ce coord (réutilisé entre appels).
		ChunkSlotId GetOrAllocateSlot(engine::world::GlobalChunkCoord coord);

		/// Marque le slot comme résident en ajoutant `additionalBytes` à sa
		/// résidence trackée. Le caller (cache mesh ou splat) fournit la
		/// taille en bytes consommée par sa propre ressource. La somme des
		/// tailles déclarées par tous les caches est trackée vs `gpuBudgetBytes`.
		void AddResidentBytes(ChunkSlotId slot, size_t additionalBytes);

		/// Met à jour le ring du chunk (Active/Visible/Far). Appelé chaque
		/// frame depuis `TerrainChunkRenderer::RenderVisibleChunks` via
		/// `World::GetRingForChunk(coord)`.
		void UpdateRing(engine::world::GlobalChunkCoord coord, engine::world::ChunkRing ring);

		/// Touche le slot (déplace en tête de la liste LRU). Appelé chaque
		/// frame pour les chunks dessinés afin de les protéger de l'eviction.
		void Touch(ChunkSlotId slot);

		/// Demande la liste des chunks à évincer pour respecter le budget.
		/// Itère depuis le moins récemment utilisé en sautant les chunks
		/// Active/Visible. Décrémente `m_residentBytes` au fur et à mesure
		/// (le caller libère ensuite les ressources Vulkan via les caches).
		/// \return liste des slots à libérer (peut être vide).
		std::vector<ChunkSlotId> CollectEvictionsForBudget();

		/// Retire un slot après que tous les caches ont libéré leurs ressources.
		/// Le slot id est invalidé.
		void RemoveSlot(ChunkSlotId slot);

		size_t GetResidentBytes() const { return m_residentBytes; }
		size_t GetSlotCount() const { return m_slots.size(); }
		ChunkResidency GetResidency(ChunkSlotId slot) const;
		engine::world::ChunkRing GetRingForSlot(ChunkSlotId slot) const;

		/// Retourne le coord associé au slot. Utilisé par
		/// `TerrainChunkRenderer::Tick` pour libérer les entrées des caches
		/// concrets (mesh, splat) au moment de l'éviction. Retourne `{0,0}`
		/// si le slot n'existe pas (caller doit avoir vérifié au préalable
		/// que le slot vient de `CollectEvictionsForBudget`).
		engine::world::GlobalChunkCoord GetCoordForSlot(ChunkSlotId slot) const;

	private:
		struct Slot
		{
			engine::world::GlobalChunkCoord coord{0, 0};
			engine::world::ChunkRing ring = engine::world::ChunkRing::Far;
			size_t residentBytes = 0;
			ChunkResidency residency = ChunkResidency::Pending;
		};

		Config m_cfg;
		std::unordered_map<ChunkSlotId, Slot> m_slots;
		std::list<ChunkSlotId> m_lru; ///< front = most recent, back = candidate eviction
		std::unordered_map<uint64_t, ChunkSlotId> m_coordToSlot; ///< pack(coord) -> slot
		ChunkSlotId m_nextSlotId = 1;
		size_t m_residentBytes = 0;

		static uint64_t PackCoord(engine::world::GlobalChunkCoord c);
	};
}
