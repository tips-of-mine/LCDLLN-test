#pragma once

#include <cstdlib>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <utility>

namespace engine::core {

enum class LogLevel {
    Trace = 0,
    Debug,
    Info,
    Warn,
    Error,
    Fatal
};

struct LogSettings {
    LogLevel minLevel = LogLevel::Info;
    std::string filePath = "engine.log";
    bool flushAlways = false;
};

class Log {
public:
    static void Init(const LogSettings& settings = {});
    static void Shutdown();

    template <typename... Args>
    static void Write(LogLevel level, const char* subsystem, Args&&... args) {
        WriteImpl(level, subsystem, BuildMessage(std::forward<Args>(args)...));
    }

private:
    static std::string BuildMessage();

    template <typename T, typename... Rest>
    static std::string BuildMessage(T&& value, Rest&&... rest) {
        std::ostringstream oss;
        Append(oss, std::forward<T>(value));
        (Append(oss, std::forward<Rest>(rest)), ...);
        return oss.str();
    }

    template <typename T>
    static void Append(std::ostringstream& oss, T&& value) {
        oss << std::forward<T>(value);
    }

    static void WriteImpl(LogLevel level, const char* subsystem, const std::string& message);
    static const char* ToString(LogLevel level);

    static std::mutex s_mutex;
    static std::ofstream s_file;
    static LogLevel s_minLevel;
    static bool s_flushAlways;
    static bool s_initialized;
};

} // namespace engine::core

#define LOG_TRACE(subsystem, ...) ::engine::core::Log::Write(::engine::core::LogLevel::Trace, #subsystem, __VA_ARGS__)
#define LOG_DEBUG(subsystem, ...) ::engine::core::Log::Write(::engine::core::LogLevel::Debug, #subsystem, __VA_ARGS__)
#define LOG_INFO(subsystem, ...)  ::engine::core::Log::Write(::engine::core::LogLevel::Info, #subsystem, __VA_ARGS__)
#define LOG_WARN(subsystem, ...)  ::engine::core::Log::Write(::engine::core::LogLevel::Warn, #subsystem, __VA_ARGS__)
#define LOG_ERROR(subsystem, ...) ::engine::core::Log::Write(::engine::core::LogLevel::Error, #subsystem, __VA_ARGS__)
#define LOG_FATAL(subsystem, ...) \
    do { \
        ::engine::core::Log::Write(::engine::core::LogLevel::Fatal, #subsystem, __VA_ARGS__); \
        std::abort(); \
    } while (false)
