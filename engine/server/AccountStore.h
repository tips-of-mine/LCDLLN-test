#pragma once

#include "engine/server/AccountRecord.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace engine::server
{
	/// Abstraction stockage comptes (RAM ou MySQL) pour auth / inscription / reset.
	class AccountStore
	{
	public:
		virtual ~AccountStore() = default;

		/// country_code: code ISO-2 pays depuis le formulaire d'inscription.
		/// Retourne account_id (> 0) + remplit tag_id_out avec le TAG-ID généré. 0 = échec.
		virtual uint64_t CreateAccount(std::string_view login, std::string_view email, std::string_view client_hash,
			std::string_view first_name, std::string_view last_name, std::string_view birth_date,
			std::string_view country_code,
			std::string& tag_id_out,
			AccountEmailLocale email_locale = AccountEmailLocale::English) = 0;

		virtual std::optional<AccountRecord> FindByLogin(std::string_view normalisedLogin) = 0;
		virtual std::optional<AccountRecord> FindByAccountId(uint64_t account_id) = 0;
		virtual bool ExistsEmail(std::string_view normalisedEmail) = 0;
		virtual bool ExistsLogin(std::string_view normalisedLogin) = 0;
		virtual std::optional<AccountRecord> FindByEmail(std::string_view normalisedEmail) = 0;
		virtual bool SetEmailVerified(uint64_t account_id) = 0;
		virtual bool UpdatePasswordHash(uint64_t account_id, std::string_view new_final_hash) = 0;
	};
}
