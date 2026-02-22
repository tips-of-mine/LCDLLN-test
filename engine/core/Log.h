#pragma once

#include <cstdint>
#include <cstdlib>
#include <string>

namespace engine::core {

enum class LogLevel {
    Trace = 0,
    Debug,
    Info,
    Warn,
    Error,
    Fatal,
    Off
};

struct LogConfig {
    std::string filePath;
    LogLevel level = LogLevel::Info;
    bool alsoConsole = true;
};

class Log {
public:
    static void Init(const LogConfig& config);
    static void Shutdown();
    static void SetLevel(LogLevel level);
    static LogLevel GetLevel();
    static bool IsInitialized();
    static void Write(LogLevel level, const char* subsystem, const char* format, ...);
};

} // namespace engine::core

#define LOG_TRACE(Subsystem, Format, ...) \
    ::engine::core::Log::Write(::engine::core::LogLevel::Trace, #Subsystem, Format __VA_OPT__(,) __VA_ARGS__)
#define LOG_DEBUG(Subsystem, Format, ...) \
    ::engine::core::Log::Write(::engine::core::LogLevel::Debug, #Subsystem, Format __VA_OPT__(,) __VA_ARGS__)
#define LOG_INFO(Subsystem, Format, ...) \
    ::engine::core::Log::Write(::engine::core::LogLevel::Info, #Subsystem, Format __VA_OPT__(,) __VA_ARGS__)
#define LOG_WARN(Subsystem, Format, ...) \
    ::engine::core::Log::Write(::engine::core::LogLevel::Warn, #Subsystem, Format __VA_OPT__(,) __VA_ARGS__)
#define LOG_ERROR(Subsystem, Format, ...) \
    ::engine::core::Log::Write(::engine::core::LogLevel::Error, #Subsystem, Format __VA_OPT__(,) __VA_ARGS__)
#define LOG_FATAL(Subsystem, Format, ...) \
    do { \
        ::engine::core::Log::Write(::engine::core::LogLevel::Fatal, #Subsystem, Format __VA_OPT__(,) __VA_ARGS__); \
        std::abort(); \
    } while (false)
