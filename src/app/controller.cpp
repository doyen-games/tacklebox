#include "app/controller.hpp"

#include <algorithm>
#include <fstream>
#include <set>
#include <sstream>

#include <dwarfkit/antelope/serializer.hpp>
#include <dwarfkit/core/hash.hpp>

#include <dwarfkit/plugins/transact/resource_provider.hpp>
#include <dwarfkit/session.hpp>
#include <dwarfkit/transport/curl_fetch_provider.hpp>

#include "app/autopilot_util.hpp"
#include "app/bridge.hpp"
#include "app/link.hpp"
#include "chain/netreg.hpp"
#include "core/clipboard.hpp"
#include "core/log.hpp"
#include "core/util.hpp"

namespace tb {

namespace dk = dwarfkit;

// Set while a schedule's transact runs on this worker thread: the guard then
// refuses to prompt and only the auto-sign fast path may sign.
static thread_local bool t_scheduledSign = false;

Controller::Controller(AppState& state, TaskRunner& runner)
    : state_(state), runner_(runner), link_(std::make_unique<LinkService>(*this)) {}

Controller::~Controller() = default;

void Controller::init() {
    state_.vaultExists = Vault::fileExists();
    state_.lastActivityMs = nowMs();
}

void Controller::noteActivity() { state_.lastActivityMs = nowMs(); }

void Controller::tick() {
    state_.workerPending = runner_.pending();

    // Autopilot + pinned data run only while unlocked; both are cheap when idle.
    static int64_t lastSlowTick = 0;
    if (nowMs() - lastSlowTick > 2000) {
        lastSlowTick = nowMs();
        tickSchedules();
        refreshPinnedQueries();
    }

    // Auto-lock after inactivity.
    if (state_.unlocked) {
        int minutes;
        {
            std::lock_guard<std::mutex> lock(vaultMutex_);
            minutes = vault_.security().autoLockMinutes;
        }
        if (minutes > 0 && nowMs() - state_.lastActivityMs > int64_t(minutes) * 60000) {
            // Never yank the vault away mid-signature.
            if (!state_.signPrompt && !state_.pluginPrompt) {
                lockVault();
                toast(Toast::Info, "Vault auto-locked after inactivity");
            }
        }
    }

    // Clipboard auto-clear: only wipe if it still holds what we put there.
    if (state_.clipboardClearAtMs && nowMs() >= state_.clipboardClearAtMs) {
        if (clipboardGet() == state_.clipboardArmed) clipboardSet("");
        state_.clipboardClearAtMs = 0;
        secureWipe(state_.clipboardArmed.data(), state_.clipboardArmed.size());
        state_.clipboardArmed.clear();
    }

    // Expire toasts.
    int64_t now = nowMs();
    std::erase_if(state_.toasts, [now](const Toast& t) {
        return now - t.bornMs > int64_t(t.ttlSec * 1000.0f);
    });
}

void Controller::shutdown() {
    link_->stopAll();
    broker_.cancelAll();
    if (state_.unlocked) {
        std::lock_guard<std::mutex> lock(vaultMutex_);
        vault_.lock();
    }
}

// --- snapshot ----------------------------------------------------------------

void Controller::refreshSnapshot() {
    std::lock_guard<std::mutex> lock(vaultMutex_);
    VaultSnapshot snap;
    snap.keys = vault_.keys();
    for (auto& k : snap.keys) {
        secureWipe(k.wif.data(), k.wif.size());
        k.wif.clear();  // secrets never enter UI state
    }
    snap.accounts = vault_.accounts();
    snap.networks = vault_.networks();
    snap.rules = vault_.rules();
    snap.security = vault_.security();
    snap.audit = vault_.audit();
    snap.pinnedQueries = vault_.pinnedQueries();
    snap.schedules = vault_.schedules();
    snap.linkSessions = vault_.linkSessions();
    for (auto& link : snap.linkSessions) {
        secureWipe(link.requestKeyWif.data(), link.requestKeyWif.size());
        link.requestKeyWif.clear();
    }
    snap.version = ++vaultVersion_;
    state_.vault = std::move(snap);
    state_.unlocked = vault_.unlocked();
    if (state_.selectedAccount >= static_cast<int>(state_.vault.accounts.size()))
        state_.selectedAccount = state_.vault.accounts.empty() ? -1 : 0;
    if (state_.selectedAccount < 0 && !state_.vault.accounts.empty())
        state_.selectedAccount = 0;
    link_->sync();
}

void Controller::refreshSnapshotFromWorker() {
    runner_.postMain([this] { refreshSnapshot(); });
}

// --- vault lifecycle ---------------------------------------------------------

void Controller::createVault(const std::string& password) {
    state_.busyUnlock = true;
    state_.unlockError.clear();
    runner_.run([this, password] {
        SecureBytes pw(password);
        dk::Result<void> result = [&]() -> dk::Result<void> {
            std::lock_guard<std::mutex> lock(vaultMutex_);
            return vault_.create(pw);
        }();
        runner_.postMain([this, result] {
            state_.busyUnlock = false;
            if (!result) {
                state_.unlockError = result.error().message;
                return;
            }
            state_.vaultExists = true;
            refreshSnapshot();
            state_.page = Page::Setup;  // the first-run guide picks chains + keys
            toast(Toast::Success, "Vault created");
            noteActivity();
        });
    });
}

void Controller::unlockVault(const std::string& password) {
    state_.busyUnlock = true;
    state_.unlockError.clear();
    runner_.run([this, password] {
        SecureBytes pw(password);
        dk::Result<void> result = [&]() -> dk::Result<void> {
            std::lock_guard<std::mutex> lock(vaultMutex_);
            return vault_.unlock(pw);
        }();
        runner_.postMain([this, result] {
            state_.busyUnlock = false;
            if (!result) {
                state_.unlockError = result.error().message;
                return;
            }
            refreshSnapshot();
            // Return to the chain + account in use last time.
            std::string lastAccount, lastChain;
            {
                std::lock_guard<std::mutex> lock(vaultMutex_);
                lastAccount = vault_.lastAccount();
                lastChain = vault_.lastChain();
            }
            if (!lastAccount.empty()) {
                for (size_t i = 0; i < state_.vault.accounts.size(); ++i) {
                    const AccountRef& a = state_.vault.accounts[i];
                    if (a.chainId + "|" + a.actor + "|" + a.permission == lastAccount) {
                        state_.selectedAccount = static_cast<int>(i);
                        state_.selectedChainId = a.chainId;
                    }
                }
            }
            if (state_.selectedChainId.empty()) state_.selectedChainId = lastChain;
            if (state_.selectedChainId.empty() && state_.currentAccount())
                state_.selectedChainId = state_.currentAccount()->chainId;
            // A vault with no keys yet lands in the first-run guide.
            state_.page = state_.vault.keys.empty() ? Page::Setup : Page::Dashboard;
            noteActivity();
            refreshAccount(false);
        });
    });
}

void Controller::lockVault() {
    {
        std::lock_guard<std::mutex> lock(vaultMutex_);
        vault_.lock();
    }
    state_.unlocked = false;
    state_.vault = VaultSnapshot{};
    state_.accountData.clear();
    state_.pinnedData.clear();
    state_.schedulesInFlight.clear();
    state_.signPrompt.reset();
    state_.pluginPrompt.reset();
    state_.contracts = ContractsViewState{};
    state_.resources = ResourcesViewState{};
    state_.governance = GovernanceViewState{};
    state_.msig = MsigViewState{};
    state_.deploy = DeployViewState{};
    state_.page = Page::Dashboard;
    link_->sync();
}

void Controller::changePassword(const std::string& current, const std::string& next,
                                std::function<void(bool, std::string)> done) {
    runner_.run([this, current, next, done] {
        SecureBytes cur(current), nxt(next);
        dk::Result<void> result = [&]() -> dk::Result<void> {
            std::lock_guard<std::mutex> lock(vaultMutex_);
            return vault_.changePassword(cur, nxt);
        }();
        runner_.postMain([done, ok = bool(result),
                          msg = result ? std::string() : result.error().message] {
            done(ok, msg);
        });
    });
}

void Controller::verifyPassword(const std::string& password, std::function<void(bool)> done) {
    runner_.run([this, password, done] {
        SecureBytes pw(password);
        bool ok = false;
        {
            std::lock_guard<std::mutex> lock(vaultMutex_);
            // changePassword with same password would rotate the salt; verify
            // by attempting a scoped unlock of the on-disk envelope instead.
            Vault probe;
            ok = bool(probe.unlock(pw));
            probe.lock();
        }
        runner_.postMain([done, ok] { done(ok); });
    });
}

// --- keys --------------------------------------------------------------------

void Controller::importKey(const std::string& wif, const std::string& label) {
    runner_.run([this, wif, label] {
        dk::Result<std::string> result = [&]() -> dk::Result<std::string> {
            std::lock_guard<std::mutex> lock(vaultMutex_);
            return vault_.importKey(wif, label);
        }();
        runner_.postMain([this, result] {
            if (!result) {
                toast(Toast::Error, result.error().message);
                return;
            }
            refreshSnapshot();
            toast(Toast::Success, "Key imported: " + middleEllipsis(*result));
        });
    });
}

void Controller::generateKey(const std::string& label) {
    runner_.run([this, label] {
        dk::Result<std::string> result = [&]() -> dk::Result<std::string> {
            std::lock_guard<std::mutex> lock(vaultMutex_);
            return vault_.generateKey(label);
        }();
        runner_.postMain([this, result] {
            if (!result) {
                toast(Toast::Error, result.error().message);
                return;
            }
            refreshSnapshot();
            toast(Toast::Success, "New key generated: " + middleEllipsis(*result));
        });
    });
}

void Controller::removeKey(const std::string& pub) {
    runner_.run([this, pub] {
        bool removed;
        {
            std::lock_guard<std::mutex> lock(vaultMutex_);
            removed = vault_.removeKey(pub);
        }
        runner_.postMain([this, removed] {
            refreshSnapshot();
            toast(removed ? Toast::Info : Toast::Error,
                  removed ? "Key removed; linked accounts became watch-only"
                          : "Key not found");
        });
    });
}

void Controller::revealKey(const std::string& pub, const std::string& password,
                           std::function<void(std::string, std::string)> done) {
    runner_.run([this, pub, password, done] {
        SecureBytes pw(password);
        std::string wif, error;
        {
            Vault probe;  // verify against disk so a stale in-memory state can't leak
            if (probe.unlock(pw)) {
                wif = probe.wifFor(pub);
                if (wif.empty()) error = "key not found in vault";
                probe.lock();
            } else {
                error = "wrong password";
            }
        }
        runner_.postMain([done, wif, error] { done(wif, error); });
    });
}

// --- accounts ----------------------------------------------------------------

void Controller::addAccount(const std::string& chainId, const std::string& actor,
                            const std::string& permission) {
    auto svc = service(chainId);
    if (!svc) {
        toast(Toast::Error, "No such network configured");
        return;
    }
    runner_.run([this, svc, chainId, actor, permission] {
        auto account = svc->fetchAccount(actor);
        std::string error, matchedKey;
        bool watch = true;
        if (!account) {
            error = "account not found: " + account.error().message;
        } else {
            // Find a vault key present in the requested permission's authority.
            std::set<std::string> vaultKeys;
            {
                std::lock_guard<std::mutex> lock(vaultMutex_);
                for (const auto& k : vault_.keys()) vaultKeys.insert(k.pub);
            }
            const json& perms = account->raw.value("permissions", json::array());
            for (const auto& p : perms) {
                if (p.value("perm_name", "") != permission) continue;
                for (const auto& k : p["required_auth"].value("keys", json::array())) {
                    std::string keyStr = k.value("key", "");
                    // Normalize legacy EOS... keys through dwarfkit for comparison.
                    auto parsed = dk::PublicKey::from(keyStr);
                    std::string normalized = parsed ? parsed->toString() : keyStr;
                    if (vaultKeys.count(normalized)) {
                        matchedKey = normalized;
                        watch = false;
                        break;
                    }
                }
            }
        }
        runner_.postMain([this, chainId, actor, permission, error, matchedKey, watch] {
            if (!error.empty()) {
                toast(Toast::Error, error);
                return;
            }
            {
                std::lock_guard<std::mutex> lock(vaultMutex_);
                vault_.addAccount({chainId, actor, permission, matchedKey, watch});
            }
            refreshSnapshot();
            for (size_t i = 0; i < state_.vault.accounts.size(); ++i)
                if (state_.vault.accounts[i].chainId == chainId &&
                    state_.vault.accounts[i].actor == actor &&
                    state_.vault.accounts[i].permission == permission)
                    state_.selectedAccount = static_cast<int>(i);
            toast(Toast::Success, watch ? "Watch-only account added (no vault key in authority)"
                                        : "Account added and linked to vault key");
            refreshAccount(true);
        });
    });
}

void Controller::removeAccount(const AccountRef& account) {
    std::lock_guard<std::mutex> lock(vaultMutex_);
    vault_.removeAccount(account.chainId, account.actor, account.permission);
    runner_.postMain([this] { refreshSnapshot(); });
}

void Controller::selectAccount(int index) {
    if (index < 0 || index >= static_cast<int>(state_.vault.accounts.size())) return;
    const AccountRef& a = state_.vault.accounts[static_cast<size_t>(index)];
    bool chainChanged = a.chainId != state_.selectedChainId;
    state_.selectedAccount = index;
    state_.selectedChainId = a.chainId;
    {
        std::lock_guard<std::mutex> lock(vaultMutex_);
        if (vault_.unlocked()) {
            vault_.setLastAccount(a.chainId + "|" + a.actor + "|" + a.permission);
            vault_.setLastChain(a.chainId);
        }
    }
    if (chainChanged) {
        // Chain switch invalidates chain-scoped view data.
        state_.resources = ResourcesViewState{};
        state_.governance = GovernanceViewState{};
        state_.explore = ExploreViewState{};
        state_.assets = AssetsViewState{};
    }
    refreshAccount(false);
}

void Controller::selectChain(const std::string& chainId) {
    if (chainId.empty() || chainId == state_.selectedChainId) return;
    state_.selectedChainId = chainId;
    {
        std::lock_guard<std::mutex> lock(vaultMutex_);
        if (vault_.unlocked()) vault_.setLastChain(chainId);
    }
    // Prefer the last-used account on this chain, else the first, else none.
    std::string lastAccount;
    {
        std::lock_guard<std::mutex> lock(vaultMutex_);
        lastAccount = vault_.lastAccount();
    }
    int pick = -1;
    for (size_t i = 0; i < state_.vault.accounts.size(); ++i) {
        const AccountRef& a = state_.vault.accounts[i];
        if (a.chainId != chainId) continue;
        if (pick < 0) pick = static_cast<int>(i);
        if (a.chainId + "|" + a.actor + "|" + a.permission == lastAccount)
            pick = static_cast<int>(i);
    }
    state_.selectedAccount = pick;
    if (pick >= 0) {
        const AccountRef& a = state_.vault.accounts[static_cast<size_t>(pick)];
        std::lock_guard<std::mutex> lock(vaultMutex_);
        if (vault_.unlocked())
            vault_.setLastAccount(a.chainId + "|" + a.actor + "|" + a.permission);
    }
    state_.resources = ResourcesViewState{};
    state_.governance = GovernanceViewState{};
    state_.explore = ExploreViewState{};
    state_.assets = AssetsViewState{};
    noteActivity();
    if (pick >= 0) refreshAccount(false);
}

void Controller::enableNetworks(const std::vector<NetworkDef>& enable) {
    {
        std::lock_guard<std::mutex> lock(vaultMutex_);
        // Drop known presets the user unchecked; leave custom chains alone.
        for (const auto& preset : allPresets()) {
            bool wanted = false;
            for (const auto& net : enable)
                if (net.chainId == preset.chainId) wanted = true;
            if (!wanted && vault_.network(preset.chainId))
                vault_.removeNetwork(preset.chainId);
        }
        for (const auto& net : enable)
            if (!vault_.network(net.chainId)) vault_.upsertNetwork(net);
    }
    refreshSnapshot();
    // Keep the selection pointing at an enabled chain.
    if (!state_.currentNetwork() || state_.vault.networks.empty()) {
        state_.selectedChainId.clear();
    } else if (state_.selectedChainId.empty()) {
        state_.selectedChainId = state_.vault.networks[0].chainId;
    }
    toast(Toast::Success,
          std::to_string(state_.vault.networks.size()) + " chain(s) enabled");
}

void Controller::importKeysBulk(const std::string& text) {
    runner_.run([this, text] {
        int imported = 0, duplicates = 0, failed = 0;
        {
            std::lock_guard<std::mutex> lock(vaultMutex_);
            size_t start = 0;
            while (start <= text.size()) {
                size_t nl = text.find('\n', start);
                std::string line = nl == std::string::npos ? text.substr(start)
                                                           : text.substr(start, nl - start);
                start = nl == std::string::npos ? text.size() + 1 : nl + 1;
                line = trim(line);
                if (line.empty()) continue;
                auto result = vault_.importKey(line, "imported");
                if (result)
                    ++imported;
                else if (result.error().message.find("already in vault") != std::string::npos)
                    ++duplicates;
                else
                    ++failed;
            }
        }
        runner_.postMain([this, imported, duplicates, failed] {
            refreshSnapshot();
            std::string summary = std::to_string(imported) + " key(s) imported";
            if (duplicates) summary += ", " + std::to_string(duplicates) + " already present";
            if (failed) summary += ", " + std::to_string(failed) + " not valid keys";
            toast(failed && !imported ? Toast::Error : Toast::Success, summary);
        });
    });
}

void Controller::discoverAccounts() {
    if (state_.discovery.running) return;
    state_.discovery = DiscoveryState{};
    state_.discovery.running = true;

    std::vector<std::string> keys;
    for (const auto& key : state_.vault.keys) keys.push_back(key.pub);
    std::vector<NetworkDef> networks = state_.vault.networks;
    if (keys.empty() || networks.empty()) {
        state_.discovery.running = false;
        state_.discovery.log.push_back(keys.empty() ? "no keys in the vault yet"
                                                    : "no chains enabled yet");
        return;
    }

    runner_.run([this, keys, networks] {
        for (const auto& net : networks) {
            auto svc = service(net.chainId);
            std::string line;
            int found = 0;
            if (!svc) {
                line = net.name + ": no service";
            } else if (auto rows = svc->discoverByKeys(keys); !rows) {
                line = net.name + ": " + rows.error().message;
            } else {
                std::set<std::string> keySet(keys.begin(), keys.end());
                for (const auto& row : rows->value("accounts", json::array())) {
                    std::string actor = row.value("account_name", "");
                    std::string permission = row.value("permission_name", "");
                    std::string authKey = row.value("authorizing_key", "");
                    if (actor.empty() || permission.empty()) continue;
                    // Normalize legacy key forms for the vault link.
                    std::string normalized = authKey;
                    if (auto parsed = dk::PublicKey::from(authKey))
                        normalized = parsed->toString();
                    if (!keySet.count(normalized)) continue;
                    {
                        std::lock_guard<std::mutex> lock(vaultMutex_);
                        size_t before = vault_.accounts().size();
                        vault_.addAccount({net.chainId, actor, permission, normalized, false});
                        if (vault_.accounts().size() > before) ++found;
                    }
                }
                line = net.name + ": " + std::to_string(found) + " account(s)";
            }
            runner_.postMain([this, line, found] {
                state_.discovery.log.push_back(line);
                state_.discovery.added += found;
                refreshSnapshot();
            });
        }
        runner_.postMain([this] {
            state_.discovery.running = false;
            if (state_.discovery.added > 0) {
                toast(Toast::Success, "Discovery added " +
                                          std::to_string(state_.discovery.added) +
                                          " account(s)");
                if (state_.selectedAccount < 0 && !state_.vault.accounts.empty())
                    selectAccount(0);
            } else {
                toast(Toast::Info, "Discovery finished; no new accounts found");
            }
        });
    });
}

// --- chain data --------------------------------------------------------------

std::shared_ptr<ChainService> Controller::currentService() {
    const NetworkDef* net = state_.currentNetwork();
    return net ? service(net->chainId) : nullptr;
}

std::shared_ptr<ChainService> Controller::service(const std::string& chainId) {
    NetworkDef net;
    {
        std::lock_guard<std::mutex> lock(vaultMutex_);
        NetworkDef* found = vault_.network(chainId);
        if (!found) return nullptr;
        net = *found;
    }
    std::lock_guard<std::mutex> lock(servicesMutex_);
    auto it = services_.find(chainId);
    if (it != services_.end()) {
        it->second->setNetwork(net);
        return it->second;
    }
    auto svc = std::make_shared<ChainService>(net);
    services_[chainId] = svc;
    return svc;
}

void Controller::refreshAccount(bool force) {
    const AccountRef* account = state_.currentAccount();
    if (!account) return;
    std::string key = state_.accountKey(*account);
    AccountData& data = state_.accountData[key];
    if (data.loading) return;
    if (!force && data.loaded && nowSec() - data.snap.fetchedAt < 30) return;

    auto svc = service(account->chainId);
    if (!svc) return;
    data.loading = true;
    std::string actor = account->actor;
    runner_.run([this, svc, actor, key] {
        auto snap = svc->fetchAccount(actor);
        runner_.postMain([this, key, snap] {
            AccountData& d = state_.accountData[key];
            d.loading = false;
            if (snap) {
                d.snap = *snap;
                d.loaded = true;
                d.error.clear();
            } else {
                d.error = snap.error().message;
            }
        });
    });
}

void Controller::probeEndpoints(const std::string& chainId) {
    auto svc = service(chainId);
    if (!svc || state_.busyHealth) return;
    state_.busyHealth = true;
    NetworkDef net = svc->net();
    runner_.run([this, svc, chainId, net] {
        std::vector<EndpointHealth> results;
        for (NodeType type : {NodeType::Rpc, NodeType::Atomic, NodeType::Hyperion,
                              NodeType::Light}) {
            for (const auto& node : net.list(type).nodes) {
                EndpointHealth health = svc->probe(type, node.url);
                health.nickname = node.nickname;
                results.push_back(std::move(health));
            }
        }
        runner_.postMain([this, chainId, results] {
            state_.health[chainId] = results;
            state_.busyHealth = false;
        });
    });
}

void Controller::addNetwork(const NetworkDef& net) {
    for (NodeType type :
         {NodeType::Rpc, NodeType::Atomic, NodeType::Hyperion, NodeType::Light}) {
        for (const auto& node : net.list(type).nodes) {
            std::string why;
            if (!endpointAllowed(node.url, &why)) {
                toast(Toast::Error, why);
                return;
            }
        }
    }
    {
        std::lock_guard<std::mutex> lock(vaultMutex_);
        vault_.upsertNetwork(net);
    }
    refreshSnapshot();
    toast(Toast::Success, "Network saved: " + net.name);
}

void Controller::removeNetwork(const std::string& chainId) {
    {
        std::lock_guard<std::mutex> lock(vaultMutex_);
        vault_.removeNetwork(chainId);
    }
    refreshSnapshot();
}

void Controller::loadContract(const std::string& account) {
    const AccountRef* current = state_.currentAccount();
    const NetworkDef* net = state_.currentNetwork();
    if (!current || !net) {
        toast(Toast::Warn, "Select an account first");
        return;
    }
    auto svc = service(net->chainId);
    if (!svc) return;
    state_.contracts.loading = true;
    state_.contracts.error.clear();
    state_.contracts.account = account;
    state_.contracts.abi.reset();
    state_.contracts.tableRows = json();
    runner_.run([this, svc, account] {
        auto abi = svc->fetchAbi(account);
        auto hashes = svc->fetchContractHashes(account, 60);
        runner_.postMain([this, account, abi, hashes] {
            state_.contracts.loading = false;
            if (state_.contracts.account != account) return;  // superseded
            if (!abi) {
                state_.contracts.error = abi.error().message;
                return;
            }
            state_.contracts.abi = std::make_shared<dk::ABI>(*abi);
            if (hashes) {
                state_.contracts.codeHash = hashes->codeHash;
                state_.contracts.abiHash = hashes->abiHash;
            }
        });
    });
}

void Controller::loadTableRows(const std::string& contract, const std::string& table,
                               const std::string& scope) {
    const NetworkDef* net = state_.currentNetwork();
    if (!net) return;
    auto svc = service(net->chainId);
    if (!svc) return;
    state_.contracts.tableLoading = true;
    state_.contracts.tableError.clear();
    state_.contracts.tableName = table;
    state_.contracts.tableScope = scope;
    json params{{"json", true},
                {"code", contract},
                {"table", table},
                {"scope", scope.empty() ? contract : scope},
                {"limit", 50}};
    runner_.run([this, svc, params, table] {
        auto rows = svc->fetchTableRows(params);
        runner_.postMain([this, rows, table] {
            state_.contracts.tableLoading = false;
            if (state_.contracts.tableName != table) return;
            if (!rows) {
                state_.contracts.tableError = rows.error().message;
                return;
            }
            state_.contracts.tableRows = *rows;
        });
    });
}

// --- block explorer ----------------------------------------------------------

void Controller::exploreOverview(bool force) {
    const NetworkDef* net = state_.currentNetwork();
    if (!net) return;
    ExploreViewState& ex = state_.explore;
    if (ex.loadingOverview) return;
    if (!force && nowMs() - ex.infoFetchedAt < 3000) return;  // gentle poll
    auto svc = service(net->chainId);
    if (!svc) return;
    ex.loadingOverview = true;
    runner_.run([this, svc] {
        auto info = svc->fetchInfo();
        std::vector<json> blocks;
        std::string error;
        if (info) {
            uint32_t head = info->value("head_block_num", 0u);
            // A short strip of recent blocks, summarized to what the list shows.
            for (uint32_t i = 0; i < 8 && head > i; ++i) {
                auto block = svc->fetchBlock(json(head - i));
                if (!block) break;
                size_t txCount = block->contains("transactions")
                                     ? (*block)["transactions"].size()
                                     : 0;
                blocks.push_back(json{{"block_num", block->value("block_num", 0u)},
                                      {"id", block->value("id", "")},
                                      {"producer", block->value("producer", "")},
                                      {"timestamp", block->value("timestamp", "")},
                                      {"tx_count", txCount}});
            }
        } else {
            error = info.error().message;
        }
        runner_.postMain([this, info = std::move(info), blocks = std::move(blocks),
                          error = std::move(error)] {
            ExploreViewState& view = state_.explore;
            view.loadingOverview = false;
            view.overviewError = error;
            if (info) {
                view.info = *info;
                view.infoFetchedAt = nowMs();
                view.recentBlocks = blocks;
            }
        });
    });
}

void Controller::exploreAccount(const std::string& actor) {
    const NetworkDef* net = state_.currentNetwork();
    if (!net) return;
    auto svc = service(net->chainId);
    if (!svc) return;
    ExploreViewState& ex = state_.explore;
    ex.mode = ExploreViewState::Mode::Account;
    ex.accountName = actor;
    ex.loadingAccount = true;
    ex.accountError.clear();
    ex.actionsError.clear();
    ex.account = json();
    ex.actions = json();
    runner_.run([this, svc, actor] {
        auto account = svc->fetchAccount(actor);
        // History is optional on public nodes; failure is informational.
        auto actions = svc->fetchActions(actor, -1, -30);
        runner_.postMain([this, actor, account = std::move(account),
                          actions = std::move(actions)] {
            ExploreViewState& view = state_.explore;
            if (view.accountName != actor) return;
            view.loadingAccount = false;
            if (account)
                view.account = account->raw;
            else
                view.accountError = account.error().message;
            if (actions)
                view.actions = *actions;
            else
                view.actionsError = "this endpoint does not serve account history";
        });
    });
}

void Controller::exploreBlock(const std::string& numOrId) {
    const NetworkDef* net = state_.currentNetwork();
    if (!net) return;
    auto svc = service(net->chainId);
    if (!svc) return;
    ExploreViewState& ex = state_.explore;
    ex.mode = ExploreViewState::Mode::Block;
    ex.blockQuery = numOrId;
    ex.loadingBlock = true;
    ex.blockError.clear();
    ex.block = json();
    // Numeric strings travel as numbers so nodeos treats them as heights.
    json query = numOrId;
    if (!numOrId.empty() && numOrId.find_first_not_of("0123456789") == std::string::npos) {
        try {
            query = static_cast<uint32_t>(std::stoul(numOrId));
        } catch (...) {}
    }
    runner_.run([this, svc, query, numOrId] {
        auto block = svc->fetchBlock(query);
        runner_.postMain([this, numOrId, block = std::move(block)] {
            ExploreViewState& view = state_.explore;
            if (view.blockQuery != numOrId) return;
            view.loadingBlock = false;
            if (block)
                view.block = *block;
            else
                view.blockError = block.error().message;
        });
    });
}

void Controller::exploreTransaction(const std::string& txId) {
    const NetworkDef* net = state_.currentNetwork();
    if (!net) return;
    auto svc = service(net->chainId);
    if (!svc) return;
    ExploreViewState& ex = state_.explore;
    ex.mode = ExploreViewState::Mode::Transaction;
    ex.txId = txId;
    ex.loadingTx = true;
    ex.txError.clear();
    ex.txStatus = json();
    ex.txDetail = json();
    ex.txFromBlock = json();
    runner_.run([this, svc, txId] {
        auto status = svc->fetchTxStatus(txId);
        auto detail = svc->fetchTransaction(txId, std::nullopt);
        json fromBlock;
        // When history is unavailable but the status names a block, dig the
        // transaction out of the block itself.
        if (!detail && status) {
            uint32_t blockNum = status->value("block_number", 0u);
            if (blockNum == 0) blockNum = status->value("block_num", 0u);
            if (blockNum > 0) {
                if (auto block = svc->fetchBlock(json(blockNum));
                    block && block->contains("transactions"))
                    for (const auto& tx : (*block)["transactions"]) {
                        if (tx.contains("trx") && tx["trx"].is_object() &&
                            tx["trx"].value("id", "") == txId)
                            fromBlock = tx;
                    }
            }
        }
        runner_.postMain([this, txId, status = std::move(status), detail = std::move(detail),
                          fromBlock = std::move(fromBlock)] {
            ExploreViewState& view = state_.explore;
            if (view.txId != txId) return;
            view.loadingTx = false;
            if (status)
                view.txStatus = *status;
            if (detail)
                view.txDetail = *detail;
            else if (!fromBlock.is_null())
                view.txFromBlock = fromBlock;
            if (!status && !detail && fromBlock.is_null())
                view.txError =
                    "transaction not found (it may be old and this endpoint keeps no "
                    "history)";
        });
    });
}

void Controller::exploreSearch(const std::string& rawQuery) {
    std::string query = toLower(trim(rawQuery));
    if (query.empty()) return;
    bool allHex = query.size() == 64 &&
                  query.find_first_not_of("0123456789abcdef") == std::string::npos;
    bool allDigits = query.find_first_not_of("0123456789") == std::string::npos;
    if (allHex)
        exploreTransaction(query);
    else if (allDigits)
        exploreBlock(query);
    else
        exploreAccount(query);
}

// --- NFT gallery -------------------------------------------------------------

void Controller::loadAssets(const std::string& owner, bool force) {
    const NetworkDef* net = state_.currentNetwork();
    if (!net) return;
    auto svc = service(net->chainId);
    if (!svc) return;
    AssetsViewState& av = state_.assets;
    if (av.loading) return;
    if (!force && av.owner == owner && nowSec() - av.fetchedAt < 60) return;
    av.loading = true;
    av.owner = owner;
    av.error.clear();
    runner_.run([this, svc, owner] {
        auto assets = svc->fetchAtomicAssets(
            json{{"owner", owner}, {"limit", 60}, {"order", "desc"}, {"sort", "transferred"}});
        runner_.postMain([this, owner, assets = std::move(assets)] {
            AssetsViewState& view = state_.assets;
            if (view.owner != owner) return;
            view.loading = false;
            view.fetchedAt = nowSec();
            if (assets)
                view.assets = *assets;
            else
                view.error = assets.error().message;
        });
    });
}

// --- vault portability -------------------------------------------------------

void Controller::exportVault(const std::string& destPath) {
    runner_.run([this, destPath] {
        auto result = Vault::exportTo(destPath);
        runner_.postMain([this, result, destPath] {
            if (result)
                toast(Toast::Success, "Vault exported (still encrypted) to " + destPath);
            else
                toast(Toast::Error, result.error().message);
        });
    });
}

void Controller::importVault(const std::string& srcPath) {
    runner_.run([this, srcPath] {
        dwarfkit::Result<void> result = [&]() -> dwarfkit::Result<void> {
            std::lock_guard<std::mutex> lock(vaultMutex_);
            vault_.lock();
            return Vault::importFrom(srcPath);
        }();
        runner_.postMain([this, result] {
            if (!result) {
                toast(Toast::Error, result.error().message);
                return;
            }
            state_.vaultExists = true;
            state_.unlocked = false;
            state_.vault = VaultSnapshot{};
            state_.accountData.clear();
            state_.unlockError.clear();
            toast(Toast::Success,
                  "Vault imported. Unlock it with the password it was sealed with; the "
                  "previous vault was kept as a backup.");
        });
    });
}

// --- transactions ------------------------------------------------------------

std::unique_ptr<dk::Session> Controller::makeSession(const AccountRef& account) {
    auto svc = service(account.chainId);
    if (!svc) return nullptr;
    auto permission = dk::PermissionLevel::from(account.actor + "@" + account.permission);
    if (!permission) return nullptr;

    dk::SessionArgs args;
    args.chain = svc->chainDef();
    args.permissionLevel = *permission;
    args.walletPlugin = std::make_shared<VaultWalletPlugin>(*this, account.pubKey);
    args.appName = "TackleBox";

    dk::SessionOptions options;
    options.fetch = svc->fetch();
    options.abiCache = svc->abiCache();
    options.ui = std::make_shared<TackleUI>(*this);

    // Fuel-style cosigning: the provider may prepend a cosigning action (or
    // quote a fee, surfaced through the plugin prompt) so low-resource
    // accounts can still transact. Opt-in via Settings.
    bool useResourceProvider;
    {
        std::lock_guard<std::mutex> lock(vaultMutex_);
        useResourceProvider = vault_.security().useResourceProvider;
    }
    if (useResourceProvider)
        options.transactPlugins = std::vector<std::shared_ptr<dk::AbstractTransactPlugin>>{
            std::make_shared<dk::TransactPluginResourceProvider>()};

    return std::make_unique<dk::Session>(args, options);
}

void Controller::transactAsync(const AccountRef account, dk::TransactArgs args,
                               const std::string& flowName, bool* busyFlag) {
    if (account.watch || account.pubKey.empty()) {
        toast(Toast::Warn, "This is a watch-only account; add its key to sign");
        return;
    }
    if (busyFlag) *busyFlag = true;
    runner_.run([this, account, args = std::move(args), flowName, busyFlag] {
        auto session = makeSession(account);
        if (!session) {
            runner_.postMain([this, busyFlag] {
                if (busyFlag) *busyFlag = false;
                toast(Toast::Error, "Could not build a session for this account");
            });
            return;
        }
        auto result = session->transact(args);
        runner_.postMain([this, busyFlag, flowName, result = std::move(result)] {
            if (busyFlag) *busyFlag = false;
            state_.pipelineStatus.clear();
            if (!result) {
                if (result.error().kind == dk::ErrorKind::Canceled)
                    toast(Toast::Info, flowName + " rejected");
                else
                    toast(Toast::Error, flowName + " failed: " + result.error().message);
                return;
            }
            std::string txId;
            if (result->response && result->response->contains("transaction_id"))
                txId = result->response->value("transaction_id", "");
            toast(Toast::Success,
                  flowName + " confirmed" + (txId.empty() ? "" : "  " + middleEllipsis(txId, 10, 6)));
            refreshSnapshot();  // audit entry / rule counters changed
            refreshAccount(true);
        });
    });
}

void Controller::sendTransfer(const std::string& to, const std::string& quantity,
                              const std::string& memo, const std::string& tokenContract) {
    const AccountRef* account = state_.currentAccount();
    if (!account) return;
    json action{{"account", tokenContract.empty() ? "eosio.token" : tokenContract},
                {"name", "transfer"},
                {"authorization", json::array({{{"actor", account->actor},
                                                {"permission", account->permission}}})},
                {"data",
                 {{"from", account->actor}, {"to", to}, {"quantity", quantity}, {"memo", memo}}}};
    dk::TransactArgs args;
    args.action = action;
    transactAsync(*account, std::move(args), "Transfer", &state_.busyTransfer);
}

void Controller::runContractAction(const std::string& contract, const std::string& action,
                                   const json& data) {
    const AccountRef* account = state_.currentAccount();
    if (!account) return;
    json actionJson{{"account", contract},
                    {"name", action},
                    {"authorization", json::array({{{"actor", account->actor},
                                                    {"permission", account->permission}}})},
                    {"data", data}};
    dk::TransactArgs args;
    args.action = actionJson;
    transactAsync(*account, std::move(args), contract + "::" + action, &state_.busyContract);
}

void Controller::signEsr(const std::string& uri) {
    const AccountRef* account = state_.currentAccount();
    if (!account) return;
    dk::TransactArgs args;
    args.request = trim(uri);
    transactAsync(*account, std::move(args), "Signing request", &state_.busyEsr);
}

void Controller::resolveSignPrompt(bool approved) {
    auto prompt = state_.signPrompt;
    if (!prompt) return;
    prompt->approved = approved;
    state_.signPrompt.reset();
    broker_.resolve(prompt->id, true);
    noteActivity();
}

void Controller::resolvePluginPrompt(bool accepted) {
    auto prompt = state_.pluginPrompt;
    if (!prompt) return;
    prompt->accepted = accepted;
    state_.pluginPrompt.reset();
    broker_.resolve(prompt->id, true);
    noteActivity();
}

// --- the guard ---------------------------------------------------------------

dk::Result<dk::WalletPluginSignResponse> Controller::guardedSign(
    const dk::ResolvedSigningRequest& resolved, dk::TransactContext& context,
    const std::string& publicKey) {
    const std::string chainId = context.chain.id.hexString();
    const std::string signerActor = resolved.signer.actor.toString();
    const std::string signerPermission = resolved.signer.permission.toString();

    // 1. Decoded actions -> guard inputs.
    std::vector<guard::ActionInput> guardActions;
    std::vector<guard::RiskActionInput> riskActions;
    for (const auto& action : resolved.resolvedTransaction.actions) {
        guard::ActionInput input;
        input.contract = action.account.toString();
        input.action = action.name.toString();
        input.data = action.data;
        guardActions.push_back(input);
        riskActions.push_back({input.contract, input.action, input.data});
    }
    if (guardActions.empty())
        return dk::err(dk::ErrorKind::Invalid, "refusing to sign a transaction with no actions");

    // 2. Fresh contract hashes for every touched contract; a pin is only as
    //    good as the freshness of what it is compared against.
    auto svc = service(chainId);
    bool allHashesFetched = true;
    std::map<std::string, ContractHashes> hashes;
    for (auto& input : guardActions) {
        if (hashes.count(input.contract)) {
            input.codeHash = hashes[input.contract].codeHash;
            input.abiHash = hashes[input.contract].abiHash;
            continue;
        }
        if (svc) {
            auto fetched = svc->fetchContractHashes(input.contract, /*maxAgeSec=*/0);
            if (fetched) {
                hashes[input.contract] = *fetched;
                input.codeHash = fetched->codeHash;
                input.abiHash = fetched->abiHash;
                continue;
            }
        }
        allHashesFetched = false;
    }

    // 3. Evaluate whitelist + risk against a consistent vault snapshot.
    std::vector<guard::WhitelistRule> rules;
    SecurityPrefs security;
    guard::RiskContext riskContext;
    riskContext.signerActor = signerActor;
    {
        std::lock_guard<std::mutex> lock(vaultMutex_);
        rules = vault_.rules();
        security = vault_.security();
        for (const auto& rule : vault_.rules()) riskContext.knownContracts.insert(rule.contract);
        for (const auto& entry : vault_.audit()) {
            size_t sep = entry.summary.find("::");
            if (sep != std::string::npos)
                riskContext.knownContracts.insert(entry.summary.substr(0, sep));
        }
    }
    // Core balance for drain detection, from the UI cache if fresh enough.
    // (accountData is main-thread state; reading a copy via postMain would be
    // overkill - the audit-log path plus balances fetched at prompt build time
    // keep this advisory only.)

    auto evaluation =
        guard::evaluate(rules, chainId, signerActor, signerPermission, guardActions);
    auto risks = guard::analyze(riskActions, riskContext);

    // 4. Persist stale pins immediately: the rule must not fast-path again.
    if (!evaluation.stale.empty()) {
        std::lock_guard<std::mutex> lock(vaultMutex_);
        for (const auto& observation : evaluation.stale) {
            if (auto* rule = vault_.rule(observation.ruleId)) {
                rule->status = guard::RuleStatus::Stale;
                rule->observedCodeHash = observation.observedCodeHash;
                rule->observedAbiHash = observation.observedAbiHash;
            }
        }
        (void)vault_.save();
        refreshSnapshotFromWorker();
        runner_.postMain([this] {
            toast(Toast::Warn,
                  "A whitelisted contract changed on-chain; its rule was suspended "
                  "until you re-approve it");
        });
    }

    bool hasCriticalRisk = std::any_of(risks.begin(), risks.end(), [](const guard::RiskFlag& f) {
        return f.severity == guard::RiskSeverity::Critical;
    });

    // Summary line for the audit log.
    std::ostringstream summaryStream;
    summaryStream << guardActions[0].contract << "::" << guardActions[0].action;
    if (guardActions.size() > 1) summaryStream << " (+" << guardActions.size() - 1 << " more)";
    const std::string summary = summaryStream.str();
    const std::string signer = signerActor + "@" + signerPermission;

    auto signNow = [&]() -> dk::Result<dk::WalletPluginSignResponse> {
        std::string wif;
        {
            std::lock_guard<std::mutex> lock(vaultMutex_);
            if (!vault_.unlocked())
                return dk::err(dk::ErrorKind::Canceled, "vault locked before signing");
            wif = vault_.wifFor(publicKey);
            // Bump matched rule usage stats while we hold the lock.
            for (const auto& ruleId : evaluation.usedRuleIds)
                if (auto* rule = vault_.rule(ruleId)) {
                    rule->useCount++;
                    rule->lastUsedAt = nowSec();
                }
        }
        if (wif.empty())
            return dk::err(dk::ErrorKind::NotFound,
                           "no vault key for " + middleEllipsis(publicKey) +
                               "; the account may be watch-only");
        auto key = dk::PrivateKey::from(wif);
        secureWipe(wif.data(), wif.size());
        if (!key) return dk::err(dk::ErrorKind::Internal, "stored key failed to parse");
        auto signature = key->signDigest(resolved.signingDigest());
        if (!signature) return dk::err(signature.error());
        dk::WalletPluginSignResponse response;
        response.signatures = {*signature};
        return response;
    };

    // 5. Auto-sign fast path: every action trusted by an auto rule, the master
    //    switch on, hashes verified fresh, and nothing risk-critical.
    if (evaluation.overall == guard::VerdictLevel::TrustedAuto && security.allowAutoSign &&
        allHashesFetched && !hasCriticalRisk) {
        bool allPinned = true;
        for (const auto& verdict : evaluation.actions) {
            auto it = std::find_if(rules.begin(), rules.end(), [&](const guard::WhitelistRule& r) {
                return r.id == verdict.ruleId;
            });
            if (it == rules.end() || !it->pin) allPinned = false;
        }
        if (allPinned) {
            auto response = signNow();
            recordAudit(chainId, signer, summary, "auto-signed", bool(response),
                        std::string());
            if (response) {
                runner_.postMain([this, summary] {
                    toast(Toast::Success, "Auto-signed by whitelist: " + summary);
                });
            }
            return response;
        }
    }

    // Scheduled runs never prompt: no auto path means no signature.
    if (t_scheduledSign) {
        recordAudit(chainId, signer, summary, "schedule-blocked", false, std::string());
        return dk::err(dk::ErrorKind::Canceled,
                       "schedule blocked: no matching pinned auto-sign rule (or auto-sign "
                       "is off, hashes unverified, or a critical risk flag is present)");
    }

    // 6. Human review. Build the prompt payload and block on the modal.
    auto prompt = std::make_shared<SignPrompt>();
    prompt->id = broker_.nextId();
    prompt->chainId = chainId;
    prompt->chainName = context.chain.name();
    prompt->signer = signer;
    prompt->overall = evaluation.overall;
    prompt->hashesVerified = allHashesFetched;
    prompt->risks = risks;
    // Countdown mirrors the transaction's actual expiration header.
    prompt->expiresAtMs =
        int64_t(resolved.resolvedTransaction.expiration.value) * 1000;
    for (size_t i = 0; i < resolved.resolvedTransaction.actions.size(); ++i) {
        const auto& action = resolved.resolvedTransaction.actions[i];
        SignPrompt::ActionView view;
        view.contract = action.account.toString();
        view.action = action.name.toString();
        for (const auto& auth : action.authorization) {
            if (!view.authorization.empty()) view.authorization += ", ";
            view.authorization += auth.actor.toString() + "@" + auth.permission.toString();
        }
        view.data = action.data;
        view.verdict = evaluation.actions[i];
        prompt->actions.push_back(std::move(view));
    }

    PromptBroker::Pending pending;
    pending.id = prompt->id;
    pending.kind = "sign";
    pending.payload = prompt;
    bool answered = broker_.wait(pending, [this, prompt] {
        runner_.postMain([this, prompt] {
            state_.signPrompt = prompt;
            noteActivity();
        });
    });

    if (!answered || !prompt->approved) {
        recordAudit(chainId, signer, summary, guard::verdictLevelName(evaluation.overall), false,
                    std::string());
        return dk::err(dk::ErrorKind::Canceled, "rejected in TackleBox");
    }

    auto response = signNow();
    recordAudit(chainId, signer, summary, guard::verdictLevelName(evaluation.overall),
                bool(response), std::string());
    return response;
}

void Controller::recordAudit(const std::string& chainId, const std::string& signer,
                             const std::string& summary, const std::string& verdict,
                             bool approved, const std::string& txId) {
    {
        std::lock_guard<std::mutex> lock(vaultMutex_);
        if (!vault_.unlocked()) return;
        vault_.appendAudit({nowSec(), chainId, signer, summary, txId, verdict, approved});
    }
    refreshSnapshotFromWorker();
}

// --- whitelist ---------------------------------------------------------------

guard::WhitelistRule Controller::draftRuleFromAction(const SignPrompt::ActionView& action,
                                                     const std::string& chainId,
                                                     const std::string& signer) const {
    guard::WhitelistRule rule;
    rule.id = uuid4();
    rule.chainId = chainId;
    rule.signer = signer;
    rule.contract = action.contract;
    rule.action = action.action;
    rule.createdAt = nowSec();
    rule.note = action.contract + "::" + action.action;
    if (action.data.is_object()) {
        for (auto it = action.data.begin(); it != action.data.end(); ++it) {
            guard::ParamConstraint constraint;
            constraint.kind = guard::ConstraintKind::Exact;
            constraint.values = {it.value()};
            rule.params[it.key()] = constraint;
        }
    }
    return rule;
}

void Controller::saveRule(guard::WhitelistRule rule, bool pin) {
    state_.busyRule = true;
    runner_.run([this, rule = std::move(rule), pin]() mutable {
        std::string error;
        if (pin) {
            auto svc = service(rule.chainId == "*" ? std::string() : rule.chainId);
            if (!svc) {
                error = "contract pinning needs a specific chain (rule chain is '" +
                        rule.chainId + "')";
            } else {
                auto hashes = svc->fetchContractHashes(rule.contract, 0);
                if (!hashes) {
                    error = "could not fetch contract hashes: " + hashes.error().message;
                } else {
                    rule.pin = guard::ContractPin{hashes->codeHash, hashes->abiHash, nowSec()};
                    rule.status = guard::RuleStatus::Active;
                    rule.observedCodeHash.clear();
                    rule.observedAbiHash.clear();
                }
            }
        } else {
            rule.pin.reset();
            if (rule.autoSign) {
                // Policy: auto-sign without an integrity pin is not a thing.
                rule.autoSign = false;
                runner_.postMain([this] {
                    toast(Toast::Warn, "Auto-sign requires contract pinning; it was turned off");
                });
            }
        }
        if (error.empty()) {
            std::lock_guard<std::mutex> lock(vaultMutex_);
            vault_.upsertRule(rule);
        }
        runner_.postMain([this, error] {
            state_.busyRule = false;
            if (!error.empty()) {
                toast(Toast::Error, error);
                return;
            }
            refreshSnapshot();
            toast(Toast::Success, "Whitelist rule saved");
        });
    });
}

void Controller::removeRule(const std::string& id) {
    {
        std::lock_guard<std::mutex> lock(vaultMutex_);
        vault_.removeRule(id);
    }
    refreshSnapshot();
}

void Controller::setRuleStatus(const std::string& id, guard::RuleStatus status) {
    {
        std::lock_guard<std::mutex> lock(vaultMutex_);
        if (auto* rule = vault_.rule(id)) {
            rule->status = status;
            (void)vault_.save();
        }
    }
    refreshSnapshot();
}

void Controller::reapproveRule(const std::string& id) {
    guard::WhitelistRule copy;
    {
        std::lock_guard<std::mutex> lock(vaultMutex_);
        auto* rule = vault_.rule(id);
        if (!rule) return;
        copy = *rule;
    }
    copy.status = guard::RuleStatus::Active;
    saveRule(std::move(copy), /*pin=*/true);
}

void Controller::updateSecurity(const SecurityPrefs& prefs) {
    {
        std::lock_guard<std::mutex> lock(vaultMutex_);
        vault_.security() = prefs;
        (void)vault_.save();
    }
    refreshSnapshot();
}

// --- resources ---------------------------------------------------------------

void Controller::loadRamMarket(bool force) {
    auto svc = currentService();
    if (!svc) return;
    ResourcesViewState& rv = state_.resources;
    if (rv.ramLoading) return;
    if (!force && nowSec() - rv.ram.fetchedAt < 60) return;
    rv.ramLoading = true;
    rv.ramError.clear();
    runner_.run([this, svc] {
        auto market = svc->fetchRamMarket();
        runner_.postMain([this, market] {
            state_.resources.ramLoading = false;
            if (market)
                state_.resources.ram = *market;
            else
                state_.resources.ramError = market.error().message;
        });
    });
}

void Controller::quotePowerUp(double cpuMs, double netKb) {
    auto svc = currentService();
    if (!svc || state_.resources.quoteLoading) return;
    state_.resources.quoteLoading = true;
    state_.resources.quoteError.clear();
    runner_.run([this, svc, cpuMs, netKb] {
        auto quote = svc->quotePowerUp(cpuMs, netKb);
        runner_.postMain([this, quote] {
            state_.resources.quoteLoading = false;
            if (quote)
                state_.resources.quote = *quote;
            else
                state_.resources.quoteError = quote.error().message;
        });
    });
}

namespace {

json systemAction(const AccountRef& account, const char* name, json data) {
    return json{{"account", "eosio"},
                {"name", name},
                {"authorization", json::array({{{"actor", account.actor},
                                                {"permission", account.permission}}})},
                {"data", std::move(data)}};
}

}  // namespace

void Controller::buyRamBytes(const std::string& receiver, int64_t bytes) {
    const AccountRef* account = state_.currentAccount();
    if (!account || bytes <= 0) return;
    dk::TransactArgs args;
    args.action = systemAction(*account, "buyrambytes",
                               {{"payer", account->actor},
                                {"receiver", receiver.empty() ? account->actor : receiver},
                                {"bytes", bytes}});
    transactAsync(*account, std::move(args), "RAM purchase", &state_.resources.busyAction);
}

void Controller::sellRam(int64_t bytes) {
    const AccountRef* account = state_.currentAccount();
    if (!account || bytes <= 0) return;
    dk::TransactArgs args;
    args.action = systemAction(*account, "sellram",
                               {{"account", account->actor}, {"bytes", bytes}});
    transactAsync(*account, std::move(args), "RAM sale", &state_.resources.busyAction);
}

void Controller::transferRam(const std::string& to, int64_t bytes, const std::string& memo) {
    const AccountRef* account = state_.currentAccount();
    if (!account || bytes <= 0 || to.empty()) return;
    dk::TransactArgs args;
    args.action = systemAction(*account, "ramtransfer",
                               {{"from", account->actor},
                                {"to", to},
                                {"bytes", bytes},
                                {"memo", memo}});
    transactAsync(*account, std::move(args), "RAM transfer", &state_.resources.busyAction);
}

void Controller::stake(const std::string& receiver, const std::string& netQty,
                       const std::string& cpuQty) {
    const AccountRef* account = state_.currentAccount();
    if (!account) return;
    dk::TransactArgs args;
    args.action = systemAction(*account, "delegatebw",
                               {{"from", account->actor},
                                {"receiver", receiver.empty() ? account->actor : receiver},
                                {"stake_net_quantity", netQty},
                                {"stake_cpu_quantity", cpuQty},
                                {"transfer", false}});
    transactAsync(*account, std::move(args), "Stake", &state_.resources.busyAction);
}

void Controller::unstake(const std::string& receiver, const std::string& netQty,
                         const std::string& cpuQty) {
    const AccountRef* account = state_.currentAccount();
    if (!account) return;
    dk::TransactArgs args;
    args.action = systemAction(*account, "undelegatebw",
                               {{"from", account->actor},
                                {"receiver", receiver.empty() ? account->actor : receiver},
                                {"unstake_net_quantity", netQty},
                                {"unstake_cpu_quantity", cpuQty}});
    transactAsync(*account, std::move(args), "Unstake", &state_.resources.busyAction);
}

void Controller::powerUp(uint32_t days, int64_t cpuFrac, int64_t netFrac,
                         const std::string& maxPayment) {
    const AccountRef* account = state_.currentAccount();
    if (!account) return;
    dk::TransactArgs args;
    args.action = systemAction(*account, "powerup",
                               {{"payer", account->actor},
                                {"receiver", account->actor},
                                {"days", days},
                                {"net_frac", netFrac},
                                {"cpu_frac", cpuFrac},
                                {"max_payment", maxPayment}});
    transactAsync(*account, std::move(args), "PowerUp", &state_.resources.busyAction);
}

// --- governance ----------------------------------------------------------------

void Controller::loadProducers(bool force) {
    auto svc = currentService();
    if (!svc) return;
    GovernanceViewState& gv = state_.governance;
    if (gv.loading) return;
    if (!force && nowSec() - gv.fetchedAt < 120) return;
    gv.loading = true;
    gv.error.clear();
    runner_.run([this, svc] {
        auto producers = svc->fetchProducers(150);
        runner_.postMain([this, producers] {
            GovernanceViewState& view = state_.governance;
            view.loading = false;
            view.fetchedAt = nowSec();
            if (producers)
                view.producers = *producers;
            else
                view.error = producers.error().message;
        });
    });
}

void Controller::voteProducers(std::vector<std::string> producers) {
    const AccountRef* account = state_.currentAccount();
    if (!account) return;
    if (producers.size() > 30) {
        toast(Toast::Error, "At most 30 producers can be voted for");
        return;
    }
    // The chain requires the producer list sorted ascending by name value.
    std::sort(producers.begin(), producers.end(), [](const std::string& a, const std::string& b) {
        return dk::Name::from(a).value < dk::Name::from(b).value;
    });
    dk::TransactArgs args;
    args.action = systemAction(*account, "voteproducer",
                               {{"voter", account->actor},
                                {"proxy", ""},
                                {"producers", producers}});
    transactAsync(*account, std::move(args), "Vote", &state_.governance.busyVote);
}

void Controller::voteProxy(const std::string& proxy) {
    const AccountRef* account = state_.currentAccount();
    if (!account || proxy.empty()) return;
    dk::TransactArgs args;
    args.action = systemAction(*account, "voteproducer",
                               {{"voter", account->actor},
                                {"proxy", proxy},
                                {"producers", json::array()}});
    transactAsync(*account, std::move(args), "Proxy vote", &state_.governance.busyVote);
}

// --- contract deployment --------------------------------------------------------

namespace {

Result<std::vector<uint8_t>> readFileBytes(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return dk::err(dk::ErrorKind::Storage, "cannot open " + path);
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)),
                               std::istreambuf_iterator<char>());
    if (bytes.empty()) return dk::err(dk::ErrorKind::Invalid, path + " is empty");
    return bytes;
}

}  // namespace

