// M32.1 — Server-side friend system implementation.
// Handles bilateral friend requests, DB persistence and in-memory presence tracking.
// On platforms without MySQL (e.g. WIN32 game shard), the system operates in no-DB
// mode: presence tracking is fully functional; persistent DB operations are skipped.

#include "engine/server/FriendSystem.h"
#include "engine/core/Log.h"

#ifdef ENGINE_HAS_MYSQL
#  include "engine/server/db/DbHelpers.h"
#  include <mysql.h>
#endif

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>

namespace engine::server
{
	// -------------------------------------------------------------------------
	// Helpers
	// -------------------------------------------------------------------------

	namespace
	{
#ifdef ENGINE_HAS_MYSQL
		/// Build a NULL-terminated SQL string with \p input escaped via the MySQL API.
		/// Returns false when the connection handle is null or the string is too long.
		bool EscapeString(MYSQL* mysql, std::string_view input, std::string& outEscaped)
		{
			if (!mysql)
				return false;
			// MySQL requires at most 2*len+1 bytes for the escaped output.
			outEscaped.resize(input.size() * 2 + 1);
			unsigned long written = mysql_real_escape_string(
				mysql,
				outEscaped.data(),
				input.data(),
				static_cast<unsigned long>(input.size()));
			outEscaped.resize(static_cast<size_t>(written));
			return true;
		}

		/// Fetch the first uint64_t value from a single-column SELECT result row.
		/// Returns 0 when the result is empty or on error.
		uint64_t FetchFirstU64(MYSQL_RES* res)
		{
			if (!res)
				return 0;
			MYSQL_ROW row = mysql_fetch_row(res);
			if (!row || !row[0])
				return 0;
			return static_cast<uint64_t>(std::strtoull(row[0], nullptr, 10));
		}
#endif // ENGINE_HAS_MYSQL
	} // namespace

	// -------------------------------------------------------------------------
	// Init / Shutdown
	// -------------------------------------------------------------------------

	bool FriendSystem::Init(MYSQL* mysql)
	{
		if (m_initialized)
		{
			LOG_WARN(Core, "[FriendSystem] Init called more than once, ignoring");
			return true;
		}

		if (!mysql)
		{
			LOG_WARN(Core, "[FriendSystem] Init OK (no DB configured — no-DB mode)");
		}
		else
		{
			LOG_INFO(Core, "[FriendSystem] Init OK (DB connected)");
		}

		m_initialized = true;
		return true;
	}

	void FriendSystem::Shutdown()
	{
		m_presence.clear();
		m_requestRateLimit.clear();
		m_initialized = false;
		LOG_INFO(Core, "[FriendSystem] Destroyed");
	}

	// -------------------------------------------------------------------------
	// Private helpers
	// -------------------------------------------------------------------------

	bool FriendSystem::IsRateLimited(uint64_t playerId)
	{
		auto now = Clock::now();
		auto& timestamps = m_requestRateLimit[playerId];

		// Evict entries older than the window.
		while (!timestamps.empty())
		{
			auto age = std::chrono::duration_cast<std::chrono::seconds>(now - timestamps.front()).count();
			if (age >= kRequestRateLimitWindowSec)
				timestamps.pop_front();
			else
				break;
		}

		if (timestamps.size() >= kMaxRequestsPerWindow)
			return true;

		timestamps.push_back(now);
		return false;
	}

	uint64_t FriendSystem::LookupPlayerIdByName(std::string_view name, MYSQL* mysql) const
	{
#ifdef ENGINE_HAS_MYSQL
		if (!mysql || name.empty())
			return 0;

		std::string safeName;
		// const_cast: EscapeString only reads mysql handle, no mutations performed.
		if (!EscapeString(mysql, name, safeName))
			return 0;

		std::string sql = "SELECT id FROM characters WHERE name = '";
		sql += safeName;
		sql += "' LIMIT 1";

		MYSQL_RES* res = db::DbQuery(mysql, sql);
		if (!res)
		{
			LOG_WARN(Core, "[FriendSystem] LookupPlayerIdByName query failed for name '{}'", name);
			return 0;
		}
		uint64_t id = FetchFirstU64(res);
		db::DbFreeResult(res);
		return id;
#else
		(void)mysql; (void)name;
		return 0;
#endif
	}

