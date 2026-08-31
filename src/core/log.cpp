#include "core/log.hpp"

#include <cstdio>

#include "core/util.hpp"

namespace tb {

std::mutex Log::mutex_;
std::deque<LogEntry> Log::ring_;

static constexpr size_t kRingMax = 2000;

void Log::write(LogLevel level, const char* fmt, va_list args) {
    char buf[2048];
    std::vsnprintf(buf, sizeof buf, fmt, args);

    static const char* names[] = {"debug", "info ", "warn ", "error"};
    std::fprintf(stderr, "[tacklebox %s] %s\n", names[static_cast<int>(level)], buf);

    std::lock_guard<std::mutex> lock(mutex_);
    ring_.push_back({nowMs(), level, buf});
    while (ring_.size() > kRingMax) ring_.pop_front();
}

#define TB_LOG_IMPL(level)              \
    va_list args;                       \
    va_start(args, fmt);                \
    write(level, fmt, args);            \
    va_end(args)

void Log::debug(const char* fmt, ...) { TB_LOG_IMPL(LogLevel::Debug); }
void Log::info(const char* fmt, ...) { TB_LOG_IMPL(LogLevel::Info); }
void Log::warn(const char* fmt, ...) { TB_LOG_IMPL(LogLevel::Warn); }
void Log::error(const char* fmt, ...) { TB_LOG_IMPL(LogLevel::Error); }

std::vector<LogEntry> Log::tail(size_t count) {
    std::lock_guard<std::mutex> lock(mutex_);
    size_t n = ring_.size() < count ? ring_.size() : count;
    return std::vector<LogEntry>(ring_.end() - static_cast<ptrdiff_t>(n), ring_.end());
}

}  // namespace tb
