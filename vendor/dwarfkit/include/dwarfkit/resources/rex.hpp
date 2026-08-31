// Port of resources src/rex.ts.
#pragma once

#include <dwarfkit/resources/resources.hpp>

namespace dwarfkit {

struct REXState {
    DK_STRUCT("rexstate")
    uint8_t version = 0;
    Asset total_lent;
    Asset total_unlent;
    Asset total_rent;
    Asset total_lendable;
    Asset total_rex;
    Asset namebid_proceeds;
    uint64_t loan_num = 0;
    DK_FIELDS(version, total_lent, total_unlent, total_rent, total_lendable, total_rex,
              namebid_proceeds, loan_num)

    double reserved() const {
        return static_cast<double>(total_lent.units) / static_cast<double>(total_lendable.units);
    }
    Asset::Symbol symbol() const { return total_lent.symbol; }
    int precision() const { return total_lent.symbol.precision(); }
    // (lent + unlent) / rex to 18 decimal digits
    double value() const;

    Asset exchange(const Asset& amount) const;

    double cpu_price_per_ms(const SampleUsage& sample, double ms = 1) const {
        return cpu_price_per_us(sample, ms * 1000);
    }
    double cpu_price_per_us(const SampleUsage& sample, double us = 1000) const {
        return price_per(sample, us, sample.cpu);
    }
    double net_price_per_kb(const SampleUsage& sample, double kilobytes = 1) const {
        return net_price_per_byte(sample, kilobytes * 1000);
    }
    double net_price_per_byte(const SampleUsage& sample, double bytes = 1000) const {
        return price_per(sample, bytes, sample.net);
    }
    double price_per(const SampleUsage& sample, double unit = 1000,
                     std::optional<UInt128> usage = std::nullopt) const;
};

class REXAPI {
public:
    explicit REXAPI(const Resources& parent) : parent_(parent) {}
    Result<REXState> get_state() const;

private:
    const Resources& parent_;
};

}  // namespace dwarfkit
