#pragma once
// CMANGOS.16 (Phase 1b) — LocaleStrings : cache (stringId, localeId) avec
// fallback sur la default_locale.

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>

namespace engine::server::db
{
	class ConnectionPool;
}

namespace engine::server::shard::globals
{
	/// Locale id : 0=fr_FR (default LCDLLN), 1=en_US, etc.
	using LocaleId = uint8_t;

	class LocaleStrings
	{
	public:
		LocaleStrings() = default;
		~LocaleStrings() = default;
		LocaleStrings(const LocaleStrings&) = delete;
		LocaleStrings& operator=(const LocaleStrings&) = delete;

		/// Charge `locale_strings` depuis la DB. \pre Une seule fois.
		bool Load(engine::server::db::ConnectionPool& pool, LocaleId defaultLocale);

		/// Retourne le texte pour (stringId, localeId). Fallback sur defaultLocale
		/// si la locale demandée est manquante. Si même la default_locale manque,
		/// retourne `"[stringId=<id>]"` (jamais empty pour aider le debug).
		std::string GetString(uint32_t stringId, LocaleId localeId) const;

		/// Helper format avec placeholders `{0}`, `{1}`, ...
		/// Maximum 4 args supportés en Phase 1b. YAGNI — étendre si besoin.
		std::string Format(uint32_t stringId, LocaleId localeId,
			std::string_view arg0 = {},
			std::string_view arg1 = {},
			std::string_view arg2 = {},
			std::string_view arg3 = {}) const;

		size_t Size() const { return m_strings.size(); }

	private:
		struct Key
		{
			uint32_t stringId;
			LocaleId localeId;
			bool operator==(const Key& o) const = default;
		};
		struct KeyHash
		{
			size_t operator()(const Key& k) const noexcept
			{
				return (static_cast<size_t>(k.stringId) << 8) ^ k.localeId;
			}
		};

		std::unordered_map<Key, std::string, KeyHash> m_strings;
		LocaleId m_defaultLocale = 0;
		bool m_loaded = false;
	};
}
