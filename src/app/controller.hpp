// The application brain. Owns the vault (behind a mutex: workers sign with it
// while the UI edits it), the per-network chain services, the task runner and
// the prompt broker. UI code calls the methods below on the main thread; they
// return immediately and apply results through postMain.
#pragma once

#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>

#include <dwarfkit/session.hpp>

#include "app/account_util.hpp"
#include "app/state.hpp"
#include "core/task_runner.hpp"

namespace tb {

class Controller {
public:
    Controller(AppState& state, TaskRunner& runner);
    ~Controller();

    AppState& state() { return state_; }
    TaskRunner& runner() { return runner_; }
    PromptBroker& broker() { return broker_; }

    // --- lifecycle ----------------------------------------------------------
    void init();
    void tick();          // once per frame on main: autolock, clipboard, spinner
    void shutdown();      // cancel prompts, lock

    void noteActivity();  // any input event; feeds the auto-lock timer

    // --- vault --------------------------------------------------------------
    void createVault(const std::string& password);
    void unlockVault(const std::string& password);
    // hard=true always seals the vault (panic, background, shutdown).
    // Otherwise, with SecurityPrefs.autopilotStandby and enabled schedules,
    // the UI locks while the vault stays open in memory so autopilot runs on.
    void lockVault(bool hard = false);
    void changePassword(const std::string& current, const std::string& next,
                        std::function<void(bool, std::string)> done);
    // Verify the vault password (per-sign confirmation). Callback on main.
    void verifyPassword(const std::string& password, std::function<void(bool)> done);

    void importKey(const std::string& wif, const std::string& label);
    void generateKey(const std::string& label);
    void removeKey(const std::string& pub);
    // Reveal-flow confirmation that the key is written down somewhere safe.
    void markKeyBackedUp(const std::string& pub);
    // Reveal a private key after password re-entry. Callback on main; the
    // string is the WIF (empty + message on failure).
    void revealKey(const std::string& pub, const std::string& password,
                   std::function<void(std::string, std::string)> done);

    // Verify on chain that actor@permission exists (and which vault key, if
    // any, can sign for it), then store it.
    void addAccount(const std::string& chainId, const std::string& actor,
                    const std::string& permission);
    void removeAccount(const AccountRef& account);
    void selectAccount(int index);
    // Chain-first navigation: look at a chain (with or without an account on
    // it); remembers the choice and picks that chain's last-used account.
    void selectChain(const std::string& chainId);

    // --- on-chain account creation ------------------------------------------
    // One permission of the new account. Keys listed by public key; the
    // generate flags below add freshly-minted vault keys at submit time.
    struct NewAccountSpec {
        std::string name;
        acct::AuthorityDraft owner, active;
        bool generateOwnerKey = true;   // mint + append a key to owner
        bool generateActiveKey = true;  // mint + append a key to active
        int64_t ramBytes = 4096;
        std::string cpuStake, netStake;  // human amounts ("1.0"); empty/0 = skip
        bool transferStake = false;      // delegatebw transfer flag (gift)
    };
    // Build newaccount + buyrambytes (+ delegatebw), sign through the guard
    // as the selected account, then port the new account straight into the
    // vault as a wallet account and select it.
    void createAccount(const NewAccountSpec& spec);
    // Availability probe; callback (exists, error) on main.
    void checkAccountName(const std::string& name,
                          std::function<void(bool, std::string)> done);

    // Setup wizard / Anchor migration.
    // Replace the preset-enabled set: upsert `enable`, drop known presets not
    // in it (custom chains are never touched here).
    void enableNetworks(const std::vector<NetworkDef>& enable);
    // Paste-many key import (one WIF per line); toasts a summary.
    void importKeysBulk(const std::string& text);
    // Scan every enabled chain for accounts controlled by any vault key and
    // add them (progress in state.discovery).
    void discoverAccounts();

    // --- chain data ---------------------------------------------------------
    void refreshAccount(bool force);
    void probeEndpoints(const std::string& chainId);  // all node types
    // USD prices via the network's oracle (60s TTL unless forced). No-op with
    // the oracle off. Display-only by design.
    void refreshPrices(bool force);
    // Fetch the core price with a candidate (possibly unsaved) config;
    // callback (formatted price, error) on main. Settings' TEST button.
    void testOracle(const NetworkDef& net,
                    std::function<void(std::string, std::string)> done);
    void addNetwork(const NetworkDef& net);  // upsert: also how endpoint edits commit
    void removeNetwork(const std::string& chainId);

    // Contracts explorer.
    void loadContract(const std::string& account);
    void loadTableRows(const std::string& contract, const std::string& table,
                       const std::string& scope);

