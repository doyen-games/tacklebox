#include "dk_user_interface.h"

#include <godot_cpp/core/class_db.hpp>

namespace dwarfkit_godot {

using namespace godot;

void DkUserInterface::_bind_methods() {
    ClassDB::bind_method(D_METHOD("_run_prompt", "args"), &DkUserInterface::RunPrompt);
    ClassDB::bind_method(D_METHOD("_run_status", "message"), &DkUserInterface::RunStatus);
    ClassDB::bind_method(D_METHOD("_run_error", "message"), &DkUserInterface::RunError);
    // scripts extending DkUserInterface override these
    ClassDB::bind_method(D_METHOD("_prompt", "args"), &DkUserInterface::_prompt);
    ClassDB::bind_method(D_METHOD("_status", "message"), &DkUserInterface::_status);
    ClassDB::bind_method(D_METHOD("_error", "message"), &DkUserInterface::_error);
}

Ref<Semaphore> DkUserInterface::Done() {
    if (done_.is_null()) {
        done_.instantiate();
    }
    return done_;
}

void DkUserInterface::RunPrompt(const Dictionary& args) {
    // call() dispatches to the script override when one exists
    call("_prompt", args);
    Done()->post();
}

void DkUserInterface::RunStatus(const String& message) {
    call("_status", message);
    Done()->post();
}

void DkUserInterface::RunError(const String& message) {
    call("_error", message);
    Done()->post();
}

void DkUserInterface::PromptFromWorker(const Dictionary& args) {
    call_deferred("_run_prompt", args);
    Done()->wait();
}

void DkUserInterface::StatusFromWorker(const String& message) {
    call_deferred("_run_status", message);
    Done()->wait();
}

void DkUserInterface::ErrorFromWorker(const String& message) {
    call_deferred("_run_error", message);
    Done()->wait();
}

// ---- DkGodotUserInterface --------------------------------------------------

dwarfkit::Result<dwarfkit::UserInterfaceLoginResponse> DkGodotUserInterface::login(
    dwarfkit::LoginContext& context) {
    dwarfkit::UserInterfaceLoginResponse response;
    if (context.chain) {
        response.chainId = context.chain->id;
    }
    response.permissionLevel = context.permissionLevel;
    response.walletPluginIndex = 0;
    return response;
}

dwarfkit::Result<void> DkGodotUserInterface::onError(const dwarfkit::Error& error) {
    if (ui_.is_valid()) {
        ui_->ErrorFromWorker(ToGodot(error.message));
    }
    return {};
}

dwarfkit::Result<dwarfkit::UserInterfaceAccountCreationResponse>
DkGodotUserInterface::onAccountCreate(dwarfkit::CreateAccountContext&) {
    return dwarfkit::err(dwarfkit::ErrorKind::Unsupported,
                         "Account creation UI is not implemented");
}

dwarfkit::Result<dwarfkit::PromptResponse> DkGodotUserInterface::prompt(
    const dwarfkit::PromptArgs& args, dwarfkit::CancelToken) {
    if (ui_.is_valid()) {
        Dictionary dict;
        dict["title"] = ToGodot(args.title);
        dict["body"] = ToGodot(args.body.value_or(""));
        Array elements;
        for (const auto& element : args.elements) {
            Dictionary item;
            switch (element.type) {
                case dwarfkit::PromptElementType::qr: item["type"] = "qr"; break;
                case dwarfkit::PromptElementType::link: item["type"] = "link"; break;
                case dwarfkit::PromptElementType::button: item["type"] = "button"; break;
                case dwarfkit::PromptElementType::countdown:
                    item["type"] = "countdown";
                    break;
                case dwarfkit::PromptElementType::textarea:
                    item["type"] = "textarea";
                    break;
                default: item["type"] = "other"; break;
            }
            if (element.label) {
                item["label"] = ToGodot(*element.label);
            }
            item["data"] = JsonToVariant(element.data);
            elements.push_back(item);
        }
        dict["elements"] = elements;
        ui_->PromptFromWorker(dict);
    }
    return dwarfkit::PromptResponse{};
}

void DkGodotUserInterface::status(const std::string& message) {
    if (ui_.is_valid()) {
        ui_->StatusFromWorker(ToGodot(message));
    }
}

}  // namespace dwarfkit_godot
