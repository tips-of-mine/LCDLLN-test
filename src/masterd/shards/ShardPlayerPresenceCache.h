#pragma once

#include "src/shared/network/ShardPayloads.h"

#include <cstdint>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <vector>

namespace engine::server
{
	/// Cache en mémoire de la présence enrichie des joueurs en jeu, alimenté par les
	/// heartbeats enrichis shard→master (protocole v9). Clé = accountId.
	///
	/// Source de vérité volontairement volatile : aucune persistance DB. Le shard
	/// remonte périodiquement l'ensemble de ses joueurs ; \ref Update remplace donc
	/// l'intégralité des entrées de ce shard à chaque heartbeat (un joueur disparu du
	/// rapport sort naturellement du cache). \ref Clear purge un shard tombé.
	///
	/// Thread-safe : le heartbeat est traité sur le thread réseau master, la lecture
	/// (\ref Get / \ref Snapshot) depuis le thread du HealthEndpoint.
	class ShardPlayerPresenceCache
	{
	public:
		struct Entry
		{
			uint64_t accountId = 0;
			uint64_t characterId = 0;
			uint32_t level = 0;
			uint32_t zoneId = 0;
			uint32_t shardId = 0;
		};

		/// Remplace l'ensemble des entrées appartenant à \a shardId par \a players.
		/// Les joueurs absents du nouveau rapport (mais présents avant) sont retirés.
		void Update(uint32_t shardId, const std::vector<engine::network::ShardPlayerPresence>& players);

		/// Purge toutes les entrées d'un shard (appelé quand le shard tombe / est évincé).
		void Clear(uint32_t shardId);

		/// Retourne l'entrée pour \a accountId si présente.
		std::optional<Entry> Get(uint64_t accountId) const;

		/// Copie de toutes les entrées (pour l'API /online-accounts).
		std::vector<Entry> Snapshot() const;

	private:
		mutable std::mutex m_mutex;
		std::unordered_map<uint64_t, Entry> m_byAccount;
	};
}