    // Block explorer.
    void exploreOverview(bool force);           // get_info + recent blocks
    void exploreAccount(const std::string& actor);
    void exploreBlock(const std::string& numOrId);
    void exploreTransaction(const std::string& txId);
    // Route a free-form search: tx id (64 hex), block number, or account name.
    void exploreSearch(const std::string& query);

    // NFT gallery (Atomic Assets API).
    void loadAssets(const std::string& owner, bool force);
    void transferAsset(const std::string& assetId, const std::string& to,
                       const std::string& memo);
    void burnAsset(const std::string& assetId);

    // Vault portability.
    void exportVault(const std::string& destPath);
    void importVault(const std::string& srcPath);  // locks first; confirm in UI

    // --- resources ----------------------------------------------------------
    void loadRamMarket(bool force);
    void quotePowerUp(double cpuMs, double netKb);
    void buyRamBytes(const std::string& receiver, int64_t bytes);
    void sellRam(int64_t bytes);
    void transferRam(const std::string& to, int64_t bytes, const std::string& memo);
    void stake(const std::string& receiver, const std::string& netQty,
               const std::string& cpuQty);
    void loadDelegations(bool force);  // outgoing delband rows for the actor
    void unstake(const std::string& receiver, const std::string& netQty,
                 const std::string& cpuQty);
    void powerUp(uint32_t days, int64_t cpuFrac, int64_t netFrac,
                 const std::string& maxPayment);

    // --- governance ---------------------------------------------------------
    void loadProducers(bool force);
    void voteProducers(std::vector<std::string> producers);  // sorted internally
    void voteProxy(const std::string& proxy);
    void loadProxies(bool force);  // ranked registry, cached ~10 minutes

    // --- contract deployment ------------------------------------------------
    // Reads .wasm/.abi on a worker, stages a preview (hash, size) then a
    // setcode+setabi transaction through the normal guard flow.
    void previewDeploy(const std::string& wasmPath, const std::string& abiPath);
    void deployContract();

    // --- msig ---------------------------------------------------------------
    void loadProposals(const std::string& proposer);
    void loadProposalDetail(const std::string& proposer, const std::string& name);
    void msigPropose(const std::string& proposalName,
                     const std::vector<std::string>& requested,  // actor@permission
                     int expireHours);
    void msigApprove(const std::string& proposer, const std::string& name, bool approve);
    void msigExec(const std::string& proposer, const std::string& name);
    void msigCancel(const std::string& proposer, const std::string& name);
    void stageMsigAction(const json& action);  // from the Contracts page
    // Msig templates: reusable proposal shapes with predisposed fields.
    void saveMsigTemplate(const MsigTemplate& tpl);
    void removeMsigTemplate(const std::string& id);
    void applyMsigTemplate(const std::string& id);  // fills the builder draft

    // --- saved contracts ----------------------------------------------------
    void saveContractBookmark(const SavedContract& saved);
    void removeContractBookmark(const std::string& chainId, const std::string& account);

    // --- pinned queries -----------------------------------------------------
    void savePinnedQuery(const PinnedQuery& query);
    void removePinnedQuery(const std::string& id);
    void refreshPinnedQueries();  // honors each query's refreshSec; called in tick

    // --- dashboard board (drag & drop with memory) --------------------------
    void saveDashboardTiles(std::vector<DashTile> tiles);  // whole-board commit
    void addDashboardTile(const std::string& kind, int span);  // no-op if present
    void removeDashboardTile(const std::string& kind);
    // Persisted drag-reorder for the other draggable lists.
    void moveSchedule(size_t from, size_t to);
    void moveAccount(size_t from, size_t to);  // keeps the selection pinned

    // --- schedules (autopilot) ----------------------------------------------
    void saveSchedule(Schedule schedule);
    void removeSchedule(const std::string& id);
    void runScheduleNow(const std::string& id);
    void tickSchedules();  // called from tick while unlocked

    // --- tokens -------------------------------------------------------------
    void addToken(const std::string& chainId, const TokenDef& token);
    void removeToken(const std::string& chainId, const TokenDef& token);

    // --- account groups (pinned wallet sections) -----------------------------
    void setAccountGroup(const std::string& accountKey, const std::string& group);
    void renameAccountGroup(const std::string& from, const std::string& to);
    void removeAccountGroup(const std::string& name);
    void moveAccountGroup(size_t from, size_t to);

    // --- contacts (address book) --------------------------------------------
    void addContact(const Contact& contact);
    void removeContact(const std::string& actor, const std::string& chainId);

    // --- custom networks ----------------------------------------------------
    // Probe get_info on a URL; callback (chainIdHex, error) on main.
    void probeCustomChain(const std::string& url,
                          std::function<void(std::string, std::string)> done);

