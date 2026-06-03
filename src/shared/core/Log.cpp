#include "src/shared/core/Log.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_map>

#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#  include <io.h>
#else
#  include <mutex>
#  include <unistd.h>
#endif

namespace engine::core
{
	namespace
	{
		std::ofstream s_file;
		bool          s_consoleEnabled = false;
		/// M44.4 — Si true, formate chaque ligne en JSON (jsonl) au lieu du format bracketé.
		bool          s_jsonOutput = false;
		/// M45 — Seuil distinct pour le fichier principal. Si \c LogLevel::Off, suit \c s_level.
		LogLevel      s_fileLevel = LogLevel::Off;
		/// M45 — Couleurs console actives (n'est vrai que si stdout est un TTY ET \c consoleColors=true).
		bool          s_consoleColors = false;
		/// Chemins résolus → flux (un fichier peut servir plusieurs sous-systèmes).
		std::unordered_map<std::string, std::ofstream> s_extraFilesByPath;
		/// Sous-système (chaîne du macro LOG_*) → chemin résolu dans \c s_extraFilesByPath.
		std::unordered_map<std::string, std::string> s_subsystemToResolvedPath;

		/// M45 — Fichiers spécialisés (cmangos GMLog / CharLog / DBError / WorldPacket / Custom).
		/// Tous sont indépendants du système \c subsystemFiles ci-dessus.
		std::ofstream s_gmLogFile;
		std::ofstream s_charLogFile;
		std::ofstream s_dbErrorFile;
		std::ofstream s_packetFile;
		std::ofstream s_customFile;
		bool          s_gmLogPerAccount = false;
		std::filesystem::path s_gmLogDir;
		/// M45 — Cache des flux per-account ouverts à la volée. Clé : accountId.
		std::unordered_map<uint32_t, std::ofstream> s_gmFilesByAccount;

#if defined(_WIN32)
		CRITICAL_SECTION s_cs;
		bool             s_csInit = false;

		void LockLog()   { if (s_csInit) EnterCriticalSection(&s_cs); }
		void UnlockLog() { if (s_csInit) LeaveCriticalSection(&s_cs); }
		void InitLock()  { InitializeCriticalSection(&s_cs); s_csInit = true; }
		void DestroyLock()
		{
			if (s_csInit) { DeleteCriticalSection(&s_cs); s_csInit = false; }
		}
		bool StdoutIsTty()
		{
			return _isatty(_fileno(stdout)) != 0;
		}
#else
		std::mutex& GetMutex() { static std::mutex m; return m; }
		void LockLog()    { GetMutex().lock(); }
		void UnlockLog()  { GetMutex().unlock(); }
		void InitLock()   {}
		void DestroyLock(){}
		bool StdoutIsTty()
		{
			return ::isatty(fileno(stdout)) != 0;
		}
#endif

		const char* LevelToString(LogLevel level)
		{
			switch (level)
			{
			case LogLevel::Trace: return "TRACE";
			case LogLevel::Debug: return "DEBUG";
			case LogLevel::Info:  return "INFO";
			case LogLevel::Warn:  return "WARN";
			case LogLevel::Error: return "ERROR";
			case LogLevel::Fatal: return "FATAL";
			case LogLevel::Off:   return "OFF";
			default:              return "UNKNOWN";
			}
		}

