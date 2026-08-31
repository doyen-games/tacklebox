// Port of resources src/ram.ts.
#pragma once

#include <dwarfkit/resources/resources.hpp>

namespace dwarfkit {

struct Connector {
    DK_STRUCT("connector")
    Asset balance;
    double weight = 0;
    DK_FIELDS(balance, weight)
};

struct ExchangeState {
    DK_STRUCT("exchange_state")
    Asset supply;
    Connector base;
    Connector quote;
    DK_FIELDS(supply, base, quote)
};

struct RAMState : ExchangeState {
    DK_STRUCT_BASE("ramstate", ExchangeState)

    Result<Asset> price_per(double bytes) const;
    Result<Asset> price_per_kb(double kilobytes) const { return price_per(kilobytes * 1000); }

    // (quote * value) / (base - value), using 'ceil' to round up. Derived from
    // eosio.system exchange_state.cpp get_input.
    Result<int64_t> get_input(int64_t base, int64_t quote, int64_t value) const;
};

class RAMAPI {
public:
    explicit RAMAPI(const Resources& parent) : parent_(parent) {}
    Result<RAMState> get_state() const;

private:
    const Resources& parent_;
};

}  // namespace dwarfkit
