#pragma once

#include "engine/core/Config.h"

#include <string>
#include <string_view>
#include <unordered_map>

namespace engine::client
{
	/// Loaded theme definition built from one `theme.json` and its resolved stylesheet.
	struct ThemeDefinition
	{
		std::string id;
		std::string displayName;
		std::string sourceType;
		std::string sourceId;
		std::string themeRelativeDirectory;
		std::string themeJsonRelativePath;
		std::string styleRelativePath;
		std::string resolvedQss;
		std::unordered_map<std::string, std::string> variables;
		std::unordered_map<std::string, std::string> assetRefs;
	};

	/// Loads UI themes from content-relative `theme.json` and `style.qss` files.
	class ThemeManager final
	{
	public:
		/// Construct an uninitialized theme manager.
		ThemeManager() = default;

		/// Release loaded theme resources.
		~ThemeManager();

		/// Initialize the manager and load the configured default theme.
		bool Init(const engine::core::Config& config);

		/// Shutdown the manager and release resolved style data.
		void Shutdown();

		/// Apply one explicit theme id from `ui/themes/<id>/`.
		bool SetTheme(std::string_view themeId);

		/// Apply one race theme from `ui/races/<raceId>/` with fallback to the default theme.
		bool SetRace(std::string_view raceId);

		/// Return the active loaded theme definition.
		const ThemeDefinition& GetActiveTheme() const { return m_activeTheme; }

	private:
		/// Load one theme definition from the provided content-relative directory.
		bool LoadThemeDirectory(std::string_view relativeDirectory, std::string_view sourceType, std::string_view sourceId, ThemeDefinition& outTheme);

		/// Resolve `${key}` placeholders in one stylesheet using the loaded theme variables.
		std::string ResolveQssVariables(std::string_view qssText, const ThemeDefinition& theme) const;

		/// Collect scalar values from a flattened config into the theme definition.
		void PopulateThemeVariables(const engine::core::Config& themeConfig, ThemeDefinition& outTheme) const;

		engine::core::Config m_config{};
		ThemeDefinition m_activeTheme{};
		std::string m_defaultThemeId;
		std::string m_themeRoot;
		std::string m_raceRoot;
		bool m_initialized = false;
	};
}