	size_t FriendSystem::CountAcceptedFriends(uint64_t playerId, MYSQL* mysql) const
	{
#ifdef ENGINE_HAS_MYSQL
		if (!mysql)
			return 0;

		char sql[256];
		std::snprintf(sql, sizeof(sql),
			"SELECT COUNT(*) FROM friends WHERE player_id = %llu AND status = 1",
			static_cast<unsigned long long>(playerId));

		MYSQL_RES* res = db::DbQuery(mysql, sql);
		if (!res)
			return 0;
		uint64_t count = FetchFirstU64(res);
		db::DbFreeResult(res);
		return static_cast<size_t>(count);
#else
		(void)playerId; (void)mysql;
		return 0;
#endif
	}

	// -------------------------------------------------------------------------
	// Friend request flow
	// -------------------------------------------------------------------------

	uint64_t FriendSystem::SendFriendRequest(uint64_t         requesterId,
	                                          std::string_view requesterName,
	                                          std::string_view targetName,
	                                          MYSQL*           mysql)
	{
		if (!m_initialized)
		{
			LOG_WARN(Core, "[FriendSystem] SendFriendRequest called before Init");
			return 0;
		}

		if (IsRateLimited(requesterId))
		{
			LOG_WARN(Core, "[FriendSystem] SendFriendRequest rate limited for player {}", requesterId);
			return 0;
		}

		if (!mysql)
		{
			LOG_WARN(Core, "[FriendSystem] SendFriendRequest: no DB (no-DB mode)");
			return 0;
		}

#ifdef ENGINE_HAS_MYSQL
		uint64_t targetId = LookupPlayerIdByName(targetName, mysql);
		if (targetId == 0)
		{
			LOG_WARN(Core, "[FriendSystem] SendFriendRequest: target '{}' not found", targetName);
			return 0;
		}

		if (targetId == requesterId)
		{
			LOG_WARN(Core, "[FriendSystem] SendFriendRequest: player {} tried to add themselves", requesterId);
			return 0;
		}

		// Enforce friends cap.
		if (CountAcceptedFriends(requesterId, mysql) >= kMaxFriendsPerPlayer)
		{
			LOG_WARN(Core, "[FriendSystem] SendFriendRequest: player {} at friend cap ({})",
				requesterId, kMaxFriendsPerPlayer);
			return 0;
		}

		// Check for existing relationship.
		char checkSql[256];
		std::snprintf(checkSql, sizeof(checkSql),
			"SELECT status FROM friends WHERE player_id = %llu AND friend_id = %llu LIMIT 1",
			static_cast<unsigned long long>(requesterId),
			static_cast<unsigned long long>(targetId));

		MYSQL_RES* res = db::DbQuery(mysql, checkSql);
		if (res)
		{
			MYSQL_ROW row = mysql_fetch_row(res);
			db::DbFreeResult(res);
			if (row)
			{
				LOG_WARN(Core, "[FriendSystem] SendFriendRequest: relationship already exists between {} and {}",
					requesterId, targetId);
				return 0;
			}
		}

		// Insert pending row (requester → target).
		char insertSql[256];
		std::snprintf(insertSql, sizeof(insertSql),
			"INSERT INTO friends (player_id, friend_id, status) VALUES (%llu, %llu, 0)",
			static_cast<unsigned long long>(requesterId),
			static_cast<unsigned long long>(targetId));

		if (!db::DbExecute(mysql, insertSql))
		{
			LOG_ERROR(Core, "[FriendSystem] SendFriendRequest: INSERT failed for {} → {}", requesterId, targetId);
			return 0;
		}

		LOG_INFO(Core, "[FriendSystem] Friend request sent: {} ('{}') → {} ('{}')",
			requesterId, requesterName, targetId, targetName);
		return targetId;
#else
		(void)requesterName; (void)targetName;
		return 0;
#endif
	}

