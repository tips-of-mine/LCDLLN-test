#pragma once
// CMANGOS.05 (Phase 2.05d) — VMapStreamer + ManagedModel : streaming
// dynamique des tiles vmap. Acquire au passage d'une cellule en mode
// "Active" (cf. GridState), Release quand tous les joueurs sont
// partis. Decharge differee pour eviter le ping-pong load/unload.
//
// **Design** :
//   - Un tile est identifie par une cle string (typiquement un chemin
//     relatif type "zone1/tile_0_0.vmap"). Le streamer ne connait pas
//     l'arborescence ; un loader callback fourni par le caller
//     transforme la cle → buffer .vmap (typiquement via FileSystem).
//   - ManagedModel = VMapManager + refcount + last-zero-ref timestamp.
//     Quand refcount tombe a 0, on demarre un timer ; apres
//     `releaseDelay`, le tile est decharge.
//   - Le streamer expose `Acquire(key)` et `Release(key)`. Il faut
//     appeler `Tick(now)` regulierement pour declencher les decharges.
//
// **Hors scope** : limite par memoire RAM/GPU (audit §8 "DynamicTree
// memory" — pas applicable au vmap statique). Eviction LRU automatique
// si on depasse `maxLoadedTiles` viendra plus tard.

#include "src/shardd/internals/vmap/VMapManager.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace engine::server::shard::vmap
{
	/// Loader callback : prend une cle de tile, retourne un buffer
	/// `.vmap`. Doit retourner un buffer vide si la cle est inconnue.
	using VMapTileLoader = std::function<std::vector<uint8_t>(std::string_view key)>;

	struct VMapStreamerConfig
	{
		using Duration = std::chrono::steady_clock::duration;
		/// Delai apres `refcount = 0` avant decharge effective. Default
		/// 30s : un joueur qui sort puis rentre dans la cellule
		/// (ping-pong au bord d'un chunk) ne paie pas le cout de
		/// reload.
		Duration releaseDelay = std::chrono::seconds(30);
	};

	/// VMapManager wrappe + refcount + timer. Possede par le streamer.
	class ManagedModel
	{
	public:
		using TimePoint = std::chrono::steady_clock::time_point;

		ManagedModel() = default;

		bool LoadFromBuffer(std::span<const uint8_t> blob)
		{
			return m_manager.LoadTile(blob);
		}

		const VMapManager& Manager() const noexcept { return m_manager; }
		VMapManager& Manager() noexcept { return m_manager; }

		uint32_t RefCount() const noexcept { return m_refCount; }
		void IncRef() noexcept
		{
			++m_refCount;
			m_zeroRefSince = TimePoint{};
		}
		/// Decremente. Si retombe a 0, demarre le timer no-ref a \p now.
		void DecRef(TimePoint now) noexcept
		{
			if (m_refCount > 0)
				--m_refCount;
			if (m_refCount == 0)
				m_zeroRefSince = now;
		}

		/// True si refcount = 0 ET le timer "no-ref" est ecoule depuis
		/// au moins \p delay.
		bool ShouldRelease(TimePoint now, VMapStreamerConfig::Duration delay) const noexcept
		{
			if (m_refCount > 0) return false;
			if (m_zeroRefSince == TimePoint{}) return false;
			return (now - m_zeroRefSince) >= delay;
		}

	private:
		VMapManager m_manager;
		uint32_t    m_refCount = 0;
		TimePoint   m_zeroRefSince{};  ///< 0 si refcount > 0
	};

	class VMapStreamer
	{
	public:
		using TimePoint = ManagedModel::TimePoint;

		VMapStreamer() = default;

		/// Configure le loader (obligatoire avant Acquire).
		void SetLoader(VMapTileLoader loader) { m_loader = std::move(loader); }

		void SetConfig(VMapStreamerConfig cfg) noexcept { m_cfg = cfg; }
		const VMapStreamerConfig& Config() const noexcept { return m_cfg; }

		/// Acquire une reference sur le tile \p key. Charge si pas encore
		/// en RAM. Retourne pointeur vers le ManagedModel ou nullptr si
		/// le loader echoue.
		ManagedModel* Acquire(std::string_view key);

		/// Release la reference. Le tile reste en RAM jusqu'a ce que
		/// `Tick(now)` decharge apres `releaseDelay`.
		void Release(std::string_view key, TimePoint now);

		/// Decharge tous les tiles dont le timer "no-ref" a expire.
		/// Retourne le nombre de tiles decharges.
		size_t Tick(TimePoint now);

		/// Nombre de tiles actuellement en RAM (compte tout, ref ou pas).
		size_t LoadedTileCount() const noexcept { return m_tiles.size(); }

		/// Lookup direct sans bumping refcount (lectures).
		const ManagedModel* Find(std::string_view key) const noexcept
		{
			auto it = m_tiles.find(std::string(key));
			return (it == m_tiles.end()) ? nullptr : it->second.get();
		}

		/// Reset complet (decharge tous les tiles immediatement).
		void Clear() { m_tiles.clear(); }

	private:
		VMapTileLoader     m_loader;
		VMapStreamerConfig m_cfg{};
		std::unordered_map<std::string, std::unique_ptr<ManagedModel>> m_tiles;
	};
}
