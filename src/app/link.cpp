#include "app/link.hpp"

#include <dwarfkit/protocol_esr/buoy.hpp>
#include <dwarfkit/protocol_esr/sealed_messages.hpp>
#include <dwarfkit/signing_request.hpp>
#include <dwarfkit/transport/curl_websocket_provider.hpp>

#include "app/controller.hpp"
#include "core/log.hpp"
#include "core/util.hpp"

namespace tb {

namespace dk = dwarfkit;

namespace {

std::string hostOf(const std::string& url) {
    auto scheme = url.find("://");
    size_t start = scheme == std::string::npos ? 0 : scheme + 3;
    auto end = url.find('/', start);
    return url.substr(start, end == std::string::npos ? std::string::npos : end - start);
}

}  // namespace

std::optional<bool> esrBroadcastFlag(const dk::TransactArgs& args) {
    if (!args.request) return std::nullopt;
    if (const auto* uri = std::get_if<std::string>(&*args.request)) {
        auto parsed = dk::SigningRequest::from(trim(*uri));
        if (!parsed) return std::nullopt;
        return parsed->shouldBroadcast();
    }
    if (const auto* request = std::get_if<dk::SigningRequest>(&*args.request))
        return request->shouldBroadcast();
    return std::nullopt;
}

std::string scrubCallbackUrl(std::string url) {
    size_t open;
    while ((open = url.find("{{")) != std::string::npos) {
        size_t close = url.find("}}", open);
        if (close == std::string::npos) break;
        url.erase(open, close - open + 2);
    }
    return url;
}

void postEsrRejection(dk::FetchProvider& fetch, const std::string& callbackUrl,
                      const std::string& reason) {
    std::string url = scrubCallbackUrl(callbackUrl);
    if (url.empty()) return;
    if (!startsWith(url, "https://") && !startsWith(url, "http://localhost") &&
        !startsWith(url, "http://127.0.0.1"))
        return;
    dk::FetchRequest post;
    post.url = url;
    post.method = "POST";
    post.body = json{{"rejected", reason}}.dump();
    post.headers = {{"Content-Type", "application/json"}};
    auto response = fetch.fetch(post);
    if (!response)
        Log::warn("link: rejection callback POST failed: %s",
                  response.error().message.c_str());
    else
        Log::info("link: told the dapp the request was declined");
}

LinkService::~LinkService() { stopAll(); }

void LinkService::stopAll() {
    std::map<std::string, Active> taken;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        taken.swap(active_);
    }
    for (auto& [id, active] : taken) {
        active.token.cancel();
        if (active.thread.joinable()) active.thread.join();
    }
}

void LinkService::sync() {
    AppState& state = controller_.state();
    std::lock_guard<std::mutex> lock(mutex_);

    if (!state.unlocked) {
        for (auto& [id, active] : active_) {
            active.token.cancel();
            if (active.thread.joinable()) active.thread.detach();  // exits on cancel
        }
        active_.clear();
        return;
    }

    // Stop listeners whose session disappeared.
    for (auto it = active_.begin(); it != active_.end();) {
        bool found = false;
        for (const auto& session : state.vault.linkSessions)
            if (session.id == it->first) found = true;
        if (!found) {
            it->second.token.cancel();
            if (it->second.thread.joinable()) it->second.thread.detach();
            it = active_.erase(it);
        } else {
            ++it;
        }
    }
    // Start listeners for new sessions. The thread needs the request key,
    // which the snapshot blanks - fetch it through the controller.
    for (const auto& snap : state.vault.linkSessions) {
        if (active_.count(snap.id)) continue;
        LinkSession full = snap;
        full.requestKeyWif = controller_.linkSessionKey(snap.id);
        if (full.requestKeyWif.empty()) continue;
        Active active;
        active.token = dk::CancelToken();
        auto token = active.token;
        active.thread = std::thread([this, full, token] { listenLoop(full, token); });
        active_[snap.id] = std::move(active);
        Log::info("link: listening for %s (%s)", full.appName.c_str(),
                  full.channelId.c_str());
    }
}

