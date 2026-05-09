#include "engine/client/ThemeManager.h"

#include "engine/core/Log.h"
#include "engine/platform/FileSystem.h"

namespace engine::client
{
	ThemeManager::~ThemeManager()
	{
		Shutdown();
	}

	bool ThemeManager::Init(const engine::core::Config& config)
	{
		if (m_initialized)
		{
			LOG_WARN(Core, "[ThemeManager] Init ignored: already initialized");
			return true;
		}

		m_config = config;
		m_defaultThemeId = m_config.GetString("ui.theme.default_id", "default");
		m_themeRoot = m_config.GetString("ui.theme.root", "ui/themes");
		m_raceRoot = m_config.GetString("ui.theme.race_root", "ui/races");
		m_initialized = true;

		if (!SetTheme(m_defaultThemeId))
		{
			LOG_ERROR(Core, "[ThemeManager] Init FAILED: default theme load failed ({})", m_defaultThemeId);
			m_initialized = false;
			return false;
		}

		LOG_INFO(Core, "[ThemeManager] Init OK (default_theme={}, theme_root={}, race_root={})",
			m_defaultThemeId,
			m_themeRoot,
			m_raceRoot);
		return true;
	}

	void ThemeManager::Shutdown()
	{
		if (!m_initialized)
		{
			return;
		}

		m_initialized = false;
		m_activeTheme = {};
		m_defaultThemeId.clear();
		m_themeRoot.clear();
		m_raceRoot.clear();
		LOG_INFO(Core, "[ThemeManager] Destroyed");
	}

	bool ThemeManager::SetTheme(std::string_view themeId)
	{
		if (!m_initialized)
		{
			LOG_ERROR(Core, "[ThemeManager] SetTheme FAILED: manager not initialized");
			return false;
		}

		if (themeId.empty())
		{
			LOG_WARN(Core, "[ThemeManager] SetTheme FAILED: empty theme id");
			return false;
		}

		ThemeDefinition theme{};
		const std::string relativeDirectory = m_themeRoot + "/" + std::string(themeId);
		if (!LoadThemeDirectory(relativeDirectory, "theme", themeId, theme))
		{
			LOG_ERROR(Core, "[ThemeManager] SetTheme FAILED: {}", themeId);
			return false;
		}

		m_activeTheme = std::move(theme);
		LOG_INFO(Core, "[ThemeManager] Theme applied (id={}, qss_bytes={})", m_activeTheme.id, m_activeTheme.resolvedQss.size());
		return true;
	}

	bool ThemeManager::SetRace(std::string_view raceId)
	{
		if (!m_initialized)
		{
			LOG_ERROR(Core, "[ThemeManager] SetRace FAILED: manager not initialized");
			return false;
		}

		if (raceId.empty())
		{
			LOG_WARN(Core, "[ThemeManager] SetRace FAILED: empty race id");
			return false;
		}

		ThemeDefinition theme{};
		const std::string relativeDirectory = m_raceRoot + "/" + std::string(raceId);
		if (LoadThemeDirectory(relativeDirectory, "race", raceId, theme))
		{
			m_activeTheme = std::move(theme);
			LOG_INFO(Core, "[ThemeManager] Race theme applied (race={}, theme={})", raceId, m_activeTheme.id);
			return true;
		}

		LOG_WARN(Core, "[ThemeManager] Race theme missing: {} -> fallback to default {}", raceId, m_defaultThemeId);
		return SetTheme(m_defaultThemeId);
	}

