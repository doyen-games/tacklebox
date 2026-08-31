// Port of resources test/{ram,rex,powerup,index}.ts against the recorded
// eos/jungle4/wax fixtures.
#include <doctest/doctest.h>

#include <dwarfkit/resources.hpp>

#include "../../util/mock_fetch_provider.hpp"

using namespace dwarfkit;
using namespace dwarfkit::test;

namespace {

Resources makeResources(const std::string& url, const std::string& sampleAccount = "") {
    ResourcesOptions options;
    options.api = makeClient(DK_FIXTURE_DIR "/resources/data", url);
    if (!sampleAccount.empty()) {
        options.sampleAccount = sampleAccount;
    }
    return Resources::make(options).value();
}

Resources eos() { return makeResources("https://eos.greymass.com"); }
Resources jungle() { return makeResources("https://jungle4.greymass.com"); }
Resources wax() { return makeResources("https://wax.greymass.com", "boost.wax"); }

// the tests pin adjusted utilization decay to a fixed timestamp
PowerUpStateOptions fixture(const Resources& resources) {
    const auto info = resources.api->v1.chain.get_info().value();
    PowerUpStateOptions options;
    options.timestamp = TimePointSec(1616784396);
    options.virtual_block_cpu_limit = info.virtual_block_cpu_limit;
    options.virtual_block_net_limit = info.virtual_block_net_limit;
    return options;
}

}  // namespace