void LinkService::beginLogin(const std::string& esrUri) {
    controller_.runner().run([this, esrUri] {
        auto outcome = [&]() -> dk::Result<std::string> {
            auto request = dk::SigningRequest::from(trim(esrUri));
            if (!request) return dk::err(request.error());
            if (!request->isIdentity())
                return dk::err(dk::ErrorKind::Invalid,
                               "that is a signing request, not a login request - paste it "
                               "into the ESR lane instead");

            const AccountRef* accountPtr = controller_.state().currentAccount();
            if (!accountPtr)
                return dk::err(dk::ErrorKind::Invalid, "select an account first");
            AccountRef account = *accountPtr;

            // Chain compatibility.
            if (!request->isMultiChain()) {
                auto chainId = request->getChainId();
                if (chainId && chainId->hexString() != account.chainId)
                    return dk::err(dk::ErrorKind::Invalid,
                                   "this login request is for a different chain");
            }

            auto permission = dk::PermissionLevel::from(account.actor + "@" +
                                                        account.permission);
            if (!permission) return dk::err(permission.error());

            dk::TransactionContext ctx;
            ctx.expire_seconds = 120;
            ctx.ref_block_num = 0;
            ctx.ref_block_prefix = 0;
            ctx.timestamp = dk::TimePointSec(static_cast<uint32_t>(nowSec()));
            auto resolved = request->resolve(dk::AbiMap{}, *permission, ctx);
            if (!resolved) return dk::err(resolved.error());

            auto callback = resolved->getCallback({});
            if (!callback || !*callback)
                return dk::err(dk::ErrorKind::Invalid,
                               "identity request carries no callback to answer");
            std::string appHost = hostOf((**callback).url);

            // Human gate: connecting hands the dapp a push channel.
            bool accepted = controller_.pluginPromptBlocking(
                "Link request (experimental)",
                appHost + " wants to connect to " + account.display() +
                    ". It will be able to push signing requests into this wallet; every "
                    "one still passes the guard and signing review.",
                {});
            if (!accepted) {
                // Tell the dapp now; otherwise its login modal waits out the
                // request expiry.
                if (auto svc = controller_.currentService())
                    postEsrRejection(*svc->fetch(), (**callback).url,
                                     "Login was declined in TackleBox");
                return dk::err(dk::ErrorKind::Canceled, "declined");
            }

            auto signature =
                controller_.signIdentityDigest(account, resolved->signingDigest());
            if (!signature) return dk::err(signature.error());

            // Session key + channel.
            auto requestKey = dk::PrivateKey::generate(dk::KeyType::K1);
            if (!requestKey) return dk::err(requestKey.error());
            auto requestPub = requestKey->toPublic();
            if (!requestPub) return dk::err(requestPub.error());

            LinkSession session;
            session.id = uuid4();
            session.appName = appHost;
            session.chainId = account.chainId;
            session.actor = account.actor;
            session.permission = account.permission;
            session.requestKeyWif = requestKey->toString();
            session.channelId = uuid4();
            session.createdAt = nowSec();

            auto cb = resolved->getCallback({*signature});
            if (!cb || !*cb) return dk::err(dk::ErrorKind::Internal, "callback vanished");
            json payload = (**cb).payload;
            payload["link_ch"] = session.serviceUrl + "/" + session.channelId;
            payload["link_key"] = requestPub->toString();
            payload["link_name"] = "TackleBox";

            auto svc = controller_.currentService();
            if (!svc) return dk::err(dk::ErrorKind::Internal, "no chain service");
            dk::FetchRequest post;
            post.url = (**cb).url;
            post.method = "POST";
            post.body = payload.dump();
            post.headers = {{"Content-Type", "application/json"}};
            auto response = svc->fetch()->fetch(post);
            if (!response)
                return dk::err(dk::ErrorKind::Transport,
                               "could not reach the dapp callback: " +
                                   response.error().message);
            if (response->status < 200 || response->status >= 300)
                return dk::err(dk::ErrorKind::Api, "dapp callback answered HTTP " +
                                                       std::to_string(response->status));

            controller_.storeLinkSession(session);
            return session.appName;
        }();

        controller_.runner().postMain([this, outcome] {
            if (outcome) {
                controller_.toast(Toast::Success,
                                  "Linked to " + *outcome + "; listening for requests");
                sync();
            } else if (outcome.error().kind != dk::ErrorKind::Canceled) {
                controller_.toast(Toast::Error, "Link failed: " + outcome.error().message);
            }
        });
    });
}

void LinkService::listenLoop(LinkSession session, dk::CancelToken token) {
    auto requestKey = dk::PrivateKey::from(session.requestKeyWif);
    secureWipe(session.requestKeyWif.data(), session.requestKeyWif.size());
    if (!requestKey) {
        Log::error("link: session %s has an unusable request key", session.appName.c_str());
        return;
    }

    dk::CurlWebSocketProvider ws;
    dk::buoy::ListenerOptions options;
    options.channel = session.channelId;
    options.service = session.serviceUrl;
    options.webSocket = &ws;
    auto listener = dk::buoy::Listener::make(options);
    if (!listener) {
        Log::error("link: listener setup failed: %s", listener.error().message.c_str());
        return;
    }
    if (auto connected = listener->connect(); !connected) {
        Log::warn("link: initial connect failed (%s); receive loop will retry",
                  connected.error().message.c_str());
    }

    while (!token.cancelled()) {
        auto message = listener->receiveMessage(std::chrono::seconds(5), token);
        if (!message) {
            if (token.cancelled()) break;
            continue;  // timeouts and reconnects are routine
        }
        auto sealed = dk::Serializer::decode<dk::SealedMessage>(
            std::span<const uint8_t>(message->array.data(), message->array.size()));
        if (!sealed) {
            Log::warn("link: dropped a message that did not decode as sealed");
            continue;
        }
        auto opened =
            dk::unsealMessage(sealed->ciphertext, *requestKey, sealed->from, sealed->nonce);
        if (!opened) {
            Log::warn("link: could not unseal a message (wrong key?)");
            continue;
        }
        std::string content = trim(*opened);
        if (content.rfind("esr:", 0) != 0 && content.rfind("eosio:", 0) != 0) {
            Log::warn("link: unsealed content is not a signing request");
            continue;
        }
        Log::info("link: signing request received from %s", session.appName.c_str());
        controller_.runner().postMain([this, session, content] {
            controller_.toast(Toast::Info,
                              "Signing request from " + session.appName + " arrived");
            controller_.signEsrFromLink(session.id, content);
        });
    }
}

}  // namespace tb