		/// M45 — Couleur ANSI/Win32 par niveau (cmangos-style).
		/// POSIX : code SGR (ex. "31" = rouge). Win32 : attribut FOREGROUND_*.
#if defined(_WIN32)
		WORD LevelToWinColor(LogLevel level)
		{
			switch (level)
			{
			case LogLevel::Trace: return FOREGROUND_INTENSITY;                                                       // gris
			case LogLevel::Debug: return FOREGROUND_GREEN | FOREGROUND_BLUE;                                         // cyan
			case LogLevel::Info:  return FOREGROUND_GREEN | FOREGROUND_RED | FOREGROUND_BLUE | FOREGROUND_INTENSITY; // blanc vif
			case LogLevel::Warn:  return FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY;                   // jaune
			case LogLevel::Error: return FOREGROUND_RED | FOREGROUND_INTENSITY;                                      // rouge vif
			case LogLevel::Fatal: return FOREGROUND_RED | FOREGROUND_INTENSITY | BACKGROUND_RED;                     // rouge sur fond rouge éteint
			default:              return FOREGROUND_GREEN | FOREGROUND_RED | FOREGROUND_BLUE;
			}
		}
#endif
		const char* LevelToAnsi(LogLevel level)
		{
			switch (level)
			{
			case LogLevel::Trace: return "\x1b[90m";       // gris
			case LogLevel::Debug: return "\x1b[36m";       // cyan
			case LogLevel::Info:  return "\x1b[37;1m";     // blanc vif
			case LogLevel::Warn:  return "\x1b[33;1m";     // jaune
			case LogLevel::Error: return "\x1b[31;1m";     // rouge
			case LogLevel::Fatal: return "\x1b[97;41;1m";  // blanc sur rouge
			default:              return "\x1b[0m";
			}
		}

		void WriteConsoleColored(const std::string& line, LogLevel level)
		{
			if (!s_consoleColors)
			{
				std::fputs(line.c_str(), stdout);
				std::fflush(stdout);
				return;
			}
#if defined(_WIN32)
			HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
			CONSOLE_SCREEN_BUFFER_INFO csbi{};
			WORD prev = 0;
			if (h != INVALID_HANDLE_VALUE && GetConsoleScreenBufferInfo(h, &csbi))
			{
				prev = csbi.wAttributes;
				SetConsoleTextAttribute(h, LevelToWinColor(level));
			}
			std::fputs(line.c_str(), stdout);
			std::fflush(stdout);
			if (h != INVALID_HANDLE_VALUE)
				SetConsoleTextAttribute(h, prev != 0 ? prev : (FOREGROUND_GREEN | FOREGROUND_RED | FOREGROUND_BLUE));
#else
			std::fputs(LevelToAnsi(level), stdout);
			std::fputs(line.c_str(), stdout);
			std::fputs("\x1b[0m", stdout);
			std::fflush(stdout);
#endif
		}
	}

	std::atomic<LogLevel> Log::s_level{ LogLevel::Info };
	std::atomic<bool>     Log::s_active{ false };
	std::atomic<uint64_t> Log::s_filters{ 0 };

	namespace
	{
		// Sous-projet 1, bloc E — observateur global optionnel (console in-app).
		// Set-once au boot via Log::SetSink ; lu sans verrou par WriteLine.
		Log::LogSink      g_logSink;
		std::atomic<bool> g_hasLogSink{ false };
	}

	void Log::SetSink(LogSink sink)
	{
		LockLog();
		g_logSink = std::move(sink);
		g_hasLogSink.store(static_cast<bool>(g_logSink), std::memory_order_release);
		UnlockLog();
	}

	std::string Log::MakeTimestampedFilename(std::string_view prefix)
	{
		const auto now = std::chrono::system_clock::now();
		const std::time_t t = std::chrono::system_clock::to_time_t(now);
		std::tm tm{};
#if defined(_WIN32)
		localtime_s(&tm, &t);
#else
		localtime_r(&t, &tm);
#endif
		char buf[64]{};
		std::strftime(buf, sizeof(buf), "-%Y%m%d-%H%M%S.log", &tm);
		return std::string(prefix) + buf;
	}

