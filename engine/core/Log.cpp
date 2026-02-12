#include "engine/core/Log.h"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <thread>

namespace engine::core {

std::mutex Log::s_mutex;
std::ofstream Log::s_file;
LogLevel Log::s_minLevel = LogLevel::Info;
bool Log::s_flushAlways = false;
bool Log::s_initialized = false;

void Log::Init(const LogSettings& settings) {
    std::lock_guard<std::mutex> lock(s_mutex);
    s_minLevel = settings.minLevel;
    s_flushAlways = settings.flushAlways;
    if (!settings.filePath.empty()) {
        s_file.open(settings.filePath, std::ios::out | std::ios::app);
    }
    s_initialized = true;
}

void Log::Shutdown() {
    std::lock_guard<std::mutex> lock(s_mutex);
    if (s_file.is_open()) {
        s_file.flush();
        s_file.close();
    }
    s_initialized = false;
}

std::string Log::BuildMessage() {
    return {};
}

const char* Log::ToString(LogLevel level) {
    switch (level) {
    case LogLevel::Trace: return "TRACE";
    case LogLevel::Debug: return "DEBUG";
    case LogLevel::Info: return "INFO";
    case LogLevel::Warn: return "WARN";
    case LogLevel::Error: return "ERROR";
    case LogLevel::Fatal: return "FATAL";
    default: return "UNKNOWN";
    }
}

void Log::WriteImpl(LogLevel level, const char* subsystem, const std::string& message) {
    if (level < s_minLevel) {
        return;
    }

    const auto now = std::chrono::system_clock::now();
    const auto epochMs = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
    const std::time_t currentTime = std::chrono::system_clock::to_time_t(now);

    std::tm localTm{};
#ifdef _WIN32
    localtime_s(&localTm, &currentTime);
#else
    localtime_r(&currentTime, &localTm);
#endif

    std::ostringstream prefix;
    prefix << '[' << std::put_time(&localTm, "%Y-%m-%d %H:%M:%S") << '.' << std::setfill('0') << std::setw(3)
           << epochMs.count() << ']';
    prefix << "[T:" << std::this_thread::get_id() << ']';
    prefix << '[' << ToString(level) << ']';
    prefix << '[' << subsystem << "] ";

    const std::string line = prefix.str() + message;

    std::lock_guard<std::mutex> lock(s_mutex);
    if (!s_initialized) {
        std::cout << line << '\n';
        return;
    }

    std::cout << line << '\n';

    if (s_file.is_open()) {
        s_file << line << '\n';
        if (s_flushAlways) {
            s_file.flush();
        }
    }
}

} // namespace engine::core
