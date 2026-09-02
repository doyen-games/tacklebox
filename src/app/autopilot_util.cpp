#include "app/autopilot_util.hpp"

#include <cmath>
#include <cstdio>
#include <ctime>

#include "core/util.hpp"
#include "guard/rules.hpp"

namespace tb::autopilot {

using dwarfkit::ErrorKind;

dwarfkit::Result<std::string> computePercentAmount(const std::string& balance, double percent,
                                                   const std::string& reserve) {
    auto bal = guard::parseAsset(balance);
    if (!bal) return dwarfkit::err(ErrorKind::Invalid, "balance did not parse: " + balance);
    if (!(percent > 0.0) || percent > 100.0)
        return dwarfkit::err(ErrorKind::Invalid, "percent must be in (0, 100]");

    int64_t available = bal->amount;
    if (!reserve.empty()) {
        auto res = guard::parseAsset(reserve);
        if (!res)
            return dwarfkit::err(ErrorKind::Invalid, "reserve did not parse: " + reserve);
        if (res->code != bal->code || res->precision != bal->precision)
            return dwarfkit::err(ErrorKind::Invalid,
                                 "reserve symbol/precision must match the balance (" +
                                     reserve + " vs " + balance + ")");
        available -= res->amount;
    }
    if (available <= 0)
        return dwarfkit::err(ErrorKind::Invalid,
                             "nothing available after the reserve (balance " + balance + ")");

    // Basis points, exact integer math, floored twice (never rounds up).
    int64_t bp = static_cast<int64_t>(std::floor(percent * 100.0 + 0.5));
    if (bp < 1) bp = 1;
    if (bp > 10000) bp = 10000;
    int64_t units = (available / 10000) * bp + ((available % 10000) * bp) / 10000;
    if (units <= 0)
        return dwarfkit::err(ErrorKind::Invalid, "computed amount is zero at this balance");
    if (units > available) units = available;  // paranoia; cannot happen with floor math

    // Format with the balance's precision.
    char buf[64];
    if (bal->precision == 0) {
        std::snprintf(buf, sizeof buf, "%lld %s", static_cast<long long>(units),
                      bal->code.c_str());
    } else {
        int64_t scale = 1;
        for (uint8_t i = 0; i < bal->precision; ++i) scale *= 10;
        std::snprintf(buf, sizeof buf, "%lld.%0*lld %s", static_cast<long long>(units / scale),
                      static_cast<int>(bal->precision),
                      static_cast<long long>(units % scale), bal->code.c_str());
    }
    return std::string(buf);
}

namespace {

std::string substitute(const std::string& text, const TemplateContext& context) {
    std::string out;
    out.reserve(text.size());
    size_t i = 0;
    while (i < text.size()) {
        if (text[i] == '{') {
            size_t close = text.find('}', i);
            if (close != std::string::npos) {
                std::string key = text.substr(i + 1, close - i - 1);
                bool known = true;
                if (key == "actor") out += context.actor;
                else if (key == "amount") out += context.amount;
                else if (key == "balance") out += context.balance;
                else if (key == "date") out += formatIsoUtc(context.unixSec).substr(0, 10);
                else if (key == "time") out += formatIsoUtc(context.unixSec).substr(11, 8);
                else known = false;
                if (known) {
                    i = close + 1;
                    continue;
                }
            }
        }
        out += text[i++];
    }
    return out;
}

}  // namespace

dwarfkit::json applyTemplates(dwarfkit::json data, const TemplateContext& context) {
    if (data.is_string()) return substitute(data.get<std::string>(), context);
    if (data.is_object())
        for (auto it = data.begin(); it != data.end(); ++it)
            it.value() = applyTemplates(it.value(), context);
    if (data.is_array())
        for (auto& element : data) element = applyTemplates(element, context);
    return data;
}

// --- schedule timing ---------------------------------------------------------

namespace {

std::tm toLocal(int64_t unixSec) {
    time_t t = static_cast<time_t>(unixSec);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    return tm;
}

}  // namespace

dwarfkit::Result<int64_t> parseDateTimeLocal(const std::string& text) {
    int y = 0, mo = 0, d = 0, h = 0, mi = 0, s = 0;
    int n = std::sscanf(trim(text).c_str(), "%d-%d-%d %d:%d:%d", &y, &mo, &d, &h, &mi, &s);
    if (n < 5)
        return dwarfkit::err(ErrorKind::Invalid,
                             "expected YYYY-MM-DD HH:MM[:SS], got '" + trim(text) + "'");
    if (y < 1970 || y > 3000 || mo < 1 || mo > 12 || d < 1 || d > 31 || h < 0 || h > 23 ||
        mi < 0 || mi > 59 || s < 0 || s > 59)
        return dwarfkit::err(ErrorKind::Invalid, "date/time out of range");
    std::tm tm{};
    tm.tm_year = y - 1900;
    tm.tm_mon = mo - 1;
    tm.tm_mday = d;
    tm.tm_hour = h;
    tm.tm_min = mi;
    tm.tm_sec = s;
    tm.tm_isdst = -1;  // let the zone decide
    time_t t = std::mktime(&tm);
    if (t == time_t(-1))
        return dwarfkit::err(ErrorKind::Invalid, "date/time did not resolve");
    return static_cast<int64_t>(t);
}

dwarfkit::Result<int> parseTimeOfDay(const std::string& text) {
    int h = 0, mi = 0, s = 0;
    int n = std::sscanf(trim(text).c_str(), "%d:%d:%d", &h, &mi, &s);
    if (n < 2)
        return dwarfkit::err(ErrorKind::Invalid,
                             "expected HH:MM[:SS], got '" + trim(text) + "'");
    if (h < 0 || h > 23 || mi < 0 || mi > 59 || s < 0 || s > 59)
        return dwarfkit::err(ErrorKind::Invalid, "time of day out of range");
    return h * 3600 + mi * 60 + s;
}

std::string formatDateTimeLocal(int64_t unixSec) {
    if (unixSec <= 0) return "";
    std::tm tm = toLocal(unixSec);
    char buf[24];
    std::snprintf(buf, sizeof buf, "%04d-%02d-%02d %02d:%02d:%02d", tm.tm_year + 1900,
                  tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
    return buf;
}

std::string formatTimeOfDay(int secOfDay) {
    if (secOfDay < 0) secOfDay = 0;
    secOfDay %= 86400;
    char buf[12];
    std::snprintf(buf, sizeof buf, "%02d:%02d:%02d", secOfDay / 3600,
                  (secOfDay % 3600) / 60, secOfDay % 60);
    return buf;
}

int64_t computeNextRun(const Schedule& schedule, int64_t now) {
    const int64_t interval = schedule.intervalSec < 60 ? 60 : schedule.intervalSec;
    int64_t floor = now + 1;
    if (schedule.startAt > 0 && schedule.startAt > floor) floor = schedule.startAt;

    int64_t next = 0;
    switch (schedule.timingMode) {
        case Schedule::TimeAnchored: {
            // Fixed grid: startAt, startAt+N, startAt+2N... independent of
            // when runs actually happened, so it never drifts.
            int64_t anchor = schedule.startAt > 0
                                 ? schedule.startAt
                                 : (schedule.lastRunAt > 0 ? schedule.lastRunAt : now);
            if (anchor >= floor) {
                next = anchor;
            } else {
                int64_t steps = (floor - anchor + interval - 1) / interval;
                next = anchor + steps * interval;
            }
            break;
        }
        case Schedule::TimeDaily: {
            // The next local occurrence of dailySec; mktime renormalizes the
            // day step so DST transitions keep the wall-clock time.
            int daily = schedule.dailySec >= 0 && schedule.dailySec < 86400
                            ? schedule.dailySec
                            : 0;
            std::tm tm = toLocal(floor);
            tm.tm_hour = daily / 3600;
            tm.tm_min = (daily % 3600) / 60;
            tm.tm_sec = daily % 60;
            tm.tm_isdst = -1;
            time_t cand = std::mktime(&tm);
            int guard = 0;
            while (static_cast<int64_t>(cand) < floor && guard++ < 4) {
                tm.tm_mday += 1;
                tm.tm_isdst = -1;
                cand = std::mktime(&tm);
            }
            next = static_cast<int64_t>(cand);
            break;
        }
        default: {  // TimeRelative
            int64_t base = schedule.lastRunAt > 0 ? schedule.lastRunAt : now;
            next = base + interval;
            if (next < floor) next = floor;  // long overdue: due immediately
            break;
        }
    }
    if (schedule.endAt > 0 && next > schedule.endAt) return 0;
    return next;
}

}  // namespace tb::autopilot
