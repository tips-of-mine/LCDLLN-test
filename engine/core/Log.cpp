/**
 * @file Log.cpp
 * @brief Implementation of thread-safe logging (console + file, level filtering).
 */

#include "engine/core/Log.h"
#ifdef _WIN32
#include <windows.h>
#endif
#include <algorithm>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>

namespace engine::core {

namespace {

constexpr size_t kLogBufferSize = 4096;
constexpr size_t kFormatBufferSize = 3584; // leave room for prefix

std::mutex g_logMutex;
std::FILE* g_logFile = nullptr;
LogLevel g_minLevel = LogLevel::Debug;
bool g_initialized = false;

const char* LevelToString(LogLevel level) {
    switch (level) {
        case LogLevel::Debug: return "DEBUG";
        case LogLevel::Info:  return "INFO ";
        case LogLevel::Warn:  return "WARN ";
        case LogLevel::Error: return "ERROR";
        case LogLevel::Fatal: return "FATAL";
        default: return "?????";
    }
}

void FormatTimestamp(char* out, size_t outSize) {
    auto now = std::chrono::system_clock::now();
    auto t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    std::tm tm_buf;
#ifdef _WIN32
    localtime_s(&tm_buf, &t);
#else
    localtime_r(&t, &tm_buf);
#endif
    std::snprintf(out, outSize, "%04d-%02d-%02d %02d:%02d:%02d.%03lld",
        tm_buf.tm_year + 1900, tm_buf.tm_mon + 1, tm_buf.tm_mday,
        tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec,
        static_cast<long long>(ms.count()));
}

} // namespace

void LogInit(std::string_view logFilePath, LogLevel minLevel) {
    std::lock_guard<std::mutex> lock(g_logMutex);
    if (g_initialized) return;
    g_minLevel = minLevel;
    if (!logFilePath.empty()) {
        std::string path(logFilePath);
        g_logFile = std::fopen(path.c_str(), "a");
        if (!g_logFile) {
            std::fprintf(stderr, "[Log] Failed to open log file: %s\n", path.c_str());
        }
    }
    g_initialized = true;
}

void LogShutdown() {
    std::lock_guard<std::mutex> lock(g_logMutex);
    if (g_logFile) {
        std::fflush(g_logFile);
        std::fclose(g_logFile);
        g_logFile = nullptr;
    }
    g_initialized = false;
}

void LogSetLevel(LogLevel level) {
    std::lock_guard<std::mutex> lock(g_logMutex);
    g_minLevel = level;
}

void LogWrite(LogLevel level, const char* subsystem, const char* fmt, ...) {
    if (level < g_minLevel || level == LogLevel::Off) return;
    char fmtBuf[kFormatBufferSize];
    va_list args;
    va_start(args, fmt);
    int n = std::vsnprintf(fmtBuf, sizeof(fmtBuf), fmt, args);
    va_end(args);
    if (n < 0) return;
    size_t msgLen = static_cast<size_t>(std::min(n, static_cast<int>(sizeof(fmtBuf) - 1)));
    fmtBuf[msgLen] = '\0';

    char timestamp[32];
    FormatTimestamp(timestamp, sizeof(timestamp));
    unsigned long long tid = 0;
#ifdef _WIN32
    tid = static_cast<unsigned long long>(GetCurrentThreadId());
#else
    tid = static_cast<unsigned long long>(std::hash<std::thread::id>{}(std::this_thread::get_id()));
#endif

    char line[kLogBufferSize];
    int len = std::snprintf(line, sizeof(line), "[%s][T:%llu][%s][%s] %s\n",
        timestamp, tid, LevelToString(level), subsystem, fmtBuf);
    if (len <= 0) return;
    size_t lineLen = static_cast<size_t>(std::min(len, static_cast<int>(sizeof(line) - 1)));

    std::lock_guard<std::mutex> lock(g_logMutex);
    if (g_logFile) {
        std::fwrite(line, 1, lineLen, g_logFile);
        std::fflush(g_logFile);
    }
    std::FILE* out = (level >= LogLevel::Error) ? stderr : stdout;
    std::fwrite(line, 1, lineLen, out);
    std::fflush(out);
}

void LogFatalAbort() {
    std::abort();
}

} // namespace engine::core
