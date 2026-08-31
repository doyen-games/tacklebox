// Shared UI state. Owned and mutated by the main thread only; workers hand
// results to the Controller which applies them via TaskRunner::postMain.
#pragma once

#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "chain/service.hpp"
#include "guard/engine.hpp"
#include "guard/risk.hpp"
#include "vault/vault.hpp"

namespace tb {

enum class Page {
    Dashboard,
    Explore,
    Transfer,
    Assets,
    Contracts,
    Resources,
    Governance,
    Msig,
    Autopilot,
    Whitelist,
    Vault,
    CreateAccount,  // on-chain account creation + port-in pipeline
    History,
    Settings,
    Setup,  // first-run guide: chains -> keys -> account discovery
};

struct Toast {
    enum Kind { Info, Success, Warn, Error };
    Kind kind = Info;
    std::string text;
    int64_t bornMs = 0;
    float ttlSec = 5.0f;
};

// UI-facing copy of the vault. Private keys never appear here.
struct VaultSnapshot {
    std::vector<KeyEntry> keys;  // wif fields are blanked
    std::vector<AccountRef> accounts;
    std::vector<NetworkDef> networks;
    std::vector<guard::WhitelistRule> rules;
    SecurityPrefs security;
    std::vector<AuditEntry> audit;
    std::vector<PinnedQuery> pinnedQueries;
    std::vector<Schedule> schedules;
    std::vector<LinkSession> linkSessions;  // requestKeyWif blanked
    uint64_t version = 0;
};

struct AccountData {
    AccountSnapshot snap;
    bool loading = false;
    bool loaded = false;
    std::string error;
};

// A signing decision waiting on the human. Built on a worker inside the
// wallet plugin, rendered by the signing modal, resolved through the broker.
struct SignPrompt {
    uint64_t id = 0;
    std::string chainId;
    std::string chainName;
    std::string signer;  // actor@permission
    struct ActionView {
        std::string contract;
        std::string action;
        std::string authorization;
        json data;
        guard::ActionVerdict verdict;
    };
    std::vector<ActionView> actions;
    std::vector<guard::RiskFlag> risks;
    guard::VerdictLevel overall = guard::VerdictLevel::Unlisted;
    bool hashesVerified = false;  // fresh get_raw_abi succeeded for every contract
    int64_t expiresAtMs = 0;

    // Written by the UI before resolving.
    bool approved = false;
};

// Generic prompt from a dwarfkit transact plugin (e.g. resource provider fee).
struct PluginPrompt {
    uint64_t id = 0;
    std::string title;
    std::string body;
    std::vector<std::string> lines;  // rendered prompt elements, simplified
    bool accepted = false;
};

// Block explorer state. `mode` picks the sub-view; loads happen on workers.
struct ExploreViewState {
    enum class Mode { Overview, Account, Block, Transaction };
    Mode mode = Mode::Overview;

    // overview
    json info;                 // last get_info
    int64_t infoFetchedAt = 0; // ms
    std::vector<json> recentBlocks;  // newest first, summarized
    bool loadingOverview = false;
    std::string overviewError;

    // account
    std::string accountName;
    json account;              // raw get_account
    json actions;              // v1/history get_actions result (may be error-empty)
    std::string actionsError;  // "endpoint does not serve history" etc.
    bool loadingAccount = false;
    std::string accountError;

    // block
    std::string blockQuery;    // number or id as typed
    json block;
    bool loadingBlock = false;
    std::string blockError;