	bool ThemeManager::LoadThemeDirectory(std::string_view relativeDirectory, std::string_view sourceType, std::string_view sourceId, ThemeDefinition& outTheme)
	{
		const std::string themeJsonRelativePath = std::string(relativeDirectory) + "/theme.json";
		const std::string styleRelativePath = std::string(relativeDirectory) + "/style.qss";
		const auto themeJsonFullPath = engine::platform::FileSystem::ResolveContentPath(m_config, themeJsonRelativePath);
		const auto styleFullPath = engine::platform::FileSystem::ResolveContentPath(m_config, styleRelativePath);

		if (!engine::platform::FileSystem::Exists(themeJsonFullPath))
		{
			LOG_WARN(Core, "[ThemeManager] Theme load FAILED: missing theme.json ({})", themeJsonRelativePath);
			return false;
		}

		if (!engine::platform::FileSystem::Exists(styleFullPath))
		{
			LOG_WARN(Core, "[ThemeManager] Theme load FAILED: missing style.qss ({})", styleRelativePath);
			return false;
		}

		engine::core::Config themeConfig;
		if (!themeConfig.LoadFromFile(themeJsonFullPath.string()))
		{
			LOG_ERROR(Core, "[ThemeManager] Theme load FAILED: invalid theme.json ({})", themeJsonRelativePath);
			return false;
		}

		const std::string qssText = engine::platform::FileSystem::ReadAllTextContent(m_config, styleRelativePath);
		if (qssText.empty())
		{
			LOG_ERROR(Core, "[ThemeManager] Theme load FAILED: empty style.qss ({})", styleRelativePath);
			return false;
		}

		outTheme = {};
		outTheme.id = themeConfig.GetString("id", std::string(sourceId));
		outTheme.displayName = themeConfig.GetString("displayName", outTheme.id);
		outTheme.sourceType = std::string(sourceType);
		outTheme.sourceId = std::string(sourceId);
		outTheme.themeRelativeDirectory = std::string(relativeDirectory);
		outTheme.themeJsonRelativePath = themeJsonRelativePath;
		outTheme.styleRelativePath = styleRelativePath;
		PopulateThemeVariables(themeConfig, outTheme);
		outTheme.resolvedQss = ResolveQssVariables(qssText, outTheme);

		LOG_INFO(Core, "[ThemeManager] Theme loaded (id={}, source_type={}, dir={})",
			outTheme.id,
			outTheme.sourceType,
			outTheme.themeRelativeDirectory);
		return true;
	}

	std::string ThemeManager::ResolveQssVariables(std::string_view qssText, const ThemeDefinition& theme) const
	{
		std::string resolved;
		resolved.reserve(qssText.size() + 64u);
		size_t position = 0;
		while (position < qssText.size())
		{
			const size_t markerStart = qssText.find("${", position);
			if (markerStart == std::string_view::npos)
			{
				resolved.append(qssText.substr(position));
				break;
			}

			resolved.append(qssText.substr(position, markerStart - position));
			const size_t markerEnd = qssText.find('}', markerStart + 2u);
			if (markerEnd == std::string_view::npos)
			{
				LOG_WARN(Core, "[ThemeManager] QSS variable ignored: missing closing brace");
				resolved.append(qssText.substr(markerStart));
				break;
			}

			const std::string key(qssText.substr(markerStart + 2u, markerEnd - (markerStart + 2u)));
			const auto it = theme.variables.find(key);
			if (it != theme.variables.end())
			{
				resolved += it->second;
			}
			else
			{
				LOG_WARN(Core, "[ThemeManager] QSS variable missing: {}", key);
			}

			position = markerEnd + 1u;
		}

		return resolved;
	}

	void ThemeManager::PopulateThemeVariables(const engine::core::Config& themeConfig, ThemeDefinition& outTheme) const
	{
		static constexpr std::string_view kVariableKeys[] = {
			"id",
			"displayName",
			"palette.primary",
			"palette.secondary",
			"palette.accent",
			"palette.background",
			"palette.surface",
			"palette.panel",
			"palette.text",
			"palette.mutedText",
			"palette.border",
			"typography.fontFamily",
			"typography.uiFontFamily",
			"typography.baseSize",
			"typography.titleSize",
			"metrics.radius",
			"metrics.padding"
		};

		for (const std::string_view key : kVariableKeys)
		{
			if (!themeConfig.Has(key))
			{
				continue;
			}
			outTheme.variables.emplace(std::string(key), themeConfig.GetString(key));
		}

		static constexpr std::string_view kAssetKeys[] = {
			"assets.iconHud",
			"assets.iconButton",
			"assets.iconTooltip",
			"assets.fontRegular"
		};
		for (const std::string_view key : kAssetKeys)
		{
			if (!themeConfig.Has(key))
			{
				continue;
			}
			outTheme.assetRefs.emplace(std::string(key), themeConfig.GetString(key));
		}

		// Expose logical widget classes required by the ticket.
		outTheme.variables.emplace("logical.hud", "hud");
		outTheme.variables.emplace("logical.panel", "panel");
		outTheme.variables.emplace("logical.button", "button");
		outTheme.variables.emplace("logical.tooltip", "tooltip");
		LOG_INFO(Core, "[ThemeManager] Theme variables ready (id={}, vars={}, assets={})",
			outTheme.id,
			outTheme.variables.size(),
			outTheme.assetRefs.size());
	}
}