	uint64_t FriendSystem::AcceptFriendRequest(uint64_t         accepterId,
	                                            std::string_view requesterName,
	                                            MYSQL*           mysql)
	{
		if (!m_initialized)
		{
			LOG_WARN(Core, "[FriendSystem] AcceptFriendRequest called before Init");
			return 0;
		}

		if (!mysql)
		{
			LOG_WARN(Core, "[FriendSystem] AcceptFriendRequest: no DB (no-DB mode)");
			return 0;
		}

#ifdef ENGINE_HAS_MYSQL
		uint64_t requesterId = LookupPlayerIdByName(requesterName, mysql);
		if (requesterId == 0)
		{
			LOG_WARN(Core, "[FriendSystem] AcceptFriendRequest: requester '{}' not found", requesterName);
			return 0;
		}

		// Enforce friends cap on accepting side.
		if (CountAcceptedFriends(accepterId, mysql) >= kMaxFriendsPerPlayer)
		{
			LOG_WARN(Core, "[FriendSystem] AcceptFriendRequest: player {} at friend cap", accepterId);
			return 0;
		}

		db::ScopedTransaction tx(mysql);

		// Update existing pending row (requester → accepter) to accepted.
		char updateSql[256];
		std::snprintf(updateSql, sizeof(updateSql),
			"UPDATE friends SET status = 1 WHERE player_id = %llu AND friend_id = %llu AND status = 0",
			static_cast<unsigned long long>(requesterId),
			static_cast<unsigned long long>(accepterId));

		if (!db::DbExecute(mysql, updateSql))
		{
			LOG_ERROR(Core, "[FriendSystem] AcceptFriendRequest: UPDATE failed {} → {}", requesterId, accepterId);
			return 0;
		}

		// Insert or update reverse row (accepter → requester) as accepted.
		char upsertSql[512];
		std::snprintf(upsertSql, sizeof(upsertSql),
			"INSERT INTO friends (player_id, friend_id, status) VALUES (%llu, %llu, 1) "
			"ON DUPLICATE KEY UPDATE status = 1",
			static_cast<unsigned long long>(accepterId),
			static_cast<unsigned long long>(requesterId));

		if (!db::DbExecute(mysql, upsertSql))
		{
			LOG_ERROR(Core, "[FriendSystem] AcceptFriendRequest: UPSERT reverse row failed {} → {}", accepterId, requesterId);
			return 0;
		}

		tx.Commit();
		LOG_INFO(Core, "[FriendSystem] Friend request accepted: {} ← '{}'", accepterId, requesterName);
		return requesterId;
#else
		(void)accepterId; (void)requesterName;
		return 0;
#endif
	}

	uint64_t FriendSystem::DeclineFriendRequest(uint64_t         declinerId,
	                                             std::string_view requesterName,
	                                             MYSQL*           mysql)
	{
		if (!m_initialized)
		{
			LOG_WARN(Core, "[FriendSystem] DeclineFriendRequest called before Init");
			return 0;
		}

		if (!mysql)
		{
			LOG_WARN(Core, "[FriendSystem] DeclineFriendRequest: no DB (no-DB mode)");
			return 0;
		}

#ifdef ENGINE_HAS_MYSQL
		uint64_t requesterId = LookupPlayerIdByName(requesterName, mysql);
		if (requesterId == 0)
		{
			LOG_WARN(Core, "[FriendSystem] DeclineFriendRequest: requester '{}' not found", requesterName);
			return 0;
		}

		char sql[256];
		std::snprintf(sql, sizeof(sql),
			"UPDATE friends SET status = 2 WHERE player_id = %llu AND friend_id = %llu AND status = 0",
			static_cast<unsigned long long>(requesterId),
			static_cast<unsigned long long>(declinerId));

		if (!db::DbExecute(mysql, sql))
		{
			LOG_ERROR(Core, "[FriendSystem] DeclineFriendRequest: UPDATE failed {} → {}", requesterId, declinerId);
			return 0;
		}

		LOG_INFO(Core, "[FriendSystem] Friend request declined by {}: requester '{}'", declinerId, requesterName);
		return requesterId;
#else
		(void)declinerId; (void)requesterName;
		return 0;
#endif
	}

	bool FriendSystem::RemoveFriend(uint64_t         playerId,
	                                std::string_view friendName,
	                                MYSQL*           mysql)
	{
		if (!m_initialized)
		{
			LOG_WARN(Core, "[FriendSystem] RemoveFriend called before Init");
			return false;
		}

		if (!mysql)
		{
			LOG_WARN(Core, "[FriendSystem] RemoveFriend: no DB (no-DB mode)");
			return false;
		}

#ifdef ENGINE_HAS_MYSQL
		uint64_t friendId = LookupPlayerIdByName(friendName, mysql);
		if (friendId == 0)
		{
			LOG_WARN(Core, "[FriendSystem] RemoveFriend: friend '{}' not found", friendName);
			return false;
		}

		db::ScopedTransaction tx(mysql);

		char deleteSql[256];
		std::snprintf(deleteSql, sizeof(deleteSql),
			"DELETE FROM friends WHERE (player_id = %llu AND friend_id = %llu) "
			"OR (player_id = %llu AND friend_id = %llu)",
			static_cast<unsigned long long>(playerId),
			static_cast<unsigned long long>(friendId),
			static_cast<unsigned long long>(friendId),
			static_cast<unsigned long long>(playerId));

		if (!db::DbExecute(mysql, deleteSql))
		{
			LOG_ERROR(Core, "[FriendSystem] RemoveFriend: DELETE failed player={} friend={}", playerId, friendId);
			return false;
		}

		tx.Commit();
		LOG_INFO(Core, "[FriendSystem] Friend removed: player {} removed '{}'", playerId, friendName);
		return true;
#else
		(void)playerId; (void)friendName;
		return false;
#endif
	}