void Controller::previewDeploy(const std::string& wasmPath, const std::string& abiPath) {
    state_.deploy.wasmPath = wasmPath;
    state_.deploy.abiPath = abiPath;
    state_.deploy.wasmHashPreview.clear();
    state_.deploy.wasmBytes = 0;
    if (wasmPath.empty()) return;
    runner_.run([this, wasmPath] {
        auto bytes = readFileBytes(wasmPath);
        std::string hash, error;
        size_t size = 0;
        if (bytes) {
            auto digest = dk::sha256(*bytes);
            hash = toHex(digest);
            size = bytes->size();
        } else {
            error = bytes.error().message;
        }
        runner_.postMain([this, wasmPath, hash, size, error] {
            if (state_.deploy.wasmPath != wasmPath) return;
            if (!error.empty()) {
                toast(Toast::Error, error);
                return;
            }
            state_.deploy.wasmHashPreview = hash;
            state_.deploy.wasmBytes = size;
        });
    });
}

void Controller::deployContract() {
    const AccountRef* accountPtr = state_.currentAccount();
    if (!accountPtr) return;
    AccountRef account = *accountPtr;
    std::string wasmPath = state_.deploy.wasmPath;
    std::string abiPath = state_.deploy.abiPath;
    if (wasmPath.empty() && abiPath.empty()) {
        toast(Toast::Warn, "Pick a .wasm and/or .abi file first");
        return;
    }
    state_.deploy.busy = true;
    runner_.run([this, account, wasmPath, abiPath] {
        json actions = json::array();
        std::string error;

        if (!wasmPath.empty()) {
            auto wasm = readFileBytes(wasmPath);
            if (!wasm) {
                error = wasm.error().message;
            } else {
                actions.push_back(json{{"account", "eosio"},
                                       {"name", "setcode"},
                                       {"authorization",
                                        json::array({{{"actor", account.actor},
                                                      {"permission", account.permission}}})},
                                       {"data",
                                        {{"account", account.actor},
                                         {"vmtype", 0},
                                         {"vmversion", 0},
                                         {"code", toHex(*wasm)}}}});
            }
        }
        if (error.empty() && !abiPath.empty()) {
            auto abiBytes = readFileBytes(abiPath);
            if (!abiBytes) {
                error = abiBytes.error().message;
            } else {
                std::string abiText(abiBytes->begin(), abiBytes->end());
                auto abi = dk::ABI::from(std::string_view(abiText));
                if (!abi) {
                    error = "ABI file did not parse: " + abi.error().message;
                } else {
                    auto packed = dk::Serializer::encode(*abi);
                    if (!packed) {
                        error = "ABI serialization failed: " + packed.error().message;
                    } else {
                        actions.push_back(
                            json{{"account", "eosio"},
                                 {"name", "setabi"},
                                 {"authorization",
                                  json::array({{{"actor", account.actor},
                                                {"permission", account.permission}}})},
                                 {"data",
                                  {{"account", account.actor}, {"abi", packed->hexString()}}}});
                    }
                }
            }
        }

        runner_.postMain([this, account, actions, error] {
            state_.deploy.busy = false;
            if (!error.empty()) {
                toast(Toast::Error, error);
                return;
            }
            dk::TransactArgs args;
            args.actions = actions;
            transactAsync(account, std::move(args), "Contract deployment",
                          &state_.deploy.busy);
        });
    });
}

