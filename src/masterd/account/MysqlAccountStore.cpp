/// @file MysqlAccountStore.cpp
/// @brief Implémentation de MysqlAccountStore — opérations CRUD MySQL sur la table `accounts`.
///
/// N1-D : toutes les queries SQL sont maintenant des prepared statements via
/// `SqlPreparedStatementCache` (récupéré sur `ConnectionPool::Guard::cache()`).
/// Plus de `mysql_real_escape_string` ni de concaténation — les bindings
/// `std::string_view` sont safe par construction. Aucun mot de passe en clair
/// ni hash intermédiaire n'est loggué ou persisté — seul le hash Argon2 final
/// est écrit.

#include "src/masterd/account/MysqlAccountStore.h"
#include "src/shared/account/AccountValidation.h"
#include "src/shared/db/ConnectionPool.h"
#include "src/shared/db/DbHelpers.h"
#include "src/shared/db/SqlPreparedStatement.h"
#include "src/shared/auth/Argon2Hash.h"
#include "src/shared/core/Log.h"
#include "src/shared/network/NetErrorCode.h"

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
		/// Suffixe utilisé comme placeholder d'e-mail pour les comptes sans adresse réelle.
		/// Format : "<login_normalisé>@lcdlln.no-email.local".
		/// Permet de satisfaire la contrainte UNIQUE(email) en base sans exposer de vraie adresse.
		constexpr std::string_view kNoEmailPlaceholderSuffix = "@lcdlln.no-email.local";

		/// Code d'erreur MySQL ER_DUP_ENTRY (violation de clé unique ou primaire).
		/// Évite la dépendance à mysqld_error.h dont la disponibilité varie selon l'installation.
		constexpr unsigned kMysqlDupKey = 1062;

		/// Liste de colonnes pour les SELECT * équivalents.
		/// Ordre fixe : id, email, login, password_hash, account_status,
		/// email_verified, email_locale, first_name, last_name, birth_date,
		/// country_code, tag_id.
		constexpr std::string_view kAccountColumns =
			"id, email, login, password_hash, account_status, email_verified, "
			"email_locale, first_name, last_name, birth_date, country_code, tag_id";

		/// Lit un AccountRecord depuis le résultat courant d'un SqlPreparedStatement.
		/// Les 12 colonnes doivent être dans l'ordre `kAccountColumns`.
		/// Les e-mails placeholder (@lcdlln.no-email.local) sont silencieusement
		/// remplacés par une chaîne vide (transparence vis-à-vis de l'appelant).
		AccountRecord StmtToRecord(engine::server::db::SqlPreparedStatement* stmt)
		{
			AccountRecord r;
			r.account_id = stmt->GetUInt64(0);
			std::string db_email = stmt->GetString(1);
			if (db_email.size() >= kNoEmailPlaceholderSuffix.size()
				&& db_email.compare(db_email.size() - kNoEmailPlaceholderSuffix.size(), kNoEmailPlaceholderSuffix.size(),
					kNoEmailPlaceholderSuffix) == 0)
				r.email.clear();
			else
				r.email = std::move(db_email);
			r.login = stmt->GetString(2);
			r.final_hash = stmt->GetString(3);
			const uint64_t st = stmt->GetUInt64(4);
			r.status = (st == 0u) ? AccountStatus::Active : AccountStatus::Locked;
			r.email_verified = (stmt->GetUInt64(5) != 0u);
			const uint64_t loc = stmt->GetUInt64(6);
			if (loc <= static_cast<uint64_t>(AccountEmailLocale::Italian))
				r.email_locale = static_cast<AccountEmailLocale>(loc);
			r.first_name = stmt->GetString(7);
			r.last_name = stmt->GetString(8);
			r.birth_date = stmt->GetString(9);
			r.country_code = stmt->GetString(10);
			r.tag_id = stmt->GetString(11);
			return r;
		}
	} // namespace

	MysqlAccountStore::MysqlAccountStore(engine::server::db::ConnectionPool* pool) : m_pool(pool) {}

	uint64_t MysqlAccountStore::CreateAccount(std::string_view login, std::string_view email, std::string_view client_hash,
		std::string_view first_name, std::string_view last_name, std::string_view birth_date,
		std::string_view country_code,
		std::string& tag_id_out,
		AccountEmailLocale email_locale)
	{
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
		// Validate cc is exactly 2 ASCII uppercase letters (anti-injection guard).
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
		// N1-D : prepared statement avec LIKE bindé (bind = prefix + "%").
		uint64_t seq = 1;
		{
			auto conn = m_pool->Acquire();
			MYSQL* mysql_seq = conn.get();
			auto* cache_seq = conn.cache();
			if (mysql_seq && cache_seq)
			{
				auto* stmt_seq = cache_seq->Acquire(mysql_seq,
					"SELECT COALESCE(MAX(CAST(SUBSTR(tag_id, 6) AS UNSIGNED)), 0) + 1 "
					"FROM accounts WHERE tag_id LIKE ?");
				const std::string like_pattern = prefix + "%";
				if (stmt_seq
					&& stmt_seq->Bind(0, std::string_view(like_pattern))
					&& stmt_seq->Execute()
					&& stmt_seq->FetchRow())
				{
					seq = stmt_seq->GetUInt64(0, 1);
				}
			}
		}

		// Build the TAG-ID: prefix (5 chars) + 5-digit zero-padded sequence.
		std::ostringstream tag_ss;
		tag_ss << prefix << std::setw(5) << std::setfill('0') << seq;
		const std::string local_tag_id = tag_ss.str(); // e.g. "FR60200001" — assigned to tag_id_out only on success

		auto guard = m_pool->Acquire();
		MYSQL* mysql = guard.get();
		auto* cache = guard.cache();
		if (!mysql || !cache)
		{
			LOG_WARN(Auth, "[MysqlAccountStore] CreateAccount: no connection or cache");
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
		const uint32_t loc = static_cast<uint32_t>(email_locale);

		// N1-D : INSERT prepared (9 binds, account_status=0 / email_verified=0 littéraux).
		auto* stmt = cache->Acquire(mysql,
			"INSERT INTO accounts (email, login, password_hash, account_status, email_locale, email_verified, "
			"country_code, tag_id, first_name, last_name, birth_date) "
			"VALUES (?, ?, ?, 0, ?, 0, ?, ?, ?, ?, ?)");
		auto t0 = std::chrono::steady_clock::now();
		const bool ok = stmt
			&& stmt->Bind(0, std::string_view(db_email))
			&& stmt->Bind(1, std::string_view(login_key))
			&& stmt->Bind(2, std::string_view(final_hash))
			&& stmt->Bind(3, loc)
			&& stmt->Bind(4, std::string_view(cc))
			&& stmt->Bind(5, std::string_view(local_tag_id))
			&& stmt->Bind(6, first_name)
			&& stmt->Bind(7, last_name)
			&& stmt->Bind(8, birth_date)
			&& stmt->Execute();
		engine::server::db::DbRecordLatencySince(t0);
		if (!ok)
		{
			const unsigned err = mysql_errno(mysql);
			if (err == kMysqlDupKey)
				LOG_WARN(Auth, "[MysqlAccountStore] CreateAccount: duplicate login or email");
			else
				LOG_ERROR(Auth, "[MysqlAccountStore] CreateAccount INSERT: {}", mysql_error(mysql));
			return 0;
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
		auto* cache = guard.cache();
		if (!mysql || !cache)
			return std::nullopt;
		// N1-D : prepared statement (bind login).
		auto* stmt = cache->Acquire(mysql,
			"SELECT id, email, login, password_hash, account_status, email_verified, "
			"email_locale, first_name, last_name, birth_date, country_code, tag_id "
			"FROM accounts WHERE login = ? LIMIT 1");
		if (!stmt || !stmt->Bind(0, normalisedLogin) || !stmt->Execute() || !stmt->FetchRow())
			return std::nullopt;
		return StmtToRecord(stmt);
	}

	std::optional<AccountRecord> MysqlAccountStore::FindByAccountId(uint64_t account_id)
	{
		if (!m_pool || !m_pool->IsInitialized())
			return std::nullopt;
		auto guard = m_pool->Acquire();
		MYSQL* mysql = guard.get();
		auto* cache = guard.cache();
		if (!mysql || !cache)
			return std::nullopt;
		// N1-D : prepared statement (bind account_id).
		auto* stmt = cache->Acquire(mysql,
			"SELECT id, email, login, password_hash, account_status, email_verified, "
			"email_locale, first_name, last_name, birth_date, country_code, tag_id "
			"FROM accounts WHERE id = ? LIMIT 1");
		if (!stmt || !stmt->Bind(0, account_id) || !stmt->Execute() || !stmt->FetchRow())
			return std::nullopt;
		return StmtToRecord(stmt);
	}

	bool MysqlAccountStore::ExistsEmail(std::string_view normalisedEmail)
	{
		if (normalisedEmail.empty())
			return false;
		if (!m_pool || !m_pool->IsInitialized())
			return false;
		auto guard = m_pool->Acquire();
		MYSQL* mysql = guard.get();
		auto* cache = guard.cache();
		if (!mysql || !cache)
			return false;
		auto* stmt = cache->Acquire(mysql, "SELECT 1 FROM accounts WHERE email = ? LIMIT 1");
		if (!stmt || !stmt->Bind(0, normalisedEmail) || !stmt->Execute())
			return false;
		return stmt->FetchRow();
	}

	bool MysqlAccountStore::ExistsLogin(std::string_view normalisedLogin)
	{
		if (!m_pool || !m_pool->IsInitialized())
			return false;
		auto guard = m_pool->Acquire();
		MYSQL* mysql = guard.get();
		auto* cache = guard.cache();
		if (!mysql || !cache)
			return false;
		auto* stmt = cache->Acquire(mysql, "SELECT 1 FROM accounts WHERE login = ? LIMIT 1");
		if (!stmt || !stmt->Bind(0, normalisedLogin) || !stmt->Execute())
			return false;
		return stmt->FetchRow();
	}

	std::optional<AccountRecord> MysqlAccountStore::FindByEmail(std::string_view normalisedEmail)
	{
		if (normalisedEmail.empty())
			return std::nullopt;
		if (!m_pool || !m_pool->IsInitialized())
			return std::nullopt;
		auto guard = m_pool->Acquire();
		MYSQL* mysql = guard.get();
		auto* cache = guard.cache();
		if (!mysql || !cache)
			return std::nullopt;
		auto* stmt = cache->Acquire(mysql,
			"SELECT id, email, login, password_hash, account_status, email_verified, "
			"email_locale, first_name, last_name, birth_date, country_code, tag_id "
			"FROM accounts WHERE email = ? LIMIT 1");
		if (!stmt || !stmt->Bind(0, normalisedEmail) || !stmt->Execute() || !stmt->FetchRow())
			return std::nullopt;
		return StmtToRecord(stmt);
	}

	bool MysqlAccountStore::SetEmailVerified(uint64_t account_id)
	{
		if (!m_pool || !m_pool->IsInitialized())
			return false;
		auto guard = m_pool->Acquire();
		MYSQL* mysql = guard.get();
		auto* cache = guard.cache();
		if (!mysql || !cache)
			return false;
		auto* stmt = cache->Acquire(mysql,
			"UPDATE accounts SET email_verified = 1 WHERE id = ? LIMIT 1");
		if (!stmt || !stmt->Bind(0, account_id) || !stmt->Execute())
			return false;
		if (stmt->AffectedRows() == 0u)
		{
			LOG_WARN(Auth, "[MysqlAccountStore] SetEmailVerified: account_id={} not found", account_id);
			return false;
		}
		LOG_INFO(Auth, "[MysqlAccountStore] SetEmailVerified OK (account_id={})", account_id);
		return true;
	}

	void MysqlAccountStore::PersistEmailVerificationCode(uint64_t account_id, const std::string& code)
	{
		if (!m_pool || !m_pool->IsInitialized())
			return;
		auto guard = m_pool->Acquire();
		MYSQL* mysql = guard.get();
		auto* cache = guard.cache();
		if (!mysql || !cache)
			return;
		// N1-D : delete preceded by insert, both prepared.
		// Delete any previous unverified entry for this account, then insert the new one.
		{
			auto* delStmt = cache->Acquire(mysql,
				"DELETE FROM email_verifications WHERE account_id = ? AND verified_at IS NULL");
			if (delStmt && delStmt->Bind(0, account_id))
				delStmt->Execute();
		}
		auto* insStmt = cache->Acquire(mysql,
			"INSERT INTO email_verifications (account_id, code, expires_at) "
			"VALUES (?, ?, NOW() + INTERVAL 15 MINUTE)");
		if (!insStmt
			|| !insStmt->Bind(0, account_id)
			|| !insStmt->Bind(1, std::string_view(code))
			|| !insStmt->Execute())
		{
			LOG_WARN(Auth, "[MysqlAccountStore] PersistEmailVerificationCode: INSERT failed (account_id={})", account_id);
			return;
		}
		LOG_INFO(Auth, "[MysqlAccountStore] PersistEmailVerificationCode OK (account_id={})", account_id);
	}

	bool MysqlAccountStore::UpdatePasswordHash(uint64_t account_id, std::string_view new_final_hash)
	{
		if (!m_pool || !m_pool->IsInitialized())
			return false;
		auto guard = m_pool->Acquire();
		MYSQL* mysql = guard.get();
		auto* cache = guard.cache();
		if (!mysql || !cache)
			return false;
		auto* stmt = cache->Acquire(mysql,
			"UPDATE accounts SET password_hash = ? WHERE id = ? LIMIT 1");
		if (!stmt
			|| !stmt->Bind(0, new_final_hash)
			|| !stmt->Bind(1, account_id)
			|| !stmt->Execute())
			return false;
		if (stmt->AffectedRows() == 0u)
		{
			LOG_WARN(Auth, "[MysqlAccountStore] UpdatePasswordHash: account_id={} not found", account_id);
			return false;
		}
		LOG_INFO(Auth, "[MysqlAccountStore] UpdatePasswordHash OK (account_id={})", account_id);
		return true;
	}

	AccountRole MysqlAccountStore::GetRole(uint64_t account_id)
	{
		if (!m_pool)
			return AccountRole::Player;
		auto guard = m_pool->Acquire();
		MYSQL* mysql = guard.get();
		auto* cache = guard.cache();
		if (!mysql || !cache)
			return AccountRole::Player;
		auto* stmt = cache->Acquire(mysql, "SELECT role FROM accounts WHERE id = ? LIMIT 1");
		if (!stmt || !stmt->Bind(0, account_id) || !stmt->Execute() || !stmt->FetchRow())
			return AccountRole::Player;
		const std::string roleStr = stmt->GetString(0);
		if (roleStr.empty())
			return AccountRole::Player;
		return ParseRole(roleStr.c_str());
	}

	bool MysqlAccountStore::SetRole(uint64_t account_id, AccountRole role)
	{
		if (role == AccountRole::Console)
			return false;  // jamais persisté

		if (!m_pool)
			return false;
		auto guard = m_pool->Acquire();
		MYSQL* mysql = guard.get();
		auto* cache = guard.cache();
		if (!mysql || !cache)
			return false;

		const std::string roleStr = std::string(RoleToString(role));
		auto* stmt = cache->Acquire(mysql,
			"UPDATE accounts SET role = ? WHERE id = ? LIMIT 1");
		if (!stmt
			|| !stmt->Bind(0, std::string_view(roleStr))
			|| !stmt->Bind(1, account_id)
			|| !stmt->Execute())
			return false;
		if (stmt->AffectedRows() == 0u)
		{
			LOG_WARN(Auth, "[MysqlAccountStore] SetRole: account_id={} not found", account_id);
			return false;
		}
		LOG_INFO(Auth, "[MysqlAccountStore] SetRole account_id={} role={}",
			account_id, roleStr);
		return true;
	}

	/// Met a jour account_status (Active=0, Locked=2) via UPDATE direct.
	/// Utilise par AdminCommandHandler::DispatchBan.
	bool MysqlAccountStore::SetAccountStatus(uint64_t account_id, AccountStatus status)
	{
		if (!m_pool)
			return false;
		auto guard = m_pool->Acquire();
		MYSQL* mysql = guard.get();
		auto* cache = guard.cache();
		if (!mysql || !cache)
			return false;

		const uint32_t statusInt = static_cast<uint32_t>(status);
		auto* stmt = cache->Acquire(mysql,
			"UPDATE accounts SET account_status = ? WHERE id = ? LIMIT 1");
		if (!stmt
			|| !stmt->Bind(0, statusInt)
			|| !stmt->Bind(1, account_id)
			|| !stmt->Execute())
			return false;
		if (stmt->AffectedRows() == 0u)
		{
			LOG_WARN(Auth, "[MysqlAccountStore] SetAccountStatus: account_id={} not found", account_id);
			return false;
		}
		LOG_INFO(Auth, "[MysqlAccountStore] SetAccountStatus account_id={} status={}",
			account_id, statusInt);
		return true;
	}
}
