#include "engine/core/Log.h"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <mutex>
#include <thread>

namespace engine::core
{
	namespace
	{
		std::mutex& GetMutex()
		{
			static std::mutex m;
			return m;
		}
		
		std::ofstream& GetFile()
		{
			static std::ofstream f;
			return f;
		}

		LogSettings& GetSettings()
		{
			static LogSettings s;
			return s;
		}

		const char* ToString(LogLevel level)
		{
			switch (level)
			{
			case LogLevel::Trace: return "TRACE";
			case LogLevel::Debug: return "DEBUG";
			case LogLevel::Info: return "INFO";
			case LogLevel::Warn: return "WARN";
			case LogLevel::Error: return "ERROR";
			case LogLevel::Fatal: return "FATAL";
			case LogLevel::Off: return "OFF";
			default: return "UNKNOWN";
			}
		}

		std::string TimestampNow()
		{
			using namespace std::chrono;
			const auto now = system_clock::now();
			const auto ms = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;
			const std::time_t t = system_clock::to_time_t(now);

			std::tm tm{};
#if defined(_WIN32)
			localtime_s(&tm, &t);
#else
			localtime_r(&t, &tm);
#endif

			char buffer[32]{};
			std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &tm);
			return std::format("{}.{:03}", buffer, static_cast<int>(ms.count()));
		}

		uint64_t ThreadIdNumber()
		{
			return static_cast<uint64_t>(std::hash<std::thread::id>{}(std::this_thread::get_id()));
		}
	}

	std::atomic<LogLevel> Log::s_level{ LogLevel::Info };

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
		std::fprintf(stderr, "[LOG] Init entree\n"); std::fflush(stderr);

		std::fprintf(stderr, "[LOG] avant scoped_lock\n"); std::fflush(stderr);
		std::scoped_lock lock(GetMutex());
		std::fprintf(stderr, "[LOG] apres scoped_lock\n"); std::fflush(stderr);

		GetSettings() = settings;
		std::fprintf(stderr, "[LOG] g_settings OK\n"); std::fflush(stderr);

		s_level.store(settings.level, std::memory_order_relaxed);
		std::fprintf(stderr, "[LOG] s_level OK\n"); std::fflush(stderr);

		if (GetFile().is_open()) GetFile().close();
		if (!settings.filePath.empty())
        	GetFile() = std::ofstream(settings.filePath, std::ios::out | std::ios::app);
		std::fprintf(stderr, "[LOG] avant ofstream\n"); std::fflush(stderr);

		g_file = std::ofstream("C:/temp/lcdlln.log", std::ios::out | std::ios::app);
		std::fprintf(stderr, "[LOG] apres ofstream open=%d\n", (int)g_file.is_open()); std::fflush(stderr);
	}

	void Log::Shutdown()
	{
		std::scoped_lock lock(GetMutex());
		if (GetFile().is_open()) { GetFile().flush(); GetFile().close(); }
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
    if (level < s_level.load(std::memory_order_relaxed)) return;
    thread_local std::string line;
    line.clear();
    std::format_to(std::back_inserter(line), "[{}][T:{}][{}][{}] {}\n",
        TimestampNow(), ThreadIdNumber(), ToString(level),
        subsystem ? subsystem : "Unknown", message);
    std::scoped_lock lock(GetMutex());
    if (GetSettings().console)
    {
        FILE* out = (level >= LogLevel::Error) ? stderr : stdout;
        std::fwrite(line.data(), 1, line.size(), out);
        std::fflush(out);
    }
    if (GetFile().is_open())
    {
        GetFile().write(line.data(), static_cast<std::streamsize>(line.size()));
        if (GetSettings().flushAlways) GetFile().flush();
    }
}
}

