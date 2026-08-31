#include "core/paths.hpp"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <system_error>

#include "core/log.hpp"

namespace fs = std::filesystem;

namespace tb {

namespace {
fs::path g_override;
}

void overrideDataDir(const fs::path& dir) { g_override = dir; }

fs::path dataDir() {
    static fs::path dir = [] {
        fs::path base;
        if (!g_override.empty()) {
            std::error_code overrideEc;
            fs::create_directories(g_override, overrideEc);
            return g_override;
        }
#ifdef _WIN32
        if (const char* appdata = std::getenv("APPDATA"); appdata && *appdata)
            base = fs::path(appdata) / "TackleBox";
        else
            base = fs::path(".") / "TackleBox";
#elif defined(__APPLE__)
        if (const char* home = std::getenv("HOME"); home && *home)
            base = fs::path(home) / "Library" / "Application Support" / "TackleBox";
        else
            base = fs::path(".") / "TackleBox";
#else
        if (const char* xdg = std::getenv("XDG_DATA_HOME"); xdg && *xdg)
            base = fs::path(xdg) / "tacklebox";
        else if (const char* home = std::getenv("HOME"); home && *home)
            base = fs::path(home) / ".local" / "share" / "tacklebox";
        else
            base = fs::path(".") / "tacklebox";
#endif
        std::error_code ec;
        fs::create_directories(base, ec);
        if (ec) Log::error("cannot create data dir %s: %s", base.string().c_str(),
                           ec.message().c_str());
        return base;
    }();
    return dir;
}

fs::path vaultFile() { return dataDir() / "vault.tbx"; }
fs::path settingsFile() { return dataDir() / "settings.json"; }

bool atomicWrite(const fs::path& target, const std::string& content, bool keepBackup) {
    std::error_code ec;
    fs::path tmp = target;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) {
            Log::error("atomicWrite: cannot open %s", tmp.string().c_str());
            return false;
        }
        out.write(content.data(), static_cast<std::streamsize>(content.size()));
        out.flush();
        if (!out) {
            Log::error("atomicWrite: short write to %s", tmp.string().c_str());
            return false;
        }
    }
    if (keepBackup && fs::exists(target, ec)) {
        fs::path bak = target;
        bak += ".bak";
        fs::remove(bak, ec);
        fs::rename(target, bak, ec);
        if (ec) Log::warn("atomicWrite: backup rotate failed: %s", ec.message().c_str());
    }
    fs::rename(tmp, target, ec);
    if (ec) {
        // Windows rename over an existing file can fail; fall back to
        // remove+rename (the backup above still protects the previous copy).
        fs::remove(target, ec);
        fs::rename(tmp, target, ec);
        if (ec) {
            Log::error("atomicWrite: rename failed: %s", ec.message().c_str());
            return false;
        }
    }
    return true;
}

}  // namespace tb
