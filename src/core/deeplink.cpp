#include "core/deeplink.hpp"

#include <algorithm>
#include <cctype>
#include <cwchar>
#include <cwctype>

#include "core/log.hpp"
#include "core/paths.hpp"
#include "core/util.hpp"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace tb::deeplink {

namespace {

// Case-insensitive prefix match; returns the tail after the prefix.
std::optional<std::string> tailAfter(const std::string& value, std::string_view prefix) {
    if (value.size() < prefix.size()) return std::nullopt;
    for (size_t i = 0; i < prefix.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(value[i])) !=
            std::tolower(static_cast<unsigned char>(prefix[i])))
            return std::nullopt;
    return value.substr(prefix.size());
}

std::string stripSlashes(std::string value) {
    while (!value.empty() && value.front() == '/') value.erase(value.begin());
    return value;
}

}  // namespace

Parsed parse(const std::string& rawUri) {
    const std::string uri = trim(rawUri);
    if (uri.empty()) return {};

    // Standard signing-request schemes pass through untouched.
    for (const char* scheme : {"esr:", "esr-anchor:", "eosio:"}) {
        if (tailAfter(uri, scheme)) return {Kind::Request, uri};
    }

    auto tail = tailAfter(uri, "tacklebox:");
    if (!tail) return {};
    std::string rest = stripSlashes(*tail);

    if (auto payload = tailAfter(rest, "request/")) {
        if (payload->empty()) return {Kind::Focus, {}};
        return {Kind::Request, "esr://" + *payload};
    }
    // Bare scheme, `open`, or anything else without a payload: just focus.
    return {Kind::Focus, {}};
}

std::optional<std::string> uriFromArgs(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        if (!argv[i]) continue;
        const std::string arg = argv[i];
        if (parse(arg).kind != Kind::None) return arg;
    }
    return std::nullopt;
}

#ifdef _WIN32

namespace {

// Names are scoped by the data dir so `--data-dir` dev instances coexist with
// a normally-launched wallet.
std::wstring scopeSuffix() {
    const std::wstring path = dataDir().wstring();
    uint64_t hash = 1469598103934665603ull;  // FNV-1a 64
    for (wchar_t c : path) {
        hash ^= static_cast<uint64_t>(std::towlower(c));
        hash *= 1099511628211ull;
    }
    wchar_t hex[17];
    swprintf(hex, 17, L"%016llx", static_cast<unsigned long long>(hash));
    return hex;
}

std::wstring pipeName() { return L"\\\\.\\pipe\\tacklebox.deeplink." + scopeSuffix(); }
std::wstring mutexName() { return L"Local\\tacklebox.instance." + scopeSuffix(); }

std::wstring exePath() {
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    return path;
}

bool setRegString(HKEY root, const std::wstring& subkey, const wchar_t* name,
                  const std::wstring& value) {
    HKEY key = nullptr;
    if (RegCreateKeyExW(root, subkey.c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key,
                        nullptr) != ERROR_SUCCESS)
        return false;
    const auto bytes = static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t));
    const LSTATUS status =
        RegSetValueExW(key, name, 0, REG_SZ,
                       reinterpret_cast<const BYTE*>(value.c_str()), bytes);
    RegCloseKey(key);
    return status == ERROR_SUCCESS;
}

bool schemeKeyExists(const std::wstring& scheme) {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, (L"Software\\Classes\\" + scheme).c_str(), 0,
                      KEY_QUERY_VALUE, &key) != ERROR_SUCCESS)
        return false;
    RegCloseKey(key);
    return true;
}

void writeSchemeKey(const std::wstring& scheme, const std::wstring& exe) {
    const std::wstring base = L"Software\\Classes\\" + scheme;
    setRegString(HKEY_CURRENT_USER, base, nullptr, L"URL:TackleBox (" + scheme + L")");
    setRegString(HKEY_CURRENT_USER, base, L"URL Protocol", L"");
    setRegString(HKEY_CURRENT_USER, base + L"\\DefaultIcon", nullptr, L"\"" + exe + L"\",0");
    setRegString(HKEY_CURRENT_USER, base + L"\\shell\\open\\command", nullptr,
                 L"\"" + exe + L"\" \"%1\"");
}

}  // namespace

void registerSchemes() {
    const std::wstring exe = exePath();
    if (exe.empty()) return;
    // Our own scheme always tracks the last-run executable.
    writeSchemeKey(L"tacklebox", exe);
    // esr: is shared ecosystem ground - claim it only when nobody else has.
    if (!schemeKeyExists(L"esr")) {
        writeSchemeKey(L"esr", exe);
        Log::info("deeplink: claimed the esr: scheme (was unregistered)");
    }
    Log::info("deeplink: url schemes registered for this user");
}