	LogFilter LogFilterFromName(std::string_view name)
	{
		struct Pair { std::string_view k; LogFilter v; };
		static constexpr Pair kTable[] = {
			{"transport_moves",     LogFilter::TransportMoves},
			{"creature_moves",      LogFilter::CreatureMoves},
			{"visibility_changes",  LogFilter::VisibilityChanges},
			{"weather",             LogFilter::Weather},
			{"player_stats",        LogFilter::PlayerStats},
			{"sql_text",            LogFilter::SqlText},
			{"player_moves",        LogFilter::PlayerMoves},
			{"damage",              LogFilter::Damage},
			{"combat",              LogFilter::Combat},
			{"spell_cast",          LogFilter::SpellCast},
			{"pathfinding",         LogFilter::Pathfinding},
			{"map_loading",         LogFilter::MapLoading},
			{"event_ai_dev",        LogFilter::EventAiDev},
			{"db_scripts_dev",      LogFilter::DbScriptsDev},
			{"packet_io",           LogFilter::PacketIo},
			{"chat_relay",          LogFilter::ChatRelay},
			{"auth",                LogFilter::Auth},
			{"session",             LogFilter::Session},
			{"db",                  LogFilter::Db},
			{"migration",           LogFilter::Migration},
			{"custom",              LogFilter::Custom},
		};
		for (const auto& p : kTable)
		{
			if (p.k == name)
				return p.v;
		}
		return LogFilter::None;
	}

	namespace
	{
		/// M44.4 — Escape JSON strings : quote, backslash, newline, carriage return,
		/// tab, et caractères de contrôle ASCII (\u00xx). Toléré non-ASCII passe-through
		/// (UTF-8 byte-perfect — Loki/ELK acceptent).
		void AppendJsonEscaped(std::string& out, std::string_view s)
		{
			for (char c : s)
			{
				const unsigned char uc = static_cast<unsigned char>(c);
				switch (c)
				{
					case '"':  out += "\\\""; break;
					case '\\': out += "\\\\"; break;
					case '\n': out += "\\n";  break;
					case '\r': out += "\\r";  break;
					case '\t': out += "\\t";  break;
					case '\b': out += "\\b";  break;
					case '\f': out += "\\f";  break;
					default:
						if (uc < 0x20u)
						{
							char esc[8]{};
							std::snprintf(esc, sizeof(esc), "\\u%04x", uc);
							out += esc;
						}
						else
						{
							out += c;
						}
						break;
				}
			}
		}

		/// M45 — Ouvre un fichier de log spécialisé (mode append, BOM UTF-8 si vide).
		/// Renvoie true si \p out est utilisable après l'appel.
		bool OpenSpecializedFile(std::ofstream& out, const std::filesystem::path& full)
		{
			if (full.empty())
				return false;
			std::error_code ec;
			if (full.has_parent_path())
				std::filesystem::create_directories(full.parent_path(), ec);
			out.open(full, std::ios::out | std::ios::app);
			if (!out.is_open())
			{
				std::fprintf(stderr, "[Log] Cannot open specialized log file: %s\n", full.generic_string().c_str());
				std::fflush(stderr);
				return false;
			}
			if (out.tellp() == 0)
			{
				static const unsigned char kUtf8Bom[] = { 0xEF, 0xBB, 0xBF };
				out.write(reinterpret_cast<const char*>(kUtf8Bom), sizeof(kUtf8Bom));
				out.flush();
			}
			return true;
		}

		/// M45 — Renvoie le timestamp local formaté \c "[DD/MM/YYYY][HH:MM:SS] " utilisé
		/// par les fichiers spécialisés (les logs principaux gardent le format historique).
		std::string TimestampPrefix()
		{
			const auto now = std::chrono::system_clock::now();
			const std::time_t t = std::chrono::system_clock::to_time_t(now);
			std::tm tm{};
#if defined(_WIN32)
			localtime_s(&tm, &t);
#else
			localtime_r(&t, &tm);
#endif
			char buf[32]{};
			std::snprintf(buf, sizeof(buf), "[%02d/%02d/%04d][%02d:%02d:%02d] ",
				tm.tm_mday, tm.tm_mon + 1, tm.tm_year + 1900,
				tm.tm_hour, tm.tm_min, tm.tm_sec);
			return std::string(buf);
		}
	}

