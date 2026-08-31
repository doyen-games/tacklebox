#include "core/autostart.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>

#if defined(_WIN32)
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

namespace tb {

namespace {

#if defined(TB_MOBILE)

// Mobile OSes own app lifecycles; there is nothing to register.

#elif defined(_WIN32)

constexpr const wchar_t* kRunKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr const wchar_t* kRunValue = L"TackleBox";

std::wstring exePathW() {
    wchar_t buf[MAX_PATH] = {};
    DWORD len = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return len ? std::wstring(buf, len) : std::wstring();
}

#else

std::filesystem::path selfExe() {
#if defined(__APPLE__)
    char buf[4096] = {};
    uint32_t size = sizeof buf;
    if (_NSGetExecutablePath(buf, &size) == 0) return std::filesystem::path(buf);
    return {};
#else
    std::error_code ec;
    auto path = std::filesystem::read_symlink("/proc/self/exe", ec);
    return ec ? std::filesystem::path() : path;
#endif
}

std::filesystem::path autostartEntry() {
#if defined(__APPLE__)
    const char* home = std::getenv("HOME");
    if (!home) return {};
    return std::filesystem::path(home) / "Library" / "LaunchAgents" /
           "io.tacklebox.wallet.plist";
#else
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    std::filesystem::path base;
    if (xdg && *xdg) {
        base = xdg;
    } else {
        const char* home = std::getenv("HOME");
        if (!home) return {};
        base = std::filesystem::path(home) / ".config";
    }
    return base / "autostart" / "tacklebox.desktop";
#endif
}

#endif

}  // namespace

bool autostartSupported() {
#if defined(TB_MOBILE)
    return false;
#else
    return true;
#endif
}

bool autostartEnabled() {
#if defined(TB_MOBILE)
    return false;
#elif defined(_WIN32)
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS)
        return false;
    DWORD type = 0;
    LONG status = RegQueryValueExW(key, kRunValue, nullptr, &type, nullptr, nullptr);
    RegCloseKey(key);
    return status == ERROR_SUCCESS && (type == REG_SZ || type == REG_EXPAND_SZ);
#else
    std::error_code ec;
    auto entry = autostartEntry();
    return !entry.empty() && std::filesystem::exists(entry, ec);
#endif
}

bool setAutostart(bool enabled, std::string* error) {
    auto fail = [&](const std::string& why) {
        if (error) *error = why;
        return false;
    };
#if defined(TB_MOBILE)
    return fail("not available on mobile");
#elif defined(_WIN32)
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr,
                        &key, nullptr) != ERROR_SUCCESS)
        return fail("could not open the Run registry key");
    LONG status;
    if (enabled) {
        std::wstring exe = exePathW();
        if (exe.empty()) {
            RegCloseKey(key);
            return fail("could not resolve the executable path");
        }
        std::wstring value = L"\"" + exe + L"\"";
        status = RegSetValueExW(key, kRunValue, 0, REG_SZ,
                                reinterpret_cast<const BYTE*>(value.c_str()),
                                static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
    } else {
        status = RegDeleteValueW(key, kRunValue);
        if (status == ERROR_FILE_NOT_FOUND) status = ERROR_SUCCESS;
    }
    RegCloseKey(key);
    return status == ERROR_SUCCESS ? true : fail("registry update failed");
#else
    auto entry = autostartEntry();
    if (entry.empty()) return fail("could not resolve the autostart directory");
    std::error_code ec;
    if (!enabled) {
        std::filesystem::remove(entry, ec);
        return true;
    }
    auto exe = selfExe();
    if (exe.empty()) return fail("could not resolve the executable path");
    std::filesystem::create_directories(entry.parent_path(), ec);
    std::ofstream out(entry, std::ios::trunc);
    if (!out) return fail("could not write " + entry.string());
#if defined(__APPLE__)
    out << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        << "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" "
           "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
        << "<plist version=\"1.0\"><dict>\n"
        << "  <key>Label</key><string>io.tacklebox.wallet</string>\n"
        << "  <key>ProgramArguments</key><array><string>" << exe.string()
        << "</string></array>\n"
        << "  <key>RunAtLoad</key><true/>\n"
        << "</dict></plist>\n";
#else
    out << "[Desktop Entry]\n"
        << "Type=Application\n"
        << "Name=TackleBox\n"
        << "Exec=" << exe.string() << "\n"
        << "X-GNOME-Autostart-enabled=true\n";
#endif
    return true;
#endif
}

}  // namespace tb
