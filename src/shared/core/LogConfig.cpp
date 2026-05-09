#include "engine/core/LogConfig.h"

#include "engine/core/Config.h"

#include <algorithm>
#include <cstdint>
#include <string>

namespace engine::core
{
	namespace
	{
		LogLevel ParseLogLevelText(std::string_view text)
		{
			if (text == "Trace" || text == "trace") return LogLevel::Trace;
			if (text == "Debug" || text == "debug") return LogLevel::Debug;
			if (text == "Info"  || text == "info")  return LogLevel::Info;
			if (text == "Warn"  || text == "warn")  return LogLevel::Warn;
			if (text == "Error" || text == "error") return LogLevel::Error;
			if (text == "Fatal" || text == "fatal") return LogLevel::Fatal;
			if (text == "Off"   || text == "off")   return LogLevel::Off;
			return LogLevel::Info;
		}

		/// Liste exhaustive des noms de filtres reconnus (snake_case). Tout ajout
		/// d'un \c LogFilter doit aussi être référencé ici sinon le bit ne sera
		/// jamais activé via la config.
		constexpr const char* kFilterNames[] = {
			"transport_moves", "creature_moves", "visibility_changes", "weather",
			"player_stats",    "sql_text",       "player_moves",       "damage",
			"combat",          "spell_cast",     "pathfinding",        "map_loading",
			"event_ai_dev",    "db_scripts_dev", "packet_io",          "chat_relay",
			"auth",            "session",        "db",                 "migration",
			"custom"
		};
	}

	LogSettings BuildLogSettingsFromConfig(const Config& cfg, std::string_view defaultLogFile)
	{
		LogSettings s;
		s.level       = ParseLogLevelText(cfg.GetString("log.level", "Info"));
		// Si log.file_level absent, suit log.level (s_fileLevel = Off → fallback runtime).
		const std::string fileLevelText = cfg.GetString("log.file_level", "");
		s.fileLevel   = fileLevelText.empty() ? LogLevel::Off : ParseLogLevelText(fileLevelText);
		s.console     = cfg.GetBool("log.console", true);
		s.flushAlways = cfg.GetBool("log.flush_always", true);
		s.filePath    = cfg.GetString("log.file", std::string(defaultLogFile));
		s.rotation_size_mb = static_cast<size_t>(std::max<int64_t>(0, cfg.GetInt("log.rotation_size_mb", 10)));
		s.retention_days   = static_cast<int>(cfg.GetInt("log.retention_days", 7));
		s.subsystemFiles   = cfg.GetStringMapUnderPrefix("log.subsystem_files");
		s.jsonOutput  = cfg.GetBool("log.json", false);

		// M45 — Couleurs console (ANSI/Win32). Auto-désactivé si stdout non-TTY.
		s.consoleColors = cfg.GetBool("log.console_colors", true);

		// M45 — Fichiers spécialisés.
		s.gmLogFile        = cfg.GetString("log.gm_log_file", "");
		s.gmLogPerAccount  = cfg.GetBool("log.gm_log_per_account", false);
		s.gmLogDir         = cfg.GetString("log.gm_log_dir", "");
		s.charLogFile      = cfg.GetString("log.char_log_file", "");
		s.dbErrorLogFile   = cfg.GetString("log.db_error_log_file", "");
		s.packetLogFile    = cfg.GetString("log.packet_log_file", "");
		s.customLogFile    = cfg.GetString("log.custom_log_file", "");

		// M45 — Filtres bitmask : chaque bit est activé séparément via
		// \c log.filters.<snake_case_name> = true. Les noms inconnus sont ignorés.
		uint64_t mask = 0;
		for (const char* name : kFilterNames)
		{
			std::string key = std::string("log.filters.") + name;
			if (cfg.GetBool(key, false))
			{
				const LogFilter f = LogFilterFromName(name);
				mask |= static_cast<uint64_t>(f);
			}
		}
		s.enabledFilters = mask;
		return s;
	}
}
