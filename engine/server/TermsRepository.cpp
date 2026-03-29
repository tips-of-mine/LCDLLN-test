#include "engine/server/TermsRepository.h"
#include "engine/server/db/ConnectionPool.h"
#include "engine/core/Log.h"

#include <mysql.h>

#include <algorithm>
#include <cctype>
#include <cstring>

namespace engine::server
{
	namespace
	{
		std::string NormLocale(std::string_view s)
		{
			std::string out;
			for (char c : s)
			{
				if (!std::isspace(static_cast<unsigned char>(c)))
					out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
			}
			const size_t dash = out.find('-');
			if (dash != std::string::npos)
				out.resize(dash);
			return out;
		}

		struct LocRow
		{
			std::string locale;
			std::string title;
			std::string content;
		};

		std::optional<LocRow> PickLocalization(const std::vector<LocRow>& rows, std::string_view pref,
		                                         std::string_view fallback)
		{
			if (rows.empty())
				return std::nullopt;
			const std::string p = NormLocale(pref);
			for (const auto& r : rows)
			{
				if (NormLocale(r.locale) == p)
					return r;
			}
			if (!p.empty())
			{
				for (const auto& r : rows)
				{
					const std::string rl = NormLocale(r.locale);
					if (rl.size() >= p.size() && rl.compare(0, p.size(), p) == 0)
						return r;
				}
			}
			const std::string fb = NormLocale(fallback);
			for (const auto& r : rows)
			{
				if (NormLocale(r.locale) == fb)
					return r;
			}
			return rows[0];
		}

		bool FetchAllLocales(MYSQL* mysql, uint64_t edition_id, std::vector<LocRow>& outRows)
		{
		std::string sql = "SELECT locale, title, content FROM terms_localizations WHERE edition_id = "
			+ std::to_string(edition_id) + " ORDER BY locale ASC";
			if (mysql_query(mysql, sql.c_str()) != 0)
			{
				LOG_ERROR(Core, "[TermsRepository] query locales: {}", mysql_error(mysql));
				return false;
			}
			MYSQL_RES* res = mysql_store_result(mysql);
			if (!res)
			{
				LOG_ERROR(Core, "[TermsRepository] store_result locales failed");
				return false;
			}
			outRows.clear();
			MYSQL_ROW row;
			while ((row = mysql_fetch_row(res)))
			{
				LocRow lr;
				if (row[0]) lr.locale = row[0];
				if (row[1]) lr.title = row[1];
				if (row[2]) lr.content = row[2];
				outRows.push_back(std::move(lr));
			}
			mysql_free_result(res);
			return true;
		}
	} // namespace

	void TermsRepository::Init(const engine::core::Config& config, engine::server::db::ConnectionPool* pool)
	{
		m_pool    = pool;
		m_enforce = config.GetBool("terms.enforce", false);
		m_fallback_locale = config.GetString("terms.fallback_locale", "en");
		if (m_fallback_locale.empty())
			m_fallback_locale = "en";
		if (m_enforce && (!m_pool || !m_pool->IsInitialized()))
			LOG_WARN(Core, "[TermsRepository] terms.enforce=true but DB pool unavailable — CGU checks skipped (no block)");
	}

	bool TermsRepository::HasPendingTerms(uint64_t account_id)
	{
		if (!m_enforce || !m_pool || !m_pool->IsInitialized())
			return false;
		auto guard = m_pool->Acquire();
		MYSQL* mysql = guard.get();
		if (!mysql)
			return false;
		std::string sql =
			"SELECT COUNT(*) FROM terms_editions e WHERE e.status = 'published' "
			"AND e.published_at <= UTC_TIMESTAMP() AND NOT EXISTS ("
			"SELECT 1 FROM account_terms_acceptances a WHERE a.account_id = "
			+ std::to_string(account_id) + " AND a.edition_id = e.id)";
		if (mysql_query(mysql, sql.c_str()) != 0)
		{
			LOG_ERROR(Core, "[TermsRepository] HasPendingTerms query: {}", mysql_error(mysql));
			return false;
		}
		MYSQL_RES* res = mysql_store_result(mysql);
		if (!res)
			return false;
		MYSQL_ROW row = mysql_fetch_row(res);
		unsigned long n = 0;
		if (row && row[0])
			n = std::strtoul(row[0], nullptr, 10);
		mysql_free_result(res);
		return n > 0;
	}

	uint32_t TermsRepository::CountPendingEditions(uint64_t account_id)
	{
		if (!m_enforce || !m_pool || !m_pool->IsInitialized())
			return 0;
		auto guard = m_pool->Acquire();
		MYSQL* mysql = guard.get();
		if (!mysql)
			return 0;
		std::string sql =
			"SELECT COUNT(*) FROM terms_editions e WHERE e.status = 'published' "
			"AND e.published_at <= UTC_TIMESTAMP() AND NOT EXISTS ("
			"SELECT 1 FROM account_terms_acceptances a WHERE a.account_id = "
			+ std::to_string(account_id) + " AND a.edition_id = e.id)";
		if (mysql_query(mysql, sql.c_str()) != 0)
		{
			LOG_ERROR(Core, "[TermsRepository] CountPendingEditions: {}", mysql_error(mysql));
			return 0;
		}
		MYSQL_RES* res = mysql_store_result(mysql);
		if (!res)
			return 0;
		MYSQL_ROW row = mysql_fetch_row(res);
		unsigned long n = 0;
		if (row && row[0])
			n = std::strtoul(row[0], nullptr, 10);
		mysql_free_result(res);
		return static_cast<uint32_t>(n > 999u ? 999u : n);
	}

