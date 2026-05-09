#pragma once
// CMANGOS.18 (Phase 3.18b) — MysqlMailStore : implementation MySQL
// d'IMailStore pour persistance production. Cible UNIX uniquement
// (le shard WIN32 utilise InMemoryMailStore par défaut).

#include "engine/server/mail/MailManager.h"

namespace engine::server::db { class ConnectionPool; }

namespace engine::server::mail
{
	class MysqlMailStore final : public IMailStore
	{
	public:
		explicit MysqlMailStore(engine::server::db::ConnectionPool* pool)
			: m_pool(pool) {}

		uint64_t Insert(Mail& out) override;
		std::optional<Mail> Find(uint64_t mailId) const override;
		std::vector<Mail> ListInbox(uint64_t receiverAccountId) const override;
		bool Update(const Mail& mail) override;
		bool Delete(uint64_t mailId) override;
		std::vector<uint64_t> FindExpired(uint64_t nowMs) const override;

	private:
		engine::server::db::ConnectionPool* m_pool;
	};
}