bool forwardToPrimaryInstance(const std::string& uri) {
    const std::wstring name = pipeName();
    for (int attempt = 0; attempt < 3; ++attempt) {
        HANDLE pipe = CreateFileW(name.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0,
                                  nullptr);
        if (pipe != INVALID_HANDLE_VALUE) {
            DWORD written = 0;
            const BOOL ok = WriteFile(pipe, uri.data(), static_cast<DWORD>(uri.size()),
                                      &written, nullptr);
            CloseHandle(pipe);
            return ok && written == uri.size();
        }
        if (GetLastError() == ERROR_PIPE_BUSY) {
            WaitNamedPipeW(name.c_str(), 1000);
            continue;
        }
        // Give a just-starting primary a moment to open the pipe.
        Sleep(300);
    }
    return false;
}

InstanceServer::~InstanceServer() { stop(); }

bool InstanceServer::claim() {
    mutex_ = CreateMutexW(nullptr, TRUE, mutexName().c_str());
    return mutex_ != nullptr && GetLastError() != ERROR_ALREADY_EXISTS;
}

void InstanceServer::start(std::function<void(std::string)> onUri) {
    stopEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    thread_ = std::thread([this, onUri = std::move(onUri)] {
        const std::wstring name = pipeName();
        for (;;) {
            HANDLE pipe = CreateNamedPipeW(
                name.c_str(), PIPE_ACCESS_INBOUND | FILE_FLAG_OVERLAPPED,
                PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT, 1, 0, 16 * 1024, 0,
                nullptr);
            if (pipe == INVALID_HANDLE_VALUE) {
                Log::warn("deeplink: pipe creation failed (%lu)", GetLastError());
                return;
            }

            OVERLAPPED overlapped{};
            overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            ConnectNamedPipe(pipe, &overlapped);
            const DWORD lastError = GetLastError();
            if (lastError != ERROR_IO_PENDING && lastError != ERROR_PIPE_CONNECTED) {
                CloseHandle(overlapped.hEvent);
                CloseHandle(pipe);
                if (WaitForSingleObject(static_cast<HANDLE>(stopEvent_), 100) == WAIT_OBJECT_0)
                    return;
                continue;
            }

            HANDLE waits[2] = {overlapped.hEvent, static_cast<HANDLE>(stopEvent_)};
            const DWORD waited =
                lastError == ERROR_PIPE_CONNECTED
                    ? WAIT_OBJECT_0
                    : WaitForMultipleObjects(2, waits, FALSE, INFINITE);
            if (waited != WAIT_OBJECT_0) {
                CancelIo(pipe);
                CloseHandle(overlapped.hEvent);
                CloseHandle(pipe);
                return;  // stop requested
            }

            char buffer[16 * 1024];
            DWORD read = 0;
            if (ReadFile(pipe, buffer, sizeof(buffer) - 1, &read, nullptr) && read > 0) {
                onUri(std::string(buffer, read));
            }
            DisconnectNamedPipe(pipe);
            CloseHandle(overlapped.hEvent);
            CloseHandle(pipe);
        }
    });
}

void InstanceServer::stop() {
    if (stopEvent_) SetEvent(static_cast<HANDLE>(stopEvent_));
    if (thread_.joinable()) {
        // Nudge a ConnectNamedPipe that is already signalled-free.
        HANDLE poke = CreateFileW(pipeName().c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                                  0, nullptr);
        if (poke != INVALID_HANDLE_VALUE) CloseHandle(poke);
        thread_.join();
    }
    if (stopEvent_) {
        CloseHandle(static_cast<HANDLE>(stopEvent_));
        stopEvent_ = nullptr;
    }
    if (mutex_) {
        CloseHandle(static_cast<HANDLE>(mutex_));
        mutex_ = nullptr;
    }
}

#else  // !_WIN32

void registerSchemes() {
    // macOS: CFBundleURLTypes in the app bundle's Info.plist; Linux: a
    // .desktop entry with x-scheme-handler/tacklebox. Both land with their
    // packaging work.
}

bool forwardToPrimaryInstance(const std::string&) { return false; }

InstanceServer::~InstanceServer() = default;
bool InstanceServer::claim() { return true; }
void InstanceServer::start(std::function<void(std::string)>) {}
void InstanceServer::stop() {}

#endif

}  // namespace tb::deeplink
