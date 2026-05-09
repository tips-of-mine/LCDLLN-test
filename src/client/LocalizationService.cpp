#include "engine/client/LocalizationService.h"

#include "engine/core/Log.h"
#include "engine/platform/FileSystem.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>

#if defined(_WIN32)
#	define WIN32_LEAN_AND_MEAN
#	include <windows.h>
#endif

namespace engine::client
{
	namespace
	{
		constexpr std::string_view kLocalizationRootDir = "localization";

		bool IsHex(char c)
		{
			return (c >= '0' && c <= '9')
				|| (c >= 'a' && c <= 'f')
				|| (c >= 'A' && c <= 'F');
		}

		bool ParseJsonString(std::string_view text, size_t& pos, std::string& out)
		{
			if (pos >= text.size() || text[pos] != '"')
				return false;
			++pos;
			out.clear();
			while (pos < text.size())
			{
				const char c = text[pos++];
				if (c == '"')
					return true;
				if (c == '\\')
				{
					if (pos >= text.size())
						return false;
					const char esc = text[pos++];
					switch (esc)
					{
					case '"': out.push_back('"'); break;
					case '\\': out.push_back('\\'); break;
					case '/': out.push_back('/'); break;
					case 'b': out.push_back('\b'); break;
					case 'f': out.push_back('\f'); break;
					case 'n': out.push_back('\n'); break;
					case 'r': out.push_back('\r'); break;
					case 't': out.push_back('\t'); break;
					case 'u':
						if (pos + 4 > text.size())
							return false;
						for (size_t i = 0; i < 4; ++i)
						{
							if (!IsHex(text[pos + i]))
								return false;
						}
						pos += 4;
						out.push_back('?');
						break;
					default:
						return false;
					}
				}
				else
				{
					out.push_back(c);
				}
			}
			return false;
		}

