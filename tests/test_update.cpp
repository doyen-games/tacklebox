#include <doctest/doctest.h>

#include "app/update.hpp"

using namespace tb;
using dwarfkit::json;

TEST_CASE("version comparison") {
    CHECK(update::compareVersions("v0.2.0", "v0.2.0") == 0);
    CHECK(update::compareVersions("0.2.0", "v0.2.0") == 0);  // prefix optional
    CHECK(update::compareVersions("v0.3.0", "v0.2.0") > 0);
    CHECK(update::compareVersions("v0.2.0", "v0.10.0") < 0);  // numeric, not lexical
    CHECK(update::compareVersions("v1.0.0", "v0.99.99") > 0);
    CHECK(update::compareVersions("v0.2", "v0.2.0") == 0);  // missing patch = 0
    // Prereleases sort below their release; two prereleases compare by text.
    CHECK(update::compareVersions("v0.3.0-rc1", "v0.3.0") < 0);
    CHECK(update::compareVersions("v0.3.0", "v0.3.0-rc1") > 0);
    CHECK(update::compareVersions("v0.3.0-rc1", "v0.3.0-rc2") < 0);
    CHECK(update::compareVersions("v0.3.0-rc1", "v0.2.9") > 0);
}

TEST_CASE("release feed parsing") {
    json good = {{"tag_name", "v0.3.0"},
                 {"html_url", "https://github.com/doyen-games/tacklebox/releases/tag/v0.3.0"},
                 {"body", "Fixes and features."},
                 {"published_at", "2026-09-15T12:00:00Z"},
                 {"draft", false}};
    auto release = update::parseLatestRelease(good);
    REQUIRE(release.has_value());
    CHECK(release->tag == "v0.3.0");
    CHECK(release->published == "2026-09-15");
    CHECK(release->notes == "Fixes and features.");

    // Long notes truncate for display.
    json longNotes = good;
    longNotes["body"] = std::string(2000, 'x');
    auto truncated = update::parseLatestRelease(longNotes);
    REQUIRE(truncated.has_value());
    CHECK(truncated->notes.size() == 603);  // 600 + "..."

    // Drafts and malformed shapes are rejected.
    json draft = good;
    draft["draft"] = true;
    CHECK_FALSE(update::parseLatestRelease(draft).has_value());
    CHECK_FALSE(update::parseLatestRelease(json::array()).has_value());
    CHECK_FALSE(update::parseLatestRelease(json{{"message", "Not Found"}}).has_value());
}
