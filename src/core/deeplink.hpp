// Deep-link plumbing: the tacklebox: URL scheme (plus a polite claim on esr:),
// parsing of incoming request URIs, and the single-instance forwarder that
// hands a second launch's URI to the running wallet.
//
// The wharfkit plugin fires `tacklebox://request/<payload>` where <payload> is
// an ESR request body without its scheme - its own scheme so a machine where
// Anchor owns esr: still opens TackleBox. Plain esr:/eosio: URIs are accepted
// too for the case where TackleBox holds the esr: registration.
//
// Per platform:
//   Windows  HKCU\Software\Classes registration; a named pipe + mutex scoped
//            by the data dir carry forwarded uris.
//   Linux    a per-user .desktop entry with x-scheme-handler/* and a
//            mimeapps.list default (system installs ship the entry through
//            the package instead); a unix socket + lock file in the runtime
//            dir carry forwarded uris.
//   macOS    CFBundleURLTypes in Info.plist; LaunchServices delivers uris to
//            the running app as an Apple event, which SDL surfaces as a drop
//            event (main.cpp routes it here). The unix socket covers a bare
//            binary launched twice.
#pragma once

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <thread>

namespace tb::deeplink {

enum class Kind {
    None,     // not a deep link we understand
    Focus,    // bring the running wallet to the front, nothing else
    Request,  // carries a signing/login request (esr uri in `esr`)
};

struct Parsed {
    Kind kind = Kind::None;
    std::string esr;  // set when kind == Request; normalized to esr://...
};

// Classify a command line / forwarded uri. Pure, no I/O.
Parsed parse(const std::string& uri);

// The first argv entry that parses as a deep link.
std::optional<std::string> uriFromArgs(int argc, char** argv);

// Register URL schemes for the current user. tacklebox: always points at
// this executable; esr: is claimed only when no other handler owns it.
// `iconPng` (Linux) is written next to a per-user .desktop entry so the
// launcher shows the mark for a portable/AppImage copy; ignored elsewhere.
// No-op on macOS (the bundle's Info.plist declares the schemes) and mobile.
void registerSchemes(const unsigned char* iconPng = nullptr, size_t iconSize = 0);

// True when another instance owns this data dir's endpoint and the uri was
// forwarded to it; the caller should exit without opening a window.
bool forwardToPrimaryInstance(const std::string& uri);

// Owns the single-instance claim for this data dir. `claim()` must be called
// before any window exists; when it returns false another instance runs and
// this process should forward + exit. `start()` then accepts forwarded uris
// (delivered on an internal thread) until `stop()`.
class InstanceServer {
public:
    ~InstanceServer();

    bool claim();
    void start(std::function<void(std::string)> onUri);
    void stop();

private:
#if defined(_WIN32)
    void* mutex_ = nullptr;      // HANDLE, held for the process lifetime
    void* stopEvent_ = nullptr;  // HANDLE, signalled by stop()
    std::thread thread_;
#else
    int lockFd_ = -1;             // flock'd for the process lifetime
    int listenFd_ = -1;           // unix socket accepting forwarded uris
    int wakePipe_[2] = {-1, -1};  // stop() writes a byte to unblock poll()
    std::thread thread_;
#endif
};

// --- Pure helpers behind the Linux registration, exposed for tests ---------

// The freedesktop entry for the tacklebox scheme handler. `execPath` is
// quoted per the Desktop Entry spec; `%u` carries the uri.
std::string desktopEntry(const std::string& execPath);

// A mimeapps.list with our defaults applied to [Default Applications]:
// x-scheme-handler/tacklebox always points at `desktopId`; esr: only when no
// other handler is set (reported through `claimedEsr`). Other sections and
// lines survive untouched; a missing section is appended.
std::string mimeAppsWithDefaults(const std::string& existing, const std::string& desktopId,
                                 bool& claimedEsr);

}  // namespace tb::deeplink
