#pragma once
// CMANGOS.25 (Phase 3.25b) — MysqlIgnoreStore : implementation MySQL
// d'IIgnoreStore pour persistance production. Cible UNIX shard.
// (WIN32 sandbox utilise InMemoryIgnoreStore.)

#include "src/masterd/social/IgnoreList.h"

namespace engine::server::db { class ConnectionPool; }

namespace engine::server::social
{
	class MysqlIgnoreStore final : public IIgnoreStore
	{
	public:
		explicit MysqlIgnoreStore(engine::server::db::ConnectionPool* pool)
			: m_pool(pool) {}

		bool Add(uint64_t ownerAccountId, uint64_t targetAccountId) override;
		bool Remove(uint64_t ownerAccountId, uint64_t targetAccountId) override;
		bool IsIgnored(uint64_t ownerAccountId, uint64_t targetAccountId) const override;
		std::vector<uint64_t> List(uint64_t ownerAccountId) const override;
		size_t Size(uint64_t ownerAccountId) const override;

	private:
		engine::server::db::ConnectionPool* m_pool;
	};
}
