#include "src/shardd/internals/globals/LocaleStrings.h"

#include "src/shared/db/ConnectionPool.h"
#include "src/shared/db/SqlPreparedStatement.h"
#include "src/shared/core/Log.h"

#include <mysql.h>

namespace engine::server::shard::globals
{
	bool LocaleStrings::Load(engine::server::db::ConnectionPool& pool, LocaleId defaultLocale)
	{
		if (m_loaded)
			return false;

		m_defaultLocale = defaultLocale;

		auto guard = pool.Acquire();
		MYSQL* mysql = guard.get();
		auto* cache = guard.cache();
		if (!mysql || !cache)
			return false;

		// N1-I : prepared statement no-param.
		auto* stmt = cache->Acquire(mysql,
			"SELECT string_id, locale_id, text FROM locale_strings");
		if (!stmt || !stmt->Execute())
			return false;

		while (stmt->FetchRow())
		{
			Key k{
				static_cast<uint32_t>(stmt->GetUInt64(0)),
				static_cast<LocaleId>(stmt->GetInt32(1))
			};
			m_strings.emplace(k, stmt->GetString(2));
		}

		m_loaded = true;
		LOG_INFO(Core, "[LocaleStrings] Loaded {} strings, default locale = {}",
			m_strings.size(), static_cast<int>(defaultLocale));
		return true;
	}

	std::string LocaleStrings::GetString(uint32_t stringId, LocaleId localeId) const
	{
		auto it = m_strings.find(Key{stringId, localeId});
		if (it != m_strings.end())
			return it->second;
		// Fallback default locale.
		if (localeId != m_defaultLocale)
		{
			auto it2 = m_strings.find(Key{stringId, m_defaultLocale});
			if (it2 != m_strings.end())
				return it2->second;
		}
		// Sentinel debug : jamais empty.
		return "[stringId=" + std::to_string(stringId) + "]";
	}

	std::string LocaleStrings::Format(uint32_t stringId, LocaleId localeId,
		std::string_view arg0,
		std::string_view arg1,
		std::string_view arg2,
		std::string_view arg3) const
	{
		std::string s = GetString(stringId, localeId);
		// Replace {0}..{3} (1 pass naïf — performance OK pour les chaînes courtes).
		auto replace = [](std::string& s, std::string_view placeholder, std::string_view value) {
			const auto pos = s.find(placeholder);
			if (pos != std::string::npos)
				s.replace(pos, placeholder.size(), value);
		};
		replace(s, "{0}", arg0);
		replace(s, "{1}", arg1);
		replace(s, "{2}", arg2);
		replace(s, "{3}", arg3);
		return s;
	}
}