    // --- dapp links (experimental) ------------------------------------------
    // A tacklebox:/esr: uri from the command line or a forwarded second
    // launch (main thread). Focus links raise the window; requests route to
    // the link-login or ESR signing flow, queued until the vault is unlocked.
    void handleDeepLink(const std::string& uri);
    // Main-thread hook that raises + flashes the OS window; set by the shell.
    void setRaiseWindow(std::function<void()> raise);
    void linkLogin(const std::string& esrUri);       // pasted identity request
    void removeLinkSessionById(const std::string& id);
    // LinkService support (worker threads):
    std::string linkSessionKey(const std::string& id);           // WIF copy
    dwarfkit::Result<dwarfkit::Signature> signIdentityDigest(
        const AccountRef& account, const dwarfkit::Checksum256& digest);
    void storeLinkSession(const LinkSession& session);
    // Sign a pushed request with the link's account and answer its callback.
    void signEsrFromLink(const std::string& sessionId, const std::string& uri);

    // --- transactions -------------------------------------------------------
    void sendTransfer(const std::string& to, const std::string& quantity,
                      const std::string& memo, const std::string& tokenContract);
    void runContractAction(const std::string& contract, const std::string& action,
                           const json& data);
    void signEsr(const std::string& uri);

    // Signing modal resolution (main thread).
    void resolveSignPrompt(bool approved);
    void resolvePluginPrompt(bool accepted);

    // --- whitelist ----------------------------------------------------------
    // Build a draft rule from a prompt's action (prefilled Exact constraints).
    guard::WhitelistRule draftRuleFromAction(const SignPrompt::ActionView& action,
                                             const std::string& chainId,
                                             const std::string& signer) const;
    // Persist a rule; when pin==true the current contract hashes are fetched
    // on a worker and stamped into the rule before saving. done(ok, error)
    // fires on main so the editor can stay open (draft intact) on failure.
    void saveRule(guard::WhitelistRule rule, bool pin,
                  std::function<void(bool, std::string)> done = {});
    void removeRule(const std::string& id);
    void setRuleStatus(const std::string& id, guard::RuleStatus status);
    // Stale rule -> fetch fresh hashes, re-pin, reactivate.
    void reapproveRule(const std::string& id);

    void updateSecurity(const SecurityPrefs& prefs);

    // --- updates ------------------------------------------------------------
    // Query GitHub Releases for the latest version. Notify-only: results land
    // in state.update and the user opens the release page themselves. Manual
    // checks toast the outcome; the automatic post-unlock check stays silent
    // unless a newer release exists.
    void checkForUpdates(bool manual);

    // --- misc ---------------------------------------------------------------
    void copyToClipboard(const std::string& text, bool sensitive);
    void toast(Toast::Kind kind, const std::string& text);

    // Called by VaultWalletPlugin::sign on a worker thread.
    dwarfkit::Result<dwarfkit::WalletPluginSignResponse> guardedSign(
        const dwarfkit::ResolvedSigningRequest& resolved, dwarfkit::TransactContext& context,
        const std::string& publicKey);

    // Called by TackleUI (worker threads).
    void pipelineStatus(const std::string& message);
    bool pluginPromptBlocking(const std::string& title, const std::string& body,
                              const std::vector<std::string>& lines);

    // Service handle for the active network (UI media fetches). May be null.
    std::shared_ptr<ChainService> currentService();

private:
    std::shared_ptr<ChainService> service(const std::string& chainId);
    void applyPerformancePrefs();     // size the worker pool from prefs
    void refreshSnapshot();           // main thread: copy vault -> state
    void refreshSnapshotFromWorker(); // post the copy to main
    std::unique_ptr<dwarfkit::Session> makeSession(const AccountRef& account);
    void recordAudit(const std::string& chainId, const std::string& signer,
                     const std::string& summary, const std::string& verdict, bool approved,
                     const std::string& txId);
    // onDone(ok, txIdOrError, canceled) on main; canceled distinguishes a
    // human decline from other failures (rejection callbacks key off it).
    void transactAsync(const AccountRef account, dwarfkit::TransactArgs args,
                       const std::string& flowName, bool* busyFlag,
                       std::function<void(bool, std::string, bool)> onDone = {});

    // Deep-link routing (main thread).
    void routeEsr(const std::string& esrUri);
    void raiseWindow();

    AppState& state_;
    TaskRunner& runner_;
    PromptBroker broker_;
    std::unique_ptr<class LinkService> link_;
    std::function<void()> raiseWindow_;
    std::string pendingDeepLink_;  // esr uri parked until the vault unlocks

    std::mutex vaultMutex_;
    Vault vault_;
    uint64_t vaultVersion_ = 1;

    std::mutex servicesMutex_;
    std::map<std::string, std::shared_ptr<ChainService>> services_;
};

}  // namespace tb