// --- msig ----------------------------------------------------------------------

void Controller::loadProposals(const std::string& proposer) {
    auto svc = currentService();
    if (!svc) return;
    MsigViewState& mv = state_.msig;
    mv.proposer = proposer;
    mv.loading = true;
    mv.error.clear();
    mv.proposals = json();
    runner_.run([this, svc, proposer] {
        auto rows = svc->fetchTableRows(json{{"json", true},
                                             {"code", "eosio.msig"},
                                             {"table", "proposal"},
                                             {"scope", proposer},
                                             {"limit", 50}});
        runner_.postMain([this, proposer, rows] {
            MsigViewState& view = state_.msig;
            if (view.proposer != proposer) return;
            view.loading = false;
            if (rows)
                view.proposals = *rows;
            else
                view.error = rows.error().message;
        });
    });
}

void Controller::loadProposalDetail(const std::string& proposer, const std::string& name) {
    auto svc = currentService();
    if (!svc) return;
    MsigViewState& mv = state_.msig;
    mv.selectedName = name;
    mv.detailLoading = true;
    mv.detailError.clear();
    mv.decodedTrx = json();
    mv.approvals = json();
    runner_.run([this, svc, proposer, name] {
        json decoded, approvals;
        std::string error;

        auto row = svc->fetchTableRows(json{{"json", true},
                                            {"code", "eosio.msig"},
                                            {"table", "proposal"},
                                            {"scope", proposer},
                                            {"lower_bound", name},
                                            {"upper_bound", name},
                                            {"limit", 1}});
        if (!row || !row->contains("rows") || (*row)["rows"].empty()) {
            error = "proposal not found";
        } else {
            std::string packed = (*row)["rows"][0].value("packed_transaction", "");
            auto bytes = fromHex(packed);
            if (!bytes) {
                error = "packed transaction is not valid hex";
            } else {
                auto trx = dk::Serializer::decode<dk::Transaction>(
                    std::span<const uint8_t>(bytes->data(), bytes->size()));
                if (!trx) {
                    error = "could not unpack the proposed transaction: " + trx.error().message;
                } else {
                    json actions = json::array();
                    for (const auto& action : trx->actions) {
                        json actionJson{{"account", action.account.toString()},
                                        {"name", action.name.toString()}};
                        json auths = json::array();
                        for (const auto& auth : action.authorization)
                            auths.push_back(auth.actor.toString() + "@" +
                                            auth.permission.toString());
                        actionJson["authorization"] = auths;
                        // Decode data through the contract ABI when possible.
                        auto abi = svc->fetchAbi(action.account.toString());
                        bool decodedData = false;
                        if (abi) {
                            std::string type;
                            for (const auto& abiAction : abi->actions)
                                if (abiAction.name == action.name) type = abiAction.type;
                            if (!type.empty()) {
                                auto data = dk::Serializer::decode(
                                    std::span<const uint8_t>(action.data.array.data(),
                                                             action.data.array.size()),
                                    type, *abi);
                                if (data) {
                                    actionJson["data"] = *data;
                                    decodedData = true;
                                }
                            }
                        }
                        if (!decodedData) actionJson["data_hex"] = action.data.hexString();
                        actions.push_back(actionJson);
                    }
                    decoded = json{{"expiration", trx->expiration.toString()},
                                   {"actions", actions}};
                }
            }
        }

        auto approvalRows = svc->fetchTableRows(json{{"json", true},
                                                     {"code", "eosio.msig"},
                                                     {"table", "approvals2"},
                                                     {"scope", proposer},
                                                     {"lower_bound", name},
                                                     {"upper_bound", name},
                                                     {"limit", 1}});
        if (approvalRows && approvalRows->contains("rows") && !(*approvalRows)["rows"].empty())
            approvals = (*approvalRows)["rows"][0];

        runner_.postMain([this, name, decoded, approvals, error] {
            MsigViewState& view = state_.msig;
            if (view.selectedName != name) return;
            view.detailLoading = false;
            view.detailError = error;
            view.decodedTrx = decoded;
            view.approvals = approvals;
        });
    });
}

