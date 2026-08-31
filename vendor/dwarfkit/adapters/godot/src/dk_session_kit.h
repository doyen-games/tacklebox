// DkSessionKit / DkSession RefCounted wrappers (BLUEPRINT.md 8.2). Kit calls
// run on a std::thread; results come back through signals emitted with
// call_deferred: login_completed(session), restore_completed(session),
// transact_completed(result), error(message), status(message).
#pragma once

#include <thread>

#include <godot_cpp/classes/ref_counted.hpp>

#include <dwarfkit/plugins/wallet/anchor.hpp>
#include <dwarfkit/session.hpp>

#include "dk_user_interface.h"

namespace dwarfkit_godot {

class DkSession : public godot::RefCounted {
    GDCLASS(DkSession, godot::RefCounted)

public:
    godot::String get_chain_id() const;
    godot::String get_actor() const;
    godot::String get_permission() const;

    // Sign (and optionally broadcast) one action given as a Dictionary.
    // Emits transact_completed({transaction_id, signatures}) or
    // error(message).
    void transact(const godot::Dictionary& action, bool broadcast = true);

    std::shared_ptr<dwarfkit::Session> native;

protected:
    static void _bind_methods();
};

class DkSessionKit : public godot::RefCounted {
    GDCLASS(DkSessionKit, godot::RefCounted)

public:
    ~DkSessionKit() override;

    // Configure the kit: app name, 64-char hex chain id, API url. The Anchor
    // wallet plugin is wired over the Godot websocket transport.
    void configure(const godot::String& app_name, const godot::String& chain_id,
                   const godot::String& url);
    void set_ui(godot::Ref<DkUserInterface> ui);

    // Async: emits login_completed(DkSession) or error(message).
    void login(const godot::String& actor, const godot::String& permission);
    // Async: emits restore_completed(DkSession or null).
    void restore();
    void logout();

protected:
    static void _bind_methods();

private:
    void join();

    std::unique_ptr<dwarfkit::SessionKit> kit_;
    godot::Ref<DkUserInterface> ui_;
    std::thread worker_;
};

}  // namespace dwarfkit_godot
