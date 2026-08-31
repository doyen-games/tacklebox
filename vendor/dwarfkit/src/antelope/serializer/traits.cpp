#include <dwarfkit/antelope/serializer/traits.hpp>

#include <charconv>
#include <cmath>
#include <cstdio>

namespace dwarfkit::detail {

std::string jsNumberToString(double v) {
    if (std::isnan(v)) return "NaN";
    if (std::isinf(v)) return v > 0 ? "Infinity" : "-Infinity";
    char buffer[32];
    const auto [end, ec] = std::to_chars(buffer, buffer + sizeof(buffer), v);
    std::string rv(buffer, end);
    // JS prints exponents without zero padding: 1e-07 -> 1e-7
    if (const size_t e = rv.find('e'); e != std::string::npos && e + 2 < rv.size()) {
        size_t digits = e + 1;
        if (rv[digits] == '+' || rv[digits] == '-') digits++;
        size_t firstNonZero = digits;
        while (firstNonZero < rv.size() - 1 && rv[firstNonZero] == '0') firstNonZero++;
        rv.erase(digits, firstNonZero - digits);
    }
    return rv;
}

std::string toFixed7(double v) {
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.7f", v);
    return buffer;
}

}  // namespace dwarfkit::detail
