#include "engine/server/InMemoryAccountStore.h"
#include "engine/server/AccountValidation.h"
#include "engine/auth/Argon2Hash.h"
#include "engine/core/Log.h"

#include <chrono>
#include <ctime>
#include <mutex>
#include <string>
#include <vector>

namespace engine::server
{
	uint64_t InMemoryAccountStore::CreateAccount(std::string_view login, std::string_view email, std::string_view client_hash,
		std::string_view first_name, std::string_view last_name, std::string_view birth_date,
		std::string_view country_code,
		std::string& tag_id_out,
		AccountEmailLocale email_locale)
	{
		std::lock_guard<std::recursive_mutex> lock(m_mutex);
		std::string login_key(NormaliseLoginView(login));
		if (login_key.empty())
		{
			LOG_WARN(Auth, "[InMemoryAccountStore] CreateAccount: empty login");
			return 0;
		}
		if (engine::server::ValidateLogin(login_key) != engine::network::NetErrorCode::OK)
		{
			LOG_WARN(Auth, "[InMemoryAccountStore] CreateAccount: invalid login");
			return 0;
		}
		std::string email_norm = NormaliseEmail(email);
		if (!email_norm.empty() && engine::server::ValidateEmail(email_norm) != engine::network::NetErrorCode::OK)
		{
			LOG_WARN(Auth, "[InMemoryAccountStore] CreateAccount: invalid email");
			return 0;
		}
		if (ExistsLogin(login_key))
		{
			LOG_WARN(Auth, "[InMemoryAccountStore] CreateAccount: login already taken");
			return 0;
		}
		if (!email_norm.empty() && ExistsEmail(email_norm))
		{
			LOG_WARN(Auth, "[InMemoryAccountStore] CreateAccount: email already taken");
			return 0;
		}
		std::vector<std::uint8_t> server_salt = engine::auth::GenerateSalt();
		if (server_salt.empty())
		{
			LOG_ERROR(Auth, "[InMemoryAccountStore] CreateAccount: GenerateSalt failed");
			return 0;
		}
		engine::auth::Argon2Params params;
		std::string final_hash = engine::auth::Hash(client_hash, server_salt, params);
		if (final_hash.empty())
		{
			LOG_ERROR(Auth, "[InMemoryAccountStore] CreateAccount: Hash failed");
			return 0;
		}
		const uint64_t account_id = m_nextAccountId++;
		AccountRecord rec;
		rec.account_id = account_id;
		rec.login = login_key;
		rec.email = email_norm;
		rec.first_name = std::string(first_name);
		rec.last_name = std::string(last_name);
		rec.birth_date = std::string(birth_date);
		rec.final_hash = std::move(final_hash);
		rec.status      = AccountStatus::Active;
		rec.email_locale = email_locale;
		// Génération TAG-ID (stub RAM : suffixe = account_id, pas de vrai séquençage par préfixe).
		{
			const auto now = std::chrono::system_clock::now();
			const std::time_t t = std::chrono::system_clock::to_time_t(now);
			std::tm tm_val{};
#if defined(_WIN32)
			localtime_s(&tm_val, &t);
#else
			localtime_r(&t, &tm_val);
#endif
			const int year_digit = (tm_val.tm_year + 1900) % 10;
			const int month      = tm_val.tm_mon + 1;
			std::string cc = country_code.empty() ? "XX" : std::string(country_code).substr(0, 2);
			// Mettre en majuscules.
			for (char& c : cc) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
			char tag_buf[11];
			std::snprintf(tag_buf, sizeof(tag_buf), "%s%1d%02d%05llu",
				cc.c_str(), year_digit, month,
				static_cast<unsigned long long>(m_nextAccountId));
			tag_id_out = tag_buf;
		}
		rec.country_code = std::string(country_code);
		rec.tag_id       = tag_id_out;
		m_by_login[rec.login] = rec;
		if (!rec.email.empty())
			m_by_email[rec.email] = rec.account_id;
		LOG_INFO(Auth, "[InMemoryAccountStore] CreateAccount OK (account_id={}, login={})", account_id, rec.login);
		return account_id;
	}

	std::optional<AccountRecord> InMemoryAccountStore::FindByLogin(std::string_view normalisedLogin)
	{
		std::lock_guard<std::recursive_mutex> lock(m_mutex);
		auto it = m_by_login.find(std::string(normalisedLogin));
		if (it == m_by_login.end())
			return std::nullopt;
		return it->second;
	}

	std::optional<AccountRecord> InMemoryAccountStore::FindByAccountId(uint64_t account_id)
	{
		std::lock_guard<std::recursive_mutex> lock(m_mutex);
		for (const auto& [_, rec] : m_by_login)
			if (rec.account_id == account_id)
				return rec;
		return std::nullopt;
	}

	bool InMemoryAccountStore::ExistsEmail(std::string_view normalisedEmail)
	{
		std::lock_guard<std::recursive_mutex> lock(m_mutex);
		return m_by_email.find(std::string(normalisedEmail)) != m_by_email.end();
	}

	bool InMemoryAccountStore::ExistsLogin(std::string_view normalisedLogin)
	{
		std::lock_guard<std::recursive_mutex> lock(m_mutex);
		return m_by_login.find(std::string(normalisedLogin)) != m_by_login.end();
	}

	std::optional<AccountRecord> InMemoryAccountStore::FindByEmail(std::string_view normalisedEmail)
	{
		std::lock_guard<std::recursive_mutex> lock(m_mutex);
		auto it = m_by_email.find(std::string(normalisedEmail));
		if (it == m_by_email.end())
			return std::nullopt;
		return FindByAccountId(it->second);
	}

	bool InMemoryAccountStore::SetEmailVerified(uint64_t account_id)
	{
		std::lock_guard<std::recursive_mutex> lock(m_mutex);
		for (auto& [key, rec] : m_by_login)
		{
			if (rec.account_id == account_id)
			{
				rec.email_verified = true;
				LOG_INFO(Auth, "[InMemoryAccountStore] SetEmailVerified OK (account_id={})", account_id);
				return true;
			}
		}
		LOG_WARN(Auth, "[InMemoryAccountStore] SetEmailVerified: account_id={} not found", account_id);
		return false;
	}

	bool InMemoryAccountStore::UpdatePasswordHash(uint64_t account_id, std::string_view new_final_hash)
	{
		std::lock_guard<std::recursive_mutex> lock(m_mutex);
		for (auto& [key, rec] : m_by_login)
		{
			if (rec.account_id == account_id)
			{
				rec.final_hash = std::string(new_final_hash);
				LOG_INFO(Auth, "[InMemoryAccountStore] UpdatePasswordHash OK (account_id={})", account_id);
				return true;
			}
		}
		LOG_WARN(Auth, "[InMemoryAccountStore] UpdatePasswordHash: account_id={} not found", account_id);
		return false;
	}
}