TEST_SUITE("resources") {
    TEST_CASE("make requires url or api") {
        CHECK_FALSE(Resources::make({}).has_value());
        CHECK(Resources::make({.url = "https://eos.greymass.com"}).has_value());
    }

    TEST_CASE("[eos] ram calculations") {
        const auto resources = eos();
        const auto ram = resources.v1().ram.get_state().value();
        SUBCASE("ram.price_per(100)") {
            CHECK(ram.price_per(100).value().toString() == "0.0491 EOS");
        }
        SUBCASE("ram.price_per(100000)") {
            CHECK(ram.price_per(100000).value().toString() == "49.0784 EOS");
        }
        SUBCASE("ram.price_per_kb(1)") {
            const auto control = ram.price_per(1000).value();
            const auto actual = ram.price_per_kb(1).value();
            CHECK(control.toString() == "0.4908 EOS");
            CHECK(actual.toString() == "0.4908 EOS");
            CHECK(actual.units == control.units);
        }
    }

    TEST_CASE("[eos] rex calculations") {
        const auto resources = eos();
        const auto rex = resources.v1().rex.get_state().value();
        SUBCASE("rex.reserved") { CHECK(rex.reserved() == 0.007264384002969463); }
        SUBCASE("rex.value") { CHECK(rex.value() == 0.000132897336944197); }
        SUBCASE("rex.exchange") {
            const auto amount = Asset::from("493874015.6505 REX").value();
            const auto tokens = rex.exchange(amount);
            CHECK(tokens.toString() == "65634.5414 EOS");
        }
        SUBCASE("rex.price_per(1000)") {
            const auto usage = resources.getSampledUsage().value();
            const auto price = rex.price_per(usage, 1000);
            const auto asset = Asset::fromFloat(price, Asset::Symbol::from("4,EOS").value()).value();
            CHECK(asset.toString() == "0.0129 EOS");
            CHECK(asset.units == 129);
        }
        SUBCASE("rex.price_per(10000)") {
            const auto usage = resources.getSampledUsage().value();
            const auto price = rex.price_per(usage, 10000);
            const auto asset = Asset::fromFloat(price, Asset::Symbol::from("4,EOS").value()).value();
            CHECK(asset.toString() == "0.1289 EOS");
            CHECK(asset.units == 1289);
        }
        SUBCASE("rex.price_per(1000000)") {
            const auto usage = resources.getSampledUsage().value();
            const auto price = rex.price_per(usage, 1000000);
            const auto asset = Asset::fromFloat(price, Asset::Symbol::from("4,EOS").value()).value();
            CHECK(asset.toString() == "12.8880 EOS");
            CHECK(asset.units == 128880);
        }
    }

    TEST_CASE("[eos] powerup cpu calculations") {
        const auto resources = eos();
        const auto powerup = resources.v1().powerup.get_state().value();
        const auto options = fixture(resources);
        SUBCASE("us_to_weight") {
            const auto sample = resources.getSampledUsage().value();
            CHECK(powerup.cpu.us_to_weight(sample.cpu, 35868).value() == 397077382);
        }
        SUBCASE("weight_to_us") {
            const auto sample = resources.getSampledUsage().value();
            CHECK(powerup.cpu.weight_to_us(sample.cpu, 12930064).value() ==
                  UInt128(uint64_t(1168)));
        }
        SUBCASE("allocated") { CHECK(powerup.cpu.allocated() == 0.99); }
        SUBCASE("reserved") {
            // upstream compares Int64 truncating division against a float,
            // which BN truncates to zero on both sides
            CHECK(powerup.cpu.reserved() == 0);
        }
        SUBCASE("price_per_us(60000)") {
            const auto sample = resources.getSampledUsage().value();
            CHECK(powerup.cpu.price_per_ms(sample, 60, options).value() == 0.012);
        }
        SUBCASE("price_per_us(1000)") {
            const auto sample = resources.getSampledUsage().value();
            const auto priceUs = powerup.cpu.price_per_us(sample, 1000, options).value();
            const auto priceMs = powerup.cpu.price_per_ms(sample, 1, options).value();
            CHECK(priceUs == priceMs);
            CHECK(priceUs == 0.0002);
        }
        SUBCASE("price_per_ms(1000)") {
            const auto sample = resources.getSampledUsage().value();
            const auto price = powerup.cpu.price_per_ms(sample, 1000, options).value();
            CHECK(price == 0.1999);
            const auto asset = Asset::fromFloat(price, Asset::Symbol::from("4,EOS").value()).value();
            CHECK(asset.toString() == "0.1999 EOS");
            CHECK(asset.units == 1999);
        }
        SUBCASE("determine_adjusted_utilization") {
            const auto utilizationTs =
                static_cast<int64_t>(powerup.cpu.utilization_timestamp.value);
            const auto decaySecs = static_cast<int64_t>(powerup.cpu.decay_secs);
            PowerUpStateOptions oneDecay;
            oneDecay.timestamp =
                TimePointSec(static_cast<uint32_t>(utilizationTs + decaySecs));
            CHECK(powerup.cpu.determine_adjusted_utilization(oneDecay) == 23126085607985);
            PowerUpStateOptions tenDecays;
            tenDecays.timestamp =
                TimePointSec(static_cast<uint32_t>(utilizationTs + decaySecs * 10));
            CHECK(powerup.cpu.determine_adjusted_utilization(tenDecays) == 23123925818057);
        }
        SUBCASE("frac") {
            const auto sample = resources.getSampledUsage().value();
            CHECK(powerup.cpu.frac(sample, 100).value() == 2899435);
            CHECK(powerup.cpu.frac(sample, 1000000).value() == 28994373800);
        }
    }

    TEST_CASE("[eos] powerup net calculations") {
        const auto resources = eos();
        const auto powerup = resources.v1().powerup.get_state().value();
        const auto options = fixture(resources);
        SUBCASE("allocated") { CHECK(powerup.net.allocated() == 0.99); }
        SUBCASE("reserved") { CHECK(powerup.net.reserved() == 0); }
        SUBCASE("price_per_kb(1000)") {
            const auto sample = resources.getSampledUsage().value();
            const auto price = powerup.net.price_per_kb(sample, 1000, options).value();
            CHECK(price == 0.0001);
            const auto asset = Asset::fromFloat(price, Asset::Symbol::from("4,EOS").value()).value();
            CHECK(asset.toString() == "0.0001 EOS");
            CHECK(asset.units == 1);
        }
        SUBCASE("frac") {
            const auto sample = resources.getSampledUsage().value();
            CHECK(powerup.net.frac(sample, 1000000).value() == 5532558);
        }
    }

    TEST_CASE("[jungle] powerup cpu calculations") {
        const auto resourcesJungle = jungle();
        const auto resourcesEos = eos();
        const auto powerup = resourcesJungle.v1().powerup.get_state().value();
        const auto options = fixture(resourcesJungle);
        SUBCASE("allocated") { CHECK(powerup.cpu.allocated() == 0.99); }
        SUBCASE("reserved") { CHECK(powerup.cpu.reserved() == 0); }
        SUBCASE("price_per_us(1000000)") {
            const auto sample = resourcesEos.getSampledUsage().value();
            const auto priceUs = powerup.cpu.price_per_us(sample, 1000000, options).value();
            const auto priceMs = powerup.cpu.price_per_ms(sample, 1000, options).value();
            CHECK(priceUs == priceMs);
            CHECK(priceMs == 3.384);
        }
        SUBCASE("price_per_ms(1000)") {
            const auto sample = resourcesEos.getSampledUsage().value();
            const auto price = powerup.cpu.price_per_ms(sample, 1000, options).value();
            CHECK(price == 3.384);
            const auto asset = Asset::fromFloat(price, Asset::Symbol::from("4,EOS").value()).value();
            CHECK(asset.toString() == "3.3840 EOS");
            CHECK(asset.units == 33840);
        }
        SUBCASE("price_per_ms(1000000)") {
            const auto sample = resourcesEos.getSampledUsage().value();
            const auto price = powerup.cpu.price_per_ms(sample, 1000000, options).value();
            CHECK(price == 3814.2618);
            const auto asset = Asset::fromFloat(price, Asset::Symbol::from("4,EOS").value()).value();
            CHECK(asset.toString() == "3814.2618 EOS");
            CHECK(asset.units == 38142618);
        }
    }

    TEST_CASE("[wax] powerup calculations") {
        const auto resources = wax();
        const auto powerup = resources.v1().powerup.get_state().value();
        const auto options = fixture(resources);
        SUBCASE("cpu allocated") { CHECK(powerup.cpu.allocated() == 0.99); }
        SUBCASE("cpu reserved") { CHECK(powerup.cpu.reserved() == 0); }
        SUBCASE("cpu price_per_us(1000000)") {
            const auto sample = resources.getSampledUsage().value();
            const auto priceUs = powerup.cpu.price_per_us(sample, 1000000, options).value();
            const auto priceMs = powerup.cpu.price_per_ms(sample, 1000, options).value();
            CHECK(priceUs == priceMs);
            CHECK(priceMs == 1.58622407);
        }
        SUBCASE("cpu price_per_ms(1000)") {
            const auto sample = resources.getSampledUsage().value();
            const auto price = powerup.cpu.price_per_ms(sample, 1000, options).value();
            CHECK(price == 1.58622407);
            const auto asset = Asset::fromFloat(price, Asset::Symbol::from("8,WAX").value()).value();
            CHECK(asset.toString() == "1.58622407 WAX");
            CHECK(asset.units == 158622407);
        }
        SUBCASE("cpu price_per_ms(1000000)") {
            const auto sample = resources.getSampledUsage().value();
            const auto price = powerup.cpu.price_per_ms(sample, 1000000, options).value();
            CHECK(price == 1790.00486874);
            const auto asset = Asset::fromFloat(price, Asset::Symbol::from("8,WAX").value()).value();
            CHECK(asset.toString() == "1790.00486874 WAX");
            CHECK(asset.units == 179000486874);
        }
        SUBCASE("net allocated") { CHECK(powerup.net.allocated() == 0.99); }
        SUBCASE("net reserved") { CHECK(powerup.net.reserved() == 0); }
        SUBCASE("net price_per_kb(1000)") {
            const auto sample = resources.getSampledUsage().value();
            const auto price = powerup.net.price_per_kb(sample, 1000, options).value();
            CHECK(price == 0.00013289);
            const auto asset = Asset::fromFloat(price, Asset::Symbol::from("8,WAX").value()).value();
            CHECK(asset.toString() == "0.00013289 WAX");
            CHECK(asset.units == 13289);
        }
        SUBCASE("net frac") {
            const auto sample = resources.getSampledUsage().value();
            CHECK(powerup.net.frac(sample, 1000000).value() == 6926405);
        }
    }
}
