#include "engine/core/Log.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <mutex>
#include <string>

namespace engine::core
{
	namespace
	{
		std::ofstream s_file;
		std::mutex    s_mutex;
		bool          s_consoleEnabled = false;

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
		std::fprintf(stderr, "[LOG::INIT] debut\n"); std::fflush(stderr);
		Shutdown();
		std::fprintf(stderr, "[LOG::INIT] apres Shutdown\n"); std::fflush(stderr);

		s_level.store(settings.level, std::memory_order_relaxed);
		s_consoleEnabled = settings.console;

		if (!settings.filePath.empty())
		{
			std::fprintf(stderr, "[LOG::INIT] avant ofstream open path='%s'\n", settings.filePath.c_str()); std::fflush(stderr);
			s_file.open(settings.filePath, std::ios::out | std::ios::app);
			if (!s_file.is_open())
			{
				std::fprintf(stderr, "[LOG::INIT] ofstream open FAILED\n"); std::fflush(stderr);
				return;
			}
			std::fprintf(stderr, "[LOG::INIT] ofstream open OK\n"); std::fflush(stderr);
		}

		if (!s_file.is_open() && !s_consoleEnabled)
		{
			std::fprintf(stderr, "[LOG::INIT] no sink active, return\n"); std::fflush(stderr);
			return;
		}

		s_active.store(true, std::memory_order_release);
		std::fprintf(stderr, "[LOG::INIT] tout OK\n"); std::fflush(stderr);

		LOG_INFO(Core, "[Log] Init OK (file={}, console={})",
			settings.filePath.empty() ? "<none>" : settings.filePath,
			settings.console ? "on" : "off");
	}

	void Log::Shutdown()
	{
		if (!s_active.exchange(false, std::memory_order_acq_rel))
			return;

		std::lock_guard<std::mutex> lock(s_mutex);
		if (s_file.is_open())
		{
			s_file.flush();
			s_file.close();
		}
		s_consoleEnabled = false;
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

		const std::string line =
			std::string("[") + LevelToString(level) + "][" +
			(subsystem ? subsystem : "?") + "] " +
			std::string(message) + "\n";

		std::lock_guard<std::mutex> lock(s_mutex);
		if (s_file.is_open())
		{
			s_file << line;
			s_file.flush();
		}
		if (s_consoleEnabled)
			std::fputs(line.c_str(), stdout);
	}
}