#pragma once
// CMANGOS.33 (Phase 5.33a) — LfgQueue : queue de matchmaking pour
// donjons. Players join + role (Tank/Healer/Damage), queue forme un
// groupe avec 1T+1H+3D. Header-only.
//
// Audit 2026-06-10 (Lot B1) — THREAD-SAFE : les handlers du master sont
// dispatchés sur un pool de workers NetServer (défaut 4) ; chaque méthode
// publique verrouille m_mutex (aucune méthode publique n'en appelle une autre).

#include <algorithm>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace engine::server::lfg
{
	using PlayerId  = uint64_t;
	using DungeonId = uint32_t;

	enum class LfgRole : uint8_t
	{
		Tank   = 0,
		Healer = 1,
		Damage = 2,
	};

	struct LfgEntry
	{
		PlayerId  playerId;
		LfgRole   role;
		uint64_t  joinedTsMs;
	};

	struct LfgGroup
	{
		std::vector<PlayerId> members;
		DungeonId             dungeonId = 0;
	};

	class LfgQueue
	{
	public:
		void Join(DungeonId dungeon, PlayerId player, LfgRole role, uint64_t nowMs)
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			LfgEntry e{player, role, nowMs};
			m_byDungeon[dungeon].push_back(e);
		}

		bool Leave(DungeonId dungeon, PlayerId player)
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			auto it = m_byDungeon.find(dungeon);
			if (it == m_byDungeon.end()) return false;
			auto& v = it->second;
			for (auto eit = v.begin(); eit != v.end(); ++eit)
			{
				if (eit->playerId == player) { v.erase(eit); return true; }
			}
			return false;
		}

		size_t QueueSize(DungeonId dungeon) const
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			auto it = m_byDungeon.find(dungeon);
			return (it == m_byDungeon.end()) ? 0 : it->second.size();
		}

		/// Tente de former un groupe 1T+1H+3D pour \p dungeon. Retourne
		/// le groupe formé (et retire les membres de la queue) ou nullopt.
		std::optional<LfgGroup> TryMatch(DungeonId dungeon)
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			auto it = m_byDungeon.find(dungeon);
			if (it == m_byDungeon.end()) return std::nullopt;
			auto& q = it->second;

			std::optional<size_t> tankIdx, healerIdx;
			std::vector<size_t> dpsIdx;
			for (size_t i = 0; i < q.size(); ++i)
			{
				if (!tankIdx && q[i].role == LfgRole::Tank) tankIdx = i;
				else if (!healerIdx && q[i].role == LfgRole::Healer) healerIdx = i;
				else if (q[i].role == LfgRole::Damage && dpsIdx.size() < 3) dpsIdx.push_back(i);
			}
			if (!tankIdx || !healerIdx || dpsIdx.size() < 3) return std::nullopt;

			LfgGroup g;
			g.dungeonId = dungeon;
			g.members.push_back(q[*tankIdx].playerId);
			g.members.push_back(q[*healerIdx].playerId);
			for (auto i : dpsIdx) g.members.push_back(q[i].playerId);

			// Remove from queue (descending order pour preserver les indices).
			std::vector<size_t> toRemove = {*tankIdx, *healerIdx};
			toRemove.insert(toRemove.end(), dpsIdx.begin(), dpsIdx.end());
			std::sort(toRemove.begin(), toRemove.end(), std::greater<>{});
			for (auto i : toRemove) q.erase(q.begin() + i);

			return g;
		}

	private:
		/// Audit Lot B1 — protège m_byDungeon contre les workers concurrents.
		mutable std::mutex m_mutex;
		std::unordered_map<DungeonId, std::vector<LfgEntry>> m_byDungeon;
	};
}