void Controller::stageMsigAction(const json& action) {
    state_.msig.draftActions.push_back(action);
    toast(Toast::Info, "Action staged in the msig builder (" +
                           std::to_string(state_.msig.draftActions.size()) + " staged)");
}

void Controller::msigPropose(const std::string& proposalName,
                             const std::vector<std::string>& requested, int expireHours) {
    const AccountRef* accountPtr = state_.currentAccount();
    if (!accountPtr) return;
    AccountRef account = *accountPtr;
    std::vector<json> draft = state_.msig.draftActions;
    if (draft.empty()) {
        toast(Toast::Warn, "Stage at least one action (Contracts page: TO MSIG)");
        return;
    }
    json requestedLevels = json::array();
    for (const auto& level : requested) {
        auto at = level.find('@');
        if (at == std::string::npos || at == 0) {
            toast(Toast::Error, "Requested approvals must be actor@permission: " + level);
            return;
        }
        requestedLevels.push_back(
            {{"actor", level.substr(0, at)}, {"permission", level.substr(at + 1)}});
    }
    auto svc = currentService();
    if (!svc) return;
    state_.msig.busyAction = true;
    runner_.run([this, svc, account, proposalName, requestedLevels, draft, expireHours] {
        // Pre-encode every action's data against its contract ABI: the msig
        // trx field carries actions with binary data.
        json packedActions = json::array();
        std::string error;
        for (const auto& action : draft) {
            std::string contract = action.value("account", "");
            std::string actionName = action.value("name", "");
            auto abi = svc->fetchAbi(contract);
            if (!abi) {
                error = "no ABI for " + contract + ": " + abi.error().message;
                break;
            }
            std::string type;
            for (const auto& abiAction : abi->actions)
                if (dk::Name(abiAction.name).toString() == actionName) type = abiAction.type;
            if (type.empty()) {
                error = contract + " has no action " + actionName;
                break;
            }
            json data = action.contains("data") ? action["data"] : json::object();
            auto packed = dk::Serializer::encode(data, type, *abi);
            if (!packed) {
                error = "encoding " + contract + "::" + actionName +
                        " failed: " + packed.error().message;
                break;
            }
            json packedAction = action;
            packedAction["data"] = packed->hexString();
            packedActions.push_back(packedAction);
        }
        runner_.postMain([this, account, proposalName, requestedLevels, packedActions, error,
                          expireHours] {
            state_.msig.busyAction = false;
            if (!error.empty()) {
                toast(Toast::Error, error);
                return;
            }
            dk::TimePointSec expiration(
                static_cast<uint32_t>(nowSec() + int64_t(expireHours) * 3600));
            json trx{{"expiration", expiration.toString()},
                     {"ref_block_num", 0},
                     {"ref_block_prefix", 0},
                     {"max_net_usage_words", 0},
                     {"max_cpu_usage_ms", 0},
                     {"delay_sec", 0},
                     {"context_free_actions", json::array()},
                     {"actions", packedActions},
                     {"transaction_extensions", json::array()}};
            dk::TransactArgs args;
            args.action = json{{"account", "eosio.msig"},
                               {"name", "propose"},
                               {"authorization",
                                json::array({{{"actor", account.actor},
                                              {"permission", account.permission}}})},
                               {"data",
                                {{"proposer", account.actor},
                                 {"proposal_name", proposalName},
                                 {"requested", requestedLevels},
                                 {"trx", trx}}}};
            state_.msig.draftActions.clear();
            transactAsync(account, std::move(args), "Msig proposal",
                          &state_.msig.busyAction);
        });
    });
}

