#pragma once
// CMANGOS.23 (Phase 5.23b) — MysqlQuestStateStore : persistance MySQL
// du QuestStateTracker (table account_quest_state, migration 0048).
// Cible UNIX shard.

#include "src/masterd/quests/QuestState.h"

#include <cstdint>
#include <vector>

namespace engine::server::db { class ConnectionPool; }

namespace engine::server::quests
{
	struct QuestStateRow
	{
		AccountId   accountId;
		QuestId     questId;
		QuestStatus status;
	};

	class MysqlQuestStateStore
	{
	public:
		explicit MysqlQuestStateStore(engine::server::db::ConnectionPool* pool)
			: m_pool(pool) {}

		/// Upsert d'un (account, quest, status). Insert si absent sinon update.
		bool Upsert(AccountId accountId, QuestId questId, QuestStatus status);

		/// Suppression d'une ligne (utile pour reset quest cote admin).
		bool Delete(AccountId accountId, QuestId questId);

		/// Charge toutes les lignes pour un account (au login).
		std::vector<QuestStateRow> Load(AccountId accountId) const;

	private:
		engine::server::db::ConnectionPool* m_pool;
	};
}
