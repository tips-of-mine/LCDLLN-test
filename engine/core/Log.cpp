#include "engine/core/Log.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <string>

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

	void Log::Init(const LogSettings& settings)
	{
		Shutdown();
		InitLock();

		s_level.store(settings.level, std::memory_order_relaxed);
		s_consoleEnabled = settings.console;

		if (!settings.filePath.empty())
		{
			s_file.open(settings.filePath, std::ios::out | std::ios::app);
			if (!s_file.is_open())
			{
				std::fprintf(stderr, "[Log] Cannot open log file: %s\n", settings.filePath.c_str());
				std::fflush(stderr);
				return;
			}
		}

		if (!s_file.is_open() && !s_consoleEnabled)
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
		s_consoleEnabled = false;
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

		// Timestamp HH:MM:SS.mmm
		const auto now    = std::chrono::system_clock::now();
		const auto ms     = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
		const std::time_t t = std::chrono::system_clock::to_time_t(now);
		std::tm tm{};
#if defined(_WIN32)
		localtime_s(&tm, &t);
#else
		localtime_r(&t, &tm);
#endif
		char timeBuf[16]{};
		std::snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d:%02d.%03d",
			tm.tm_hour, tm.tm_min, tm.tm_sec, static_cast<int>(ms.count()));

		// Format: [HH:MM:SS.mmm][LEVEL][Subsystem] message
		const char* lvlStr = LevelToString(level);
		const char* sys    = subsystem ? subsystem : "?";
		char header[64]{};
		std::snprintf(header, sizeof(header), "[%s][%-5s][%s] ", timeBuf, lvlStr, sys);

		const std::string line = std::string(header) + std::string(message) + "\n";

		LockLog();
		if (s_file.is_open())
		{
			s_file << line;
			s_file.flush();
		}
		if (s_consoleEnabled)
			std::fputs(line.c_str(), stdout);
		UnlockLog();
	}
}