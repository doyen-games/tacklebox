#include <dwarfkit/resources/resources.hpp>

#include <cstdlib>

namespace dwarfkit {

namespace resources_detail {

namespace {

Result<Int128> pow10_128(int exponent) {
    Int128 rv(int64_t(1));
    const Int128 ten(int64_t(10));
    for (int i = 0; i < exponent; i++) {
        rv = rv.multiplying(ten);
    }
    return rv;
}

// checked positive multiply; errors when the product would exceed Int128
bool mulWouldOverflow(const Int128& a, const Int128& b) {
    if (a == Int128::zero() || b == Int128::zero()) {
        return false;
    }
    const Int128 product = a.multiplying(b);
    const auto quotient = product.dividing(b);
    return !quotient || !(quotient->equals(a)) || product.isNegative();
}

// round-half-up division of nonnegative values
Result<Int128> divRoundHalfUp(const Int128& a, const Int128& b) {
    const UInt128 num{a.lo, a.hi};
    const UInt128 den{b.lo, b.hi};
    DK_TRY(quotient, num.dividing(den));
    DK_TRY(rem, num.remainder(den));
    const UInt128 doubled{rem.lo << 1, (rem.hi << 1) | (rem.lo >> 63)};
    UInt128 rounded = quotient;
    if (doubled >= den) {
        rounded = rounded.adding(UInt128(uint64_t(1)));
    }
    return Int128{rounded.lo, rounded.hi};
}

// decimal string "intpart.fracpart" of scaled / 10^precision, then strtod
double scaledToDouble(const Int128& scaled, int precision) {
    const Int128 scale = pow10_128(precision).value_or(Int128(int64_t(1)));
    const UInt128 value{scaled.lo, scaled.hi};
    const UInt128 den{scale.lo, scale.hi};
    const UInt128 intPart = value.dividing(den).value_or(UInt128());
    const UInt128 fracPart = value.remainder(den).value_or(UInt128());
    std::string frac = fracPart.toString();
    frac.insert(frac.begin(), static_cast<size_t>(precision) - frac.size(), '0');
    const std::string text = intPart.toString() + "." + frac;
    return std::strtod(text.c_str(), nullptr);
}

}  // namespace

Result<Int128> decimalDivideScaled(const Int128& a, const Int128& b, int precision) {
    DK_TRY(scale, pow10_128(precision));
    if (mulWouldOverflow(a, scale)) {
        return err(ErrorKind::Invalid, "decimal division overflow");
    }
    return divRoundHalfUp(a.multiplying(scale), b);
}

double decimalDivide(const Int128& a, const Int128& b, int precision) {
    const auto scaled = decimalDivideScaled(a, b, precision);
    if (!scaled) {
        // out of 128-bit range: plain double division is the best available
        return a.toDouble() / b.toDouble();
    }
    return scaledToDouble(*scaled, precision);
}

double decimalDivideDouble(double numerator, const Int128& den, int precision) {
    // js: intToBigDecimal(String(value)).divide(den, precision) then Number()
    const std::string text = detail::jsNumberToString(numerator);
    if (text.find('e') != std::string::npos || text.find('E') != std::string::npos ||
        text.find('-') != std::string::npos) {
        return numerator / den.toDouble();
    }
    // split mantissa and count fraction digits
    std::string digits;
    int fractionDigits = 0;
    bool seenDot = false;
    for (const char c : text) {
        if (c == '.') {
            seenDot = true;
            continue;
        }
        digits += c;
        if (seenDot) {
            fractionDigits++;
        }
    }
    const auto mantissa = Int128::from(std::string_view(digits));
    if (!mantissa || fractionDigits > precision) {
        return numerator / den.toDouble();
    }
    const auto scale = pow10_128(precision - fractionDigits);
    if (!scale || mulWouldOverflow(*mantissa, *scale)) {
        return numerator / den.toDouble();
    }
    const auto scaled = divRoundHalfUp(mantissa->multiplying(*scale), den);
    if (!scaled) {
        return numerator / den.toDouble();
    }
    return scaledToDouble(*scaled, precision);
}

}  // namespace resources_detail

Result<Resources> Resources::make(const ResourcesOptions& options) {
    Resources rv;
    if (options.sampleAccount) {
        rv.sampleAccount = *options.sampleAccount;
    }
    if (options.symbol) {
        rv.symbol = *options.symbol;
    }
    if (options.api) {
        rv.api = options.api;
    } else if (!options.url.empty()) {
        rv.api = std::make_shared<APIClient>(
            APIClientOptions{.url = options.url, .fetch = options.fetch});
    } else {
        return err(ErrorKind::Invalid, "Missing url or api client");
    }
    return rv;
}

namespace {

// the upstream divCeil helper, quirks included: truncating division, then
// minus one when there is a remainder and the quotient exceeds one
UInt128 sampleDivCeil(const UInt128& num, const UInt128& den) {
    UInt128 quotient{}, one(uint64_t(1));
    const auto division = num.dividing(den);
    if (division) {
        quotient = *division;
    }
    const auto rem = num.remainder(den);
    if (rem && !(*rem == UInt128::zero()) && quotient > one) {
        quotient = quotient.subtracting(one);
    }
    return quotient;
}

}  // namespace

Result<SampleUsage> Resources::getSampledUsage() const {
    DK_TRY(account, api->v1.chain.get_account(Name::from(sampleAccount)));
    const UInt128 us =
        UInt128(static_cast<uint64_t>(account.cpu_limit.max)).multiplying(UInt128(BNPrecision));
    const UInt128 byte =
        UInt128(static_cast<uint64_t>(account.net_limit.max)).multiplying(UInt128(BNPrecision));
    const UInt128 cpuWeight(static_cast<uint64_t>(account.cpu_weight));
    const UInt128 netWeight(static_cast<uint64_t>(account.net_weight));
    SampleUsage rv{std::move(account), sampleDivCeil(us, cpuWeight),
                   sampleDivCeil(byte, netWeight)};
    return rv;
}

}  // namespace dwarfkit
