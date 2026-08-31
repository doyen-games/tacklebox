#include <dwarfkit/resources/rex.hpp>

#include <cmath>

namespace dwarfkit {

double REXState::value() const {
    const Int128 lentPlusUnlent =
        Int128(total_lent.units).adding(Int128(total_unlent.units));
    return resources_detail::decimalDivide(lentPlusUnlent, Int128(total_rex.units), 18);
}

Asset REXState::exchange(const Asset& amount) const {
    const Int128 numerator = Int128(amount.units).multiplying(Int128(total_lendable.units));
    const double tokens =
        resources_detail::decimalDivide(numerator, Int128(total_rex.units), precision());
    // Asset.fromUnits(Number(...)) truncates the fraction through BN
    return Asset::fromUnits(static_cast<int64_t>(tokens), symbol());
}

double REXState::price_per(const SampleUsage& sample, double unit,
                           std::optional<UInt128> usage) const {
    const UInt128 usageValue = usage ? *usage : sample.cpu;

    // Sample token units
    const double tokensUnits = 10000;

    // Spending 1 EOS (10000 units) on REX gives this many tokens
    const double bancor = tokensUnits / (total_rent.value() / total_unlent.value());

    // The ratio of the number of tokens received vs the sampled values
    const double unitPrice = bancor * (usageValue.toDouble() / static_cast<double>(BNPrecision));

    // The token units spent per unit
    const double perunit = tokensUnits / unitPrice;

    // Multiply the per unit cost by the units requested
    const double cost = perunit * unit;

    // Converting to an Asset
    return cost / std::pow(10.0, precision());
}

Result<REXState> REXAPI::get_state() const {
    DK_TRY(response, parent_.api->v1.chain.get_table_rows<REXState>(
                         json{{"code", "eosio"}, {"scope", "eosio"}, {"table", "rexpool"}}));
    if (response.rows.empty()) {
        return err(ErrorKind::NotFound, "No rexpool state found");
    }
    return response.rows[0];
}

}  // namespace dwarfkit