void Controller::msigApprove(const std::string& proposer, const std::string& name,
                             bool approve) {
    const AccountRef* account = state_.currentAccount();
    if (!account) return;
    dk::TransactArgs args;
    args.action = json{{"account", "eosio.msig"},
                       {"name", approve ? "approve" : "unapprove"},
                       {"authorization", json::array({{{"actor", account->actor},
                                                       {"permission", account->permission}}})},
                       {"data",
                        {{"proposer", proposer},
                         {"proposal_name", name},
                         {"level",
                          {{"actor", account->actor}, {"permission", account->permission}}}}}};
    transactAsync(*account, std::move(args), approve ? "Msig approval" : "Msig unapproval",
                  &state_.msig.busyAction);
}

void Controller::msigExec(const std::string& proposer, const std::string& name) {
    const AccountRef* account = state_.currentAccount();
    if (!account) return;
    dk::TransactArgs args;
    args.action = json{{"account", "eosio.msig"},
                       {"name", "exec"},
                       {"authorization", json::array({{{"actor", account->actor},
                                                       {"permission", account->permission}}})},
                       {"data",
                        {{"proposer", proposer},
                         {"proposal_name", name},
                         {"executer", account->actor}}}};
    transactAsync(*account, std::move(args), "Msig execution", &state_.msig.busyAction);
}

