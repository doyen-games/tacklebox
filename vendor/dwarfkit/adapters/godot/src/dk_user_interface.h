// DkUserInterface: a RefCounted class scripts extend with _prompt/_status/
// _error virtuals (BLUEPRINT.md 8.2). Kit worker calls dispatch to the main
// thread with call_deferred and wait on a Semaphore.
#pragma once

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/classes/semaphore.hpp>

#include <dwarfkit/session.hpp>

#include "dk_variant.h"

namespace dwarfkit_godot {

class DkUserInterface : public godot::RefCounted {
    GDCLASS(DkUserInterface, godot::RefCounted)

public:
    // Overridable from scripts. _prompt receives {title, body, elements:
    // [{type, label, data}]} and must display and return (non-blocking, like
    // the console UI).
    virtual void _prompt(const godot::Dictionary& args) { (void)args; }
    virtual void _status(const godot::String& message) { (void)message; }
    virtual void _error(const godot::String& message) { (void)message; }

    // The kit worker calls these; each defers to the main thread and waits.
    void PromptFromWorker(const godot::Dictionary& args);
    void StatusFromWorker(const godot::String& message);
    void ErrorFromWorker(const godot::String& message);

protected:
    static void _bind_methods();

private:
    void RunPrompt(const godot::Dictionary& args);
    void RunStatus(const godot::String& message);
    void RunError(const godot::String& message);

    godot::Ref<godot::Semaphore> Done();
    godot::Ref<godot::Semaphore> done_;
};

// The dwarfkit::UserInterface driving a DkUserInterface.
class DkGodotUserInterface final : public dwarfkit::AbstractUserInterface {
public:
    explicit DkGodotUserInterface(godot::Ref<DkUserInterface> ui) : ui_(ui) {}

    dwarfkit::Result<dwarfkit::UserInterfaceLoginResponse> login(
        dwarfkit::LoginContext& context) override;
    dwarfkit::Result<void> onError(const dwarfkit::Error& error) override;
    dwarfkit::Result<dwarfkit::UserInterfaceAccountCreationResponse> onAccountCreate(
        dwarfkit::CreateAccountContext&) override;
    dwarfkit::Result<void> onAccountCreateComplete() override { return {}; }
    dwarfkit::Result<void> onLogin() override { return {}; }
    dwarfkit::Result<void> onLoginComplete() override { return {}; }
    dwarfkit::Result<void> onTransact() override { return {}; }
    dwarfkit::Result<void> onTransactComplete() override { return {}; }
    dwarfkit::Result<void> onSign() override { return {}; }
    dwarfkit::Result<void> onSignComplete() override { return {}; }
    dwarfkit::Result<void> onBroadcast() override { return {}; }
    dwarfkit::Result<void> onBroadcastComplete() override { return {}; }
    dwarfkit::Result<dwarfkit::PromptResponse> prompt(const dwarfkit::PromptArgs& args,
                                                      dwarfkit::CancelToken token) override;
    void status(const std::string& message) override;

private:
    godot::Ref<DkUserInterface> ui_;
};

}  // namespace dwarfkit_godot
