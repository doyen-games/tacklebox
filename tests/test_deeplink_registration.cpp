// The pure halves of the Linux scheme registration (deeplink.cpp writes
// their output to ~/.local/share and ~/.config).
#include <doctest/doctest.h>

#include <string>

#include "core/deeplink.hpp"

using tb::deeplink::desktopEntry;
using tb::deeplink::mimeAppsWithDefaults;

namespace {

bool contains(const std::string& text, const std::string& needle) {
    return text.find(needle) != std::string::npos;
}

int count(const std::string& text, const std::string& needle) {
    int n = 0;
    for (size_t at = text.find(needle); at != std::string::npos; at = text.find(needle, at + 1))
        ++n;
    return n;
}

}  // namespace

TEST_CASE("the desktop entry quotes the program path and declares both schemes") {
    const std::string entry = desktopEntry("/home/me/Apps/Tackle Box.AppImage");
    CHECK(contains(entry, "[Desktop Entry]\n"));
    CHECK(contains(entry, "Type=Application\n"));
    CHECK(contains(entry, "Exec=\"/home/me/Apps/Tackle Box.AppImage\" %u\n"));
    CHECK(contains(entry, "MimeType=x-scheme-handler/tacklebox;x-scheme-handler/esr;\n"));
    CHECK(contains(entry, "Icon=tacklebox\n"));
    CHECK(contains(entry, "Categories=Finance;"));
}

TEST_CASE("reserved characters inside the path are escaped for the Exec key") {
    const std::string entry = desktopEntry("/opt/we\"ird/$HOME/tacklebox");
    CHECK(contains(entry, "Exec=\"/opt/we\\\"ird/\\$HOME/tacklebox\" %u\n"));
}

TEST_CASE("a missing mimeapps.list gets a defaults section with both schemes") {
    bool claimedEsr = false;
    const std::string out = mimeAppsWithDefaults("", "tacklebox.desktop", claimedEsr);
    CHECK(claimedEsr);
    CHECK(out == "[Default Applications]\n"
                 "x-scheme-handler/tacklebox=tacklebox.desktop\n"
                 "x-scheme-handler/esr=tacklebox.desktop\n");
}

TEST_CASE("another handler's esr: default is left alone, ours is added once") {
    const std::string existing =
        "[Added Associations]\n"
        "text/plain=gedit.desktop;\n"
        "\n"
        "[Default Applications]\n"
        "x-scheme-handler/esr=anchor.desktop\n"
        "x-scheme-handler/tacklebox=old-tacklebox.desktop\n"
        "\n"
        "[Removed Associations]\n"
        "image/png=foo.desktop;\n";
    bool claimedEsr = true;
    const std::string out = mimeAppsWithDefaults(existing, "tacklebox.desktop", claimedEsr);
    CHECK_FALSE(claimedEsr);
    CHECK(contains(out, "x-scheme-handler/esr=anchor.desktop\n"));
    CHECK(contains(out, "x-scheme-handler/tacklebox=tacklebox.desktop\n"));
    CHECK_FALSE(contains(out, "old-tacklebox"));
    CHECK(count(out, "x-scheme-handler/tacklebox=") == 1);
    CHECK(count(out, "x-scheme-handler/esr=") == 1);
    // Everything outside the defaults section is byte-identical.
    CHECK(contains(out, "[Added Associations]\ntext/plain=gedit.desktop;\n"));
    CHECK(contains(out, "[Removed Associations]\nimage/png=foo.desktop;\n"));
}

TEST_CASE("a file without the defaults section keeps its content and gains the section") {
    const std::string existing = "[Added Associations]\nx/y=z.desktop;\n";
    bool claimedEsr = false;
    const std::string out = mimeAppsWithDefaults(existing, "tacklebox.desktop", claimedEsr);
    CHECK(claimedEsr);
    CHECK(out == "[Added Associations]\n"
                 "x/y=z.desktop;\n"
                 "\n"
                 "[Default Applications]\n"
                 "x-scheme-handler/tacklebox=tacklebox.desktop\n"
                 "x-scheme-handler/esr=tacklebox.desktop\n");
}

TEST_CASE("an empty esr: default counts as unclaimed and CRLF input is normalized") {
    const std::string existing = "[Default Applications]\r\nx-scheme-handler/esr=\r\n";
    bool claimedEsr = false;
    const std::string out = mimeAppsWithDefaults(existing, "tacklebox.desktop", claimedEsr);
    CHECK(claimedEsr);
    CHECK_FALSE(contains(out, "\r"));
    CHECK(contains(out, "x-scheme-handler/esr=tacklebox.desktop\n"));
}