	void Log::Init(const LogSettings& settings)
	{
		Shutdown();
		InitLock();

		s_level.store(settings.level, std::memory_order_relaxed);
		s_consoleEnabled = settings.console;
		s_jsonOutput = settings.jsonOutput;
		s_fileLevel = settings.fileLevel;
		s_filters.store(settings.enabledFilters, std::memory_order_relaxed);
		s_consoleColors = settings.consoleColors && settings.console && StdoutIsTty();

		if (!settings.filePath.empty())
		{
			s_file.open(settings.filePath, std::ios::out | std::ios::app);
			if (!s_file.is_open())
			{
				std::fprintf(stderr, "[Log] Cannot open log file: %s\n", settings.filePath.c_str());
				std::fflush(stderr);
				// Ne pas retourner : si la console est active on continue sans fichier.
			}
			else if (s_file.tellp() == 0)
			{
				// BOM UTF-8 pour que les éditeurs / outils Windows détectent l'encodage (accents, tirets typographiques).
				static const unsigned char kUtf8Bom[] = { 0xEF, 0xBB, 0xBF };
				s_file.write(reinterpret_cast<const char*>(kUtf8Bom), sizeof(kUtf8Bom));
				s_file.flush();
			}
		}

		s_extraFilesByPath.clear();
		s_subsystemToResolvedPath.clear();
		const std::filesystem::path mainPath(settings.filePath);
		const std::filesystem::path baseDir =
			settings.filePath.empty() ? std::filesystem::path(".") :
			(mainPath.has_parent_path() ? mainPath.parent_path() : std::filesystem::path("."));
		{
			static const unsigned char kUtf8Bom[] = { 0xEF, 0xBB, 0xBF };
			for (const auto& [subsystem, relName] : settings.subsystemFiles)
			{
				if (subsystem.empty() || relName.empty())
				{
					continue;
				}
				const std::filesystem::path relP(relName);
				const std::filesystem::path full = relP.is_absolute() ? relP : (baseDir / relP);
				const std::string fullStr = full.generic_string();
				if (s_extraFilesByPath.find(fullStr) == s_extraFilesByPath.end())
				{
					std::ofstream ofs(fullStr, std::ios::out | std::ios::app);
					if (!ofs.is_open())
					{
						std::fprintf(stderr, "[Log] Cannot open subsystem log file: %s\n", fullStr.c_str());
						std::fflush(stderr);
						continue;
					}
					if (ofs.tellp() == 0)
					{
						ofs.write(reinterpret_cast<const char*>(kUtf8Bom), sizeof(kUtf8Bom));
						ofs.flush();
					}
					s_extraFilesByPath.emplace(fullStr, std::move(ofs));
				}
				s_subsystemToResolvedPath.emplace(subsystem, fullStr);
			}
		}

		// M45 — Fichiers spécialisés (GM/Char/DBError/Packet/Custom).
		s_gmLogPerAccount = settings.gmLogPerAccount;
		s_gmLogDir = settings.gmLogDir.empty() ? (baseDir / "gmlogs") : std::filesystem::path(settings.gmLogDir);
		if (s_gmLogPerAccount)
		{
			std::error_code ec;
			std::filesystem::create_directories(s_gmLogDir, ec);
		}
		else if (!settings.gmLogFile.empty())
		{
			const std::filesystem::path p(settings.gmLogFile);
			OpenSpecializedFile(s_gmLogFile, p.is_absolute() ? p : (baseDir / p));
		}
		if (!settings.charLogFile.empty())
		{
			const std::filesystem::path p(settings.charLogFile);
			OpenSpecializedFile(s_charLogFile, p.is_absolute() ? p : (baseDir / p));
		}
		if (!settings.dbErrorLogFile.empty())
		{
			const std::filesystem::path p(settings.dbErrorLogFile);
			OpenSpecializedFile(s_dbErrorFile, p.is_absolute() ? p : (baseDir / p));
		}
		if (!settings.packetLogFile.empty())
		{
			const std::filesystem::path p(settings.packetLogFile);
			OpenSpecializedFile(s_packetFile, p.is_absolute() ? p : (baseDir / p));
		}
		if (!settings.customLogFile.empty())
		{
			const std::filesystem::path p(settings.customLogFile);
			OpenSpecializedFile(s_customFile, p.is_absolute() ? p : (baseDir / p));
		}

#if defined(_WIN32)
		if (s_consoleEnabled)
		{
			SetConsoleOutputCP(CP_UTF8);
			SetConsoleCP(CP_UTF8);
		}
#endif

		if (!s_file.is_open() && !s_consoleEnabled && s_extraFilesByPath.empty())
			return;

		s_active.store(true, std::memory_order_release);
	}

