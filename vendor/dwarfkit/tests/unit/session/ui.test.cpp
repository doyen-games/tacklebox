// Port of session test/tests/ui.ts (the abstract translate default returns
// the default value or key instead of throwing, see DIVERGENCES.md)
#include <doctest/doctest.h>

#include "../../util/mock_session.hpp"

using namespace dwarfkit;
using namespace dwarfkit::test;

namespace {

json mockLocaleDefinitions() { return json{{"en", {{"foo", "bar"}}}}; }

json mockNamespacedLocaleDefinitions() {
    return json{{"en", {{"unittest", {{"foo", "bar"}}}}}};
}

class ImplementedUI : public MockUserInterface {
public:
    json localeDefinitions = json::object();

    void addTranslations(const LocaleDefinitions& definitions) override {
        log("addTranslations");
        for (const auto& item : definitions.items()) {
            localeDefinitions[item.key()] = item.value();
        }
    }

    std::string translate(const std::string& key,
                          const UserInterfaceTranslateOptions& options = {},
                          const std::string& ns = "") override {
        const json& en = localeDefinitions.contains("en") ? localeDefinitions["en"]
                                                          : json::object();
        if (!ns.empty()) {
            if (en.contains(ns) && en[ns].contains(key)) {
                return en[ns][key].get<std::string>();
            }
        }
        if (en.contains(key)) {
            return en[key].get<std::string>();
        }
        if (!options.defaultValue.empty()) {
            return options.defaultValue;
        }
        return "[[" + key + " not localized]]";
    }
};

}  // namespace

TEST_SUITE("session-ui") {
    TEST_CASE("addTranslations should add definitions") {
        ImplementedUI ui;
        ui.addTranslations(mockLocaleDefinitions());
        CHECK(nlohmann::json(ui.localeDefinitions) == nlohmann::json(mockLocaleDefinitions()));
    }

    TEST_CASE("getTranslate should return a function") {
        MockUserInterface ui;
        const auto t = ui.getTranslate("unittest");
        CHECK(static_cast<bool>(t));
    }

    TEST_CASE("getTranslate should return the translation") {
        ImplementedUI ui;
        ui.addTranslations(mockNamespacedLocaleDefinitions());
        const auto t = ui.getTranslate("unittest");
        CHECK(t("foo", {.defaultValue = "bar"}) == "bar");
    }

    TEST_CASE("translate default falls back to the key") {
        MockUserInterface ui;
        // upstream throws "must be implemented"; without exceptions the
        // abstract default hands back the key (or the provided default)
        CHECK(ui.translate("foo") == "foo");
        CHECK(ui.translate("foo", {.defaultValue = "fallback"}) == "fallback");
    }

    TEST_CASE("translate should return the translation") {
        ImplementedUI ui;
        ui.addTranslations(mockLocaleDefinitions());
        CHECK(ui.translate("foo") == "bar");
    }
}
