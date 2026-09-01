// Deep-link plumbing: the tacklebox: URL scheme (plus a polite claim on esr:),
// parsing of incoming request URIs, and the Windows single-instance forwarder
// that hands a second launch's URI to the running wallet.
//
// The wharfkit plugin fires `tacklebox://request/<payload>` where <payload> is
// an ESR request body without its scheme - its own scheme so a machine where
// Anchor owns esr: still opens TackleBox. Plain esr:/eosio: URIs are accepted
// too for the case where TackleBox holds the esr: registration.
#pragma once

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

// Register URL schemes for the current user (Windows: HKCU\Software\Classes).
// tacklebox: always points at this executable; esr: is claimed only when no
// other handler owns it. No-op on other platforms and mobile.
void registerSchemes();

// True when another instance owns this data dir's pipe and the uri was
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
#ifdef _WIN32
    void* mutex_ = nullptr;      // HANDLE, held for the process lifetime
    void* stopEvent_ = nullptr;  // HANDLE, signalled by stop()
    std::thread thread_;
#endif
};

}  // namespace tb::deeplink
