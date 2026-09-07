#include "ui/qa.hpp"

#include <cstdio>
#include <filesystem>
#include <vector>

#include <SDL3/SDL.h>
#ifdef TB_MOBILE
#include <SDL3/SDL_opengles2.h>
#else
#include <SDL3/SDL_opengl.h>
#endif

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <imgui_internal.h>  // openCombo: force combo popups for the tour

#include <dwarfkit/antelope/chain/abi.hpp>

#include "app/controller.hpp"
#include "app/state.hpp"
#include "chain/netreg.hpp"
#include "chain/prices.hpp"
#include "core/log.hpp"
#include "core/util.hpp"
#include "ui/app_ui.hpp"
#include "ui/layout.hpp"
#include "ui/ui_helpers.h"

namespace tb::ui::qa {

namespace {

std::string g_dir;
bool g_active = false;
int g_stepIndex = -1;
float g_pageScroll = -1.0f;  // set by steps in enter(); shell applies it
std::string g_forceOpenTag;  // interaction surface the step wants open
bool g_forceOpenUsed = false;  // one caller per frame
int g_framesLeft = 0;
bool g_captureDue = false;
std::string g_pendingName;
bool g_fixturesInjected = false;

struct Step {
    const char* name;
    // Mutate state to show the screen. Runs once when the step starts.
    void (*enter)(AppState&, Controller&);
};

std::string factorTag() {
    switch (layoutConfig().override_) {
        case 0: return "phone";
        case 1: return "tablet";
        case 2: return "desktop";
        default: return "auto";
    }
}

// --- fixtures ---------------------------------------------------------------
// Pure state-snapshot fixtures: nothing touches the real vault file or the
// network. Freshness stamps are "now" so views skip their background loads.

void injectFixtures(AppState& state) {
    if (g_fixturesInjected) return;
    g_fixturesInjected = true;
    int64_t now = nowSec();

    VaultSnapshot& v = state.vault;
    v.networks = defaultNetworks();
    std::string eosId = v.networks[0].chainId;
    std::string waxId = v.networks.size() > 1 ? v.networks[1].chainId : eosId;
    // Exercise the endpoint policy UI: round robin on, a nicknamed disabled
    // node, and health rows for the settings screenshot.
    NetworkDef& fixtureNet = v.networks[0];
    fixtureNet.rpc.roundRobin = true;
    if (fixtureNet.rpc.nodes.size() > 1) fixtureNet.rpc.nodes[1].enabled = false;
    // The Assets page needs an Atomic pool to reach its grid at all.
    if (fixtureNet.atomic.empty())
        fixtureNet.atomic.nodes.push_back({"https://aa.example.invalid", "", 0, true});
    for (const auto& node : fixtureNet.rpc.enabledSorted())
        state.health[eosId].push_back(
            {NodeType::Rpc, node->url, node->nickname, true, true, 42, "", ""});
    if (!fixtureNet.hyperion.empty())
        state.health[eosId].push_back({NodeType::Hyperion,
                                       fixtureNet.hyperion.nodes[0].url,
                                       fixtureNet.hyperion.nodes[0].nickname, false, false,
                                       -1, "", "HTTP 502"});
    // Oracle prices for the dashboard (fresh stamp suppresses the fetch).
    state.prices.usd = {{priceKey("eosio.token", "EOS"), 0.5123},
                        {priceKey("core.vaulta", "A"), 0.5123}};
    state.prices.fetchedAt = now;
    // A busy board: classic tiles + every pinnable extra + the fixture pins
    // (their tiles are appended below once the pins exist).
    v.dashboardTiles = defaultDashboard();
    v.dashboardTiles.push_back({"ram", 1});
    v.dashboardTiles.push_back({"chaininfo", 1});
    v.dashboardTiles.push_back({"prices", 1});
    v.dashboardTiles.push_back({"schedules", 1});

    v.keys = {{"PUB_K1_6MRyAjQq8ud7hVNYcfnVPJqcVpscN5So8BhtHuGYqET5GDW5CV", "", "main key",
               now - 86400 * 30},
              {"PUB_K1_8Sw3QYCyJ7DoRUqBLUuS54FvUC9GkJPBc4jbTn6cQj7cNbLDwo", "", "cold key",
               now - 86400 * 7}};
    v.accounts = {{eosId, "seafarer.gm", "active", v.keys[0].pub, false},
                  {waxId, "deckhand.gm", "active", "", true}};

    guard::WhitelistRule trusted;
    trusted.id = "qa-rule-1";
    trusted.chainId = eosId;
    trusted.signer = "seafarer.gm@active";
    trusted.contract = "eosio.token";
    trusted.action = "transfer";
    trusted.autoSign = true;
    trusted.pin = guard::ContractPin{"aa11bb22cc33dd44ee55ff66aa11bb22cc33dd44",
                                     "1122334455667788991122334455667788991122", now - 86400};
    guard::ParamConstraint to;
    to.kind = guard::ConstraintKind::Exact;
    to.values = {dwarfkit::json("coldwallet.x")};
    guard::ParamConstraint quantity;
    quantity.kind = guard::ConstraintKind::Range;
    quantity.max = dwarfkit::json("100.0000 EOS");
    trusted.params = {{"to", to}, {"quantity", quantity}};
    trusted.note = "weekly stack to cold storage";
    trusted.useCount = 12;
    trusted.lastUsedAt = now - 3600;

    guard::WhitelistRule stale = trusted;
    stale.id = "qa-rule-2";
    stale.contract = "somedapp.gm";
    stale.action = "claim";
    stale.autoSign = false;
    stale.status = guard::RuleStatus::Stale;
    stale.observedCodeHash = "ff00ff00ff00ff00ff00ff00ff00ff00ff00ff00";
    stale.observedAbiHash = "0e0e0e0e0e0e0e0e0e0e0e0e0e0e0e0e0e0e0e0e";
    stale.note = "dapp claim";

    guard::WhitelistRule plain;
    plain.id = "qa-rule-3";
    plain.chainId = "*";
    plain.signer = "*@*";
    plain.contract = "eosio";
    plain.action = "voteproducer";
    plain.note = "any vote";
    v.rules = {trusted, stale, plain};

    Schedule stack;
    stack.id = "qa-sched-1";
    stack.label = "stack to cold wallet";
    stack.chainId = eosId;
    stack.actor = "seafarer.gm";
    stack.contract = "eosio.token";
    stack.action = "transfer";
    stack.amountMode = Schedule::AmountPercent;
    stack.amountPercent = 25.0;
    stack.amountTokenCode = "EOS";
    stack.amountField = "quantity";
    stack.timingMode = Schedule::TimeDaily;  // exercise the exact-time card line
    stack.dailySec = 9 * 3600;
    stack.intervalSec = 86400;
    stack.endAt = now + 30 * 86400;
    stack.nextRunAt = now + 5 * 86400;  // far future: the ticker stays quiet
    stack.lastRunAt = now - 2 * 86400;
    stack.lastResult = "signed 4f9c2a...d81 - sent 12.5000 EOS";

    Schedule blocked = stack;
    blocked.id = "qa-sched-2";
    blocked.label = "keep vote proxied";
    blocked.contract = "eosio";
    blocked.action = "voteproducer";
    blocked.amountMode = Schedule::AmountFixed;
    blocked.timingMode = Schedule::TimeAnchored;  // and the grid card line
    blocked.dailySec = -1;
    blocked.intervalSec = 7 * 86400;
    blocked.startAt = now - 3600;
    blocked.endAt = 0;
    blocked.lastResult = "blocked: schedule blocked: no matching pinned auto-sign rule";
    v.schedules = {stack, blocked};

    // Address book with a saved exchange memo, msig templates and contract
    // bookmarks - the new predisposed-data surfaces.
    v.contacts = {{"treasury.gm", "ops treasury", eosId, ""},
                  {"binancecleos", "Binance deposit", "", "100234567"}};
    MsigTemplate payroll;
    payroll.id = "qa-tpl-1";
    payroll.label = "weekly payroll";
    payroll.proposalName = "payroll";
    payroll.requested = "seafarer.gm@active, deckhand.gm@active";
    payroll.expireHours = 96;
    payroll.actions = dwarfkit::json::array(
        {{{"account", "eosio.token"},
          {"name", "transfer"},
          {"authorization", dwarfkit::json::array(
                                {{{"actor", "treasury.gm"}, {"permission", "active"}}})},
          {"data",
           {{"from", "treasury.gm"}, {"to", "crew.gm"}, {"quantity", "250.0000 EOS"},
            {"memo", "wages"}}}}});
    v.msigTemplates = {payroll};
    v.savedContracts = {{eosId, "eosio.token", ""},
                        {eosId, "atomicassets", ""},
                        {eosId, "swap.defi", ""}};
    v.savedActions = {{eosId, "eosio.token", "transfer"},
                      {eosId, "eosio", "delegatebw"}};

    // A loaded contract so the Contracts page shows the identity card, the
    // action list (with bookmark pins) and the table browser.
    ContractsViewState& cv = state.contracts;
    cv.account = "eosio.token";
    cv.codeHash = "aa11bb22cc33dd44ee55ff66aa11bb22cc33dd44aa11bb22cc33dd44ee55ff66";
    cv.abiHash = "1122334455667788991122334455667788991122334455667788991122334455";
    auto abi = std::make_shared<dwarfkit::ABI>();
    dwarfkit::ABI::Struct transferStruct;
    transferStruct.name = "transfer";
    transferStruct.fields = {{"from", "name"},
                             {"to", "name"},
                             {"quantity", "asset"},
                             {"memo", "string"}};
    abi->structs.push_back(transferStruct);
    dwarfkit::ABI::Struct issueStruct;
    issueStruct.name = "issue";
    issueStruct.fields = {{"to", "name"}, {"quantity", "asset"}, {"memo", "string"}};
    abi->structs.push_back(issueStruct);
    dwarfkit::ABI::Action transferAction;
    transferAction.name = dwarfkit::Name::from("transfer");
    transferAction.type = "transfer";
    abi->actions.push_back(transferAction);
    dwarfkit::ABI::Action issueAction;
    issueAction.name = dwarfkit::Name::from("issue");
    issueAction.type = "issue";
    abi->actions.push_back(issueAction);
    dwarfkit::ABI::Table accountsTable;
    accountsTable.name = dwarfkit::Name::from("accounts");
    accountsTable.type = "account";
    abi->tables.push_back(accountsTable);
    dwarfkit::ABI::Table statTable;
    statTable.name = dwarfkit::Name::from("stat");
    statTable.type = "currency_stats";
    abi->tables.push_back(statTable);
    cv.abi = abi;

    // Assets grid + detail sheet.
    AssetsViewState& av = state.assets;
    av.owner = "seafarer.gm";
    av.fetchedAt = now;
    av.assets = dwarfkit::json{
        {"data",
         dwarfkit::json::array(
             {{{"asset_id", "1099511627776"},
               {"name", "Tackle Crate"},
               {"owner", "seafarer.gm"},
               {"template_mint", "42"},
               {"collection", {{"collection_name", "tacklecrates"}}},
               {"schema", {{"schema_name", "crates"}}}},
              {{"asset_id", "1099511627777"},
               {"name", "Rusty Anchor"},
               {"owner", "seafarer.gm"},
               {"template_mint", "7"},
               {"collection", {{"collection_name", "tacklecrates"}}},
               {"schema", {{"schema_name", "relics"}}}}})}};

    PinnedQuery pin;
    pin.id = "qa-pin-1";
    pin.chainId = eosId;
    pin.label = "oracle median";
    pin.contract = "oracle.gm";
    pin.table = "prices";
    pin.fieldPath = "median";
    v.pinnedQueries = {pin};
    v.dashboardTiles.push_back({"pin:qa-pin-1", 1});
    PinnedData& pinData = state.pinnedData["qa-pin-1"];
    pinData.rows = dwarfkit::json::array({{{"median", "1.2345 USD"}, {"age", 12}}});
    pinData.fetchedAt = now;

    v.audit = {{now - 120, eosId, "seafarer.gm@active", "eosio.token::transfer", "4f9c2a1b",
                "auto-signed", true},
               {now - 5200, eosId, "seafarer.gm@active", "eosio::voteproducer", "8ac1d200",
                "trusted", true},
               {now - 9000, eosId, "seafarer.gm@active", "somedapp.gm::claim", "",
                "stale-pin", false},
               {now - 86000, eosId, "seafarer.gm@active", "eosio::delegatebw", "77aa19",
                "unlisted", true}};
    v.security.allowAutoSign = true;
    v.version += 1;

    state.selectedChainId = eosId;
    state.selectedAccount = 0;

    AccountData& acct = state.accountData[eosId + "|seafarer.gm"];
    acct.loaded = true;
    acct.snap.actor = "seafarer.gm";
    acct.snap.fetchedAt = now;
    acct.snap.balances = {{"eosio.token", "1234.5678 EOS"}, {"core.vaulta", "500.0000 A"}};
    acct.snap.cpuUs = {5300, 42000};
    acct.snap.netBytes = {1200, 190000};
    acct.snap.ramBytes = {5100, 12288};
    acct.snap.raw = dwarfkit::json{
        {"created", "2021-04-02T11:22:33.000"},
        {"core_liquid_balance", "1234.5678 EOS"},
        {"voter_info",
         {{"proxy", ""},
          {"staked", 2000000},  // 200.0000 EOS total -> 40 delegated out
          {"producers", dwarfkit::json::array({"teamgreymass", "aus1genereos"})}}},
        {"self_delegated_bandwidth",
         {{"cpu_weight", "150.0000 EOS"}, {"net_weight", "10.0000 EOS"}}},
        {"refund_request",
         {{"cpu_amount", "3.0000 EOS"},
          {"net_amount", "2.0000 EOS"},
          {"request_time", "2026-08-30T10:00:00"}}}};

    // Chain-scoped view fixtures, stamped fresh so nothing fetches.
    ExploreViewState& ex = state.explore;
    ex.infoFetchedAt = nowMs();
    ex.info = dwarfkit::json{{"head_block_num", 421337421},
                             {"last_irreversible_block_num", 421337090},
                             {"head_block_producer", "teamgreymass"},
                             {"block_cpu_limit", 200000},
                             {"server_version_string", "v5.0.2"}};
    for (int i = 0; i < 6; ++i)
        ex.recentBlocks.push_back(dwarfkit::json{{"block_num", 421337421 - i},
                                                 {"id", "0x00"},
                                                 {"producer", "teamgreymass"},
                                                 {"timestamp", "2026-08-31T10:15:0" +
                                                                   std::to_string(i)},
                                                 {"tx_count", 14 - i}});

    ResourcesViewState& rv = state.resources;
    rv.ram.pricePerKb = "0.0132 EOS";
    rv.ram.baseBytes = 68719476736;
    rv.ram.quoteBalance = "4123456.7890 EOS";
    rv.ram.fetchedAt = now;
    rv.delegations = dwarfkit::json{
        {"rows", dwarfkit::json::array(
                     {{{"from", "seafarer.gm"}, {"to", "seafarer.gm"},
                       {"net_weight", "10.0000 EOS"}, {"cpu_weight", "150.0000 EOS"}},
                      {{"from", "seafarer.gm"}, {"to", "workerbee.gm"},
                       {"net_weight", "5.0000 EOS"}, {"cpu_weight", "25.0000 EOS"}}})}};
    rv.delegationsFetchedAt = now;
    rv.delegationsActor = "seafarer.gm";

    GovernanceViewState& gv = state.governance;
    gv.fetchedAt = now;
    gv.proxiesFetchedAt = now;
    gv.proxies = {
        {"greymassvote", "Greymass Fuel", "", "https://greymass.com", 8.1e15,
         415651401.0, 21, true},
        {"brockpierce1", "Brock Pierce", "", "", 4.9e15, 224299415.0, 22, true},
        {"colinproxy11", "Colin Talks Crypto", "", "", 2.2e15, 48834349.0, 28, true},
        {"investingwad", "Investing WAD", "", "", 9.5e14, 39748259.0, 8, true},
        {"sleepyproxy1", "Sleepy", "", "", 1.1e14, 3985252.0, 24, false}};
    dwarfkit::json producerRows = dwarfkit::json::array();
    const char* producers[] = {"teamgreymass", "aus1genereos", "eosnationftw", "bp.defibox",
                               "newdex.bp",    "big.one",      "eoscannonchn", "atticlabeosb"};
    for (const char* producer : producers)
        producerRows.push_back(dwarfkit::json{{"owner", producer},
                                              {"is_active", 1},
                                              {"url", std::string("https://") + producer +
                                                          ".example"}});
    gv.producers = dwarfkit::json{{"rows", producerRows}};

    MsigViewState& mv = state.msig;
    mv.draftActions.push_back(dwarfkit::json{
        {"account", "eosio.token"},
        {"name", "transfer"},
        {"authorization", dwarfkit::json::array(
                              {{{"actor", "seafarer.gm"}, {"permission", "active"}}})},
        {"data",
         {{"from", "seafarer.gm"}, {"to", "treasury.gm"}, {"quantity", "50.0000 EOS"},
          {"memo", "ops budget"}}}});
}

std::shared_ptr<SignPrompt> makeSignPrompt(const AppState& state) {
    auto prompt = std::make_shared<SignPrompt>();
    prompt->id = 0;  // never resolved; QA only renders
    prompt->chainId = state.selectedChainId;
    prompt->chainName = "Vaulta";
    prompt->signer = "seafarer.gm@active";
    prompt->overall = guard::VerdictLevel::Unlisted;
    prompt->hashesVerified = true;
    prompt->expiresAtMs = nowMs() + 95 * 1000;
    // Show the dapp-link provenance row + UNLINK escape hatch.
    prompt->linkSessionId = "qa-link-1";
    prompt->linkAppName = "dapp.example";

    SignPrompt::ActionView transfer;
    transfer.contract = "eosio.token";
    transfer.action = "transfer";
    transfer.authorization = "seafarer.gm@active";
    transfer.data = dwarfkit::json{{"from", "seafarer.gm"},
                                   {"to", "coldwallet.x"},
                                   {"quantity", "25.0000 EOS"},
                                   {"memo", "stacked by TackleBox 2026-08-31"}};
    transfer.verdict.level = guard::VerdictLevel::Trusted;
    transfer.verdict.ruleId = "qa-rule-1";
    transfer.verdict.ruleNote = "weekly stack to cold storage";

    SignPrompt::ActionView auth;
    auth.contract = "eosio";
    auth.action = "updateauth";
    auth.authorization = "seafarer.gm@active";
    auth.data = dwarfkit::json{{"account", "seafarer.gm"},
                               {"permission", "active"},
                               {"parent", "owner"}};
    auth.verdict.level = guard::VerdictLevel::Unlisted;
    auth.verdict.detail = "no whitelist rule covers this action";
    prompt->actions = {transfer, auth};

    prompt->risks.push_back({guard::RiskSeverity::Critical, "perm-change",
                             "modifies account permissions (updateauth); a malicious "
                             "version of this hands over the account",
                             1});
    return prompt;
}

// --- the tour ----------------------------------------------------------------

void showLocked(AppState& state, Controller&) {
    state.vaultExists = true;
    state.unlocked = false;
    state.standbyLocked = false;
}

void showShellPage(AppState& state, Controller& controller, Page page) {
    state.vaultExists = true;
    state.unlocked = true;
    state.standbyLocked = false;
    injectFixtures(state);
    state.signPrompt.reset();
    state.pluginPrompt.reset();
    tb::ui::closeRuleEditor();
    tb::ui::closeAutopilotModals();
    state.page = page;
    controller.noteActivity();
}

const Step kSteps[] = {
    {"00-onboarding",
     [](AppState& state, Controller&) {
         state.vaultExists = false;
         state.unlocked = false;
     }},
    {"01-unlock", [](AppState& state, Controller& c) { showLocked(state, c); }},
    {"02-setup", [](AppState& s, Controller& c) { showShellPage(s, c, Page::Setup); }},
    {"03-dashboard", [](AppState& s, Controller& c) { showShellPage(s, c, Page::Dashboard); }},
    {"03b-dashboard-tiles",
     [](AppState& s, Controller& c) {
         showShellPage(s, c, Page::Dashboard);
         g_pageScroll = ::ui::S(820.0f);  // the pinned/extra tiles below the hero
     }},
    {"04-explore", [](AppState& s, Controller& c) { showShellPage(s, c, Page::Explore); }},
    {"05-transfer", [](AppState& s, Controller& c) { showShellPage(s, c, Page::Transfer); }},
    {"06-assets", [](AppState& s, Controller& c) { showShellPage(s, c, Page::Assets); }},
    {"07-contracts", [](AppState& s, Controller& c) { showShellPage(s, c, Page::Contracts); }},
    {"08-resources", [](AppState& s, Controller& c) { showShellPage(s, c, Page::Resources); }},
    {"09-governance",
     [](AppState& s, Controller& c) { showShellPage(s, c, Page::Governance); }},
    {"10-msig", [](AppState& s, Controller& c) { showShellPage(s, c, Page::Msig); }},
    {"11-autopilot", [](AppState& s, Controller& c) { showShellPage(s, c, Page::Autopilot); }},
    {"12-whitelist", [](AppState& s, Controller& c) { showShellPage(s, c, Page::Whitelist); }},
    {"13-vault", [](AppState& s, Controller& c) { showShellPage(s, c, Page::Vault); }},
    {"14-history", [](AppState& s, Controller& c) { showShellPage(s, c, Page::History); }},
    {"15-settings", [](AppState& s, Controller& c) { showShellPage(s, c, Page::Settings); }},
    {"15b-settings-oracle",
     [](AppState& s, Controller& c) {
         showShellPage(s, c, Page::Settings);
         g_pageScroll = ::ui::S(820.0f);  // land on the first network's oracle row
     }},
    {"16-signmodal",
     [](AppState& state, Controller& c) {
         showShellPage(state, c, Page::Dashboard);
         state.signPrompt = makeSignPrompt(state);
     }},
    {"17-createaccount",
     [](AppState& s, Controller& c) { showShellPage(s, c, Page::CreateAccount); }},
    {"18-settings-policies",
     [](AppState& s, Controller& c) {
         showShellPage(s, c, Page::Settings);
         // One network keeps the networks card short so the scroll lands on
         // the security policy + startup/background cards.
         if (s.vault.networks.size() > 1) s.vault.networks.resize(1);
         g_pageScroll = ::ui::S(1890.0f);
     }},
    {"19-settings-about",
     [](AppState& s, Controller& c) {
         showShellPage(s, c, Page::Settings);
         if (s.vault.networks.size() > 1) s.vault.networks.resize(1);
         // A pending update so the About card shows the notify-only flow.
         s.update.available = true;
         s.update.latestTag = "v9.9.9";
         s.update.releaseUrl = "https://github.com/on-a-t-break/tacklebox/releases";
         s.update.notes = "QA fixture release: illustrative notes for the About card.";
         s.update.checkedAt = nowSec();
         g_pageScroll = ::ui::S(99999.0f);  // clamps to the page bottom
     }},
    // Interaction steps: surfaces invisible until clicked, opened for the
    // same padding/spacing/design pass as everything else.
    {"20-chain-menu",
     [](AppState& s, Controller& c) {
         showShellPage(s, c, Page::Dashboard);
         g_forceOpenTag = "chain-menu";
     }},
    {"21-account-menu",
     [](AppState& s, Controller& c) {
         showShellPage(s, c, Page::Dashboard);
         // Pinned sections show in the switcher: group the fixture accounts.
         if (!s.vault.accountGroups.empty() || s.vault.accounts.empty()) {
         } else {
             s.vault.accountGroups = {"Daily drivers", "Cold storage"};
             s.vault.accounts[0].group = "Daily drivers";
         }
         g_forceOpenTag = "account-menu";
     }},
    {"22-rule-editor",
     [](AppState& s, Controller& c) {
         showShellPage(s, c, Page::Whitelist);
         guard::WhitelistRule draft;
         draft.chainId = s.vault.networks.empty() ? "*" : s.vault.networks[0].chainId;
         draft.signer = "seafarer.gm@active";
         draft.contract = "eosio.token";
         draft.action = "transfer";
         guard::ParamConstraint to;
         to.kind = guard::ConstraintKind::Exact;
         to.values = {dwarfkit::json("coldwallet.x")};
         guard::ParamConstraint quantity;
         quantity.kind = guard::ConstraintKind::Range;
         quantity.max = dwarfkit::json("100.0000 EOS");
         draft.params = {{"to", to}, {"quantity", quantity}};
         draft.note = "weekly stack to cold storage";
         tb::ui::openRuleEditor(draft);
     }},
    {"23-add-tile",
     [](AppState& s, Controller& c) {
         showShellPage(s, c, Page::Dashboard);
         // Leave something addable so the palette shows real entries.
         std::erase_if(s.vault.dashboardTiles,
                       [](const DashTile& t) { return t.kind == "prices"; });
         g_forceOpenTag = "add-tile";
         g_pageScroll = ::ui::S(99999.0f);  // palette button sits at the bottom
     }},
    {"24-endpoint-pools",
     [](AppState& s, Controller& c) {
         // Round-robin toggles + PING & RANK live at the top of the card.
         showShellPage(s, c, Page::Settings);
     }},
    {"25-token-combo",
     [](AppState& s, Controller& c) {
         showShellPage(s, c, Page::Transfer);
         g_forceOpenTag = "token-combo";
     }},
    {"26-vault-groups",
     [](AppState& s, Controller& c) {
         showShellPage(s, c, Page::Vault);
         // Seed pinned sections so the groups manager renders its design pass.
         if (s.vault.accountGroups.empty() && !s.vault.accounts.empty()) {
             s.vault.accountGroups = {"Daily drivers", "Cold storage"};
             s.vault.accounts[0].group = "Daily drivers";
         }
         g_pageScroll = ::ui::S(99999.0f);  // manager sits below the accounts
     }},
    {"27-schedule-editor",
     [](AppState& s, Controller& c) {
         showShellPage(s, c, Page::Autopilot);
         tb::ui::openScheduleEditor();
         g_forceOpenTag = "sched-timing";  // the timing-mode dropdown, open
     }},
    {"28-msig-template",
     [](AppState& s, Controller& c) {
         showShellPage(s, c, Page::Msig);
         tb::ui::openMsigTemplateEditor();
     }},
    {"29-governance-proxies",
     [](AppState& s, Controller& c) {
         showShellPage(s, c, Page::Governance);
         g_forceOpenTag = "gov-proxy-tab";  // flips the view to the PROXY tab
     }},
    {"30-resources-stake",
     [](AppState& s, Controller& c) {
         showShellPage(s, c, Page::Resources);
         g_forceOpenTag = "res-stake-tab";  // stake grid + delegations table
         g_pageScroll = ::ui::S(300.0f);    // land on Self/Delegated + delband
     }},
    {"31-contacts-combo",
     [](AppState& s, Controller& c) {
         showShellPage(s, c, Page::Transfer);
         g_forceOpenTag = "contacts-combo";
     }},
    {"32-contact-editor",
     [](AppState& s, Controller& c) {
         showShellPage(s, c, Page::Transfer);
         Contact prefill;
         prefill.actor = "binancecleos";
         prefill.label = "Binance deposit";
         prefill.memo = "100234567";
         tb::ui::openContactEditor(prefill, true);
     }},
    {"33-settings-framerate",
     [](AppState& s, Controller& c) {
         showShellPage(s, c, Page::Settings);
         if (s.vault.networks.size() > 1) s.vault.networks.resize(1);
         g_forceOpenTag = "fps-background";
         g_pageScroll = ::ui::S(99999.0f);  // appearance card sits near the bottom
     }},
    {"34-unlock-standby",
     [](AppState& state, Controller& c) {
         showLocked(state, c);
         state.standbyLocked = true;  // the note must clear the tagline
     }},
    // Full modal/dropdown coverage: every surface that only exists after a
    // click gets its own opened shot.
    {"35-receive-qr",
     [](AppState& s, Controller& c) {
         showShellPage(s, c, Page::Dashboard);
         g_forceOpenTag = "receive";
     }},
    {"36-more-sheet",
     [](AppState& s, Controller& c) {
         showShellPage(s, c, Page::Dashboard);
         g_forceOpenTag = "more-sheet";  // phone bottom-bar sheet
     }},
    {"37-reveal-key",
     [](AppState& s, Controller& c) {
         showShellPage(s, c, Page::Vault);
         if (!s.vault.keys.empty()) tb::ui::openRevealModal(s.vault.keys[0].pub);
     }},
    {"38-rule-del-confirm",
     [](AppState& s, Controller& c) {
         showShellPage(s, c, Page::Whitelist);
         g_forceOpenTag = "rule-del-confirm";
     }},
    {"39-key-del-confirm",
     [](AppState& s, Controller& c) {
         showShellPage(s, c, Page::Vault);
         g_forceOpenTag = "key-del-confirm";
     }},
    {"40-import-confirm",
     [](AppState& s, Controller& c) {
         showShellPage(s, c, Page::Settings);
         g_forceOpenTag = "import-confirm";
     }},
    {"41-asset-detail",
     [](AppState& s, Controller& c) {
         showShellPage(s, c, Page::Assets);
         if (s.assets.assets.contains("data") && !s.assets.assets["data"].empty())
             s.assets.selected = s.assets.assets["data"][0];
     }},
    {"42-contracts-actions",
     [](AppState& s, Controller& c) {
         // Loaded contract: identity card, bookmark pin, generated form.
         showShellPage(s, c, Page::Contracts);
     }},
    {"43-contracts-table-combo",
     [](AppState& s, Controller& c) {
         showShellPage(s, c, Page::Contracts);
         g_forceOpenTag = "table-combo";  // also flips to the TABLES tab
     }},
    {"44-sign-whitelist-stacked",
     [](AppState& state, Controller& c) {
         // The in-prompt whitelist flow: rule editor stacked over the
         // signing modal, prefilled from the action.
         showShellPage(state, c, Page::Dashboard);
         state.signPrompt = makeSignPrompt(state);
         guard::WhitelistRule draft;
         draft.chainId = state.signPrompt->chainId;
         draft.signer = state.signPrompt->signer;
         draft.contract = "eosio";
         draft.action = "updateauth";
         draft.note = "from signing request";
         tb::ui::openRuleEditor(draft);
     }},
    {"45-vault-grouppick",
     [](AppState& s, Controller& c) {
         showShellPage(s, c, Page::Vault);
         if (s.vault.accountGroups.empty() && !s.vault.accounts.empty())
             s.vault.accountGroups = {"Daily drivers", "Cold storage"};
         g_forceOpenTag = "group-pick";
     }},
    {"46-create-keymode",
     [](AppState& s, Controller& c) {
         showShellPage(s, c, Page::CreateAccount);
         g_forceOpenTag = "ca-keymode";
     }},
    {"47-network-preset",
     [](AppState& s, Controller& c) {
         showShellPage(s, c, Page::Settings);
         if (s.vault.networks.size() > 1) s.vault.networks.resize(1);
         g_forceOpenTag = "net-preset";  // the add-network preset picker
         g_pageScroll = ::ui::S(700.0f);
     }},
    {"48-saved-contracts",
     [](AppState& s, Controller& c) {
         showShellPage(s, c, Page::Contracts);
         g_forceOpenTag = "saved-contracts";
     }},
    {"49-saved-actions",
     [](AppState& s, Controller& c) {
         showShellPage(s, c, Page::Contracts);
         g_forceOpenTag = "saved-actions";
     }},
    {"50-plugin-prompt",
     [](AppState& state, Controller& c) {
         showShellPage(state, c, Page::Dashboard);
         auto prompt = std::make_shared<PluginPrompt>();
         prompt->title = "Resource provider";
         prompt->body = "The transaction service offers to cover CPU for this "
                        "transaction.";
         prompt->lines = {"fee: 0.0150 EOS", "provider: fuel.example"};
         state.pluginPrompt = prompt;
     }},
    {"51-wizard-pipeline",
     [](AppState& s, Controller& c) {
         showShellPage(s, c, Page::Autopilot);
         tb::ui::openAutoStakeWizard(0);
     }},
    {"52-wizard-stake",
     [](AppState& s, Controller& c) {
         showShellPage(s, c, Page::Autopilot);
         tb::ui::openAutoStakeWizard(3);
         g_forceOpenTag = "wiz-stake-res";  // the CPU/NET picker, open
     }},
    {"53-wizard-review",
     [](AppState& s, Controller& c) {
         showShellPage(s, c, Page::Autopilot);
         tb::ui::openAutoStakeWizard(5);
     }},
    {"54-whitelist-import-tx",
     [](AppState& s, Controller& c) {
         showShellPage(s, c, Page::Whitelist);
         g_forceOpenTag = "wl-import-tx";
     }},
    {"55-create-authorities",
     [](AppState& s, Controller& c) {
         showShellPage(s, c, Page::CreateAccount);
         g_forceOpenTag = "ca-advanced";  // expands both authority editors
     }},
};
constexpr int kStepCount = static_cast<int>(sizeof(kSteps) / sizeof(kSteps[0]));
constexpr int kSettleFrames = 6;

}  // namespace

void configure(const std::string& outDir) {
    g_dir = outDir;
    g_active = true;
    std::error_code ec;
    std::filesystem::create_directories(outDir, ec);
}

bool active() { return g_active; }

float pageScrollY() { return g_pageScroll; }

bool forceOpen(const char* tag) {
    if (!g_active || g_forceOpenUsed || g_forceOpenTag != tag) return false;
    g_forceOpenUsed = true;
    return true;
}

bool wantsOpen(const char* tag) { return g_active && g_forceOpenTag == tag; }

void openCombo(const char* label) {
    // BeginCombo's popup id, per imgui internals; must run in the same ID
    // stack position as the upcoming BeginCombo call.
    ImGuiID id = ImGui::GetCurrentWindow()->GetID(label);
    ImGuiID popupId = ImHashStr("##ComboPopup", 0, id);
    if (!ImGui::IsPopupOpen(popupId, ImGuiPopupFlags_None))
        ImGui::OpenPopupEx(popupId, ImGuiPopupFlags_None);
}

bool beforeFrame(AppState& state, Controller& controller) {
    g_forceOpenUsed = false;
    if (!g_active) return true;
    if (g_framesLeft > 0) {
        --g_framesLeft;
        if (g_framesLeft == 0) g_captureDue = true;  // capture on this frame
        return true;
    }
    // Advance to the next step (after the previous frame's capture happened).
    ++g_stepIndex;
    if (g_stepIndex >= kStepCount) return false;  // tour complete: quit
    g_pageScroll = -1.0f;  // steps opt back in from enter()
    g_forceOpenTag.clear();
    const Step& step = kSteps[g_stepIndex];
    step.enter(state, controller);
    g_pendingName = factorTag() + "-" + step.name;
    g_framesLeft = kSettleFrames;
    return true;
}

void capture(SDL_Window* window) {
    if (!g_active || !g_captureDue) return;
    g_captureDue = false;

    int w = 0, h = 0;
    SDL_GetWindowSizeInPixels(window, &w, &h);
    if (w <= 0 || h <= 0) return;
    std::vector<unsigned char> pixels(static_cast<size_t>(w) * h * 4);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    // GL reads bottom-up; PNG wants top-down.
    std::vector<unsigned char> flipped(pixels.size());
    size_t rowBytes = static_cast<size_t>(w) * 4;
    for (int y = 0; y < h; ++y)
        std::memcpy(&flipped[static_cast<size_t>(y) * rowBytes],
                    &pixels[static_cast<size_t>(h - 1 - y) * rowBytes], rowBytes);
    std::string path = (std::filesystem::path(g_dir) / (g_pendingName + ".png")).string();
    if (stbi_write_png(path.c_str(), w, h, 4, flipped.data(), static_cast<int>(rowBytes)))
        Log::info("qa: wrote %s", path.c_str());
    else
        Log::error("qa: failed to write %s", path.c_str());
}

}  // namespace tb::ui::qa
