/**
 * @file Log.cpp
 * @brief Implementation of the thread-safe logging system.
 *
 * Design notes:
 *  - A single std::mutex serialises all writes (simple, safe).
 *  - Entries are built into a fixed-size stack buffer (512 bytes) to avoid
 *    heap allocations in the hot path; larger messages fall back to a
 *    std::string.
 *  - The file sink is flushed after every write in Debug builds; in Release
 *    it is flushed periodically by the OS (acceptable for non-fatal writes).
 */

#include "Log.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <mutex>
#include <thread>
#include <array>
#include <cstring>

namespace engine::core {

// ---------------------------------------------------------------------------
// Internal state (file-scoped)
// ---------------------------------------------------------------------------
namespace {

/// Guards all writes to console and file.
std::mutex  g_logMutex;

/// Optional file sink (nullptr = disabled).
std::ofstream g_logFile;

/// Minimum level; reads are relaxed because the mutex also barriers writes.
std::atomic<int> g_minLevel{ static_cast<int>(LogLevel::Debug) };

/// Fixed-size stack buffer used to build log lines without heap allocation.
constexpr std::size_t kStackBufSize = 512;

// ---------------------------------------------------------------------------
// Timestamp helper
// ---------------------------------------------------------------------------

/**
 * @brief Fills @p buf with the current local time in the format
 *        YYYY-MM-DD HH:MM:SS.mmm  (exactly 23 chars + null terminator).
 *
 * @param buf   Output buffer; must be at least 24 bytes.
 */
void FillTimestamp(char* buf, std::size_t bufSize) noexcept {
    using namespace std::chrono;

    // Wall-clock time for human-readable date/time.
    const auto now     = system_clock::now();
    const auto nowMs   = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;
    const std::time_t tt = system_clock::to_time_t(now);

    std::tm localTime{};
#if defined(_WIN32)
    localtime_s(&localTime, &tt);
#else
    localtime_r(&tt, &localTime);
#endif

    std::snprintf(buf, bufSize,
                  "%04d-%02d-%02d %02d:%02d:%02d.%03lld",
                  localTime.tm_year + 1900,
                  localTime.tm_mon  + 1,
                  localTime.tm_mday,
                  localTime.tm_hour,
                  localTime.tm_min,
                  localTime.tm_sec,
                  static_cast<long long>(nowMs.count()));
}

// ---------------------------------------------------------------------------
// Thread-id helper
// ---------------------------------------------------------------------------

/**
 * @brief Returns a stable numeric identifier for the calling thread.
 *
 * std::hash<std::thread::id>{} is used to get a numeric value from the
 * platform thread-id; we truncate to 32-bit for brevity.
 */
unsigned int CurrentThreadId() noexcept {
    return static_cast<unsigned int>(
        std::hash<std::thread::id>{}(std::this_thread::get_id()));
}

} // namespace

// ---------------------------------------------------------------------------
// Log public API
// ---------------------------------------------------------------------------

void Log::Init(std::string_view logFilePath, LogLevel minLevel) {
    std::lock_guard<std::mutex> lock(g_logMutex);

    g_minLevel.store(static_cast<int>(minLevel), std::memory_order_relaxed);

    if (!logFilePath.empty()) {
        g_logFile.open(std::string(logFilePath),
                       std::ios::out | std::ios::app);
        if (!g_logFile.is_open()) {
            // Non-fatal: continue with console-only logging.
            std::fprintf(stderr,
                         "[Log::Init] WARNING: could not open log file '%.*s'\n",
                         static_cast<int>(logFilePath.size()), logFilePath.data());
        }
    }
}

void Log::Shutdown() {
    std::lock_guard<std::mutex> lock(g_logMutex);
    if (g_logFile.is_open()) {
        g_logFile.flush();
        g_logFile.close();
    }
}

void Log::Write(LogLevel level, std::string_view subsystem,
                std::string_view message) noexcept {
    // Level filter (atomic read, no lock needed here).
    if (static_cast<int>(level) < g_minLevel.load(std::memory_order_relaxed)) {
        return;
    }

    // Build the log line in a stack buffer when possible.
    char stackBuf[kStackBufSize];
    char timestamp[24]; // "YYYY-MM-DD HH:MM:SS.mmm\0"
    FillTimestamp(timestamp, sizeof(timestamp));

    const unsigned int tid = CurrentThreadId();

    // Attempt to format into the stack buffer.
    int needed = std::snprintf(
        stackBuf, sizeof(stackBuf),
        "[%s][T:%08X][%s][%.*s] %.*s\n",
        timestamp,
        tid,
        LogLevelName(level).data(),
        static_cast<int>(subsystem.size()), subsystem.data(),
        static_cast<int>(message.size()),   message.data());

    const char* line    = stackBuf;
    std::string dynLine; // used only if the stack buffer was too small

    if (needed < 0 || static_cast<std::size_t>(needed) >= sizeof(stackBuf)) {
        // Fallback: heap allocation for oversized messages.
        const std::size_t dynSize =
            (needed > 0) ? static_cast<std::size_t>(needed) + 1u : 2048u;
        dynLine.resize(dynSize);
        std::snprintf(dynLine.data(), dynSize,
                      "[%s][T:%08X][%s][%.*s] %.*s\n",
                      timestamp, tid,
                      LogLevelName(level).data(),
                      static_cast<int>(subsystem.size()), subsystem.data(),
                      static_cast<int>(message.size()),   message.data());
        line = dynLine.c_str();
    }

    // Serialise the write.
    std::lock_guard<std::mutex> lock(g_logMutex);

    // Console output.
    std::fputs(line, stderr);

    // File output.
    if (g_logFile.is_open()) {
        g_logFile << line;
#if defined(_DEBUG) || !defined(NDEBUG)
        g_logFile.flush(); // Ensure flush in debug builds.
#endif
    }
}

void Log::SetLevel(LogLevel level) noexcept {
    g_minLevel.store(static_cast<int>(level), std::memory_order_relaxed);
}

LogLevel Log::GetLevel() noexcept {
    return static_cast<LogLevel>(g_minLevel.load(std::memory_order_relaxed));
}

} // namespace engine::core
