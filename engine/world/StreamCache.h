#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace engine::core { class Config; }

namespace engine::world
{
	namespace terrain { struct TerrainChunk; struct TerrainLodChain; struct SplatMap; }
	namespace water   { struct WaterScene; }    // <- nouveau

	/// LRU cache for decompressed blobs keyed by asset/chunk file path (M10.3).
	/// Capacity 1–4 GB configurable via config; hit avoids re-IO when re-entering a zone.
	class StreamCache
	{
	public:
		StreamCache() = default;

		/// Initializes cache with max size in bytes from config (streaming.cache_size_mb, default 1024, clamp 1024–4096).
		void Init(const engine::core::Config& config);

		/// Returns cached blob for \p key if present and touches LRU; otherwise nullopt (caller may load from disk and Insert).
		std::optional<std::vector<uint8_t>> Lookup(std::string_view key);

		/// Inserts or replaces blob for \p key; evicts LRU entries if over capacity.
		void Insert(std::string_view key, const void* data, size_t size);

		/// Inserts or replaces blob for \p key from vector; evicts LRU entries if over capacity.
		void Insert(std::string_view key, const std::vector<uint8_t>& blob);

		/// Returns current total cached size in bytes.
		size_t GetCurrentSizeBytes() const { return m_currentSizeBytes; }
		/// Returns max capacity in bytes.
		size_t GetMaxSizeBytes() const { return m_maxSizeBytes; }
		/// Returns number of cache hits since last reset (for hit rate measurement).
		uint64_t GetHitCount() const { return m_hitCount; }
		/// Returns number of cache misses since last reset.
		uint64_t GetMissCount() const { return m_missCount; }
		/// Resets hit/miss counters (e.g. for periodic stats).
		void ResetStats() { m_hitCount = 0; m_missCount = 0; }

		/// Clears the whole cache, used by client zone preload when switching to another zone.
		void Clear();

		/// Charge le `terrain.bin` du chunk `(chunkX, chunkZ)` (M100.5). Tente le
		/// cache d'abord (clé `chunks/chunk_<i>_<j>/terrain.bin`) ; sur miss, lit
		/// le fichier `<paths.content>/chunks/chunk_<i>_<j>/terrain.bin` et
		/// l'insère dans le cache pour les futures lectures.
		/// \param config Source de la clé `paths.content` (défaut "game/data").
		/// \return shared_ptr<TerrainChunk> partagé, ou nullptr si le fichier
		/// est absent ou si la désérialisation a échoué (log d'erreur émis).
		std::shared_ptr<engine::world::terrain::TerrainChunk> LoadTerrainChunk(
			const engine::core::Config& config, int chunkX, int chunkZ);

		/// Charge le `terrain_lods.bin` du chunk `(chunkX, chunkZ)` (M100.8).
		/// Tente le cache d'abord (clé `chunks/chunk_<i>_<j>/terrain_lods.bin`),
		/// sinon lit le fichier disque et l'insère. Le fichier est optionnel :
		/// retourne nullptr sans warning si absent (les chunks neufs n'ont pas
		/// encore de LODs persistés tant qu'aucun OnCommit n'a tourné).
		std::shared_ptr<engine::world::terrain::TerrainLodChain> LoadTerrainLods(
			const engine::core::Config& config, int chunkX, int chunkZ);

		/// Charge le `splat.bin` du chunk `(chunkX, chunkZ)` (M100.9). Tente
		/// le cache (clé `chunks/chunk_<i>_<j>/splat.bin`), sinon lit le
		/// fichier disque et l'insère. Le fichier est optionnel pour les
		/// chunks neufs : retourne nullptr sans warning si absent.
		std::shared_ptr<engine::world::terrain::SplatMap> LoadSplatMap(
			const engine::core::Config& config, int chunkX, int chunkZ);

		/// Charge le `instances/water.bin` global de la zone (M100.13). Si
		/// fichier absent, retourne nullptr sans warning (cas premier lancement).
		/// \param zoneName réservé pour multi-zone (M100.34) — actuellement ignoré.
		std::shared_ptr<engine::world::water::WaterScene> LoadWater(
			const engine::core::Config& config, std::string_view zoneName);

	private:
		struct Entry
		{
			std::vector<uint8_t> blob;
			size_t sizeBytes = 0;
		};
		std::unordered_map<std::string, Entry> m_map;
		std::vector<std::string> m_lruOrder;
		size_t m_maxSizeBytes = 1024 * 1024 * 1024; // 1 GB default
		size_t m_currentSizeBytes = 0;
		uint64_t m_hitCount = 0;
		uint64_t m_missCount = 0;

		void EvictLruUntilWithinCapacity();
		void TouchLru(std::string_view key);
	};
}
