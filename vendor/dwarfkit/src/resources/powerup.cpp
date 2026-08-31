#include <dwarfkit/resources/powerup.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>

namespace dwarfkit {

using resources_detail::decimalDivide;
using resources_detail::decimalDivideDouble;
using resources_detail::decimalDivideScaled;

// ---- shared base ------------------------------------------------------------

Result<UInt128> PowerUpStateResource::utilization_increase(const UInt128& frac) const {
    // ceil((frac * weight) / 10^15), all exact decimal steps upstream
    const Int128 product =
        Int128{frac.lo, frac.hi}.multiplying(Int128(weight));
    Int128 multiplier(int64_t(1));
    for (int i = 0; i < 15; i++) {
        multiplier = multiplier.multiplying(Int128(int64_t(10)));
    }
    DK_TRY(quotient, product.dividing(multiplier, DivisionBehavior::ceil));
    return UInt128{quotient.lo, quotient.hi};
}

double PowerUpStateResource::price_function(int64_t atUtilization) const {
    double price = min_price.value();
    const double newExponent = exponent - 1.0;
    if (newExponent <= 0.0) {
        return max_price.value();
    }
    const double utilWeight = decimalDivide(Int128(atUtilization), Int128(weight), 18);
    const double difference = max_price.value() - min_price.value();
    price += difference * std::pow(utilWeight, newExponent);
    return price;
}

double PowerUpStateResource::price_integral_delta(int64_t startUtilization,
                                                  int64_t endUtilization) const {
    const Asset difference = Asset::fromUnits(max_price.units - min_price.units, symbol());
    const double coefficient = difference.value() / exponent;
    const double startU = decimalDivide(Int128(startUtilization), Int128(weight), 18);
    const double endU = decimalDivide(Int128(endUtilization), Int128(weight), 18);
    return min_price.value() * endU - min_price.value() * startU +
           coefficient * std::pow(endU, exponent) - coefficient * std::pow(startU, exponent);
}

double PowerUpStateResource::fee(const UInt128& utilizationIncrease,
                                 int64_t adjustedUtilization) const {
    int64_t startUtilization = utilization;
    const int64_t endUtilization =
        startUtilization + static_cast<int64_t>(utilizationIncrease.toUInt64());

    double feeValue = 0;
    if (startUtilization < adjustedUtilization) {
        const double minValue =
            std::min(utilizationIncrease.toDouble(),
                     static_cast<double>(adjustedUtilization - startUtilization));
        feeValue += decimalDivideDouble(price_function(adjustedUtilization) * minValue,
                                        Int128(weight), 18);
        startUtilization = adjustedUtilization;
    }
    if (startUtilization < endUtilization) {
        feeValue += price_integral_delta(startUtilization, endUtilization);
    }
    return feeValue;
}

int64_t PowerUpStateResource::determine_adjusted_utilization(
    const PowerUpStateOptions& options) const {
    int64_t adjusted = adjusted_utilization;
    if (utilization < adjusted) {
        double nowSeconds;
        if (options.timestamp) {
            nowSeconds = static_cast<double>(options.timestamp->toMilliseconds()) / 1000;
        } else {
            nowSeconds = static_cast<double>(
                             std::chrono::duration_cast<std::chrono::milliseconds>(
                                 std::chrono::system_clock::now().time_since_epoch())
                                 .count()) /
                         1000;
        }
        const double utilizationTs =
            static_cast<double>(utilization_timestamp.toMilliseconds()) / 1000;
        const double diff = static_cast<double>(adjusted - utilization);
        double delta = diff * std::exp(-(nowSeconds - utilizationTs) /
                                       static_cast<double>(decay_secs));
        delta = std::min(std::max(delta, 0.0), diff);
        // Int64.adding(Int.from(delta)) truncates the double through BN
        adjusted = utilization + static_cast<int64_t>(delta);
    }
    return adjusted;
}

// ---- shared cpu/net implementation helpers ---------------------------------

namespace {

Result<UInt128> weightToUnits(const UInt128& sample, int64_t weightValue) {
    // UInt128(weight).multiplying(sample).dividing(BNPrecision, 'ceil')
    const UInt128 product = UInt128(static_cast<uint64_t>(weightValue)).multiplying(sample);
    return product.dividing(UInt128(BNPrecision), DivisionBehavior::ceil);
}

Result<int64_t> unitsToWeight(const UInt128& sample, int64_t units) {
    // Int64(units).multiplying(BNPrecision).dividing(sample, 'floor')
    const Int128 product = Int128(units).multiplying(Int128(int64_t(BNPrecision)));
    DK_TRY(quotient, product.dividing(Int128{sample.lo, sample.hi}, DivisionBehavior::floor));
    return quotient.toInt64();
}

Result<int64_t> fracByUnits(const UInt128& sample, int64_t units, int64_t weight) {
    // bigDecimal: (us_to_weight / weight to 15 digits) * 10^15 -> Int64
    DK_TRY(converted, unitsToWeight(sample, units));
    DK_TRY(scaled, decimalDivideScaled(Int128(converted), Int128(weight), 15));
    return scaled.toInt64();
}

struct PricePolicy {
    const char* unitName;  // "CPU amount (Nus)" / "NET amount (Nbytes)"
    const char* unitSuffix;
};

Result<double> pricePer(const PowerUpStateResource& resource, const UInt128& sample,
                        int64_t weight, int64_t units, const PowerUpStateOptions& options,
                        const PricePolicy& policy) {
    // Determine the utilization increase by this action
    DK_TRY(fracValue, fracByUnits(sample, units, weight));
    DK_TRY(increase, resource.utilization_increase(UInt128(static_cast<uint64_t>(fracValue))));
    // Determine the adjusted utilization if needed
    const int64_t adjusted = resource.determine_adjusted_utilization(options);
    // Derive the fee from the increase and utilization
    const double feeValue = resource.fee(increase, adjusted);
    // Force the fee up to the next highest value of precision
    const double precisionPow = std::pow(10.0, resource.max_price.symbol.precision());
    const double price = feeValue * precisionPow;
    const double value = std::ceil(price) / precisionPow;
    DK_TRY(asset, Asset::fromFloat(feeValue, resource.symbol()));
    if (price < 1) {
        return err(ErrorKind::Invalid,
                   "Price (" + asset.toString() + ") for requested " + policy.unitName + " (" +
                       std::to_string(units) + policy.unitSuffix +
                       ") below required precision, increase requested amount.");
    }
    if (options.min_payment && options.min_payment->units > asset.units) {
        return err(ErrorKind::Invalid,
                   "Price (" + asset.toString() + ") for requested " + policy.unitName + " (" +
                       std::to_string(units) + policy.unitSuffix +
                       ") below minimum required payment (" + options.min_payment->toString() +
                       "), increase requested amount.");
    }
    // Return the modified fee
    return value;
}

}  // namespace

// ---- cpu -------------------------------------------------------------------

Result<UInt128> PowerUpStateResourceCPU::weight_to_us(const UInt128& sample,
                                                      int64_t weightValue) const {
    return weightToUnits(sample, weightValue);
}

Result<int64_t> PowerUpStateResourceCPU::us_to_weight(const UInt128& sample, int64_t us) const {
    return unitsToWeight(sample, us);
}

Result<int64_t> PowerUpStateResourceCPU::frac_by_us(const SampleUsage& usage, int64_t us) const {
    return fracByUnits(usage.cpu, us, weight);
}

Result<double> PowerUpStateResourceCPU::price_per_us(const SampleUsage& usage, double us,
                                                     const PowerUpStateOptions& options) const {
    return pricePer(*this, usage.cpu, weight, static_cast<int64_t>(us), options,
                    {"CPU amount", "us"});
}

// ---- net -------------------------------------------------------------------

Result<UInt128> PowerUpStateResourceNET::weight_to_bytes(const UInt128& sample,
                                                         int64_t weightValue) const {
    return weightToUnits(sample, weightValue);
}

Result<int64_t> PowerUpStateResourceNET::bytes_to_weight(const UInt128& sample,
                                                         int64_t bytes) const {
    return unitsToWeight(sample, bytes);
}

Result<int64_t> PowerUpStateResourceNET::frac_by_bytes(const SampleUsage& usage,
                                                       int64_t bytes) const {
    return fracByUnits(usage.net, bytes, weight);
}

Result<double> PowerUpStateResourceNET::price_per_byte(const SampleUsage& usage, double bytes,
                                                       const PowerUpStateOptions& options) const {
    // unlike the CPU variant, upstream NET pricing has no minimum precision
    // or min_payment checks
    DK_TRY(fracValue, frac_by_bytes(usage, static_cast<int64_t>(bytes)));
    DK_TRY(increase, utilization_increase(UInt128(static_cast<uint64_t>(fracValue))));
    const int64_t adjusted = determine_adjusted_utilization(options);
    const double feeValue = fee(increase, adjusted);
    const double precisionPow = std::pow(10.0, max_price.symbol.precision());
    return std::ceil(feeValue * precisionPow) / precisionPow;
}

// ---- api -------------------------------------------------------------------

Result<PowerUpState> PowerUpAPI::get_state() const {
    DK_TRY(response, parent_.api->v1.chain.get_table_rows<PowerUpState>(
                         json{{"code", "eosio"}, {"scope", ""}, {"table", "powup.state"}}));
    if (response.rows.empty()) {
        return err(ErrorKind::NotFound, "No powerup state found");
    }
    return response.rows[0];
}

}  // namespace dwarfkit