	void Log::Shutdown()
	{
		if (!s_active.exchange(false, std::memory_order_acq_rel))
			return;

		LockLog();
		if (s_file.is_open())
		{
			s_file.flush();
			s_file.close();
		}
		for (auto& [path, stream] : s_extraFilesByPath)
		{
			(void)path;
			if (stream.is_open())
			{
				stream.flush();
				stream.close();
			}
		}
		s_extraFilesByPath.clear();
		s_subsystemToResolvedPath.clear();
		auto closeIfOpen = [](std::ofstream& f) { if (f.is_open()) { f.flush(); f.close(); } };
		closeIfOpen(s_gmLogFile);
		closeIfOpen(s_charLogFile);
		closeIfOpen(s_dbErrorFile);
		closeIfOpen(s_packetFile);
		closeIfOpen(s_customFile);
		for (auto& [id, f] : s_gmFilesByAccount)
		{
			(void)id;
			closeIfOpen(f);
		}
		s_gmFilesByAccount.clear();
		s_gmLogPerAccount = false;
		s_gmLogDir.clear();
		s_consoleEnabled = false;
		s_jsonOutput = false;
		s_consoleColors = false;
		s_fileLevel = LogLevel::Off;
		s_filters.store(0, std::memory_order_relaxed);
		UnlockLog();
		DestroyLock();
	}

	bool Log::IsActive()
	{
		return s_active.load(std::memory_order_acquire);
	}

	LogLevel Log::GetLevel()
	{
		return s_level.load(std::memory_order_relaxed);
	}

	void Log::SetLevel(LogLevel level)
	{
		s_level.store(level, std::memory_order_relaxed);
	}

	bool Log::HasFilter(LogFilter f)
	{
		const uint64_t bits = static_cast<uint64_t>(f);
		if (bits == 0)
			return false;
		return (s_filters.load(std::memory_order_relaxed) & bits) != 0;
	}

	void Log::SetFilter(LogFilter f, bool enabled)
	{
		const uint64_t bits = static_cast<uint64_t>(f);
		if (bits == 0)
			return;
		uint64_t cur = s_filters.load(std::memory_order_relaxed);
		uint64_t next;
		do
		{
			next = enabled ? (cur | bits) : (cur & ~bits);
		} while (!s_filters.compare_exchange_weak(cur, next, std::memory_order_relaxed));
	}

	void Log::SetFilters(uint64_t mask)
	{
		s_filters.store(mask, std::memory_order_relaxed);
	}

	uint64_t Log::GetFilters()
	{
		return s_filters.load(std::memory_order_relaxed);
	}

