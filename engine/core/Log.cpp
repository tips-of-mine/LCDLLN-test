#include "engine/core/Log.h"

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
#else
#  include <mutex>
#endif

namespace engine::core
{
	namespace
	{
		std::ofstream s_file;
		bool          s_consoleEnabled = false;
		/// M44.4 — Si true, formate chaque ligne en JSON (jsonl) au lieu du format bracketé.
		bool          s_jsonOutput = false;
		/// Chemins résolus → flux (un fichier peut servir plusieurs sous-systèmes).
		std::unordered_map<std::string, std::ofstream> s_extraFilesByPath;
		/// Sous-système (chaîne du macro LOG_*) → chemin résolu dans \c s_extraFilesByPath.
		std::unordered_map<std::string, std::string> s_subsystemToResolvedPath;

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
#else
		std::mutex& GetMutex() { static std::mutex m; return m; }
		void LockLog()    { GetMutex().lock(); }
		void UnlockLog()  { GetMutex().unlock(); }
		void InitLock()   {}
		void DestroyLock(){}
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
	}

	std::atomic<LogLevel> Log::s_level{ LogLevel::Info };
	std::atomic<bool>     Log::s_active{ false };

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
	}

	void Log::Init(const LogSettings& settings)
	{
		Shutdown();
		InitLock();

		s_level.store(settings.level, std::memory_order_relaxed);
		s_consoleEnabled = settings.console;
		s_jsonOutput = settings.jsonOutput;

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
		{
			const std::filesystem::path mainPath(settings.filePath);
			const std::filesystem::path baseDir =
				settings.filePath.empty() ? std::filesystem::path(".") :
				(mainPath.has_parent_path() ? mainPath.parent_path() : std::filesystem::path("."));
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
		s_consoleEnabled = false;
		s_jsonOutput = false;
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
		if (s_file.is_open())
		{
			s_file << line;
			s_file.flush();
		}
		if (const auto it = s_subsystemToResolvedPath.find(sys); it != s_subsystemToResolvedPath.end())
		{
			if (const auto fit = s_extraFilesByPath.find(it->second); fit != s_extraFilesByPath.end() && fit->second.is_open())
			{
				fit->second << line;
				fit->second.flush();
			}
		}
		if (s_consoleEnabled)
		{
			std::fputs(line.c_str(), stdout);
			// Docker / pipe : stdout est souvent fully-buffered sans TTY ; sans flush les logs
			// n'apparaissent pas dans `docker compose logs` pendant longtemps.
			std::fflush(stdout);
		}
		UnlockLog();
	}
}