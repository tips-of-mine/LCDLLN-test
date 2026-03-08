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
		std::mutex g_mutex;
		std::ofstream g_file;
		LogSettings g_settings{};

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
		std::scoped_lock lock(g_mutex);
		g_settings = settings;
		s_level.store(settings.level, std::memory_order_relaxed);

		// TEST TEMPORAIRE — force l'ouverture sans condition
		if (g_file.is_open())
			g_file.close();

		g_file = std::ofstream("C:/temp/lcdlln.log", std::ios::out | std::ios::app);

		if (!g_file.is_open())
			std::fprintf(stderr, "[LOG] echec ouverture fichier\n");
		else
			std::fprintf(stderr, "[LOG] fichier ouvert OK\n");
	}

	void Log::Shutdown()
	{
		std::scoped_lock lock(g_mutex);
		if (g_file.is_open())
		{
			g_file.flush();
			g_file.close();
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
		{
			return;
		}

		thread_local std::string line;
		line.clear();
		std::format_to(std::back_inserter(line), "[{}][T:{}][{}][{}] {}\n",
			TimestampNow(),
			ThreadIdNumber(),
			ToString(level),
			subsystem ? subsystem : "Unknown",
			message);

		std::scoped_lock lock(g_mutex);

		if (g_settings.console)
		{
			FILE* out = (level >= LogLevel::Error) ? stderr : stdout;
			std::fwrite(line.data(), 1, line.size(), out);
			std::fflush(out);
		}

		if (g_file.is_open())
		{
			g_file.write(line.data(), static_cast<std::streamsize>(line.size()));
			if (g_settings.flushAlways)
			{
				g_file.flush();
			}
		}
	}
}

