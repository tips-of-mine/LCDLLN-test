#pragma once

#include "engine/server/AccountRecord.h"
#include "engine/server/AccountStore.h"

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace engine::server
{
	/// Stockage comptes en RAM (tests / fallback sans pool MySQL).
	class InMemoryAccountStore final : public AccountStore
	{
	public:
		InMemoryAccountStore() = default;

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
		mutable std::recursive_mutex m_mutex;
		uint64_t m_nextAccountId = 1;
		std::unordered_map<std::string, AccountRecord> m_by_login;
		std::unordered_map<std::string, uint64_t> m_by_email;
	};
}
