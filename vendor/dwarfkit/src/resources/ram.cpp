#include <dwarfkit/resources/ram.hpp>

namespace dwarfkit {

Result<Asset> RAMState::price_per(double bytes) const {
    const int64_t baseUnits = base.balance.units;
    const int64_t quoteUnits = quote.balance.units;
    DK_TRY(units, get_input(baseUnits, quoteUnits, static_cast<int64_t>(bytes)));
    return Asset::fromUnits(units, quote.balance.symbol);
}

Result<int64_t> RAMState::get_input(int64_t baseUnits, int64_t quoteUnits, int64_t value) const {
    // (quote * value) / (base - value), using 'ceil' to round up
    const Int128 numerator = Int128(quoteUnits).multiplying(Int128(value));
    DK_TRY(quotient,
           numerator.dividing(Int128(baseUnits - value), DivisionBehavior::ceil));
    return quotient.toInt64();
}

Result<RAMState> RAMAPI::get_state() const {
    DK_TRY(response, parent_.api->v1.chain.get_table_rows<RAMState>(
                         json{{"code", "eosio"}, {"scope", "eosio"}, {"table", "rammarket"}}));
    if (response.rows.empty()) {
        return err(ErrorKind::NotFound, "No rammarket state found");
    }
    return response.rows[0];
}

}  // namespace dwarfkit
