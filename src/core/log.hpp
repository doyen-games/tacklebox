// Leveled logger. Writes to stderr and keeps a bounded in-memory ring that the
// UI's diagnostics panel can render. Never log secrets: keys, passwords, and
// seed material must not pass through here.
#pragma once

#include <cstdarg>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

namespace tb {

enum class LogLevel { Debug, Info, Warn, Error };

struct LogEntry {
    int64_t timeMs;
    LogLevel level;
    std::string message;
};

class Log {
public:
    static void debug(const char* fmt, ...);
    static void info(const char* fmt, ...);
    static void warn(const char* fmt, ...);
    static void error(const char* fmt, ...);

    // Snapshot of the newest entries for the diagnostics view.
    static std::vector<LogEntry> tail(size_t count = 200);

private:
    static void write(LogLevel level, const char* fmt, va_list args);

    static std::mutex mutex_;
    static std::deque<LogEntry> ring_;
};

}  // namespace tb