void Controller::msigCancel(const std::string& proposer, const std::string& name) {
    const AccountRef* account = state_.currentAccount();
    if (!account) return;
    dk::TransactArgs args;
    args.action = json{{"account", "eosio.msig"},
                       {"name", "cancel"},
                       {"authorization", json::array({{{"actor", account->actor},
                                                       {"permission", account->permission}}})},
                       {"data",
                        {{"proposer", proposer},
                         {"proposal_name", name},
                         {"canceler", account->actor}}}};
    transactAsync(*account, std::move(args), "Msig cancellation", &state_.msig.busyAction);
}

// --- pinned queries -------------------------------------------------------------

void Controller::savePinnedQuery(const PinnedQuery& query) {
    {
        std::lock_guard<std::mutex> lock(vaultMutex_);
        vault_.upsertPinnedQuery(query);
    }
    refreshSnapshot();
    toast(Toast::Success, "Query pinned to the dashboard");
}

void Controller::removePinnedQuery(const std::string& id) {
    {
        std::lock_guard<std::mutex> lock(vaultMutex_);
        vault_.removePinnedQuery(id);
    }
    state_.pinnedData.erase(id);
    refreshSnapshot();
}

void Controller::refreshPinnedQueries() {
    if (!state_.unlocked) return;
    const AccountRef* account = state_.currentAccount();
    for (const auto& query : state_.vault.pinnedQueries) {
        // Chain-scoped pins refresh only while their chain is active.
        if (account && !query.chainId.empty() && query.chainId != account->chainId) continue;
        PinnedData& data = state_.pinnedData[query.id];
        if (data.loading) continue;
        int refresh = query.refreshSec < 15 ? 15 : query.refreshSec;
        if (nowSec() - data.fetchedAt < refresh) continue;
        auto svc = service(query.chainId.empty() && account ? account->chainId : query.chainId);
        if (!svc) continue;
        data.loading = true;
        std::string id = query.id;
        json params{{"json", true},
                    {"code", query.contract},
                    {"table", query.table},
                    {"scope", query.scope.empty() ? query.contract : query.scope},
                    {"limit", 5}};
        runner_.run([this, svc, id, params] {
            auto rows = svc->fetchTableRows(params);
            runner_.postMain([this, id, rows] {
                auto it = state_.pinnedData.find(id);
                if (it == state_.pinnedData.end()) return;
                it->second.loading = false;
                it->second.fetchedAt = nowSec();
                if (rows) {
                    it->second.rows = rows->value("rows", json::array());
                    it->second.error.clear();
                } else {
                    it->second.error = rows.error().message;
                }
            });
        });
    }
}