		void SkipWs(std::string_view text, size_t& pos)
		{
			while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos])) != 0)
				++pos;
		}
	}

	bool LocalizationService::Init(const engine::core::Config& cfg, std::string_view requestedLocale)
	{
		if (m_initialized)
		{
			LOG_WARN(Core, "[LocalizationService] Init ignored: already initialized");
			return true;
		}

		m_config = cfg;
		m_availableLocales.clear();
		m_defaultCatalog.clear();
		m_activeCatalog.clear();
		m_defaultLocale = "en";
		m_currentLocale = "en";

		const std::filesystem::path dir = engine::platform::FileSystem::ResolveContentPath(cfg, kLocalizationRootDir);
		for (const std::filesystem::path& entry : engine::platform::FileSystem::ListDirectory(dir))
		{
			if (!std::filesystem::is_directory(entry))
				continue;
			const std::string locale = NormalizeLocaleTag(entry.filename().string());
			if (locale.empty())
				continue;
			const std::filesystem::path localeFile = entry / (locale + ".json");
			if (!std::filesystem::exists(localeFile))
				continue;
			if (std::find(m_availableLocales.begin(), m_availableLocales.end(), locale) == m_availableLocales.end())
				m_availableLocales.push_back(locale);
		}
		std::sort(m_availableLocales.begin(), m_availableLocales.end());

		if (m_availableLocales.empty())
		{
			LOG_ERROR(Core, "[LocalizationService] Init FAILED: no locale catalogs found in {}", dir.string());
			return false;
		}

		if (!LoadCatalog(m_defaultLocale, m_defaultCatalog))
		{
			LOG_ERROR(Core, "[LocalizationService] Init FAILED: default locale '{}' missing or invalid", m_defaultLocale);
			return false;
		}

		const std::string initialLocale = ResolveSupportedLocale(requestedLocale);
		if (!SetLocale(initialLocale))
		{
			LOG_ERROR(Core, "[LocalizationService] Init FAILED: could not activate locale '{}'", initialLocale);
			return false;
		}

		m_initialized = true;
		LOG_INFO(Core, "[LocalizationService] Init OK (locales={}, current={}, fallback={})",
			m_availableLocales.size(), m_currentLocale, m_defaultLocale);
		return true;
	}

	void LocalizationService::Shutdown()
	{
		if (!m_initialized)
			return;
		m_availableLocales.clear();
		m_defaultCatalog.clear();
		m_activeCatalog.clear();
		m_currentLocale = m_defaultLocale;
		m_initialized = false;
		LOG_INFO(Core, "[LocalizationService] Destroyed");
	}

	bool LocalizationService::SetLocale(std::string_view localeTag)
	{
		if (!m_initialized && m_defaultCatalog.empty())
		{
			LOG_WARN(Core, "[LocalizationService] SetLocale ignored: service not ready");
			return false;
		}

		const std::string resolved = ResolveSupportedLocale(localeTag);
		Catalog nextCatalog;
		bool exactLocaleLoaded = true;
		if (resolved == m_defaultLocale)
		{
			nextCatalog = m_defaultCatalog;
		}
		else if (!LoadCatalog(resolved, nextCatalog))
		{
			LOG_WARN(Core, "[LocalizationService] SetLocale fallback to '{}' because '{}' could not be loaded", m_defaultLocale, localeTag);
			nextCatalog = m_defaultCatalog;
			exactLocaleLoaded = false;
		}

		m_activeCatalog = std::move(nextCatalog);
		m_currentLocale = exactLocaleLoaded ? resolved : m_defaultLocale;
		LOG_INFO(Core, "[LocalizationService] Locale applied ({})", m_currentLocale);
		return exactLocaleLoaded;
	}

	std::string LocalizationService::Translate(std::string_view key, const Params& params) const
	{
		auto activeIt = m_activeCatalog.find(std::string(key));
		if (activeIt != m_activeCatalog.end())
			return ApplyParams(activeIt->second, params);

		auto fallbackIt = m_defaultCatalog.find(std::string(key));
		if (fallbackIt != m_defaultCatalog.end())
		{
			LOG_DEBUG(Core, "[LocalizationService] Missing key '{}' in locale '{}', using fallback '{}'", key, m_currentLocale, m_defaultLocale);
			return ApplyParams(fallbackIt->second, params);
		}

		LOG_WARN(Core, "[LocalizationService] Missing translation key '{}' (locale={}, fallback={})", key, m_currentLocale, m_defaultLocale);
		return std::string(key);
	}

	std::string LocalizationService::DetectSystemLocaleTag()
	{
#if defined(_WIN32)
		wchar_t buffer[LOCALE_NAME_MAX_LENGTH] = {};
		const int len = GetUserDefaultLocaleName(buffer, LOCALE_NAME_MAX_LENGTH);
		if (len > 0)
		{
			std::string utf8;
			const int needed = WideCharToMultiByte(CP_UTF8, 0, buffer, -1, nullptr, 0, nullptr, nullptr);
			if (needed > 1)
			{
				utf8.resize(static_cast<size_t>(needed));
				WideCharToMultiByte(CP_UTF8, 0, buffer, -1, utf8.data(), needed, nullptr, nullptr);
				if (!utf8.empty() && utf8.back() == '\0')
					utf8.pop_back();
				const std::string normalized = NormalizeLocaleTag(utf8);
				LOG_INFO(Core, "[LocalizationService] System locale detected via Win32: {} -> {}", utf8, normalized);
				return normalized.empty() ? std::string("en") : normalized;
			}
		}
#endif
		const char* vars[] = { "LC_ALL", "LC_MESSAGES", "LANG" };
		for (const char* name : vars)
		{
			const char* value = std::getenv(name);
			if (value && *value)
			{
				const std::string normalized = NormalizeLocaleTag(value);
				LOG_INFO(Core, "[LocalizationService] System locale detected via env {}={} -> {}", name, value, normalized);
				return normalized.empty() ? std::string("en") : normalized;
			}
		}

		LOG_WARN(Core, "[LocalizationService] System locale detection fallback -> en");
		return "en";
	}

	std::string LocalizationService::NormalizeLocaleTag(std::string_view localeTag)
	{
		std::string out;
		out.reserve(localeTag.size());
		for (char c : localeTag)
		{
			if (c == '.' || c == '@')
				break;
			if (c == '_' || c == '-')
			{
				out.push_back('-');
				continue;
			}
			out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
		}
		if (out.size() >= 2u)
		{
			return out.substr(0, 2);
		}
		return out;
	}

	bool LocalizationService::ParseFlatJsonCatalog(std::string_view text, Catalog& outCatalog)
	{
		outCatalog.clear();
		size_t pos = 0;
		SkipWs(text, pos);
		if (pos >= text.size() || text[pos] != '{')
			return false;
		++pos;
		for (;;)
		{
			SkipWs(text, pos);
			if (pos >= text.size())
				return false;
			if (text[pos] == '}')
			{
				++pos;
				SkipWs(text, pos);
				return pos == text.size();
			}

			std::string key;
			std::string value;
			if (!ParseJsonString(text, pos, key))
				return false;
			SkipWs(text, pos);
			if (pos >= text.size() || text[pos] != ':')
				return false;
			++pos;
			SkipWs(text, pos);
			if (!ParseJsonString(text, pos, value))
				return false;
			outCatalog[key] = value;
			SkipWs(text, pos);
			if (pos >= text.size())
				return false;
			if (text[pos] == ',')
			{
				++pos;
				continue;
			}
			if (text[pos] == '}')
			{
				++pos;
				SkipWs(text, pos);
				return pos == text.size();
			}
			return false;
		}
	}

	std::string LocalizationService::ApplyParams(std::string text, const Params& params)
	{
		for (const auto& [name, value] : params)
		{
			const std::string token = "{" + name + "}";
			size_t pos = 0;
			while ((pos = text.find(token, pos)) != std::string::npos)
			{
				text.replace(pos, token.size(), value);
				pos += value.size();
			}
		}
		return text;
	}

	bool LocalizationService::LoadCatalog(std::string_view localeTag, Catalog& outCatalog) const
	{
		const std::string normalized = NormalizeLocaleTag(localeTag);
		const std::string relativePath = std::string(kLocalizationRootDir) + "/" + normalized + "/" + normalized + ".json";
		const std::filesystem::path path = engine::platform::FileSystem::ResolveContentPath(m_config, relativePath);
		const std::string text = engine::platform::FileSystem::ReadAllText(path);
		if (text.empty())
		{
			LOG_WARN(Core, "[LocalizationService] LoadCatalog FAILED: empty or missing {}", path.string());
			return false;
		}
		if (!ParseFlatJsonCatalog(text, outCatalog))
		{
			LOG_ERROR(Core, "[LocalizationService] LoadCatalog FAILED: invalid JSON {}", path.string());
			return false;
		}

		LOG_INFO(Core, "[LocalizationService] LoadCatalog OK (locale={}, entries={}, path={})",
			normalized, outCatalog.size(), path.string());
		return true;
	}

	bool LocalizationService::HasLocale(std::string_view localeTag) const
	{
		const std::string normalized = NormalizeLocaleTag(localeTag);
		return std::find(m_availableLocales.begin(), m_availableLocales.end(), normalized) != m_availableLocales.end();
	}

	std::string LocalizationService::ResolveSupportedLocale(std::string_view requestedLocale) const
	{
		const std::string normalized = NormalizeLocaleTag(requestedLocale);
		if (!normalized.empty() && HasLocale(normalized))
			return normalized;
		if (!normalized.empty())
		{
			LOG_WARN(Core, "[LocalizationService] Requested locale '{}' unavailable, fallback '{}'", normalized, m_defaultLocale);
		}
		return m_defaultLocale;
	}
}
