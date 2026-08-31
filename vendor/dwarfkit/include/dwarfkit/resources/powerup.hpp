// Port of resources src/powerup.ts and src/powerup/{abstract,cpu,net}.ts.
#pragma once

#include <dwarfkit/resources/resources.hpp>

namespace dwarfkit {

struct PowerUpStateOptions {
    // timestamp to base adjusted_utilization off
    std::optional<TimePointSec> timestamp;
    // blockchain resource limits for calculating usage
    std::optional<uint64_t> virtual_block_cpu_limit;
    std::optional<uint64_t> virtual_block_net_limit;
    // minimum payment amount
    std::optional<Asset> min_payment;
};

struct PowerUpStateResource {
    DK_STRUCT("powerupstateresource")
    uint8_t version = 0;
    int64_t weight = 0;
    int64_t weight_ratio = 0;
    int64_t assumed_stake_weight = 0;
    int64_t initial_weight_ratio = 0;
    int64_t target_weight_ratio = 0;
    TimePointSec initial_timestamp;
    TimePointSec target_timestamp;
    double exponent = 0;
    uint32_t decay_secs = 0;
    Asset min_price;
    Asset max_price;
    int64_t utilization = 0;
    int64_t adjusted_utilization = 0;
    TimePointSec utilization_timestamp;
    DK_FIELDS(version, weight, weight_ratio, assumed_stake_weight, initial_weight_ratio,
              target_weight_ratio, initial_timestamp, target_timestamp, exponent, decay_secs,
              min_price, max_price, utilization, adjusted_utilization, utilization_timestamp)

    static constexpr uint64_t default_block_cpu_limit = 200000;
    static constexpr uint64_t default_block_net_limit = 1048576000;

    // Current number of allocated units (shift from REX -> PowerUp).
    double allocated() const {
        return 1.0 - static_cast<double>(weight_ratio / target_weight_ratio) / 100.0;
    }
    // Current percentage of reserved units. Integer division, faithfully
    // reproducing the upstream Int64 truncation (the TS getter claims float).
    int64_t reserved() const { return weight == 0 ? 0 : utilization / weight; }
    Asset::Symbol symbol() const { return min_price.symbol; }

    // Mimic eosio.system powerup.cpp update_utilization / fee math
    Result<UInt128> utilization_increase(const UInt128& frac) const;
    double price_function(int64_t at_utilization) const;
    double price_integral_delta(int64_t start_utilization, int64_t end_utilization) const;
    double fee(const UInt128& utilizationIncrease, int64_t adjustedUtilization) const;
    int64_t determine_adjusted_utilization(const PowerUpStateOptions& options = {}) const;
};

struct PowerUpStateResourceCPU : PowerUpStateResource {
    DK_STRUCT_BASE("powerupstateresourcecpu", PowerUpStateResource)

    // Return smallest units per day, us (microseconds)
    double per_day(const PowerUpStateOptions& options = {}) const { return us_per_day(options); }
    double ms_per_day(const PowerUpStateOptions& options = {}) const {
        return us_per_day(options) / 1000;
    }
    double us_per_day(const PowerUpStateOptions& options = {}) const {
        const uint64_t limit = options.virtual_block_cpu_limit ? *options.virtual_block_cpu_limit
                                                               : default_block_cpu_limit;
        return static_cast<double>(limit) * 2 * 60 * 60 * 24;
    }

    Result<UInt128> weight_to_us(const UInt128& sample, int64_t weightValue) const;
    Result<int64_t> us_to_weight(const UInt128& sample, int64_t us) const;

    Result<int64_t> frac(const SampleUsage& usage, double us) const {
        return frac_by_us(usage, static_cast<int64_t>(us));
    }
    Result<int64_t> frac_by_ms(const SampleUsage& usage, double ms) const {
        return frac_by_us(usage, static_cast<int64_t>(ms * 1000));
    }
    Result<int64_t> frac_by_us(const SampleUsage& usage, int64_t us) const;

    Result<double> price_per(const SampleUsage& usage, double us = 1000,
                             const PowerUpStateOptions& options = {}) const {
        return price_per_us(usage, us, options);
    }
    Result<double> price_per_ms(const SampleUsage& usage, double ms = 1,
                                const PowerUpStateOptions& options = {}) const {
        return price_per_us(usage, ms * 1000, options);
    }
    Result<double> price_per_us(const SampleUsage& usage, double us = 1000,
                                const PowerUpStateOptions& options = {}) const;
};

struct PowerUpStateResourceNET : PowerUpStateResource {
    DK_STRUCT_BASE("powerupstateresourcenet", PowerUpStateResource)

    // Return smallest units per day, bytes
    double per_day(const PowerUpStateOptions& options = {}) const { return bytes_per_day(options); }
    double kb_per_day(const PowerUpStateOptions& options = {}) const {
        return bytes_per_day(options) / 1000;
    }
    double bytes_per_day(const PowerUpStateOptions& options = {}) const {
        const uint64_t limit = options.virtual_block_net_limit ? *options.virtual_block_net_limit
                                                               : default_block_net_limit;
        return static_cast<double>(limit) * 2 * 60 * 60 * 24;
    }

    Result<UInt128> weight_to_bytes(const UInt128& sample, int64_t weightValue) const;
    Result<int64_t> bytes_to_weight(const UInt128& sample, int64_t bytes) const;

    Result<int64_t> frac(const SampleUsage& usage, double bytes) const {
        return frac_by_bytes(usage, static_cast<int64_t>(bytes));
    }
    Result<int64_t> frac_by_kb(const SampleUsage& usage, double kilobytes) const {
        return frac_by_bytes(usage, static_cast<int64_t>(kilobytes * 1000));
    }
    Result<int64_t> frac_by_bytes(const SampleUsage& usage, int64_t bytes) const;

    Result<double> price_per(const SampleUsage& usage, double bytes = 1000,
                             const PowerUpStateOptions& options = {}) const {
        return price_per_byte(usage, bytes, options);
    }
    Result<double> price_per_kb(const SampleUsage& usage, double kilobytes = 1,
                                const PowerUpStateOptions& options = {}) const {
        return price_per_byte(usage, kilobytes * 1000, options);
    }
    Result<double> price_per_byte(const SampleUsage& usage, double bytes = 1000,
                                  const PowerUpStateOptions& options = {}) const;
};

struct PowerUpState {
    DK_STRUCT("powerupstate")
    uint8_t version = 0;
    PowerUpStateResourceNET net;
    PowerUpStateResourceCPU cpu;
    uint32_t powerup_days = 0;
    Asset min_powerup_fee;
    DK_FIELDS(version, net, cpu, powerup_days, min_powerup_fee)
};

class PowerUpAPI {
public:
    explicit PowerUpAPI(const Resources& parent) : parent_(parent) {}
    Result<PowerUpState> get_state() const;

private:
    const Resources& parent_;
};

}  // namespace dwarfkit
