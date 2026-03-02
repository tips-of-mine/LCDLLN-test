/**
 * @file Log.h
 * @brief Thread-safe logging with console + file, level filtering, subsystem tagging.
 * Format: [YYYY-MM-DD HH:MM:SS.mmm][T:threadId][LEVEL][Subsystem] message
 */

#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>

namespace engine::core {

/** Minimum level to emit (messages below are dropped). */
enum class LogLevel : uint8_t {
    Debug = 0,
    Info,
    Warn,
    Error,
    Fatal,
    Off
};

/** Initialize logging: console + optional file. Must be called before any LOG_* macro. */
void LogInit(std::string_view logFilePath = {}, LogLevel minLevel = LogLevel::Debug);

/** Shutdown logging: flush and close file. */
void LogShutdown();

/** Set minimum level (thread-safe). */
void LogSetLevel(LogLevel level);

/** Raw write (thread-safe). Used by macros; buffer is fixed-size to avoid allocations in hot path. */
void LogWrite(LogLevel level, const char* subsystem, const char* fmt, ...);

/** Abort after logging (for LOG_FATAL). */
void LogFatalAbort();

} // namespace engine::core

/** Subsystem tag for macros (string literal). */
#define LOG_SUBSYSTEM_STR(S) #S

#define LOG_DEBUG(Subsystem, ...) \
    ::engine::core::LogWrite(::engine::core::LogLevel::Debug, LOG_SUBSYSTEM_STR(Subsystem), __VA_ARGS__)
#define LOG_INFO(Subsystem, ...) \
    ::engine::core::LogWrite(::engine::core::LogLevel::Info, LOG_SUBSYSTEM_STR(Subsystem), __VA_ARGS__)
#define LOG_WARN(Subsystem, ...) \
    ::engine::core::LogWrite(::engine::core::LogLevel::Warn, LOG_SUBSYSTEM_STR(Subsystem), __VA_ARGS__)
#define LOG_ERROR(Subsystem, ...) \
    ::engine::core::LogWrite(::engine::core::LogLevel::Error, LOG_SUBSYSTEM_STR(Subsystem), __VA_ARGS__)
#define LOG_FATAL(Subsystem, ...) \
    do { \
        ::engine::core::LogWrite(::engine::core::LogLevel::Fatal, LOG_SUBSYSTEM_STR(Subsystem), __VA_ARGS__); \
        ::engine::core::LogFatalAbort(); \
    } while (0)
