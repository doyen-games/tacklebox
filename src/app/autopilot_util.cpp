#include "app/autopilot_util.hpp"

#include <cmath>
#include <cstdio>

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

}  // namespace tb::autopilot
