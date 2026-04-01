#pragma once

#include "engine/core/Config.h"

#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace engine::client
{
	/// STAB.14 — Runtime localization catalogs loaded from content-relative JSON files.
	class LocalizationService final
	{
	public:
		using Params = std::vector<std::pair<std::string, std::string>>;

		/// Load available locale catalogs and select the initial locale with fallback.
		bool Init(const engine::core::Config& cfg, std::string_view requestedLocale);

		/// Release loaded catalogs.
		void Shutdown();

		/// Current locale tag (normalized short form like "fr" or "en").
		const std::string& GetCurrentLocale() const { return m_currentLocale; }

		/// Available locales discovered from content files.
		const std::vector<std::string>& GetAvailableLocales() const { return m_availableLocales; }

		/// Try to switch locale immediately. Falls back to default locale when missing.
		bool SetLocale(std::string_view localeTag);

		/// Translate one key with optional {name} parameter substitution.
		std::string Translate(std::string_view key, const Params& params = {}) const;

		/// Best-effort system locale detection normalized to the catalog format.
		static std::string DetectSystemLocaleTag();

		/// Normalize OS/user locale names to short lowercase catalog tags.
		static std::string NormalizeLocaleTag(std::string_view localeTag);

	private:
		using Catalog = std::unordered_map<std::string, std::string>;

		static bool ParseFlatJsonCatalog(std::string_view text, Catalog& outCatalog);
		static std::string ApplyParams(std::string text, const Params& params);

		bool LoadCatalog(std::string_view localeTag, Catalog& outCatalog) const;
		bool HasLocale(std::string_view localeTag) const;
		std::string ResolveSupportedLocale(std::string_view requestedLocale) const;

		engine::core::Config m_config{};
		std::vector<std::string> m_availableLocales{};
		Catalog m_defaultCatalog{};
		Catalog m_activeCatalog{};
		std::string m_defaultLocale = "en";
		std::string m_currentLocale = "en";
		bool m_initialized = false;
	};
}
