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
    void unstake(const std::string& receiver, const std::string& netQty,
                 const std::string& cpuQty);
    void powerUp(uint32_t days, int64_t cpuFrac, int64_t netFrac,
                 const std::string& maxPayment);

    // --- governance ---------------------------------------------------------
    void loadProducers(bool force);
    void voteProducers(std::vector<std::string> producers);  // sorted internally
    void voteProxy(const std::string& proxy);

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

    // --- pinned queries -----------------------------------------------------
    void savePinnedQuery(const PinnedQuery& query);
    void removePinnedQuery(const std::string& id);
    void refreshPinnedQueries();  // honors each query's refreshSec; called in tick

    // --- schedules (autopilot) ----------------------------------------------
    void saveSchedule(Schedule schedule);
    void removeSchedule(const std::string& id);
    void runScheduleNow(const std::string& id);
    void tickSchedules();  // called from tick while unlocked

    // --- tokens -------------------------------------------------------------
    void addToken(const std::string& chainId, const TokenDef& token);
    void removeToken(const std::string& chainId, const TokenDef& token);

    // --- custom networks ----------------------------------------------------
    // Probe get_info on a URL; callback (chainIdHex, error) on main.
    void probeCustomChain(const std::string& url,
                          std::function<void(std::string, std::string)> done);

    // --- dapp links (experimental) ------------------------------------------
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
    // on a worker and stamped into the rule before saving.
    void saveRule(guard::WhitelistRule rule, bool pin);
    void removeRule(const std::string& id);
    void setRuleStatus(const std::string& id, guard::RuleStatus status);
    // Stale rule -> fetch fresh hashes, re-pin, reactivate.
    void reapproveRule(const std::string& id);

    void updateSecurity(const SecurityPrefs& prefs);

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
    void refreshSnapshot();           // main thread: copy vault -> state
    void refreshSnapshotFromWorker(); // post the copy to main
    std::unique_ptr<dwarfkit::Session> makeSession(const AccountRef& account);
    void recordAudit(const std::string& chainId, const std::string& signer,
                     const std::string& summary, const std::string& verdict, bool approved,
                     const std::string& txId);
    void transactAsync(const AccountRef account, dwarfkit::TransactArgs args,
                       const std::string& flowName, bool* busyFlag,
                       std::function<void(bool, std::string)> onDone = {});

    AppState& state_;
    TaskRunner& runner_;
    PromptBroker broker_;
    std::unique_ptr<class LinkService> link_;

    std::mutex vaultMutex_;
    Vault vault_;
    uint64_t vaultVersion_ = 1;

    std::mutex servicesMutex_;
    std::map<std::string, std::shared_ptr<ChainService>> services_;
};

}  // namespace tb
