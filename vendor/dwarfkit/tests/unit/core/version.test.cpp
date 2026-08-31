#include <doctest/doctest.h>
#include <dwarfkit/core/version.hpp>

TEST_SUITE("core") {
    TEST_CASE("version") {
        CHECK(dwarfkit::version() == dwarfkit::versionString);
    }
}
