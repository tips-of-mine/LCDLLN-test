#pragma once
#include <atomic>
#include <cstdlib>
#include <format>
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
        /// Max size per log file in MB before rotation (0 = no rotation).
        size_t rotation_size_mb = 10;
        /// Number of rotated files to retain (used as max_files for rotating sink).
        int retention_days = 7;
    };
    class Log final
    {
    public:
        /// Returns a filename with timestamp suffix: prefix-YYYYMMDD-HHMMSS.log
        static std::string MakeTimestampedFilename(std::string_view prefix);
        /// Initialize logging. Safe to call once; subsequent calls overwrite settings.
        static void Init(const LogSettings& settings);
        /// Shutdown logging (closes file handle). Safe to call multiple times.
        static void Shutdown();
        /// Returns true if the logger has been successfully initialized and is ready to write.
        static bool IsActive();
        /// Get current minimum log level.
        static LogLevel GetLevel();
        /// Set current minimum log level.
        static void SetLevel(LogLevel level);
        /// Write a log line for the given level/subsystem and already-formatted message.
        static void WriteLine(LogLevel level, const char* subsystem, std::string_view message);
        /// Format + write a log line. Temporarily bypasses std::format to isolate SEH crash.
        template <typename... Args>
        static void Write(LogLevel level, const char* subsystem, std::format_string<Args...> fmt, Args&&... args)
        {
            std::fprintf(stderr, "[WRITE] debut\n"); std::fflush(stderr);
            if (!s_active.load(std::memory_order_acquire))
            {
                std::fprintf(stderr, "[WRITE] not active\n"); std::fflush(stderr);
                return;
            }

            std::fprintf(stderr, "[WRITE] active OK\n"); std::fflush(stderr);
            if (level < s_level.load(std::memory_order_relaxed))
            {
                std::fprintf(stderr, "[WRITE] level filtered\n"); std::fflush(stderr);
                return;
            }

            std::fprintf(stderr, "[WRITE] avant WriteLine\n"); std::fflush(stderr);
            WriteLine(level, subsystem, fmt.get());
            std::fprintf(stderr, "[WRITE] apres WriteLine\n"); std::fflush(stderr);
        }
    private:
        static std::atomic<LogLevel> s_level;
        static std::atomic<bool>     s_active;
    };
}
#define LOG_TRACE(subsystem, format, ...) ::engine::core::Log::Write(::engine::core::LogLevel::Trace, #subsystem, format __VA_OPT__(,) __VA_ARGS__)
#define LOG_DEBUG(subsystem, format, ...) ::engine::core::Log::Write(::engine::core::LogLevel::Debug, #subsystem, format __VA_OPT__(,) __VA_ARGS__)
#define LOG_INFO(subsystem, format, ...)  ::engine::core::Log::Write(::engine::core::LogLevel::Info,  #subsystem, format __VA_OPT__(,) __VA_ARGS__)
#define LOG_WARN(subsystem, format, ...)  ::engine::core::Log::Write(::engine::core::LogLevel::Warn,  #subsystem, format __VA_OPT__(,) __VA_ARGS__)
#define LOG_ERROR(subsystem, format, ...) ::engine::core::Log::Write(::engine::core::LogLevel::Error, #subsystem, format __VA_OPT__(,) __VA_ARGS__)
#define LOG_FATAL(subsystem, format, ...)                    \
    do                                                       \
    {                                                        \
        ::engine::core::Log::Write(::engine::core::LogLevel::Fatal, #subsystem, format __VA_OPT__(,) __VA_ARGS__); \
        ::std::abort();                                      \
    } while (false)