// --- schedules (autopilot) ------------------------------------------------------

void Controller::saveSchedule(Schedule schedule) {
    if (schedule.intervalSec < 60) schedule.intervalSec = 60;
    if (schedule.nextRunAt == 0) schedule.nextRunAt = nowSec() + schedule.intervalSec;
    {
        std::lock_guard<std::mutex> lock(vaultMutex_);
        vault_.upsertSchedule(schedule);
    }
    refreshSnapshot();
    toast(Toast::Success, "Schedule saved; it can only ever sign through a pinned "
                          "auto-sign whitelist rule");
}

void Controller::removeSchedule(const std::string& id) {
    {
        std::lock_guard<std::mutex> lock(vaultMutex_);
        vault_.removeSchedule(id);
    }
    refreshSnapshot();
}

void Controller::runScheduleNow(const std::string& id) {
    Schedule schedule;
    {
        std::lock_guard<std::mutex> lock(vaultMutex_);
        Schedule* found = vault_.schedule(id);
        if (!found) return;
        schedule = *found;
    }
    if (state_.schedulesInFlight.count(id)) return;
    state_.schedulesInFlight.insert(id);

    // Resolve the signing account: exact permission match first.
    AccountRef account;
    bool haveAccount = false;
    for (const auto& a : state_.vault.accounts) {
        if (a.chainId != schedule.chainId || a.actor != schedule.actor) continue;
        if (!haveAccount || a.permission == schedule.permission) {
            account = a;
            haveAccount = true;
        }
    }
    auto finish = [this, id](const std::string& result) {
        {
            std::lock_guard<std::mutex> lock(vaultMutex_);
            if (Schedule* s = vault_.schedule(id)) {
                s->lastRunAt = nowSec();
                s->nextRunAt = nowSec() + s->intervalSec;
                s->lastResult = result;
                (void)vault_.save();
            }
        }
        state_.schedulesInFlight.erase(id);
        refreshSnapshot();
    };
    if (!haveAccount || account.watch || account.pubKey.empty()) {
        finish("skipped: no signing account in the vault for " + schedule.actor);
        toast(Toast::Warn, "Schedule '" + schedule.label + "' has no signing account");
        return;
    }

    runner_.run([this, account, schedule, finish] {
        // Resolve dynamic pieces against live chain state, on this worker.
        json data = schedule.data;
        autopilot::TemplateContext context;
        context.actor = account.actor;
        context.unixSec = nowSec();

        if (schedule.amountMode == Schedule::AmountPercent) {
            auto svc = service(schedule.chainId);
            if (!svc) {
                runner_.postMain([finish] { finish("failed: no chain service"); });
                return;
            }
            std::string contract = schedule.amountTokenContract.empty()
                                       ? "eosio.token"
                                       : schedule.amountTokenContract;
            auto balance = svc->fetchBalance(contract, account.actor,
                                             schedule.amountTokenCode);
            if (!balance) {
                runner_.postMain([this, finish, schedule, err = balance.error().message] {
                    finish("skipped: balance fetch failed - " + err);
                    toast(Toast::Warn, "Autopilot '" + schedule.label +
                                           "' skipped: " + err);
                });
                return;
            }
            auto amount = autopilot::computePercentAmount(*balance, schedule.amountPercent,
                                                          schedule.amountReserve);
            if (!amount) {
                runner_.postMain([this, finish, schedule, err = amount.error().message] {
                    finish("skipped: " + err);
                    toast(Toast::Info, "Autopilot '" + schedule.label + "' skipped: " + err);
                });
                return;
            }
            context.amount = *amount;
            context.balance = *balance;
            std::string field =
                schedule.amountField.empty() ? "quantity" : schedule.amountField;
            data[field] = *amount;
        }

        data = autopilot::applyTemplates(std::move(data), context);

        dk::TransactArgs args;
        args.action = json{{"account", schedule.contract},
                           {"name", schedule.action},
                           {"authorization",
                            json::array({{{"actor", account.actor},
                                          {"permission", account.permission}}})},
                           {"data", data}};

        auto session = makeSession(account);
        if (!session) {
            runner_.postMain([finish] { finish("failed: could not build session"); });
            return;
        }
        t_scheduledSign = true;
        auto result = session->transact(args);
        t_scheduledSign = false;
        runner_.postMain([this, schedule, finish, sent = context.amount,
                          result = std::move(result)] {
            state_.pipelineStatus.clear();
            if (result) {
                std::string txId;
                if (result->response && result->response->contains("transaction_id"))
                    txId = result->response->value("transaction_id", "");
                std::string summary =
                    "signed " + (txId.empty() ? "ok" : middleEllipsis(txId, 10, 6));
                if (!sent.empty()) summary += " - sent " + sent;
                finish(summary);
                toast(Toast::Success, "Autopilot ran '" + schedule.label + "'" +
                                          (sent.empty() ? "" : " (" + sent + ")"));
                refreshAccount(true);
            } else {
                finish("blocked: " + result.error().message);
                toast(Toast::Warn,
                      "Autopilot '" + schedule.label + "' " + result.error().message);
            }
        });
    });
}

