#pragma once
// CMANGOS.24 (Phase 3.24b) — MysqlReputationStore : persistance MySQL
// des valeurs de reputation par (account, faction). Migration 0047.
// Cible UNIX shard.

#include "src/masterd/reputation/ReputationManager.h"

#include <cstdint>
#include <vector>

namespace engine::server::db { class ConnectionPool; }

namespace engine::server::reputation
{
	struct ReputationRow
	{
		AccountId accountId;
		FactionId factionId;
		int32_t   value;
	};

	class MysqlReputationStore
	{
	public:
		explicit MysqlReputationStore(engine::server::db::ConnectionPool* pool)
			: m_pool(pool) {}

		/// Upsert d'une ligne (faction, account, value). Insert si absent
		/// sinon update value.
		bool Upsert(AccountId accountId, FactionId factionId, int32_t value);

		/// Charge toutes les lignes pour un account (typiquement au login).
		std::vector<ReputationRow> Load(AccountId accountId) const;

	private:
		engine::server::db::ConnectionPool* m_pool;
	};
}