	// -------------------------------------------------------------------------
	// Presence tracking
	// -------------------------------------------------------------------------

	void FriendSystem::SetPresence(uint64_t playerId, std::string_view playerName, PresenceStatus status)
	{
		auto& entry       = m_presence[playerId];
		entry.playerId    = playerId;
		entry.playerName  = std::string(playerName);
		entry.presence    = status;
		LOG_DEBUG(Core, "[FriendSystem] Presence updated: player {} ('{}') status={}",
			playerId, playerName, static_cast<int>(status));
	}

	void FriendSystem::SetOffline(uint64_t playerId)
	{
		auto it = m_presence.find(playerId);
		if (it != m_presence.end())
		{
			LOG_DEBUG(Core, "[FriendSystem] Player {} ('{}') went offline", playerId, it->second.playerName);
			m_presence.erase(it);
		}
	}

	PresenceStatus FriendSystem::GetPresence(uint64_t playerId) const
	{
		auto it = m_presence.find(playerId);
		if (it == m_presence.end())
			return PresenceStatus::Offline;
		return it->second.presence;
	}

	// -------------------------------------------------------------------------
	// Queries
	// -------------------------------------------------------------------------

	std::vector<FriendRecord> FriendSystem::GetFriendList(uint64_t playerId, MYSQL* mysql) const
	{
		std::vector<FriendRecord> result;

		if (!mysql)
		{
			LOG_WARN(Core, "[FriendSystem] GetFriendList: no DB (no-DB mode)");
			return result;
		}

#ifdef ENGINE_HAS_MYSQL
		// Join friends with characters to get display names, include pending inbound requests.
		char sql[512];
		std::snprintf(sql, sizeof(sql),
			"SELECT f.friend_id, c.name, f.status "
			"FROM friends f "
			"JOIN characters c ON c.id = f.friend_id "
			"WHERE f.player_id = %llu AND f.status IN (0, 1)",
			static_cast<unsigned long long>(playerId));

		MYSQL_RES* res = db::DbQuery(mysql, sql);
		if (!res)
		{
			LOG_WARN(Core, "[FriendSystem] GetFriendList: query failed for player {}", playerId);
			return result;
		}

		MYSQL_ROW row;
		while ((row = mysql_fetch_row(res)))
		{
			if (!row[0] || !row[1] || !row[2])
				continue;
			FriendRecord rec;
			rec.playerId   = playerId;
			rec.friendId   = static_cast<uint64_t>(std::strtoull(row[0], nullptr, 10));
			rec.friendName = row[1];
			rec.status     = static_cast<uint8_t>(std::strtoul(row[2], nullptr, 10));
			result.push_back(std::move(rec));
		}
		db::DbFreeResult(res);

		LOG_DEBUG(Core, "[FriendSystem] GetFriendList: {} entries for player {}", result.size(), playerId);
#else
		(void)playerId;
#endif
		return result;
	}

	std::vector<uint64_t> FriendSystem::GetOnlineFriendIds(uint64_t playerId, MYSQL* mysql) const
	{
		std::vector<uint64_t> result;

		if (!mysql)
			return result;

#ifdef ENGINE_HAS_MYSQL
		char sql[256];
		std::snprintf(sql, sizeof(sql),
			"SELECT friend_id FROM friends WHERE player_id = %llu AND status = 1",
			static_cast<unsigned long long>(playerId));

		MYSQL_RES* res = db::DbQuery(mysql, sql);
		if (!res)
			return result;

		MYSQL_ROW row;
		while ((row = mysql_fetch_row(res)))
		{
			if (!row[0])
				continue;
			uint64_t fid = static_cast<uint64_t>(std::strtoull(row[0], nullptr, 10));
			// Only include friends that are currently online.
			if (m_presence.count(fid) > 0)
				result.push_back(fid);
		}
		db::DbFreeResult(res);
#else
		(void)playerId;
#endif
		return result;
	}
}