	void Log::WriteLine(LogLevel level, const char* subsystem, std::string_view message)
	{
		if (level < s_level.load(std::memory_order_relaxed))
			return;

		const auto now    = std::chrono::system_clock::now();
		const std::time_t t = std::chrono::system_clock::to_time_t(now);
		std::tm tm{};
#if defined(_WIN32)
		localtime_s(&tm, &t);
#else
		localtime_r(&t, &tm);
#endif
		const char* lvlStr = LevelToString(level);
		const char* sys    = subsystem ? subsystem : "?";

		// Sous-projet 1, bloc E — observateur console in-app (éditeur monde).
		// Invoqué HORS du verrou de log (pas de deadlock/réentrance si le sink
		// venait à logguer) ; set-once au boot -> lecture concurrente sûre.
		if (g_hasLogSink.load(std::memory_order_acquire))
		{
			g_logSink(level, sys, message);
		}

		std::string line;
		if (s_jsonOutput)
		{
			// M44.4 — JSONL : un objet par ligne. Timestamp UTC ISO-8601 (gmtime, pas localtime,
			// pour faciliter l'agrégation cross-zones côté Loki/ELK).
			std::tm utc{};
#if defined(_WIN32)
			gmtime_s(&utc, &t);
#else
			gmtime_r(&t, &utc);
#endif
			char isoTs[32]{};
			std::snprintf(isoTs, sizeof(isoTs), "%04d-%02d-%02dT%02d:%02d:%02dZ",
				utc.tm_year + 1900, utc.tm_mon + 1, utc.tm_mday,
				utc.tm_hour, utc.tm_min, utc.tm_sec);

			line.reserve(message.size() + 96u);
			line += "{\"timestamp\":\"";
			line += isoTs;
			line += "\",\"level\":\"";
			line += lvlStr;
			line += "\",\"subsystem\":\"";
			AppendJsonEscaped(line, sys);
			line += "\",\"message\":\"";
			AppendJsonEscaped(line, message);
			line += "\"}\n";
		}
		else
		{
			char timeBuf[32]{};
			std::snprintf(timeBuf, sizeof(timeBuf), "%02d/%02d/%04d][%02d:%02d:%02d",
				tm.tm_mday, tm.tm_mon + 1, tm.tm_year + 1900,
				tm.tm_hour, tm.tm_min, tm.tm_sec);
			char header[72]{};
			std::snprintf(header, sizeof(header), "[%s][%-5s][%s] ", timeBuf, lvlStr, sys);
			line = std::string(header) + std::string(message) + "\n";
		}

		LockLog();
		// M45 — Le seuil fichier peut être plus permissif que le seuil console.
		const LogLevel fileSeuil = (s_fileLevel == LogLevel::Off) ? s_level.load(std::memory_order_relaxed) : s_fileLevel;
		const bool fileAllowed = (level >= fileSeuil);
		if (fileAllowed && s_file.is_open())
		{
			s_file << line;
			s_file.flush();
		}
		if (fileAllowed)
		{
			if (const auto it = s_subsystemToResolvedPath.find(sys); it != s_subsystemToResolvedPath.end())
			{
				if (const auto fit = s_extraFilesByPath.find(it->second); fit != s_extraFilesByPath.end() && fit->second.is_open())
				{
					fit->second << line;
					fit->second.flush();
				}
			}
		}
		if (s_consoleEnabled)
		{
			WriteConsoleColored(line, level);
			// Docker / pipe : stdout est souvent fully-buffered sans TTY ; sans flush les logs
			// n'apparaissent pas dans `docker compose logs` pendant longtemps.
		}
		UnlockLog();
	}

	void Log::WriteGmCommand(uint32_t accountId, std::string_view characterName, std::string_view command)
	{
		if (!s_active.load(std::memory_order_acquire))
			return;
		// Toujours dans le log principal (sous-système GM) pour traçabilité agrégée.
		Write(LogLevel::Info, "GM", "[acct={} char='{}'] {}", accountId, characterName, command);

		if (!s_gmLogPerAccount && !s_gmLogFile.is_open())
			return;

		std::string line = TimestampPrefix();
		line += "[acct=";
		line += std::to_string(accountId);
		line += " char='";
		line.append(characterName);
		line += "'] ";
		line.append(command);
		line += "\n";

		LockLog();
		if (s_gmLogPerAccount)
		{
			auto it = s_gmFilesByAccount.find(accountId);
			if (it == s_gmFilesByAccount.end())
			{
				std::ofstream ofs;
				const std::filesystem::path full = s_gmLogDir / ("account_" + std::to_string(accountId) + ".log");
				if (OpenSpecializedFile(ofs, full))
				{
					it = s_gmFilesByAccount.emplace(accountId, std::move(ofs)).first;
				}
			}
			if (it != s_gmFilesByAccount.end() && it->second.is_open())
			{
				it->second << line;
				it->second.flush();
			}
		}
		else if (s_gmLogFile.is_open())
		{
			s_gmLogFile << line;
			s_gmLogFile.flush();
		}
		UnlockLog();
	}

