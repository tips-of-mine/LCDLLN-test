#pragma once

/**
 * @file Log.h
 * @brief Thread-safe logging system for engine/game/tools.
 *
 * Format: [YYYY-MM-DD HH:MM:SS.mmm][T:threadId][LEVEL][Subsystem] message
 *
 * Usage:
 *   Log::Init("engine.log");
 *   LOG_INFO(Render, "Initializing renderer version {}", 1);
 *   LOG_FATAL(Core, "Critical failure: {}", reason);  // logs then aborts
 *   Log::Shutdown();
 */

#include <string>
#include <string_view>
#include <format>
#include <source_location>

namespace engine::core {

/// Log severity levels (ordered from lowest to highest).
enum class LogLevel : int {
    Trace   = 0,
    Debug   = 1,
    Info    = 2,
    Warning = 3,
    Error   = 4,
    Fatal   = 5,
};

/// Returns the short string name for a log level.
constexpr std::string_view LogLevelName(LogLevel level) noexcept {
    switch (level) {
        case LogLevel::Trace:   return "TRACE";
        case LogLevel::Debug:   return "DEBUG";
        case LogLevel::Info:    return "INFO ";
        case LogLevel::Warning: return "WARN ";
        case LogLevel::Error:   return "ERROR";
        case LogLevel::Fatal:   return "FATAL";
    }
    return "?????";
}

/// Core logging class. All public methods are thread-safe.
class Log {
public:
    Log() = delete;

    /**
     * @brief Initialises the logger (console + optional file sink).
     * @param logFilePath  Path to the log file; empty = console only.
     * @param minLevel     Minimum level to output.
     */
    static void Init(std::string_view logFilePath = "",
                     LogLevel minLevel            = LogLevel::Debug);

    /// Flushes and closes the file sink.
    static void Shutdown();

    /**
     * @brief Writes a formatted log entry.
     *
     * Thread-safe. The hot path does no heap allocation when the message
     * fits in the internal fixed-size stack buffer (512 bytes).
     *
     * @param level    Severity level.
     * @param subsystem Name of the subsystem (e.g. "Render", "Core").
     * @param message  Pre-formatted message string.
     */
    static void Write(LogLevel level, std::string_view subsystem,
                      std::string_view message) noexcept;

    /// Sets the minimum log level at runtime.
    static void SetLevel(LogLevel level) noexcept;

    /// Returns the current minimum log level.
    static LogLevel GetLevel() noexcept;
};

} // namespace engine::core

// ---------------------------------------------------------------------------
// Public macros — LOG_<LEVEL>(Subsystem, fmt, ...)
// ---------------------------------------------------------------------------
// Each macro:
//   1. Guards on the current log level (zero-cost when filtered).
//   2. Formats the message with std::format.
//   3. Calls Log::Write.
// LOG_FATAL additionally calls std::abort() after writing.

#define ENGINE_LOG_IMPL(level, subsystem, ...)                                \
    do {                                                                       \
        if (static_cast<int>(level) >=                                        \
            static_cast<int>(::engine::core::Log::GetLevel())) {              \
            ::engine::core::Log::Write(                                        \
                level, #subsystem,                                             \
                ::std::format(__VA_ARGS__));                                   \
        }                                                                      \
    } while (0)

/// Trace-level log (very verbose, typically stripped in release).
#define LOG_TRACE(subsystem, ...)   ENGINE_LOG_IMPL(::engine::core::LogLevel::Trace,   subsystem, __VA_ARGS__)

/// Debug-level log.
#define LOG_DEBUG(subsystem, ...)   ENGINE_LOG_IMPL(::engine::core::LogLevel::Debug,   subsystem, __VA_ARGS__)

/// Informational log.
#define LOG_INFO(subsystem, ...)    ENGINE_LOG_IMPL(::engine::core::LogLevel::Info,    subsystem, __VA_ARGS__)

/// Warning log.
#define LOG_WARN(subsystem, ...)    ENGINE_LOG_IMPL(::engine::core::LogLevel::Warning, subsystem, __VA_ARGS__)

/// Error log.
#define LOG_ERROR(subsystem, ...)   ENGINE_LOG_IMPL(::engine::core::LogLevel::Error,   subsystem, __VA_ARGS__)

/**
 * @brief Fatal log: writes the message then calls std::abort().
 *
 * Use for unrecoverable errors only. The abort ensures the call site is
 * [[noreturn]] in practice (though the macro itself cannot be marked as
 * such).
 */
#define LOG_FATAL(subsystem, ...)                                              \
    do {                                                                       \
        ::engine::core::Log::Write(::engine::core::LogLevel::Fatal,           \
                                   #subsystem,                                 \
                                   ::std::format(__VA_ARGS__));                \
        ::std::abort();                                                        \
    } while (0)