    // transaction
    std::string txId;
    json txStatus;             // get_transaction_status
    json txDetail;             // history get_transaction (when served)
    json txFromBlock;          // the tx object dug out of its block
    bool loadingTx = false;
    std::string txError;
};

// NFT gallery state (Atomic Assets API).
struct AssetsViewState {
    std::string owner;         // whose assets are shown
    json assets;               // AA API response {data: [...]}
    bool loading = false;
    std::string error;
    int64_t fetchedAt = 0;
    // detail panel
    json selected;             // one asset object, null when closed
};

// RAM market, staking and PowerUp.
struct ResourcesViewState {
    RamMarket ram;
    bool ramLoading = false;
    std::string ramError;
    PowerUpQuote quote;
    bool quoteLoading = false;
    std::string quoteError;
    bool busyAction = false;   // any resource transaction in flight
};

// Producer voting / proxying.
struct GovernanceViewState {
    json producers;            // get_producers response
    bool loading = false;
    std::string error;
    int64_t fetchedAt = 0;
    std::set<std::string> selected;  // producers picked in the UI (max 30)
    bool busyVote = false;
};

// One pinned query's latest data.
struct PinnedData {
    json rows;
    std::string error;
    bool loading = false;
    int64_t fetchedAt = 0;
};

// Msig browser + builder.
struct MsigViewState {
    std::string proposer;      // whose proposals are listed
    json proposals;            // eosio.msig proposal table rows
    bool loading = false;
    std::string error;
    // detail
    std::string selectedName;
    json approvals;            // requested/provided approvals for the selection
    json decodedTrx;           // unpacked + decoded proposed transaction
    bool detailLoading = false;
    std::string detailError;
    bool busyAction = false;
    // builder: actions staged from the Contracts page or entered as JSON
    std::vector<json> draftActions;  // full action objects {account,name,authorization,data}
};

// Contract deployment (setcode/setabi).
struct DeployViewState {
    std::string wasmPath;
    std::string abiPath;
    std::string wasmHashPreview;  // sha256 of the local wasm, shown pre-flight
    size_t wasmBytes = 0;
    bool busy = false;
};

struct ContractsViewState {
    std::string account;          // contract being inspected
    bool loading = false;
    std::string error;
    std::shared_ptr<dwarfkit::ABI> abi;
    std::string codeHash, abiHash;
    // table browser
    std::string tableName;
    std::string tableScope;
    json tableRows;
    bool tableLoading = false;
    std::string tableError;
};

// Key-based account discovery progress (Anchor import / setup wizard).
struct DiscoveryState {
    bool running = false;
    int added = 0;
    std::vector<std::string> log;  // per-chain outcomes, newest last
};

struct AppState {
    Page page = Page::Dashboard;

    bool vaultExists = false;
    bool unlocked = false;
    VaultSnapshot vault;
    int selectedAccount = -1;      // index into vault.accounts
    std::string selectedChainId;   // chain the UI is looking at (chain-first)
    DiscoveryState discovery;

    // key: chainId + "|" + actor
    std::map<std::string, AccountData> accountData;

    std::vector<Toast> toasts;
    std::shared_ptr<SignPrompt> signPrompt;
    std::shared_ptr<PluginPrompt> pluginPrompt;

    // transient busy flags / errors surfaced by views
    bool busyUnlock = false;
    std::string unlockError;
    bool busyTransfer = false;
    bool busyContract = false;
    bool busyCreateAccount = false;
    // UI locked while the vault stays open in memory for autopilot schedules
    // (SecurityPrefs.autopilotStandby). Cleared by unlock, panic and shutdown.
    bool standbyLocked = false;
    bool busyEsr = false;
    bool busyRule = false;
    int workerPending = 0;  // mirrored from TaskRunner for the activity spinner

    std::map<std::string, std::vector<EndpointHealth>> health;  // per chainId
    bool busyHealth = false;

    // USD prices for the selected chain (display-only; key = contract/SYM).
    struct PricesState {
        std::map<std::string, double> usd;
        int64_t fetchedAt = 0;
        bool loading = false;
        std::string error;
    } prices;

    ContractsViewState contracts;
    ExploreViewState explore;
    AssetsViewState assets;
    ResourcesViewState resources;
    GovernanceViewState governance;
    MsigViewState msig;
    DeployViewState deploy;
    std::map<std::string, PinnedData> pinnedData;  // by PinnedQuery id
    std::set<std::string> schedulesInFlight;

    // status line under the top bar during transact pipeline stages
    std::string pipelineStatus;

    int64_t lastActivityMs = 0;   // for auto-lock
    int64_t clipboardClearAtMs = 0;
    std::string clipboardArmed;

    bool showDiagnostics = false;

    std::string accountKey(const AccountRef& a) const { return a.chainId + "|" + a.actor; }
    const AccountRef* currentAccount() const {
        if (selectedAccount < 0 || selectedAccount >= static_cast<int>(vault.accounts.size()))
            return nullptr;
        return &vault.accounts[static_cast<size_t>(selectedAccount)];
    }
    // Chain-first: the selected chain drives every chain-scoped page, whether
    // or not an account exists on it (Explore works account-less).
    const NetworkDef* currentNetwork() const {
        if (!selectedChainId.empty())
            for (const auto& n : vault.networks)
                if (n.chainId == selectedChainId) return &n;
        if (const AccountRef* a = currentAccount())
            for (const auto& n : vault.networks)
                if (n.chainId == a->chainId) return &n;
        return vault.networks.empty() ? nullptr : &vault.networks[0];
    }
    // Indices of vault.accounts on the selected chain (account switcher).
    std::vector<int> accountsOnChain() const {
        std::vector<int> out;
        const NetworkDef* net = currentNetwork();
        if (!net) return out;
        for (size_t i = 0; i < vault.accounts.size(); ++i)
            if (vault.accounts[i].chainId == net->chainId) out.push_back(static_cast<int>(i));
        return out;
    }
};

}  // namespace tb
