#include "engine/server/MysqlAccountStore.h"
#include "engine/server/AccountValidation.h"
#include "engine/server/db/ConnectionPool.h"
#include "engine/server/db/DbHelpers.h"
#include "engine/auth/Argon2Hash.h"
#include "engine/core/Log.h"
#include "engine/network/NetErrorCode.h"

#include <mysql.h>

#include <chrono>
#include <cstdlib>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace engine::server
{
	namespace
	{
		constexpr std::string_view kNoEmailPlaceholderSuffix = "@lcdlln.no-email.local";
		/// MySQL ER_DUP_ENTRY — évite la dépendance à mysqld_error.h selon les installs.
		constexpr unsigned kMysqlDupKey = 1062;

		std::string EscapeMysql(MYSQL* mysql, std::string_view v)
		{
			if (!mysql)
				return {};
			std::vector<char> buf(v.size() * 2 + 1);
			unsigned long w = mysql_real_escape_string(mysql, buf.data(), v.data(), static_cast<unsigned long>(v.size()));
			return std::string(buf.data(), w);
		}

		AccountRecord RowToRecord(MYSQL_ROW row)
		{
			AccountRecord r;
			if (row[0])
				r.account_id = std::strtoull(row[0], nullptr, 10);
			std::string db_email = row[1] ? row[1] : "";
			if (db_email.size() >= kNoEmailPlaceholderSuffix.size()
				&& db_email.compare(db_email.size() - kNoEmailPlaceholderSuffix.size(), kNoEmailPlaceholderSuffix.size(),
					kNoEmailPlaceholderSuffix) == 0)
				r.email.clear();
			else
				r.email = std::move(db_email);
			if (row[2])
				r.login = row[2];
			if (row[3])
				r.final_hash = row[3];
			const unsigned st = row[4] ? static_cast<unsigned>(std::strtoul(row[4], nullptr, 10)) : 0;
			r.status = (st == 0) ? AccountStatus::Active : AccountStatus::Locked;
			r.email_verified = row[5] && row[5][0] != '0';
			const unsigned loc = row[6] ? static_cast<unsigned>(std::strtoul(row[6], nullptr, 10)) : 0;
			if (loc <= static_cast<unsigned>(AccountEmailLocale::Italian))
				r.email_locale = static_cast<AccountEmailLocale>(loc);
			return r;
		}

		std::optional<AccountRecord> QueryOneAccount(MYSQL* mysql, const std::string& sql)
		{
			MYSQL_RES* res = engine::server::db::DbQuery(mysql, sql);
			if (!res)
				return std::nullopt;
			MYSQL_ROW row = mysql_fetch_row(res);
			std::optional<AccountRecord> out;
			if (row)
				out = RowToRecord(row);
			mysql_free_result(res);
			return out;
		}

		bool QueryExists(MYSQL* mysql, const std::string& sql)
		{
			MYSQL_RES* res = engine::server::db::DbQuery(mysql, sql);
			if (!res)
				return false;
			MYSQL_ROW row = mysql_fetch_row(res);
			const bool ok = row != nullptr;
			mysql_free_result(res);
			return ok;
		}
	} // namespace

	MysqlAccountStore::MysqlAccountStore(engine::server::db::ConnectionPool* pool) : m_pool(pool) {}

	uint64_t MysqlAccountStore::CreateAccount(std::string_view login, std::string_view email, std::string_view client_hash,
		std::string_view first_name, std::string_view last_name, std::string_view birth_date,
		std::string_view country_code,
		std::string& tag_id_out,
		AccountEmailLocale email_locale)
	{
		(void)first_name;
		(void)last_name;
		(void)birth_date;
		if (!m_pool || !m_pool->IsInitialized())
		{
			LOG_WARN(Auth, "[MysqlAccountStore] CreateAccount: pool unavailable");
			return 0;
		}

		const std::string login_key(NormaliseLoginView(login));
		if (login_key.empty())
		{
			LOG_WARN(Auth, "[MysqlAccountStore] CreateAccount: empty login");
			return 0;
		}
		if (ValidateLogin(login_key) != engine::network::NetErrorCode::OK)
		{
			LOG_WARN(Auth, "[MysqlAccountStore] CreateAccount: invalid login");
			return 0;
		}
		const std::string email_norm = NormaliseEmail(email);
		if (!email_norm.empty() && ValidateEmail(email_norm) != engine::network::NetErrorCode::OK)
		{
			LOG_WARN(Auth, "[MysqlAccountStore] CreateAccount: invalid email");
			return 0;
		}
		// Existence checks avant Acquire : évite de tenir une connexion pendant les requêtes (pool de taille 1).
		if (ExistsLogin(login_key))
		{
			LOG_WARN(Auth, "[MysqlAccountStore] CreateAccount: login already taken");
			return 0;
		}
		if (!email_norm.empty() && ExistsEmail(email_norm))
		{
			LOG_WARN(Auth, "[MysqlAccountStore] CreateAccount: email already taken");
			return 0;
		}

		// Build TAG-ID prefix: CCYMM (2-letter country code + last year digit + 2-digit month).
		// Use current UTC time for the YMM part.
		std::string cc(country_code.size() >= 2 ? country_code.substr(0, 2) : "XX");
		// Uppercase the country code.
		for (char& c : cc)
			c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
		// Validate cc is exactly 2 ASCII uppercase letters (anti-injection guard for SQL LIKE).
		{
			bool valid = (cc.size() == 2 && cc[0] >= 'A' && cc[0] <= 'Z' && cc[1] >= 'A' && cc[1] <= 'Z');
			if (!valid)
			{
				LOG_WARN(Auth, "[MysqlAccountStore] CreateAccount: invalid country_code '{}', using XX", cc);
				cc = "XX";
			}
		}

		std::time_t now_t = std::time(nullptr);
		std::tm now_tm{};
#if defined(_WIN32)
		gmtime_s(&now_tm, &now_t);
#else
		gmtime_r(&now_t, &now_tm);
#endif
		// Last digit of calendar year (e.g. 2026 -> '6').
		const char year_digit = static_cast<char>('0' + ((now_tm.tm_year + 1900) % 10));
		// 2-digit month (01-12).
		std::ostringstream month_ss;
		month_ss << std::setw(2) << std::setfill('0') << (now_tm.tm_mon + 1);
		const std::string prefix = cc + year_digit + month_ss.str(); // e.g. "FR602"

		// Sequence query — done before main Acquire to keep pool (size 1) free.
		uint64_t seq = 1;
		{
			auto conn = m_pool->Acquire();
			MYSQL* mysql_seq = conn.get();
			if (mysql_seq)
			{
				const std::string sql_seq =
					"SELECT COALESCE(MAX(CAST(SUBSTR(tag_id, 6) AS UNSIGNED)), 0) + 1 "
					"FROM accounts WHERE tag_id LIKE '" + prefix + "%'";
				MYSQL_RES* res_seq = engine::server::db::DbQuery(mysql_seq, sql_seq);
				if (res_seq)
				{
					MYSQL_ROW row_seq = mysql_fetch_row(res_seq);
					if (row_seq && row_seq[0])
						seq = std::strtoull(row_seq[0], nullptr, 10);
					mysql_free_result(res_seq);
				}
			}
		}

		// Build the TAG-ID: prefix (5 chars) + 5-digit zero-padded sequence.
		std::ostringstream tag_ss;
		tag_ss << prefix << std::setw(5) << std::setfill('0') << seq;
		const std::string local_tag_id = tag_ss.str(); // e.g. "FR60200001" — assigned to tag_id_out only on success

		auto guard = m_pool->Acquire();
		MYSQL* mysql = guard.get();
		if (!mysql)
		{
			LOG_WARN(Auth, "[MysqlAccountStore] CreateAccount: no connection");
			return 0;
		}

		std::vector<std::uint8_t> server_salt = engine::auth::GenerateSalt();
		if (server_salt.empty())
		{
			LOG_ERROR(Auth, "[MysqlAccountStore] CreateAccount: GenerateSalt failed");
			return 0;
		}
		engine::auth::Argon2Params params;
		const std::string final_hash = engine::auth::Hash(client_hash, server_salt, params);
		if (final_hash.empty())
		{
			LOG_ERROR(Auth, "[MysqlAccountStore] CreateAccount: Hash failed");
			return 0;
		}

		const std::string db_email = email_norm.empty() ? (login_key + std::string(kNoEmailPlaceholderSuffix)) : email_norm;
		const std::string esc_login = EscapeMysql(mysql, login_key);
		const std::string esc_email = EscapeMysql(mysql, db_email);
		const std::string esc_hash = EscapeMysql(mysql, final_hash);
		const std::string esc_country = EscapeMysql(mysql, cc);
		const std::string esc_tag_id = EscapeMysql(mysql, local_tag_id);
		const unsigned loc = static_cast<unsigned>(email_locale);

		std::string sql = "INSERT INTO accounts (email, login, password_hash, account_status, email_locale, email_verified, country_code, tag_id) VALUES ('";
		sql += esc_email;
		sql += "','";
		sql += esc_login;
		sql += "','";
		sql += esc_hash;
		sql += "',0,";
		sql += std::to_string(loc);
		sql += ",0,'";
		sql += esc_country;
		sql += "','";
		sql += esc_tag_id;
		sql += "')";

		auto t0 = std::chrono::steady_clock::now();
		const int qerr = mysql_real_query(mysql, sql.c_str(), static_cast<unsigned long>(sql.size()));
		engine::server::db::DbRecordLatencySince(t0);
		if (qerr != 0)
		{
			const unsigned err = mysql_errno(mysql);
			if (err == kMysqlDupKey)
				LOG_WARN(Auth, "[MysqlAccountStore] CreateAccount: duplicate login or email");
			else
				LOG_ERROR(Auth, "[MysqlAccountStore] CreateAccount INSERT: {}", mysql_error(mysql));
			return 0;
		}
		for (;;)
		{
			MYSQL_RES* res = mysql_store_result(mysql);
			if (res)
				mysql_free_result(res);
			if (mysql_errno(mysql) != 0)
			{
				LOG_ERROR(Auth, "[MysqlAccountStore] CreateAccount drain: {}", mysql_error(mysql));
				return 0;
			}
			if (!mysql_more_results(mysql))
				break;
			if (mysql_next_result(mysql) != 0)
			{
				LOG_ERROR(Auth, "[MysqlAccountStore] CreateAccount next_result: {}", mysql_error(mysql));
				return 0;
			}
		}

		const uint64_t id = mysql_insert_id(mysql);
		if (id == 0)
		{
			LOG_ERROR(Auth, "[MysqlAccountStore] CreateAccount: insert_id=0");
			return 0;
		}
		tag_id_out = local_tag_id; // Assign only on success — caller must not use tag_id_out if return is 0.
		LOG_INFO(Auth, "[MysqlAccountStore] CreateAccount OK (account_id={}, login={}, tag_id={})", id, login_key, tag_id_out);
		return id;
	}

	std::optional<AccountRecord> MysqlAccountStore::FindByLogin(std::string_view normalisedLogin)
	{
		if (!m_pool || !m_pool->IsInitialized())
			return std::nullopt;
		auto guard = m_pool->Acquire();
		MYSQL* mysql = guard.get();
		if (!mysql)
			return std::nullopt;
		const std::string esc = EscapeMysql(mysql, normalisedLogin);
		const std::string sql =
			"SELECT id, email, login, password_hash, account_status, email_verified, email_locale FROM accounts WHERE login='"
			+ esc + "' LIMIT 1";
		return QueryOneAccount(mysql, sql);
	}

	std::optional<AccountRecord> MysqlAccountStore::FindByAccountId(uint64_t account_id)
	{
		if (!m_pool || !m_pool->IsInitialized())
			return std::nullopt;
		auto guard = m_pool->Acquire();
		MYSQL* mysql = guard.get();
		if (!mysql)
			return std::nullopt;
		const std::string sql =
			"SELECT id, email, login, password_hash, account_status, email_verified, email_locale FROM accounts WHERE id="
			+ std::to_string(account_id) + " LIMIT 1";
		return QueryOneAccount(mysql, sql);
	}

	bool MysqlAccountStore::ExistsEmail(std::string_view normalisedEmail)
	{
		if (normalisedEmail.empty())
			return false;
		if (!m_pool || !m_pool->IsInitialized())
			return false;
		auto guard = m_pool->Acquire();
		MYSQL* mysql = guard.get();
		if (!mysql)
			return false;
		const std::string esc = EscapeMysql(mysql, normalisedEmail);
		const std::string sql = "SELECT 1 FROM accounts WHERE email='" + esc + "' LIMIT 1";
		return QueryExists(mysql, sql);
	}

	bool MysqlAccountStore::ExistsLogin(std::string_view normalisedLogin)
	{
		if (!m_pool || !m_pool->IsInitialized())
			return false;
		auto guard = m_pool->Acquire();
		MYSQL* mysql = guard.get();
		if (!mysql)
			return false;
		const std::string esc = EscapeMysql(mysql, normalisedLogin);
		const std::string sql = "SELECT 1 FROM accounts WHERE login='" + esc + "' LIMIT 1";
		return QueryExists(mysql, sql);
	}

	std::optional<AccountRecord> MysqlAccountStore::FindByEmail(std::string_view normalisedEmail)
	{
		if (normalisedEmail.empty())
			return std::nullopt;
		if (!m_pool || !m_pool->IsInitialized())
			return std::nullopt;
		auto guard = m_pool->Acquire();
		MYSQL* mysql = guard.get();
		if (!mysql)
			return std::nullopt;
		const std::string esc = EscapeMysql(mysql, normalisedEmail);
		const std::string sql =
			"SELECT id, email, login, password_hash, account_status, email_verified, email_locale FROM accounts WHERE email='"
			+ esc + "' LIMIT 1";
		return QueryOneAccount(mysql, sql);
	}

	bool MysqlAccountStore::SetEmailVerified(uint64_t account_id)
	{
		if (!m_pool || !m_pool->IsInitialized())
			return false;
		auto guard = m_pool->Acquire();
		MYSQL* mysql = guard.get();
		if (!mysql)
			return false;
		const std::string sql =
			"UPDATE accounts SET email_verified=1 WHERE id=" + std::to_string(account_id) + " LIMIT 1";
		if (!engine::server::db::DbExecute(mysql, sql))
			return false;
		if (mysql_affected_rows(mysql) == 0)
		{
			LOG_WARN(Auth, "[MysqlAccountStore] SetEmailVerified: account_id={} not found", account_id);
			return false;
		}
		LOG_INFO(Auth, "[MysqlAccountStore] SetEmailVerified OK (account_id={})", account_id);
		return true;
	}

	bool MysqlAccountStore::UpdatePasswordHash(uint64_t account_id, std::string_view new_final_hash)
	{
		if (!m_pool || !m_pool->IsInitialized())
			return false;
		auto guard = m_pool->Acquire();
		MYSQL* mysql = guard.get();
		if (!mysql)
			return false;
		const std::string esc_hash = EscapeMysql(mysql, new_final_hash);
		const std::string sql = "UPDATE accounts SET password_hash='" + esc_hash + "' WHERE id=" + std::to_string(account_id)
			+ " LIMIT 1";
		if (!engine::server::db::DbExecute(mysql, sql))
			return false;
		if (mysql_affected_rows(mysql) == 0)
		{
			LOG_WARN(Auth, "[MysqlAccountStore] UpdatePasswordHash: account_id={} not found", account_id);
			return false;
		}
		LOG_INFO(Auth, "[MysqlAccountStore] UpdatePasswordHash OK (account_id={})", account_id);
		return true;
	}
}
