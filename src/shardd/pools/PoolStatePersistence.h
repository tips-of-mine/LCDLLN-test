#pragma once
// Wave 20 — Pool runtime state persistence : permet de sauvegarder
// l'etat des spawns (alive/dead/respawning) et de le recharger au
// reboot du shard, evitant un re-roll global qui briserait l'experience
// de joueurs ayant deja kill un rare spawn.
//
// Conception : 2 couches.
// - IPoolStateStore : interface abstraite (Save/Load).
// - InMemoryPoolStateStore : impl pour tests (pas de DB).
//
// L'impl Mysql sera dans src/masterd/pools/MysqlPoolStateStore.h/.cpp
// (a venir Wave 20b ou plus tard, hors scope strict de cette PR).

#include "src/shardd/pools/PoolManager.h"

#include <chrono>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace engine::server::pools
{
	/// Statut runtime d'un spawn dans une pool.
	enum class PoolSpawnStatus : uint8_t
	{
		Alive      = 0,  ///< spawn actif dans le monde
		Dead       = 1,  ///< tue ou despawn, attend respawn timer
		Respawning = 2,  ///< timer ecoule, spawn en cours de re-creation
	};

	/// Etat persistant d'un membre d'une pool. Cle unique : (poolId, spawnId).
	struct PoolMemberState
	{
		PoolId          poolId      = 0;
		SpawnId         spawnId     = 0;
		PoolSpawnStatus status      = PoolSpawnStatus::Dead;
		/// Timestamp Unix (secondes) auquel le respawn doit avoir lieu.
		/// 0 = pas de respawn programme (ex : status Alive).
		int64_t         respawnAtSec = 0;
	};

	/// Interface : impl MySQL ou InMemory selon le contexte.
	class IPoolStateStore
	{
	public:
		virtual ~IPoolStateStore() = default;

		/// Sauve l'etat de tous les membres fournis (upsert par cle (poolId, spawnId)).
		/// Retourne true si OK, false si erreur (logged par l'impl).
		virtual bool SaveAll(const std::vector<PoolMemberState>& states) = 0;

		/// Charge tous les etats persistes. Vide si store vide.
		virtual std::vector<PoolMemberState> LoadAll() = 0;

		/// Charge l'etat d'un membre specifique. Vide si absent.
		virtual std::vector<PoolMemberState> LoadByPool(PoolId poolId) = 0;
	};

	/// Impl in-memory pour tests. Clef de stockage : 64 bits (poolId << 32) | spawnId.
	class InMemoryPoolStateStore final : public IPoolStateStore
	{
	public:
		bool SaveAll(const std::vector<PoolMemberState>& states) override
		{
			for (const auto& s : states)
				m_store[Key(s.poolId, s.spawnId)] = s;
			return true;
		}

		std::vector<PoolMemberState> LoadAll() override
		{
			std::vector<PoolMemberState> out;
			out.reserve(m_store.size());
			for (const auto& kv : m_store)
				out.push_back(kv.second);
			return out;
		}

		std::vector<PoolMemberState> LoadByPool(PoolId poolId) override
		{
			std::vector<PoolMemberState> out;
			for (const auto& kv : m_store)
				if (kv.second.poolId == poolId)
					out.push_back(kv.second);
			return out;
		}

		size_t Size() const noexcept { return m_store.size(); }
		void Clear() noexcept { m_store.clear(); }

	private:
		static uint64_t Key(PoolId p, SpawnId s) noexcept
		{
			return (static_cast<uint64_t>(p) << 32) | static_cast<uint64_t>(s);
		}

		std::unordered_map<uint64_t, PoolMemberState> m_store;
	};
}
