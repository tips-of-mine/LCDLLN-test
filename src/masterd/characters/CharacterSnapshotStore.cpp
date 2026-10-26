// ETAPE2.6 — CharacterSnapshotStore : voir CharacterSnapshotStore.h.

#include "src/masterd/characters/CharacterSnapshotStore.h"

#include "src/shared/core/Log.h"
#include "src/shared/db/ConnectionPool.h"
#include "src/shared/db/SqlPreparedStatement.h"

namespace engine::server
{
	std::optional<uint64_t> FindActiveCharacterIdByName(db::ConnectionPool& pool, std::string_view name)
	{
		if (!pool.IsInitialized() || name.empty())
			return std::nullopt;
		auto guard = pool.Acquire();
		auto* mysql = guard.get();
		auto* cache = guard.cache();
		if (mysql == nullptr || cache == nullptr)
			return std::nullopt;
		auto* sel = cache->Acquire(mysql, "SELECT id FROM characters WHERE name = ? AND deleted_at IS NULL LIMIT 1");
		if (sel == nullptr || !sel->Bind(0, name) || !sel->Execute() || !sel->FetchRow())
			return std::nullopt;
		return sel->GetUInt64(0);
	}

	bool ListCharacterSnapshots(db::ConnectionPool& pool, uint64_t characterId, size_t maxCount,
		std::vector<CharacterSnapshotInfo>& out)
	{
		out.clear();
		if (!pool.IsInitialized())
			return false;
		auto guard = pool.Acquire();
		auto* mysql = guard.get();
		auto* cache = guard.cache();
		if (mysql == nullptr || cache == nullptr)
			return false;
		auto* sel = cache->Acquire(mysql,
			"SELECT id, DATE_FORMAT(created_at, '%Y-%m-%d %H:%i:%s'), reason, shard_id FROM character_snapshots "
			"WHERE character_id = ? ORDER BY id DESC LIMIT ?");
		if (sel == nullptr || !sel->Bind(0, characterId) || !sel->Bind(1, static_cast<uint64_t>(maxCount)) || !sel->Execute())
		{
			LOG_WARN(Db, "[CharacterSnapshotStore] list failed (character_id={})", characterId);
			return false;
		}
		while (sel->FetchRow())
		{
			CharacterSnapshotInfo info{};
			info.id = sel->GetUInt64(0);
			info.createdAtUtc = sel->GetString(1);
			info.reason = sel->GetString(2);
			info.shardId = static_cast<uint32_t>(sel->GetUInt64(3));
			out.push_back(std::move(info));
		}
		return true;
	}

	CharacterRestoreRequestResult RequestCharacterRestore(db::ConnectionPool& pool, uint64_t characterId,
		uint64_t snapshotId, uint64_t gmAccountId)
	{
		if (!pool.IsInitialized())
			return CharacterRestoreRequestResult::Failed;
		auto guard = pool.Acquire();
		auto* mysql = guard.get();
		auto* cache = guard.cache();
		if (mysql == nullptr || cache == nullptr)
			return CharacterRestoreRequestResult::Failed;
		auto* own = cache->Acquire(mysql, "SELECT 1 FROM character_snapshots WHERE id = ? AND character_id = ?");
		if (own == nullptr || !own->Bind(0, snapshotId) || !own->Bind(1, characterId) || !own->Execute())
			return CharacterRestoreRequestResult::Failed;
		if (!own->FetchRow())
			return CharacterRestoreRequestResult::SnapshotNotFound;
		while (own->FetchRow()) {}
		auto* up = cache->Acquire(mysql,
			"INSERT INTO character_restore_requests (character_id, snapshot_id, requested_by_account, requested_at, applied_at) "
			"VALUES (?, ?, ?, UTC_TIMESTAMP(), NULL) "
			"ON DUPLICATE KEY UPDATE snapshot_id = VALUES(snapshot_id), requested_by_account = VALUES(requested_by_account), "
			"requested_at = VALUES(requested_at), applied_at = NULL");
		if (up == nullptr || !up->Bind(0, characterId) || !up->Bind(1, snapshotId) || !up->Bind(2, gmAccountId) || !up->Execute())
		{
			LOG_WARN(Db, "[CharacterSnapshotStore] restore request failed (character_id={}, snapshot_id={})", characterId, snapshotId);
			return CharacterRestoreRequestResult::Failed;
		}
		LOG_WARN(Db, "[CharacterSnapshotStore] restore requested (character_id={}, snapshot_id={}, gm_account_id={})",
			characterId, snapshotId, gmAccountId);
		return CharacterRestoreRequestResult::Requested;
	}
}
