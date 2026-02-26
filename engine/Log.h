#pragma once
// engine/core/Log.h
// Thread-safe logging: console + file, filterable by level.
// Format: [YYYY-MM-DD HH:MM:SS.mmm][T:threadId][LEVEL][Subsystem] message
// Usage: LOG_INFO(Render, "Initialized %s", name); LOG_FATAL(Core, "Unrecoverable error");

#include <string>
#include <string_view>
#include <cstdint>

namespace engine::core {

/// Log severity levels, ordered from least to most severe.
enum class LogLevel : uint8_t {
    Debug = 0,
    Info  = 1,
    Warn  = 2,
    Error = 3,
    Fatal = 4,
};

/// Returns the string name of a level (e.g. "INFO").
const char* LogLevelName(LogLevel level) noexcept;

/// Initialize the logging system.
/// @param logFilePath  Path to the output log file (empty = no file).
/// @param minLevel     Minimum level to emit. Messages below this are discarded.
void LogInit(std::string_view logFilePath, LogLevel minLevel = LogLevel::Debug);

/// Flush all pending writes and close the log file.
void LogShutdown();

/// Change the minimum filter level at runtime.
void LogSetLevel(LogLevel minLevel);

/// Core logging function (thread-safe). Prefer the macros below.
/// @param level      Severity.
/// @param subsystem  Short subsystem tag (e.g. "Render").
/// @param fmt        printf-style format string.
/// @param ...        Arguments.
void LogWrite(LogLevel level, const char* subsystem, const char* fmt, ...) noexcept
    __attribute__((format(printf, 3, 4)));

/// LOG_FATAL logs then calls std::abort().
#define LOG_DEBUG(Subsystem, fmt, ...) \
    ::engine::core::LogWrite(::engine::core::LogLevel::Debug, #Subsystem, fmt, ##__VA_ARGS__)

#define LOG_INFO(Subsystem, fmt, ...) \
    ::engine::core::LogWrite(::engine::core::LogLevel::Info,  #Subsystem, fmt, ##__VA_ARGS__)

#define LOG_WARN(Subsystem, fmt, ...) \
    ::engine::core::LogWrite(::engine::core::LogLevel::Warn,  #Subsystem, fmt, ##__VA_ARGS__)

#define LOG_ERROR(Subsystem, fmt, ...) \
    ::engine::core::LogWrite(::engine::core::LogLevel::Error, #Subsystem, fmt, ##__VA_ARGS__)

#define LOG_FATAL(Subsystem, fmt, ...) \
    do { \
        ::engine::core::LogWrite(::engine::core::LogLevel::Fatal, #Subsystem, fmt, ##__VA_ARGS__); \
        ::engine::core::LogShutdown(); \
        std::abort(); \
    } while(0)

} // namespace engine::core
