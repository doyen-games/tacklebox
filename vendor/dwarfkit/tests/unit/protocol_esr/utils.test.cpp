// Port of protocol-esr test/tests/utils.ts
#include <doctest/doctest.h>

#include <dwarfkit/protocol_esr/utils.hpp>

using namespace dwarfkit;

namespace {

bool isHexLower(char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); }

// the upstream test's /[a-f\d]{8}-[a-f\d]{4}-[a-f\d]{4}-[a-f\d]{4}-[a-f\d]{12}/
bool matchesUuid(const std::string& value) {
    if (value.size() != 36) return false;
    for (size_t i = 0; i < value.size(); i++) {
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (value[i] != '-') return false;
        } else if (!isHexLower(value[i])) {
            return false;
        }
    }
    return true;
}

}  // namespace

TEST_SUITE("pesr-utils") {
    TEST_CASE("should return PascalCase version of snake_case string") {
        CHECK(snakeToPascal("hello_world") == "HelloWorld");
        CHECK(snakeToPascal("") == "");
    }

    TEST_CASE("should return camelCase version of snake_case string") {
        CHECK(snakeToCamel("hello_world") == "helloWorld");
        CHECK(snakeToCamel("") == "");
    }

    TEST_CASE("should print a warning message") {
        auto& sink = logWarnSink();
        const auto previous = sink;
        std::vector<std::string> messages;
        sink = [&](const std::string& message) { messages.push_back(message); };
        logWarn("this is a warning");
        sink = previous;
        REQUIRE(messages.size() == 1);
        CHECK(messages[0] == "[anchor-link] this is a warning");
    }

    TEST_CASE("should generate a UUID string") {
        CHECK(matchesUuid(uuid()));
        // v4 layout markers
        const auto value = uuid();
        CHECK(value[14] == '4');
        CHECK((value[19] == '8' || value[19] == '9' || value[19] == 'a' || value[19] == 'b'));
        CHECK(uuid() != uuid());
    }
}
