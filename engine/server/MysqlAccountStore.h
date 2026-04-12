#pragma once

#include "engine/server/AccountStore.h"

namespace engine::server::db
{
	class ConnectionPool;
}

namespace engine::server
{
	/// Persistance des comptes dans la table `accounts` (pool MySQL master).
	class MysqlAccountStore final : public AccountStore
	{
	public:
		explicit MysqlAccountStore(engine::server::db::ConnectionPool* pool);

		uint64_t CreateAccount(std::string_view login, std::string_view email, std::string_view client_hash,
			std::string_view first_name, std::string_view last_name, std::string_view birth_date,
			std::string_view country_code,
			std::string& tag_id_out,
			AccountEmailLocale email_locale = AccountEmailLocale::English) override;

		std::optional<AccountRecord> FindByLogin(std::string_view normalisedLogin) override;
		std::optional<AccountRecord> FindByAccountId(uint64_t account_id) override;
		bool ExistsEmail(std::string_view normalisedEmail) override;
		bool ExistsLogin(std::string_view normalisedLogin) override;
		std::optional<AccountRecord> FindByEmail(std::string_view normalisedEmail) override;
		bool SetEmailVerified(uint64_t account_id) override;
		bool UpdatePasswordHash(uint64_t account_id, std::string_view new_final_hash) override;

	private:
		engine::server::db::ConnectionPool* m_pool = nullptr;
	};
}
