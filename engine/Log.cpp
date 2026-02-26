// engine/core/Log.cpp
// Implementation of the logging system.
// Uses a mutex-protected write path; messages are formatted into a fixed-size
// stack buffer to avoid heap allocations in the hot path.

#include "Log.h"

#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <ctime>
#include <chrono>
#include <mutex>
#include <thread>
#include <atomic>
#include <string>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>  // for HANDLE-based console colouring (optional)
#endif

namespace engine::core {

// ─── Statics ────────────────────────────────────────────────────────────────

static std::mutex       g_mutex;
static FILE*            g_file    = nullptr;
static std::atomic<uint8_t> g_minLevel{ static_cast<uint8_t>(LogLevel::Debug) };

// Fixed-size message buffer per call (avoids alloc in hot path).
static constexpr std::size_t kBufSize = 2048;

// ─── Helpers ─────────────────────────────────────────────────────────────────

const char* LogLevelName(LogLevel level) noexcept {
    switch (level) {
        case LogLevel::Debug: return "DEBUG";
        case LogLevel::Info:  return "INFO ";
        case LogLevel::Warn:  return "WARN ";
        case LogLevel::Error: return "ERROR";
        case LogLevel::Fatal: return "FATAL";
    }
    return "?????";
}

/// Format a timestamp into buf (must be >= 24 bytes).
/// Output: YYYY-MM-DD HH:MM:SS.mmm
static void FormatTimestamp(char* buf, std::size_t bufSize) noexcept {
    using namespace std::chrono;
    auto now      = system_clock::now();
    auto now_ms   = duration_cast<milliseconds>(now.time_since_epoch());
    auto secs     = duration_cast<seconds>(now_ms);
    auto ms_part  = static_cast<int>((now_ms - secs).count());

    std::time_t tt = secs.count();
    struct tm   tmbuf {};
#ifdef _WIN32
    localtime_s(&tmbuf, &tt);
#else
    localtime_r(&tt, &tmbuf);
#endif
    std::snprintf(buf, bufSize,
        "%04d-%02d-%02d %02d:%02d:%02d.%03d",
        tmbuf.tm_year + 1900,
        tmbuf.tm_mon  + 1,
        tmbuf.tm_mday,
        tmbuf.tm_hour,
        tmbuf.tm_min,
        tmbuf.tm_sec,
        ms_part);
}

// ─── Public API ──────────────────────────────────────────────────────────────

void LogInit(std::string_view logFilePath, LogLevel minLevel) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_minLevel.store(static_cast<uint8_t>(minLevel), std::memory_order_relaxed);

    if (!logFilePath.empty()) {
        // Build null-terminated copy for fopen.
        std::string path(logFilePath);
#ifdef _WIN32
        fopen_s(&g_file, path.c_str(), "a");
#else
        g_file = std::fopen(path.c_str(), "a");
#endif
        if (!g_file) {
            // Non-fatal: continue without file logging.
            std::fprintf(stderr, "[Log] WARNING: Cannot open log file '%s'\n", path.c_str());
        }
    }
}

void LogShutdown() {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_file) {
        std::fflush(g_file);
        std::fclose(g_file);
        g_file = nullptr;
    }
    std::fflush(stdout);
}

void LogSetLevel(LogLevel minLevel) {
    g_minLevel.store(static_cast<uint8_t>(minLevel), std::memory_order_relaxed);
}

void LogWrite(LogLevel level, const char* subsystem, const char* fmt, ...) noexcept {
    // Fast path: discard below minimum level without locking.
    if (static_cast<uint8_t>(level) < g_minLevel.load(std::memory_order_relaxed)) {
        return;
    }

    // Format user message into a fixed buffer (no heap alloc).
    char msgBuf[kBufSize];
    {
        va_list args;
        va_start(args, fmt);
        std::vsnprintf(msgBuf, sizeof(msgBuf), fmt, args);
        va_end(args);
    }

    // Format timestamp.
    char tsBuf[32];
    FormatTimestamp(tsBuf, sizeof(tsBuf));

    // Gather thread id as integer.
    // std::hash gives a stable size_t from thread::id.
    const auto tid = std::hash<std::thread::id>{}(std::this_thread::get_id());

    // Build the full log line.
    char lineBuf[kBufSize + 128];
    std::snprintf(lineBuf, sizeof(lineBuf),
        "[%s][T:%zu][%s][%s] %s\n",
        tsBuf,
        tid,
        LogLevelName(level),
        subsystem ? subsystem : "?",
        msgBuf);

    // Write under lock (console + optional file).
    std::lock_guard<std::mutex> lock(g_mutex);
    std::fputs(lineBuf, stdout);
    if (g_file) {
        std::fputs(lineBuf, g_file);
#ifndef NDEBUG
        // Flush immediately in debug builds to ensure crash-safe output.
        std::fflush(g_file);
#endif
    }
}

} // namespace engine::core
