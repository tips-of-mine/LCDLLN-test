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
		Shutdown();
		s_level.store(settings.level, std::memory_order_relaxed);

		std::vector<spdlog::sink_ptr> sinks;
		if (settings.console)
		{
			auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
			sinks.push_back(console_sink);
		}

		if (!settings.filePath.empty())
		{
			const size_t max_bytes = (settings.rotation_size_mb > 0)
				? (settings.rotation_size_mb * 1024u * 1024u)
				: (10u * 1024u * 1024u);
			const int max_files = (settings.retention_days > 0)
				? std::max(1, settings.retention_days)
				: 7;
			try
			{
				auto file_sink = (settings.rotation_size_mb > 0)
					? std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
						settings.filePath, max_bytes, static_cast<size_t>(max_files))
					: std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
						settings.filePath, max_bytes, static_cast<size_t>(max_files));
				sinks.push_back(file_sink);
			}
			catch (const std::exception& e)
			{
				if (settings.console)
				{
					spdlog::default_logger()->error("[Log] Init FAILED: cannot open file {} — {}", settings.filePath, e.what());
				}
				return;
			}
		}

		if (sinks.empty())
		{
			return;
		}

		auto logger = std::make_shared<spdlog::logger>(c_runtimeLoggerName, sinks.begin(), sinks.end());
		logger->set_level(ToSpdlogLevel(settings.level));
		if (settings.flushAlways)
		{
			logger->flush_on(spdlog::level::trace);
		}
		spdlog::register_logger(logger);
		spdlog::set_default_logger(logger);

		LOG_INFO(Core, "[Log] Init OK (file={}, level={}, rotation_size_mb={}, retention_days={})",
			settings.filePath.empty() ? "<none>" : settings.filePath,
			ToString(settings.level),
			static_cast<unsigned>(settings.rotation_size_mb),
			settings.retention_days);
	}

	void Log::Shutdown()
	{
		auto logger = spdlog::get(c_runtimeLoggerName);
		if (logger)
		{
			LOG_INFO(Core, "[Log] Shutdown: runtime logger dropped");
			logger->flush();
		}
		spdlog::drop(c_runtimeLoggerName);
	}

	LogLevel Log::GetLevel()
	{
		return s_level.load(std::memory_order_relaxed);
	}

	void Log::SetLevel(LogLevel level)
	{
		s_level.store(level, std::memory_order_relaxed);
		if (auto logger = spdlog::get(c_runtimeLoggerName))
		{
			logger->set_level(ToSpdlogLevel(level));
		}
	}

	void Log::WriteLine(LogLevel level, const char* subsystem, std::string_view message)
	{
		if (level < s_level.load(std::memory_order_relaxed))
			return;

		auto logger = spdlog::get(c_runtimeLoggerName);
		if (!logger)
			return;

		spdlog::level::level_enum spd_level = ToSpdlogLevel(level);
		if (spd_level == spdlog::level::off)
			return;

		const std::string formatted = std::string("[") + (subsystem ? subsystem : "?") + "] " + std::string(message);
		logger->log(spd_level, "{}", formatted);
	}
}
