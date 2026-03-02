#pragma once

#include <atomic>
#include <cstdlib>
#include <format>
#include <iterator>
#include <string>
#include <string_view>

namespace engine::core
{
	enum class LogLevel : int
	{
		Trace = 0,
		Debug = 1,
		Info = 2,
		Warn = 3,
		Error = 4,
		Fatal = 5,
		Off = 6
	};

	struct LogSettings
	{
		/// Minimum level that will be emitted.
		LogLevel level = LogLevel::Info;

		/// Relative path to the log file to append to (created if missing).
		std::string filePath = "engine.log";

		/// If true, also write logs to stdout/stderr.
		bool console = true;

		/// If true, flush file output after each line (recommended for Debug builds).
		bool flushAlways = false;
	};

	class Log final
	{
	public:
		/// Initialize logging. Safe to call once; subsequent calls overwrite settings.
		static void Init(const LogSettings& settings);

		/// Shutdown logging (closes file handle). Safe to call multiple times.
		static void Shutdown();

		/// Get current minimum log level.
		static LogLevel GetLevel();

		/// Set current minimum log level.
		static void SetLevel(LogLevel level);

		/// Write a log line for the given level/subsystem and already-formatted message.
		static void WriteLine(LogLevel level, const char* subsystem, std::string_view message);

		/// Format + write a log line. Formatting uses C++20 `std::format`.
		template <typename... Args>
		static void Write(LogLevel level, const char* subsystem, std::string_view format, Args&&... args)
		{
			if (level < s_level.load(std::memory_order_relaxed))
			{
				return;
			}
			thread_local std::string formatted;
			formatted.clear();
			std::vformat_to(std::back_inserter(formatted), format, std::make_format_args(args...));
			WriteLine(level, subsystem, formatted);
		}

	private:
		static std::atomic<LogLevel> s_level;
	};
}

#define LOG_TRACE(subsystem, format, ...) ::engine::core::Log::Write(::engine::core::LogLevel::Trace, #subsystem, format __VA_OPT__(,) __VA_ARGS__)
#define LOG_DEBUG(subsystem, format, ...) ::engine::core::Log::Write(::engine::core::LogLevel::Debug, #subsystem, format __VA_OPT__(,) __VA_ARGS__)
#define LOG_INFO(subsystem, format, ...)  ::engine::core::Log::Write(::engine::core::LogLevel::Info,  #subsystem, format __VA_OPT__(,) __VA_ARGS__)
#define LOG_WARN(subsystem, format, ...)  ::engine::core::Log::Write(::engine::core::LogLevel::Warn,  #subsystem, format __VA_OPT__(,) __VA_ARGS__)
#define LOG_ERROR(subsystem, format, ...) ::engine::core::Log::Write(::engine::core::LogLevel::Error, #subsystem, format __VA_OPT__(,) __VA_ARGS__)

#define LOG_FATAL(subsystem, format, ...)                    \
	do                                                      \
	{                                                       \
		::engine::core::Log::Write(::engine::core::LogLevel::Fatal, #subsystem, format __VA_OPT__(,) __VA_ARGS__); \
		::std::abort();                                      \
	} while (false)

