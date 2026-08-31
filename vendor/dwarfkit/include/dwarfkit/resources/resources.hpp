// Port of wharfkit/resources src/index.ts. BN + js-big-decimal arithmetic
// maps to 128-bit integers plus exact decimal division helpers (see
// DIVERGENCES.md); v1 becomes an accessor returning the API view.
#pragma once

#include <memory>
#include <optional>
#include <string>

#include <dwarfkit/antelope.hpp>
#include <dwarfkit/transport/fetch_provider.hpp>

namespace dwarfkit {

class Resources;

// 100 * 1000 * 1000, the sample precision multiplier
inline constexpr uint64_t BNPrecision = 100000000ull;

struct SampleUsage {
    api::v1::AccountObject account;
    UInt128 cpu;
    UInt128 net;
};

namespace resources_detail {

// js-big-decimal divide(a, b, precision) then Number(...): decimal division
// to `precision` fraction digits with round half up, parsed back to a double.
double decimalDivide(const Int128& a, const Int128& b, int precision);
// the scaled integer round-half-up(a * 10^precision / b)
Result<Int128> decimalDivideScaled(const Int128& a, const Int128& b, int precision);
// bigDecimal(String(numerator)).divide(den, precision): a double numerator
// goes through its shortest decimal representation first
double decimalDivideDouble(double numerator, const Int128& den, int precision);

}  // namespace resources_detail

struct ResourcesOptions {
    std::shared_ptr<APIClient> api;
    std::string url;
    std::shared_ptr<FetchProvider> fetch;
    std::optional<std::string> sampleAccount;
    std::optional<std::string> symbol;
};

class RAMAPI;
class REXAPI;
class PowerUpAPI;

class Resources {
public:
    // Errors when neither api nor url is provided (the TS constructor throw).
    static Result<Resources> make(const ResourcesOptions& options);

    std::shared_ptr<APIClient> api;
    // the account to use when sampling usage
    std::string sampleAccount = "greymassfuel";
    // token precision/symbol
    std::string symbol = "4,EOS";

    // v1.powerup / v1.ram / v1.rex, as an accessor view (C++ members cannot
    // safely back-reference a movable parent)
    struct V1View;
    V1View v1() const;

    Result<SampleUsage> getSampledUsage() const;
};

}  // namespace dwarfkit