void Controller::tickSchedules() {
    if (!state_.unlocked) return;
    int64_t now = nowSec();
    for (const auto& schedule : state_.vault.schedules) {
        if (!schedule.enabled || schedule.nextRunAt == 0) continue;
        if (state_.schedulesInFlight.count(schedule.id)) continue;
        bool due = schedule.nextRunAt <= now;
        // A long-missed run only fires when the schedule opted in.
        if (due && now - schedule.nextRunAt > schedule.intervalSec &&
            !schedule.runMissedOnUnlock)
            continue;
        if (due) runScheduleNow(schedule.id);
    }
}

// --- tokens ---------------------------------------------------------------------

void Controller::addToken(const std::string& chainId, const TokenDef& token) {
    if (token.contract.empty() || token.code.empty()) return;
    {
        std::lock_guard<std::mutex> lock(vaultMutex_);
        NetworkDef* net = vault_.network(chainId);
        if (!net) return;
        for (const auto& existing : net->tokens)
            if (existing.contract == token.contract && existing.code == token.code) return;
        net->tokens.push_back(token);
        (void)vault_.save();
    }
    refreshSnapshot();
    refreshAccount(true);
}

void Controller::removeToken(const std::string& chainId, const TokenDef& token) {
    {
        std::lock_guard<std::mutex> lock(vaultMutex_);
        NetworkDef* net = vault_.network(chainId);
        if (!net) return;
        std::erase_if(net->tokens, [&](const TokenDef& t) {
            return t.contract == token.contract && t.code == token.code;
        });
        (void)vault_.save();
    }
    refreshSnapshot();
    refreshAccount(true);
}

// --- custom networks ------------------------------------------------------------

void Controller::probeCustomChain(const std::string& url,
                                  std::function<void(std::string, std::string)> done) {
    runner_.run([this, url, done] {
        dk::APIClientOptions options;
        options.url = url;
        options.fetch = std::make_shared<dk::CurlFetchProvider>(std::chrono::seconds(10));
        dk::APIClient probe(std::move(options));
        auto info = probe.call({.path = "/v1/chain/get_info", .params = json::object()});
        std::string chainId, error;
        if (info)
            chainId = info->value("chain_id", "");
        else
            error = info.error().message;
        runner_.postMain([done, chainId, error] { done(chainId, error); });
    });
}

// --- dapp links (experimental) --------------------------------------------------

void Controller::linkLogin(const std::string& esrUri) { link_->beginLogin(esrUri); }

void Controller::removeLinkSessionById(const std::string& id) {
    {
        std::lock_guard<std::mutex> lock(vaultMutex_);
        vault_.removeLinkSession(id);
    }
    refreshSnapshot();  // sync() inside stops the listener
    toast(Toast::Info, "Link session removed");
}

std::string Controller::linkSessionKey(const std::string& id) {
    std::lock_guard<std::mutex> lock(vaultMutex_);
    if (LinkSession* session = vault_.linkSession(id)) return session->requestKeyWif;
    return {};
}

dk::Result<dk::Signature> Controller::signIdentityDigest(const AccountRef& account,
                                                         const dk::Checksum256& digest) {
    std::string wif;
    {
        std::lock_guard<std::mutex> lock(vaultMutex_);
        if (!vault_.unlocked()) return dk::err(dk::ErrorKind::Canceled, "vault locked");
        wif = vault_.wifFor(account.pubKey);
    }
    if (wif.empty())
        return dk::err(dk::ErrorKind::NotFound, "no vault key for this account");
    auto key = dk::PrivateKey::from(wif);
    secureWipe(wif.data(), wif.size());
    if (!key) return dk::err(dk::ErrorKind::Internal, "stored key failed to parse");
    return key->signDigest(digest);
}

void Controller::storeLinkSession(const LinkSession& session) {
    {
        std::lock_guard<std::mutex> lock(vaultMutex_);
        vault_.upsertLinkSession(session);
    }
    refreshSnapshotFromWorker();
}

void Controller::signEsrFromLink(const std::string& sessionId, const std::string& uri) {
    LinkSession session;
    {
        std::lock_guard<std::mutex> lock(vaultMutex_);
        LinkSession* found = vault_.linkSession(sessionId);
        if (!found) return;
        session = *found;
        found->lastUsedAt = nowSec();
    }
    AccountRef account;
    bool haveAccount = false;
    for (const auto& a : state_.vault.accounts)
        if (a.chainId == session.chainId && a.actor == session.actor &&
            a.permission == session.permission) {
            account = a;
            haveAccount = true;
        }
    if (!haveAccount) {
        toast(Toast::Error, "Link request for " + session.actor + " but the account is gone");
        return;
    }
    state_.busyEsr = true;
    runner_.run([this, account, session, uri] {
        auto sessionKit = makeSession(account);
        if (!sessionKit) {
            runner_.postMain([this] {
                state_.busyEsr = false;
                toast(Toast::Error, "Could not build a session for the link account");
            });
            return;
        }
        dk::TransactArgs args;
        args.request = uri;
        auto result = sessionKit->transact(args);

        // Answer the dapp whichever way it went; a missing callback is fine.
        if (result && result->resolved) {
            auto cb = result->resolved->getCallback(result->signatures);
            if (cb && *cb) {
                json payload = (**cb).payload;
                payload["link_name"] = "TackleBox";
                dk::FetchRequest post;
                post.url = (**cb).url;
                post.method = "POST";
                post.body = payload.dump();
                post.headers = {{"Content-Type", "application/json"}};
                auto svc = service(account.chainId);
                if (svc) {
                    auto answered = svc->fetch()->fetch(post);
                    if (!answered)
                        Log::warn("link: callback POST failed: %s",
                                  answered.error().message.c_str());
                }
            }
        }
        runner_.postMain([this, session, result = std::move(result)] {
            state_.busyEsr = false;
            state_.pipelineStatus.clear();
            if (result)
                toast(Toast::Success, "Signed request from " + session.appName);
            else if (result.error().kind == dk::ErrorKind::Canceled)
                toast(Toast::Info, "Rejected request from " + session.appName);
            else
                toast(Toast::Error,
                      session.appName + " request failed: " + result.error().message);
            refreshSnapshot();
            refreshAccount(true);
        });
    });
}

// --- NFT actions ----------------------------------------------------------------

void Controller::transferAsset(const std::string& assetId, const std::string& to,
                               const std::string& memo) {
    const AccountRef* account = state_.currentAccount();
    if (!account || to.empty()) return;
    runContractAction("atomicassets", "transfer",
                      {{"from", account->actor},
                       {"to", to},
                       {"asset_ids", json::array({assetId})},
                       {"memo", memo}});
}

void Controller::burnAsset(const std::string& assetId) {
    const AccountRef* account = state_.currentAccount();
    if (!account) return;
    runContractAction("atomicassets", "burnasset",
                      {{"asset_owner", account->actor}, {"asset_id", assetId}});
}

// --- misc --------------------------------------------------------------------

void Controller::copyToClipboard(const std::string& text, bool sensitive) {
    clipboardSet(text);
    int clearSec = state_.vault.security.clipboardClearSec;
    if (sensitive && clearSec > 0) {
        state_.clipboardArmed = text;
        state_.clipboardClearAtMs = nowMs() + int64_t(clearSec) * 1000;
        toast(Toast::Info, "Copied; clipboard clears in " + std::to_string(clearSec) + "s");
    } else {
        toast(Toast::Info, "Copied");
    }
}

void Controller::toast(Toast::Kind kind, const std::string& text) {
    state_.toasts.push_back({kind, text, nowMs(), kind == Toast::Error ? 8.0f : 5.0f});
    if (kind == Toast::Error)
        Log::error("%s", text.c_str());
    else
        Log::info("%s", text.c_str());
}

void Controller::pipelineStatus(const std::string& message) {
    runner_.postMain([this, message] { state_.pipelineStatus = message; });
}

bool Controller::pluginPromptBlocking(const std::string& title, const std::string& body,
                                      const std::vector<std::string>& lines) {
    auto prompt = std::make_shared<PluginPrompt>();
    prompt->id = broker_.nextId();
    prompt->title = title;
    prompt->body = body;
    prompt->lines = lines;

    PromptBroker::Pending pending;
    pending.id = prompt->id;
    pending.kind = "plugin";
    pending.payload = prompt;
    bool answered = broker_.wait(pending, [this, prompt] {
        runner_.postMain([this, prompt] { state_.pluginPrompt = prompt; });
    });
    return answered && prompt->accepted;
}

}  // namespace tb
