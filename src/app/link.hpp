// Dapp link sessions (anchor-link wallet side, EXPERIMENTAL).
//
// Login: the user pastes a dapp's identity request (esr://). After an explicit
// confirmation TackleBox signs the identity proof and answers the dapp's
// callback with a session: our buoy channel + a fresh per-session request key.
//
// From then on the dapp pushes signing requests to that channel as sealed
// messages. A listener thread per session unseals them and routes the request
// through the exact same guard + signing review as everything else; after
// signing, the dapp's callback is answered.
#pragma once

#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include <dwarfkit/core/cancel.hpp>

#include "vault/vault.hpp"

namespace tb {

class Controller;

class LinkService {
public:
    explicit LinkService(Controller& controller) : controller_(controller) {}
    ~LinkService();

    // Reconcile listener threads with the vault's sessions. Main thread; call
    // after snapshot refreshes. Listeners only run while the vault is open.
    void sync();
    void stopAll();

    // Handle a pasted identity request (runs on a worker; confirms via the
    // plugin prompt; toasts the outcome).
    void beginLogin(const std::string& esrUri);

private:
    void listenLoop(LinkSession session, dwarfkit::CancelToken token);

    Controller& controller_;
    std::mutex mutex_;
    struct Active {
        std::thread thread;
        dwarfkit::CancelToken token;
    };
    std::map<std::string, Active> active_;  // by session id
};

}  // namespace tb