	void Log::WriteCharLog(uint32_t accountId, uint64_t characterId, std::string_view event)
	{
		if (!s_active.load(std::memory_order_acquire))
			return;
		Write(LogLevel::Info, "Char", "[acct={} char_id={}] {}", accountId, characterId, event);
		if (!s_charLogFile.is_open())
			return;
		std::string line = TimestampPrefix();
		line += "[acct=";
		line += std::to_string(accountId);
		line += " char_id=";
		line += std::to_string(characterId);
		line += "] ";
		line.append(event);
		line += "\n";
		LockLog();
		if (s_charLogFile.is_open())
		{
			s_charLogFile << line;
			s_charLogFile.flush();
		}
		UnlockLog();
	}

	void Log::WriteDbError(std::string_view message)
	{
		if (!s_active.load(std::memory_order_acquire))
			return;
		Write(LogLevel::Error, "DBError", "{}", message);
		if (!s_dbErrorFile.is_open())
			return;
		std::string line = TimestampPrefix();
		line.append(message);
		line += "\n";
		LockLog();
		if (s_dbErrorFile.is_open())
		{
			s_dbErrorFile << line;
			s_dbErrorFile.flush();
		}
		UnlockLog();
	}

	void Log::WriteCustom(std::string_view message)
	{
		if (!s_active.load(std::memory_order_acquire))
			return;
		Write(LogLevel::Info, "Custom", "{}", message);
		if (!s_customFile.is_open())
			return;
		std::string line = TimestampPrefix();
		line.append(message);
		line += "\n";
		LockLog();
		if (s_customFile.is_open())
		{
			s_customFile << line;
			s_customFile.flush();
		}
		UnlockLog();
	}

	void Log::WritePacketDump(std::string_view direction, uint16_t opcode,
	                          const void* data, size_t size, uint32_t connId)
	{
		if (!s_active.load(std::memory_order_acquire))
			return;
		// M45 — Gating par filtre PacketIo : zéro coût si désactivé.
		if (!HasFilter(LogFilter::PacketIo))
			return;

		const uint8_t* bytes = static_cast<const uint8_t*>(data);
		std::string body;
		body.reserve(size * 4u + 64u);
		// Entête lisible.
		char header[96]{};
		std::snprintf(header, sizeof(header),
			"PACKET %.3s connId=%u opcode=0x%04x (%u) size=%zu\n",
			direction.data(), connId, opcode, opcode, size);
		body += header;
		// 16 octets/ligne avec offset, hex et gutter ASCII.
		for (size_t i = 0; i < size; i += 16u)
		{
			char off[16]{};
			std::snprintf(off, sizeof(off), "  %04zx  ", i);
			body += off;
			// Hex.
			for (size_t j = 0; j < 16u; ++j)
			{
				if (i + j < size)
				{
					char hx[4]{};
					std::snprintf(hx, sizeof(hx), "%02x ", bytes[i + j]);
					body += hx;
				}
				else
				{
					body += "   ";
				}
				if (j == 7u)
					body += ' ';
			}
			// Gutter ASCII.
			body += " |";
			for (size_t j = 0; j < 16u && (i + j) < size; ++j)
			{
				const uint8_t c = bytes[i + j];
				body += (c >= 0x20u && c < 0x7Fu) ? static_cast<char>(c) : '.';
			}
			body += "|\n";
		}

		// Toujours dans le log principal (sous-système Packet) pour visibilité immédiate.
		Write(LogLevel::Debug, "Packet", "{}", body);

		if (!s_packetFile.is_open())
			return;
		std::string line = TimestampPrefix();
		line += body;
		LockLog();
		if (s_packetFile.is_open())
		{
			s_packetFile << line;
			s_packetFile.flush();
		}
		UnlockLog();
	}
}
