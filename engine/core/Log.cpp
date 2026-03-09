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
		std::mutex* g_mutex = new std::mutex();
		std::ofstream* g_file = new std::ofstream();
		LogSettings* g_settings = new LogSettings();

		std::mutex& GetMutex()   { return *g_mutex; }
		std::ofstream& GetFile() { return *g_file; }
		LogSettings& GetSettings() { return *g_settings; }

		const char* ToString(LogLevel level)
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
	    std::fprintf(stderr, "[LOG] 1 g_mutex=%p g_settings=%p g_file=%p\n",
	        (void*)g_mutex, (void*)g_settings, (void*)g_file);
	    std::fflush(stderr);
	
	    std::fprintf(stderr, "[LOG] 2 bypass mutex test\n"); std::fflush(stderr);
	    *g_settings = settings;
	    std::fprintf(stderr, "[LOG] 3 settings OK\n"); std::fflush(stderr);
	
	    s_level.store(settings.level, std::memory_order_relaxed);
	    std::fprintf(stderr, "[LOG] 4 s_level OK\n"); std::fflush(stderr);
	
	    if (!settings.filePath.empty())
	    {
	        std::fprintf(stderr, "[LOG] 5 ouverture '%s'\n", settings.filePath.c_str()); std::fflush(stderr);
	        g_file->open(settings.filePath, std::ios::out | std::ios::app);
	        std::fprintf(stderr, "[LOG] 6 open=%d\n", (int)g_file->is_open()); std::fflush(stderr);
	    }
	}

	void Log::Shutdown()
	{
		std::scoped_lock lock(GetMutex());
		if (GetFile().is_open())
		{
			GetFile().flush();
			GetFile().close();
		}
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

		thread_local std::string line;
		line.clear();
		std::format_to(std::back_inserter(line), "[{}][T:{}][{}][{}] {}\n",
			TimestampNow(),
			ThreadIdNumber(),
			ToString(level),
			subsystem ? subsystem : "Unknown",
			message);

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
			if (GetSettings().flushAlways)
				GetFile().flush();
		}
	}
}
