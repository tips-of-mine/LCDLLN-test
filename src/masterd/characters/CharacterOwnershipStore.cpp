// CharacterOwnershipStore — voir CharacterOwnershipStore.h (ETAPE1.2).

#include "src/masterd/characters/CharacterOwnershipStore.h"

#include "src/shared/core/Log.h"
#include "src/shared/db/ConnectionPool.h"
#include "src/shared/db/SqlPreparedStatement.h"

namespace engine::server
{
	std::optional<uint64_t> AcquireCharacterOwnership(MYSQL* mysql, db::SqlPreparedStatementCache* cache,
		uint64_t characterId, uint64_t accountId, uint32_t shardId)
	{
		if (mysql == nullptr || cache == nullptr || characterId == 0u || shardId == 0u)
			return std::nullopt;
		auto* upd = cache->Acquire(mysql,
			"UPDATE characters SET owner_shard_id = ?, owner_generation = owner_generation + 1, "
			"owner_acquired_at = UTC_TIMESTAMP() WHERE id = ? AND account_id = ? AND deleted_at IS NULL");
		if (upd == nullptr || !upd->Bind(0, shardId) || !upd->Bind(1, characterId) || !upd->Bind(2, accountId)
			|| !upd->Execute() || upd->AffectedRows() == 0u)
		{
			LOG_WARN(Net, "[CharacterOwnership] acquire FAILED (character_id={}, shard_id={}) — admission sans verrou",
				characterId, shardId);
			return std::nullopt;
		}
		auto* sel = cache->Acquire(mysql, "SELECT owner_generation FROM characters WHERE id = ?");
		if (sel == nullptr || !sel->Bind(0, characterId) || !sel->Execute() || !sel->FetchRow())
			return std::nullopt;
		const uint64_t generation = sel->GetUInt64(0);
		LOG_INFO(Net, "[CharacterOwnership] character_id={} attribué au shard_id={} (génération {})",
			characterId, shardId, generation);
		return generation;
	}

	std::optional<uint64_t> ReleaseShardOwnership(db::ConnectionPool& pool, uint32_t shardId)
	{
		if (!pool.IsInitialized() || shardId == 0u)
			return std::nullopt;
		auto guard = pool.Acquire();
		MYSQL* mysql = guard.get();
		auto* cache = guard.cache();
		if (mysql == nullptr || cache == nullptr)
			return std::nullopt;
		auto* upd = cache->Acquire(mysql, "UPDATE characters SET owner_shard_id = NULL WHERE owner_shard_id = ?");
		if (upd == nullptr || !upd->Bind(0, shardId) || !upd->Execute())
		{
			LOG_WARN(Net, "[CharacterOwnership] release FAILED (shard_id={})", shardId);
			return std::nullopt;
		}
		const uint64_t released = upd->AffectedRows();
		LOG_INFO(Net, "[CharacterOwnership] shard_id={} déclaré mort : {} personnage(s) libéré(s)", shardId, released);
		return released;
	}

	bool IsCharacterOwnedByShard(MYSQL* mysql, db::SqlPreparedStatementCache* cache,
		uint64_t characterId, uint64_t accountId)
	{
		if (mysql == nullptr || cache == nullptr)
			return false;
		auto* sel = cache->Acquire(mysql,
			"SELECT owner_shard_id IS NOT NULL FROM characters WHERE id = ? AND account_id = ? AND deleted_at IS NULL");
		if (sel == nullptr || !sel->Bind(0, characterId) || !sel->Bind(1, accountId) || !sel->Execute()
			|| !sel->FetchRow())
			return false;
		return sel->GetUInt64(0) != 0u;
	}
}
