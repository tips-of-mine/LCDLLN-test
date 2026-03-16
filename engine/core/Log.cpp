#include "engine/core/Log.h"

#include <spdlog/spdlog.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <chrono>
#include <cstring>
#include <ctime>
#include <memory>
#include <thread>

namespace engine::core
{
	namespace
	{
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

		spdlog::level::level_enum ToSpdlogLevel(LogLevel level)
		{
			switch (level)
			{
			case LogLevel::Trace: return spdlog::level::trace;
			case LogLevel::Debug: return spdlog::level::debug;
			case LogLevel::Info:  return spdlog::level::info;
			case LogLevel::Warn:  return spdlog::level::warn;
			case LogLevel::Error: return spdlog::level::err;
			case LogLevel::Fatal: return spdlog::level::critical;
			case LogLevel::Off:   return spdlog::level::off;
			default:              return spdlog::level::info;
			}
		}

		const char* const c_runtimeLoggerName = "runtime";
		std::shared_ptr<spdlog::logger> s_logger;
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
		std::fprintf(stderr, "[LOG::INIT] avant Shutdown\n"); std::fflush(stderr);
		Shutdown();
		std::fprintf(stderr, "[LOG::INIT] apres Shutdown\n"); std::fflush(stderr);
		s_level.store(settings.level, std::memory_order_relaxed);
		std::fprintf(stderr, "[LOG::INIT] apres s_level.store\n"); std::fflush(stderr);

		std::vector<spdlog::sink_ptr> sinks;
		std::fprintf(stderr, "[LOG::INIT] apres vector sinks\n"); std::fflush(stderr);

		if (settings.console)
		{
			std::fprintf(stderr, "[LOG::INIT] avant console_sink\n"); std::fflush(stderr);
			auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
			sinks.push_back(console_sink);
			std::fprintf(stderr, "[LOG::INIT] console_sink OK\n"); std::fflush(stderr);
		}
		std::fprintf(stderr, "[LOG::INIT] avant file sink check filePath='%s'\n", settings.filePath.c_str()); std::fflush(stderr);

		if (!settings.filePath.empty())
		{
			std::fprintf(stderr, "[LOG::INIT] avant max_bytes\n"); std::fflush(stderr);
			const size_t max_bytes = (settings.rotation_size_mb > 0)
				? (settings.rotation_size_mb * 1024u * 1024u)
				: (10u * 1024u * 1024u);
			std::fprintf(stderr, "[LOG::INIT] avant max_files\n"); std::fflush(stderr);
			const int max_files = (settings.retention_days > 0)
				? std::max(1, settings.retention_days)
				: 7;
			std::fprintf(stderr, "[LOG::INIT] avant make_shared file_sink\n"); std::fflush(stderr);
			try
			{
				auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
            		settings.filePath, max_bytes, static_cast<size_t>(max_files));
				std::fprintf(stderr, "[LOG::INIT] file_sink OK\n"); std::fflush(stderr);
				sinks.push_back(file_sink);
			}
			catch (const std::exception& e)
			{
				std::fprintf(stderr, "[LOG::INIT] file_sink EXCEPTION: %s\n", e.what()); std::fflush(stderr);
				if (settings.console)
				{
					spdlog::default_logger()->error("[Log] Init FAILED: cannot open file {} — {}", settings.filePath, e.what());
				}
				return;
			}
		}

		std::fprintf(stderr, "[LOG::INIT] avant sinks.empty check\n"); std::fflush(stderr);
		if (sinks.empty())
		{
			return;
		}

		std::fprintf(stderr, "[LOG::INIT] avant make_shared logger\n"); std::fflush(stderr);
		auto logger = std::make_shared<spdlog::logger>(c_runtimeLoggerName, sinks.begin(), sinks.end());
		std::fprintf(stderr, "[LOG::INIT] avant set_level\n"); std::fflush(stderr);
		logger->set_level(ToSpdlogLevel(settings.level));
		if (settings.flushAlways)
		{
			std::fprintf(stderr, "[LOG::INIT] avant flush_on\n"); std::fflush(stderr);
			logger->flush_on(spdlog::level::trace);
		}

		std::fprintf(stderr, "[LOG::INIT] avant s_logger assign\n"); std::fflush(stderr);
		s_logger = logger;
		std::fprintf(stderr, "[LOG::INIT] s_logger assign OK\n"); std::fflush(stderr);

		s_active.store(true, std::memory_order_release);
		std::fprintf(stderr, "[LOG::INIT] tout OK\n"); std::fflush(stderr);

		LOG_INFO(Core, "[Log] Init OK (file={}, level={}, rotation_size_mb={}, retention_days={})",
			settings.filePath.empty() ? "<none>" : settings.filePath,
			ToString(settings.level),
			static_cast<unsigned>(settings.rotation_size_mb),
			settings.retention_days);
	}

	void Log::Shutdown()
	{
		if (!s_active.exchange(false, std::memory_order_acq_rel))
			return;

		if (s_logger)
		{
			s_logger->flush();
			s_logger.reset();
		}
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
		if (s_logger)
		{
			s_logger->set_level(ToSpdlogLevel(level));
		}
	}

	void Log::WriteLine(LogLevel level, const char* subsystem, std::string_view message)
	{
		if (level < s_level.load(std::memory_order_relaxed))
			return;

		if (!s_logger)
			return;

		spdlog::level::level_enum spd_level = ToSpdlogLevel(level);
		if (spd_level == spdlog::level::off)
			return;

		const std::string formatted = std::string("[") + (subsystem ? subsystem : "?") + "] " + std::string(message);
		s_logger->log(spd_level, "{}", formatted);
	}
}