	bool TermsRepository::GetEditionVersionLabel(uint64_t edition_id, std::string& out_label)
	{
		out_label.clear();
		if (!m_pool || !m_pool->IsInitialized())
			return false;
		auto guard = m_pool->Acquire();
		MYSQL* mysql = guard.get();
		if (!mysql)
			return false;
		std::string sql = "SELECT version_label FROM terms_editions WHERE id = " + std::to_string(edition_id) + " LIMIT 1";
		if (mysql_query(mysql, sql.c_str()) != 0)
			return false;
		MYSQL_RES* res = mysql_store_result(mysql);
		if (!res)
			return false;
		MYSQL_ROW row = mysql_fetch_row(res);
		if (row && row[0])
			out_label = row[0];
		mysql_free_result(res);
		return !out_label.empty();
	}

	bool TermsRepository::GetFirstPending(uint64_t account_id, std::string_view locale_pref, PendingHead& out)
	{
		out = PendingHead{};
		if (!m_enforce || !m_pool || !m_pool->IsInitialized())
			return true;
		auto guard = m_pool->Acquire();
		MYSQL* mysql = guard.get();
		if (!mysql)
			return false;
		std::string sql =
			"SELECT e.id, e.version_label FROM terms_editions e WHERE e.status = 'published' "
			"AND e.published_at <= UTC_TIMESTAMP() AND NOT EXISTS ("
			"SELECT 1 FROM account_terms_acceptances a WHERE a.account_id = "
			+ std::to_string(account_id) + " AND a.edition_id = e.id) "
			"ORDER BY e.published_at ASC, e.id ASC LIMIT 1";
		if (mysql_query(mysql, sql.c_str()) != 0)
		{
			LOG_ERROR(Core, "[TermsRepository] GetFirstPending: {}", mysql_error(mysql));
			return false;
		}
		MYSQL_RES* res = mysql_store_result(mysql);
		if (!res)
			return false;
		MYSQL_ROW row = mysql_fetch_row(res);
		if (!row || !row[0])
		{
			mysql_free_result(res);
			return true;
		}
		out.edition_id = std::strtoull(row[0], nullptr, 10);
		if (row[1])
			out.version_label = row[1];
		mysql_free_result(res);

		std::vector<LocRow> locs;
		if (!FetchAllLocales(mysql, out.edition_id, locs))
			return false;
		auto pick = PickLocalization(locs, locale_pref, m_fallback_locale);
		if (!pick)
			return false;
		out.title            = pick->title;
		out.resolved_locale  = pick->locale;
		return true;
	}

	bool TermsRepository::LoadEditionContent(uint64_t edition_id, std::string_view locale_pref, ContentChunk& out)
	{
		out = ContentChunk{};
		if (!m_pool || !m_pool->IsInitialized())
			return false;
		auto guard = m_pool->Acquire();
		MYSQL* mysql = guard.get();
		if (!mysql)
			return false;
		std::vector<LocRow> locs;
		if (!FetchAllLocales(mysql, edition_id, locs) || locs.empty())
			return false;
		auto pick = PickLocalization(locs, locale_pref, m_fallback_locale);
		if (!pick)
			return false;
		out.full_text         = pick->content;
		out.resolved_locale   = pick->locale;
		return true;
	}

	bool TermsRepository::IsEditionPendingForAccount(uint64_t account_id, uint64_t edition_id)
	{
		if (!m_enforce || !m_pool || !m_pool->IsInitialized())
			return false;
		auto guard = m_pool->Acquire();
		MYSQL* mysql = guard.get();
		if (!mysql)
			return false;
		std::string sql =
			"SELECT 1 FROM terms_editions e WHERE e.id = "
			+ std::to_string(edition_id)
			+ " AND e.status = 'published' AND e.published_at <= UTC_TIMESTAMP() "
			"AND NOT EXISTS (SELECT 1 FROM account_terms_acceptances a WHERE a.account_id = "
			+ std::to_string(account_id) + " AND a.edition_id = e.id) LIMIT 1";
		if (mysql_query(mysql, sql.c_str()) != 0)
		{
			LOG_ERROR(Core, "[TermsRepository] IsEditionPendingForAccount: {}", mysql_error(mysql));
			return false;
		}
		MYSQL_RES* res = mysql_store_result(mysql);
		if (!res)
			return false;
		MYSQL_ROW row = mysql_fetch_row(res);
		const bool ok = (row != nullptr);
		mysql_free_result(res);
		return ok;
	}

	bool TermsRepository::RecordAcceptance(uint64_t account_id, uint64_t edition_id)
	{
		if (!m_pool || !m_pool->IsInitialized())
			return false;
		auto guard = m_pool->Acquire();
		MYSQL* mysql = guard.get();
		if (!mysql)
			return false;
		std::string sql = "INSERT IGNORE INTO account_terms_acceptances (account_id, edition_id) VALUES ("
			+ std::to_string(account_id) + ", " + std::to_string(edition_id) + ")";
		if (mysql_query(mysql, sql.c_str()) != 0)
		{
			LOG_ERROR(Core, "[TermsRepository] RecordAcceptance: {}", mysql_error(mysql));
			return false;
		}
		return true;
	}
}
