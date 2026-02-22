#pragma once

#include <cstdarg>
#include <cstdint>
#include <string>

namespace engine::core {

enum class LogLevel : uint8_t {
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
    bool enableConsole = true;
    bool enableFile = true;
};

class Log {
public:
    static void Init(const LogConfig& config);
    static void Shutdown();
    static void SetLevel(LogLevel level);
    static LogLevel GetLevel();

    static void Write(LogLevel level, const char* subsystem, const char* format, ...);
    static void WriteV(LogLevel level, const char* subsystem, const char* format, va_list args);
};

} // namespace engine::core

#define LOG_TRACE(Subsystem, ...) ::engine::core::Log::Write(::engine::core::LogLevel::Trace, #Subsystem, __VA_ARGS__)
#define LOG_DEBUG(Subsystem, ...) ::engine::core::Log::Write(::engine::core::LogLevel::Debug, #Subsystem, __VA_ARGS__)
#define LOG_INFO(Subsystem, ...) ::engine::core::Log::Write(::engine::core::LogLevel::Info, #Subsystem, __VA_ARGS__)
#define LOG_WARN(Subsystem, ...) ::engine::core::Log::Write(::engine::core::LogLevel::Warn, #Subsystem, __VA_ARGS__)
#define LOG_ERROR(Subsystem, ...) ::engine::core::Log::Write(::engine::core::LogLevel::Error, #Subsystem, __VA_ARGS__)
#define LOG_FATAL(Subsystem, ...) \
    do { \
        ::engine::core::Log::Write(::engine::core::LogLevel::Fatal, #Subsystem, __VA_ARGS__); \
        ::std::abort(); \
    } while (0)
