#include "dk_session_kit.h"

#include <godot_cpp/core/class_db.hpp>

#include "dk_providers.h"

namespace dwarfkit_godot {

using namespace godot;

// ---- DkSession -------------------------------------------------------------

void DkSession::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_chain_id"), &DkSession::get_chain_id);
    ClassDB::bind_method(D_METHOD("get_actor"), &DkSession::get_actor);
    ClassDB::bind_method(D_METHOD("get_permission"), &DkSession::get_permission);
    ClassDB::bind_method(D_METHOD("transact", "action", "broadcast"), &DkSession::transact,
                         DEFVAL(true));
    ADD_SIGNAL(MethodInfo("transact_completed",
                          PropertyInfo(Variant::DICTIONARY, "result")));
    ADD_SIGNAL(MethodInfo("error", PropertyInfo(Variant::STRING, "message")));
}

String DkSession::get_chain_id() const {
    return native ? ToGodot(native->chain.id.hexString()) : String();
}

String DkSession::get_actor() const {
    return native ? ToGodot(native->actor().toString()) : String();
}

String DkSession::get_permission() const {
    return native ? ToGodot(native->permission().toString()) : String();
}

void DkSession::transact(const Dictionary& action, bool broadcast) {
    if (!native) {
        emit_signal("error", String("No session"));
        return;
    }
    const dwarfkit::json actionJson = VariantToJson(action);
    std::shared_ptr<dwarfkit::Session> session = native;
    Ref<DkSession> self(this);
    std::thread(
        [self, session, actionJson, broadcast]() {
            dwarfkit::TransactOptions options;
            options.broadcast = broadcast;
            const auto result = session->transact({.action = actionJson}, options);
            if (result) {
                Dictionary dict;
                if (result->response && result->response->contains("transaction_id")) {
                    dict["transaction_id"] =
                        ToGodot((*result->response)["transaction_id"].get<std::string>());
                }
                Array signatures;
                for (const auto& signature : result->signatures) {
                    signatures.push_back(ToGodot(signature.toString()));
                }
                dict["signatures"] = signatures;
                self->call_deferred("emit_signal", "transact_completed", dict);
            } else {
                self->call_deferred("emit_signal", "error",
                                    ToGodot(result.error().message));
            }
        })
        .detach();
}

// ---- DkSessionKit ----------------------------------------------------------

void DkSessionKit::_bind_methods() {
    ClassDB::bind_method(D_METHOD("configure", "app_name", "chain_id", "url"),
                         &DkSessionKit::configure);
    ClassDB::bind_method(D_METHOD("set_ui", "ui"), &DkSessionKit::set_ui);
    ClassDB::bind_method(D_METHOD("login", "actor", "permission"), &DkSessionKit::login,
                         DEFVAL(String()), DEFVAL(String()));
    ClassDB::bind_method(D_METHOD("restore"), &DkSessionKit::restore);
    ClassDB::bind_method(D_METHOD("logout"), &DkSessionKit::logout);
    ADD_SIGNAL(MethodInfo("login_completed", PropertyInfo(Variant::OBJECT, "session")));
    ADD_SIGNAL(MethodInfo("restore_completed", PropertyInfo(Variant::OBJECT, "session")));
    ADD_SIGNAL(MethodInfo("error", PropertyInfo(Variant::STRING, "message")));
}

DkSessionKit::~DkSessionKit() {
    join();
}

void DkSessionKit::join() {
    if (worker_.joinable()) {
        worker_.join();
    }
}

void DkSessionKit::configure(const String& app_name, const String& chain_id,
                             const String& url) {
    const auto parsedId = dwarfkit::Checksum256::from(FromGodot(chain_id));
    if (!parsedId) {
        emit_signal("error", String("Invalid chain id"));
        return;
    }
    dwarfkit::SessionKitArgs args;
    args.appName = FromGodot(app_name);
    args.chains = {
        dwarfkit::ChainDefinition::from({.id = *parsedId, .url = FromGodot(url)})};
    args.ui = std::make_shared<DkGodotUserInterface>(ui_);

    dwarfkit::WalletPluginAnchorOptions anchorOptions;
    anchorOptions.buoyWs = std::make_shared<DkGodotWebSocketProvider>();
    args.walletPlugins = {std::make_shared<dwarfkit::WalletPluginAnchor>(anchorOptions)};

    dwarfkit::SessionKitOptions options;
    options.fetch = std::make_shared<DkGodotFetchProvider>();
    options.storage = MakeGodotStorage();

    kit_ = std::make_unique<dwarfkit::SessionKit>(args, options);
}

void DkSessionKit::set_ui(Ref<DkUserInterface> ui) {
    ui_ = ui;
}

void DkSessionKit::login(const String& actor, const String& permission) {
    if (!kit_) {
        emit_signal("error", String("Kit is not configured"));
        return;
    }
    join();
    Ref<DkSessionKit> self(this);
    const std::string actorStr = FromGodot(actor);
    const std::string permissionStr = FromGodot(permission);
    worker_ = std::thread(
        [self, actorStr, permissionStr]() {
            dwarfkit::LoginOptions options;
            if (!actorStr.empty()) {
                options.permissionLevel = dwarfkit::PermissionLevel{
                    dwarfkit::Name::from(actorStr),
                    dwarfkit::Name::from(permissionStr.empty() ? "active" : permissionStr)};
            }
            const auto result = self->kit_->login(options);
            if (result) {
                Ref<DkSession> session;
                session.instantiate();
                session->native = result->session;
                self->call_deferred("emit_signal", "login_completed", session);
            } else {
                self->call_deferred("emit_signal", "error",
                                    ToGodot(result.error().message));
            }
        });
}

void DkSessionKit::restore() {
    if (!kit_) {
        emit_signal("error", String("Kit is not configured"));
        return;
    }
    join();
    Ref<DkSessionKit> self(this);
    worker_ = std::thread(
        [self]() {
            const auto result = self->kit_->restore();
            if (result && *result) {
                Ref<DkSession> session;
                session.instantiate();
                session->native = *result;
                self->call_deferred("emit_signal", "restore_completed", session);
            } else if (result) {
                self->call_deferred("emit_signal", "restore_completed", Ref<DkSession>());
            } else {
                self->call_deferred("emit_signal", "error",
                                    ToGodot(result.error().message));
            }
        });
}

void DkSessionKit::logout() {
    if (kit_) {
        join();
        (void)kit_->logout();
    }
}

}  // namespace dwarfkit_godot
