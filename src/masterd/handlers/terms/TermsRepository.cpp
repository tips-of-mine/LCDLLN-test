#include "src/masterd/handlers/terms/TermsRepository.h"
#include "src/shared/core/Log.h"
#include "src/shared/db/ConnectionPool.h"
#include "src/shared/db/SqlPreparedStatement.h"

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

		// N1-C : conversion vers prepared statement. Le cache est requis ;
		// retourne false si nullptr (le call site doit fournir un cache valide
		// récupéré depuis ConnectionPool::Guard::cache()).
		bool FetchAllLocales(MYSQL* mysql, engine::server::db::SqlPreparedStatementCache* cache,
		                      uint64_t edition_id, std::vector<LocRow>& outRows)
		{
			if (!cache)
			{
				LOG_ERROR(Core, "[TermsRepository] FetchAllLocales : cache absent");
				return false;
			}
			auto* stmt = cache->Acquire(mysql,
				"SELECT locale, title, content FROM terms_localizations WHERE edition_id = ? ORDER BY locale ASC");
			if (!stmt || !stmt->Bind(0, edition_id) || !stmt->Execute())
			{
				LOG_ERROR(Core, "[TermsRepository] FetchAllLocales execute failed: {}", mysql_error(mysql));
				return false;
			}
			outRows.clear();
			while (stmt->FetchRow())
			{
				LocRow lr;
				lr.locale  = stmt->GetString(0);
				lr.title   = stmt->GetString(1);
				lr.content = stmt->GetString(2);
				outRows.push_back(std::move(lr));
			}
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
		auto* cache = guard.cache();
		if (!mysql || !cache)
			return false;
		// N1-C : prepared statement (bind account_id).
		auto* stmt = cache->Acquire(mysql,
			"SELECT COUNT(*) FROM terms_editions e WHERE e.status = 'published' "
			"AND e.published_at <= UTC_TIMESTAMP() AND NOT EXISTS ("
			"SELECT 1 FROM account_terms_acceptances a WHERE a.account_id = ? AND a.edition_id = e.id)");
		if (!stmt || !stmt->Bind(0, account_id) || !stmt->Execute() || !stmt->FetchRow())
		{
			LOG_ERROR(Core, "[TermsRepository] HasPendingTerms query: {}", mysql_error(mysql));
			return false;
		}
		return stmt->GetUInt64(0) > 0u;
	}

	uint32_t TermsRepository::CountPendingEditions(uint64_t account_id)
	{
		if (!m_enforce || !m_pool || !m_pool->IsInitialized())
			return 0;
		auto guard = m_pool->Acquire();
		MYSQL* mysql = guard.get();
		auto* cache = guard.cache();
		if (!mysql || !cache)
			return 0;
		// N1-C : prepared statement (bind account_id). Même query que HasPendingTerms
		// mais retourne le count tel quel (capé à 999 pour rester dans uint32).
		auto* stmt = cache->Acquire(mysql,
			"SELECT COUNT(*) FROM terms_editions e WHERE e.status = 'published' "
			"AND e.published_at <= UTC_TIMESTAMP() AND NOT EXISTS ("
			"SELECT 1 FROM account_terms_acceptances a WHERE a.account_id = ? AND a.edition_id = e.id)");
		if (!stmt || !stmt->Bind(0, account_id) || !stmt->Execute() || !stmt->FetchRow())
		{
			LOG_ERROR(Core, "[TermsRepository] CountPendingEditions: {}", mysql_error(mysql));
			return 0;
		}
		const uint64_t n = stmt->GetUInt64(0);
		return static_cast<uint32_t>(n > 999u ? 999u : n);
	}

	bool TermsRepository::GetEditionVersionLabel(uint64_t edition_id, std::string& out_label)
	{
		out_label.clear();
		if (!m_pool || !m_pool->IsInitialized())
			return false;
		auto guard = m_pool->Acquire();
		MYSQL* mysql = guard.get();
		auto* cache = guard.cache();
		if (!mysql || !cache)
			return false;
		auto* stmt = cache->Acquire(mysql,
			"SELECT version_label FROM terms_editions WHERE id = ? LIMIT 1");
		if (!stmt || !stmt->Bind(0, edition_id) || !stmt->Execute() || !stmt->FetchRow())
			return false;
		out_label = stmt->GetString(0);
		return !out_label.empty();
	}

	bool TermsRepository::GetFirstPending(uint64_t account_id, std::string_view locale_pref, PendingHead& out)
	{
		out = PendingHead{};
		if (!m_enforce || !m_pool || !m_pool->IsInitialized())
			return true;
		auto guard = m_pool->Acquire();
		MYSQL* mysql = guard.get();
		auto* cache = guard.cache();
		if (!mysql || !cache)
			return false;
		// N1-C : prepared statement (bind account_id).
		auto* stmt = cache->Acquire(mysql,
			"SELECT e.id, e.version_label FROM terms_editions e WHERE e.status = 'published' "
			"AND e.published_at <= UTC_TIMESTAMP() AND NOT EXISTS ("
			"SELECT 1 FROM account_terms_acceptances a WHERE a.account_id = ? AND a.edition_id = e.id) "
			"ORDER BY e.published_at ASC, e.id ASC LIMIT 1");
		if (!stmt || !stmt->Bind(0, account_id) || !stmt->Execute())
		{
			LOG_ERROR(Core, "[TermsRepository] GetFirstPending: {}", mysql_error(mysql));
			return false;
		}
		if (!stmt->FetchRow())
			return true; // aucune édition pending → OK, out reste vide
		out.edition_id = stmt->GetUInt64(0);
		out.version_label = stmt->GetString(1);

		std::vector<LocRow> locs;
		if (!FetchAllLocales(mysql, cache, out.edition_id, locs))
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
		auto* cache = guard.cache();
		if (!mysql || !cache)
			return false;
		std::vector<LocRow> locs;
		if (!FetchAllLocales(mysql, cache, edition_id, locs) || locs.empty())
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
		auto* cache = guard.cache();
		if (!mysql || !cache)
			return false;
		// N1-C : prepared statement (bind edition_id puis account_id — ordre des '?' dans le SQL).
		auto* stmt = cache->Acquire(mysql,
			"SELECT 1 FROM terms_editions e WHERE e.id = ? "
			"AND e.status = 'published' AND e.published_at <= UTC_TIMESTAMP() "
			"AND NOT EXISTS (SELECT 1 FROM account_terms_acceptances a WHERE a.account_id = ? AND a.edition_id = e.id) "
			"LIMIT 1");
		if (!stmt || !stmt->Bind(0, edition_id) || !stmt->Bind(1, account_id) || !stmt->Execute())
		{
			LOG_ERROR(Core, "[TermsRepository] IsEditionPendingForAccount: {}", mysql_error(mysql));
			return false;
		}
		return stmt->FetchRow();
	}

	bool TermsRepository::RecordAcceptance(uint64_t account_id, uint64_t edition_id)
	{
		if (!m_pool || !m_pool->IsInitialized())
			return false;
		auto guard = m_pool->Acquire();
		MYSQL* mysql = guard.get();
		auto* cache = guard.cache();
		if (!mysql || !cache)
			return false;
		// N1-C : prepared statement (bind account_id, edition_id).
		auto* stmt = cache->Acquire(mysql,
			"INSERT IGNORE INTO account_terms_acceptances (account_id, edition_id) VALUES (?, ?)");
		if (!stmt || !stmt->Bind(0, account_id) || !stmt->Bind(1, edition_id) || !stmt->Execute())
		{
			LOG_ERROR(Core, "[TermsRepository] RecordAcceptance: {}", mysql_error(mysql));
			return false;
		}
		return true;
	}
}
