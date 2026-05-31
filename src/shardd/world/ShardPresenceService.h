#pragma once

// Service de présence unifié — Niveau 1 (shard-local).
//
// Autorité UNIQUE de la présence des joueurs connectés à CE shard. Remplace les
// comptabilités de présence dispersées (FriendSystem.m_presence, à terme
// GuildSystem.m_onlinePlayers) : ces systèmes s'y réfèrent au lieu de tenir
// chacun leur propre map (cf. docs/superpowers/specs/2026-05-31-unified-presence-service-design.md).
//
// Alimenté par ServerApp aux hooks login (HandleHello) / logout (HandleGoodbye,
// éviction) et sur changement de zone/niveau. C'est aussi la source du snapshot
// poussé au master via le heartbeat enrichi (Niveau 2 / web-portal).
//
// Thread-safe : muté sur le thread gameplay, lu aussi depuis le thread
// shard→master (heartbeat) — protégé par mutex.

#include "src/shared/network/ServerProtocol.h" // PresenceStatus

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace engine::server
{
	class ShardPresenceService
	{
	public:
		/// État de présence d'un joueur connecté à ce shard.
		struct Entry
		{
			uint64_t       accountId = 0;
			uint64_t       characterId = 0;
			std::string    characterName;
			uint32_t       level = 1;
			uint32_t       zoneId = 0;
			PresenceStatus status = PresenceStatus::Online;
		};

		/// Marque \p accountId en ligne (à l'entrée en jeu). Remplace toute entrée existante.
		void SetOnline(uint64_t accountId, uint64_t characterId, std::string characterName,
			uint32_t level, uint32_t zoneId, PresenceStatus status = PresenceStatus::Online);

		/// Retire la présence (déconnexion / éviction). No-op si inconnu.
		void SetOffline(uint64_t accountId);

		/// Met à jour la zone courante d'un joueur en ligne (no-op si inconnu).
		void UpdateZone(uint64_t accountId, uint32_t zoneId);

		/// Met à jour le niveau d'un joueur en ligne (no-op si inconnu).
		void UpdateLevel(uint64_t accountId, uint32_t level);

		/// True si \p accountId est connecté à ce shard.
		bool IsOnline(uint64_t accountId) const;

		/// Statut de présence (\ref PresenceStatus::Offline si inconnu).
		PresenceStatus GetStatus(uint64_t accountId) const;

		/// Copie de l'entrée pour \p accountId, ou nullopt si absent.
		std::optional<Entry> Get(uint64_t accountId) const;

		/// Sous-ensemble de \p candidates actuellement en ligne (pour amis/guilde).
		std::vector<uint64_t> OnlineAccountIdsAmong(const std::vector<uint64_t>& candidates) const;

		/// Copie de toutes les entrées (source du snapshot heartbeat → master).
		std::vector<Entry> Snapshot() const;

	private:
		mutable std::mutex m_mutex;
		std::unordered_map<uint64_t, Entry> m_byAccount;
	};
}
