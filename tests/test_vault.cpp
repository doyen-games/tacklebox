#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <sstream>

#include "core/paths.hpp"
#include "vault/vault.hpp"

using namespace tb;

static SecureBytes pw(const char* s) { return SecureBytes(std::string_view(s)); }

// Each test starts from a clean vault file inside the sandboxed data dir.
static void resetVaultFile() {
    std::error_code ec;
    std::filesystem::remove(vaultFile(), ec);
    std::filesystem::remove(vaultFile().string() + ".bak", ec);
}

TEST_CASE("create, populate, lock, unlock round trip") {
    resetVaultFile();
    std::string pub;
    {
        Vault vault;
        REQUIRE(vault.create(pw("hunter2hunter2")));
        REQUIRE(Vault::fileExists());

        auto key = vault.generateKey("main key");
        REQUIRE(key.has_value());
        pub = *key;
        CHECK(pub.rfind("PUB_K1_", 0) == 0);

        vault.addAccount({"chainA", "alice", "active", pub, false});

        guard::WhitelistRule rule;
        rule.id = "r1";
        rule.chainId = "chainA";
        rule.signer = "alice@active";
        rule.contract = "eosio.token";
        rule.action = "transfer";
        vault.upsertRule(rule);

        NetworkDef net;
        net.chainId = "chainA";
        net.name = "Test Net";
        net.rpc.nodes = {{"https://example.invalid", "primary", 0, true},
                         {"https://backup.invalid", "backup", 1, false}};
        net.rpc.mode = static_cast<int>(SelectMode::Auto);
        net.rpc.autoThresholdQueries = 200;
        net.rpc.autoWindowSec = 30;
        net.atomic.nodes = {{"https://aa.example.invalid", "", 0, true}};
        net.light.nodes = {{"https://light.example.invalid", "", 0, true}};
        net.lightSlug = "testa";
        net.tokens = {{"alien.worlds", "TLM"}};
        vault.upsertNetwork(net);

        vault.security().autoLockMinutes = 42;
        vault.security().useResourceProvider = true;
        vault.appendAudit({1, "chainA", "alice@active", "eosio.token::transfer", "", "trusted",
                           true});

        PinnedQuery pin;
        pin.id = "pin1";
        pin.chainId = "chainA";
        pin.label = "oracle";
        pin.contract = "oracle.acct";
        pin.table = "prices";
        pin.fieldPath = "median";
        vault.upsertPinnedQuery(pin);

        Schedule schedule;
        schedule.id = "sched1";
        schedule.label = "weekly vote";
        schedule.chainId = "chainA";
        schedule.actor = "alice";
        schedule.contract = "eosio";
        schedule.action = "voteproducer";
        schedule.data = dwarfkit::json{{"voter", "alice"}};
        schedule.intervalSec = 604800;
        schedule.amountMode = Schedule::AmountPercent;
        schedule.amountField = "quantity";
        schedule.amountPercent = 12.5;
        schedule.amountTokenContract = "eosio.token";
        schedule.amountTokenCode = "WAX";
        schedule.amountReserve = "1.0000 WAX";
        vault.upsertSchedule(schedule);

        vault.setLastChain("chainA");

        LinkSession link;
        link.id = "link1";
        link.appName = "dapp.example";
        link.chainId = "chainA";
        link.actor = "alice";
        link.permission = "active";
        link.requestKeyWif = "5KQFakeKeyForRoundTripOnly111111111111111111111111";
        link.channelId = "chan-uuid";
        vault.upsertLinkSession(link);

        vault.setLastAccount("chainA|alice|active");

        vault.upsertContact({"exchangehot1", "OKX deposit", "chainA"});
        vault.upsertContact({"friendaccnt1", "roommate", ""});
        vault.upsertContact({"friendaccnt1", "roommate (renamed)", ""});  // upsert
        REQUIRE(vault.markKeyBackedUp(pub));
        CHECK_FALSE(vault.markKeyBackedUp(pub));  // already backed up
        vault.stampBackup();

        REQUIRE(vault.save());
        vault.lock();
        CHECK_FALSE(vault.unlocked());
        CHECK(vault.keys().empty());
        CHECK(vault.linkSessions().empty());
    }
    {
        Vault vault;
        REQUIRE(vault.unlock(pw("hunter2hunter2")));
        REQUIRE(vault.keys().size() == 1);
        CHECK(vault.keys()[0].pub == pub);
        CHECK_FALSE(vault.wifFor(pub).empty());
        REQUIRE(vault.accounts().size() == 1);
        CHECK(vault.accounts()[0].actor == "alice");
        REQUIRE(vault.rules().size() == 1);
        CHECK(vault.rules()[0].contract == "eosio.token");
        REQUIRE(vault.networks().size() == 1);
        const NetworkDef& net = vault.networks()[0];
        REQUIRE(net.rpc.nodes.size() == 2);
        CHECK(net.rpc.nodes[0].nickname == "primary");
        CHECK_FALSE(net.rpc.nodes[1].enabled);
        CHECK(net.rpc.mode == static_cast<int>(SelectMode::Auto));
        CHECK(net.rpc.autoThresholdQueries == 200);
        CHECK(net.rpc.autoWindowSec == 30);
        // The disabled backup never becomes primary.
        CHECK(net.activeEndpoint() == "https://example.invalid");
        CHECK(net.atomic.primaryUrl() == "https://aa.example.invalid");
        CHECK(net.light.primaryUrl() == "https://light.example.invalid");
        CHECK(net.lightSlug == "testa");
        REQUIRE(net.tokens.size() == 1);
        CHECK(vault.networks()[0].tokens[0].code == "TLM");
        CHECK(vault.security().autoLockMinutes == 42);
        CHECK(vault.security().useResourceProvider);
        REQUIRE(vault.audit().size() == 1);
        REQUIRE(vault.pinnedQueries().size() == 1);
        CHECK(vault.pinnedQueries()[0].fieldPath == "median");
        REQUIRE(vault.schedules().size() == 1);
        CHECK(vault.schedules()[0].action == "voteproducer");
        CHECK(vault.schedules()[0].data.value("voter", "") == "alice");
        CHECK(vault.schedules()[0].intervalSec == 604800);
        CHECK(vault.schedules()[0].amountMode == Schedule::AmountPercent);
        CHECK(vault.schedules()[0].amountPercent == 12.5);
        CHECK(vault.schedules()[0].amountTokenCode == "WAX");
        CHECK(vault.schedules()[0].amountReserve == "1.0000 WAX");
        CHECK(vault.lastChain() == "chainA");
        REQUIRE(vault.linkSessions().size() == 1);
        CHECK(vault.linkSessions()[0].channelId == "chan-uuid");
        CHECK_FALSE(vault.linkSessions()[0].requestKeyWif.empty());
        CHECK(vault.lastAccount() == "chainA|alice|active");
        REQUIRE(vault.contacts().size() == 2);
        CHECK(vault.contacts()[1].label == "roommate (renamed)");
        CHECK(vault.keys()[0].backedUp);
        CHECK(vault.lastBackupAt() > 0);
        REQUIRE(vault.removeContact("exchangehot1", "chainA"));
        CHECK_FALSE(vault.removeContact("exchangehot1", "chainA"));
    }
}

TEST_CASE("audit log exports as CSV") {
    std::vector<AuditEntry> entries = {
        {1725148800, "chainA", "alice@active", "eosio.token::transfer to \"bob\"",
         "4f9c2a1b", "trusted", true},
        {1725148900, "chainA", "alice@active", "memo with, comma", "", "unlisted", false},
    };
    std::string csv = auditCsv(entries);
    CHECK(csv.find("time_utc,unix,chain,signer,summary,tx_id,verdict,approved") == 0);
    CHECK(csv.find("2024-09-01T00:00:00Z,1725148800") != std::string::npos);
    // Embedded quotes double, comma-bearing fields quote.
    CHECK(csv.find("\"eosio.token::transfer to \"\"bob\"\"\"") != std::string::npos);
    CHECK(csv.find("\"memo with, comma\"") != std::string::npos);
    CHECK(csv.find(",no\n") != std::string::npos);
    CHECK(csv.find(",yes\n") != std::string::npos);
}

TEST_CASE("network json: old vault shapes migrate to endpoint pools") {
    // Pre-endpoint-section shape: endpoints/active + single aaEndpoint. The
    // formerly-active URL must come out as the top-priority RPC node.
    dwarfkit::json old = {{"chain", "c1"},
                          {"name", "Old Net"},
                          {"endpoints", {"https://a.invalid", "https://b.invalid",
                                         "https://c.invalid"}},
                          {"active", 1},
                          {"aaEndpoint", "https://aa.invalid"},
                          {"coreSymbol", "8,WAX"}};
    NetworkDef migrated = networkFromJson(old);
    REQUIRE(migrated.rpc.nodes.size() == 3);
    CHECK(migrated.rpc.primaryUrl() == "https://b.invalid");
    auto sorted = migrated.rpc.enabledSorted();
    REQUIRE(sorted.size() == 3);
    CHECK(sorted[0]->url == "https://b.invalid");
    CHECK(sorted[1]->url == "https://a.invalid");
    CHECK(sorted[2]->url == "https://c.invalid");
    REQUIRE(migrated.atomic.nodes.size() == 1);
    CHECK(migrated.atomic.primaryUrl() == "https://aa.invalid");
    CHECK(migrated.hyperion.empty());
    CHECK(migrated.coreSymbol == "8,WAX");

    // Transitional shape: bare url arrays; index order becomes priority.
    dwarfkit::json transitional = {
        {"chain", "c2"},
        {"name", "Mid Net"},
        {"rpc", {{"urls", {"https://x.invalid", "https://y.invalid"}}}}};
    NetworkDef mid = networkFromJson(transitional);
    REQUIRE(mid.rpc.nodes.size() == 2);
    CHECK(mid.rpc.primaryUrl() == "https://x.invalid");
    CHECK(mid.rpc.nodes[1].priority == 1);

    // Current shape survives a to-json/from-json round trip.
    NetworkDef net;
    net.chainId = "c3";
    net.name = "New Net";
    net.rpc.nodes = {{"https://p.invalid", "home node", 0, true},
                     {"https://q.invalid", "", 1, false}};
    net.rpc.mode = static_cast<int>(SelectMode::RoundRobin);
    net.hyperion.nodes = {{"https://h.invalid", "hyp", 0, true}};
    net.hyperion.mode = static_cast<int>(SelectMode::Auto);
    net.hyperion.autoThresholdQueries = 200;
    net.hyperion.autoWindowSec = 30;
    net.light.nodes = {{"https://l.invalid", "", 0, true}};
    net.lightSlug = "cthree";
    net.oracle = {static_cast<int>(OracleProvider::CoinGecko),
                  "https://api.coingecko.com", "wax"};
    NetworkDef back = networkFromJson(networkToJson(net));
    CHECK(back.rpc.nodes.size() == 2);
    CHECK(back.rpc.nodes[0].nickname == "home node");
    CHECK_FALSE(back.rpc.nodes[1].enabled);
    CHECK(back.rpc.mode == static_cast<int>(SelectMode::RoundRobin));
    CHECK(back.hyperion.mode == static_cast<int>(SelectMode::Auto));
    CHECK(back.hyperion.autoThresholdQueries == 200);
    CHECK(back.hyperion.autoWindowSec == 30);
    CHECK(back.light.primaryUrl() == "https://l.invalid");
    CHECK(back.lightSlug == "cthree");
    CHECK(back.oracle.provider == static_cast<int>(OracleProvider::CoinGecko));
    CHECK(back.oracle.url == "https://api.coingecko.com");
    CHECK(back.oracle.coreId == "wax");
    // Pre-oracle vaults come through with the oracle off.
    CHECK(migrated.oracle.provider == static_cast<int>(OracleProvider::Off));
    // A disabled-only pool yields no primary.
    NetworkDef dark;
    dark.rpc.nodes = {{"https://off.invalid", "", 0, false}};
    CHECK(dark.rpc.primaryUrl().empty());
}

TEST_CASE("dashboard board: defaults, pin hooks, reorder, persistence") {
    resetVaultFile();
    {
        Vault vault;
        REQUIRE(vault.create(pw("board-password")));
        // Fresh vaults start with the classic layout.
        REQUIRE(vault.dashboardTiles().size() == defaultDashboard().size());
        CHECK(vault.dashboardTiles()[0].kind == "balance");
        CHECK(vault.dashboardTiles()[0].span == 2);

        // Pinning a query adds its tile; removing it drops the tile.
        PinnedQuery pin;
        pin.id = "p1";
        pin.contract = "oracle.acct";
        pin.table = "prices";
        vault.upsertPinnedQuery(pin);
        REQUIRE(vault.dashboardTiles().back().kind == "pin:p1");
        vault.upsertPinnedQuery(pin);  // idempotent: no duplicate tile
        int pinTiles = 0;
        for (const auto& tile : vault.dashboardTiles())
            if (tile.kind == "pin:p1") ++pinTiles;
        CHECK(pinTiles == 1);

        // Custom order + extra tiles persist.
        auto tiles = vault.dashboardTiles();
        tiles.push_back({"ram", 1});
        std::swap(tiles.front(), tiles.back());
        vault.setDashboardTiles(tiles);

        // Reorder helpers for the other draggable lists.
        vault.addAccount({"c", "alice", "active", "", true});
        vault.addAccount({"c", "bob", "active", "", true});
        REQUIRE(vault.reorderAccounts(1, 0));
        CHECK(vault.accounts()[0].actor == "bob");
        CHECK_FALSE(vault.reorderAccounts(5, 0));

        Schedule s1, s2;
        s1.id = "s1";
        s2.id = "s2";
        vault.upsertSchedule(s1);
        vault.upsertSchedule(s2);
        REQUIRE(vault.reorderSchedules(1, 0));
        CHECK(vault.schedules()[0].id == "s2");

        REQUIRE(vault.save());
    }
    {
        Vault vault;
        REQUIRE(vault.unlock(pw("board-password")));
        REQUIRE_FALSE(vault.dashboardTiles().empty());
        CHECK(vault.dashboardTiles().front().kind == "ram");  // swapped order kept
        CHECK(vault.schedules()[0].id == "s2");
        CHECK(vault.accounts()[0].actor == "bob");
        // Removing the pin drops its tile.
        REQUIRE(vault.removePinnedQuery("p1"));
        for (const auto& tile : vault.dashboardTiles()) CHECK(tile.kind != "pin:p1");
    }
}

TEST_CASE("wrong password fails, tampered file fails") {
    resetVaultFile();
    {
        Vault vault;
        REQUIRE(vault.create(pw("password-one")));
    }
    {
        Vault vault;
        auto res = vault.unlock(pw("password-two"));
        CHECK_FALSE(res.has_value());
        CHECK_FALSE(vault.unlocked());
    }
    // Flip one ciphertext nibble on disk.
    {
        std::ifstream in(vaultFile());
        std::stringstream ss;
        ss << in.rdbuf();
        std::string content = ss.str();
        auto pos = content.find("\"ciphertext\": \"");
        REQUIRE(pos != std::string::npos);
        pos += 15;
        content[pos] = content[pos] == 'a' ? 'b' : 'a';
        std::ofstream out(vaultFile(), std::ios::trunc);
        out << content;
    }
    {
        Vault vault;
        CHECK_FALSE(vault.unlock(pw("password-one")).has_value());
    }
}

TEST_CASE("change password re-encrypts") {
    resetVaultFile();
    {
        Vault vault;
        REQUIRE(vault.create(pw("old-password")));
        REQUIRE(vault.generateKey("k"));
        // Wrong current password is rejected.
        CHECK_FALSE(vault.changePassword(pw("nope-nope-nope"), pw("new-password")).has_value());
        REQUIRE(vault.changePassword(pw("old-password"), pw("new-password")));
    }
    {
        Vault vault;
        CHECK_FALSE(vault.unlock(pw("old-password")).has_value());
        REQUIRE(vault.unlock(pw("new-password")));
        CHECK(vault.keys().size() == 1);
    }
}

TEST_CASE("importing a bad key fails, removing a key degrades accounts") {
    resetVaultFile();
    Vault vault;
    REQUIRE(vault.create(pw("hunter2hunter2")));
    CHECK_FALSE(vault.importKey("not-a-key", "x").has_value());

    auto key = vault.generateKey("k");
    REQUIRE(key.has_value());
    vault.addAccount({"chainA", "alice", "active", *key, false});
    REQUIRE(vault.removeKey(*key));
    REQUIRE(vault.accounts().size() == 1);
    CHECK(vault.accounts()[0].watch);
    CHECK(vault.accounts()[0].pubKey.empty());
    CHECK(vault.wifFor(*key).empty());
}

TEST_CASE("short password refused") {
    resetVaultFile();
    Vault vault;
    CHECK_FALSE(vault.create(pw("short")).has_value());
}

TEST_CASE("export and import round trip") {
    resetVaultFile();
    std::string pub;
    {
        Vault vault;
        REQUIRE(vault.create(pw("portable-pass")));
        auto key = vault.generateKey("travel key");
        REQUIRE(key.has_value());
        pub = *key;
    }
    auto exportPath = std::filesystem::temp_directory_path() / "tb-export-test.tbx";
    std::filesystem::remove(exportPath);
    REQUIRE(Vault::exportTo(exportPath));
    REQUIRE(std::filesystem::exists(exportPath));

    // Replace the vault with a different one, then import the export back.
    resetVaultFile();
    {
        Vault other;
        REQUIRE(other.create(pw("other-password")));
    }
    REQUIRE(Vault::importFrom(exportPath));
    {
        Vault vault;
        CHECK_FALSE(vault.unlock(pw("other-password")).has_value());
        REQUIRE(vault.unlock(pw("portable-pass")));
        REQUIRE(vault.keys().size() == 1);
        CHECK(vault.keys()[0].pub == pub);
    }
    // The replaced vault was preserved as a backup.
    bool backupFound = false;
    for (const auto& entry : std::filesystem::directory_iterator(vaultFile().parent_path()))
        if (entry.path().filename().string().rfind("vault.tbx.replaced-", 0) == 0)
            backupFound = true;
    CHECK(backupFound);
    std::filesystem::remove(exportPath);
}

TEST_CASE("import rejects non-vault files") {
    auto bogus = std::filesystem::temp_directory_path() / "not-a-vault.tbx";
    {
        std::ofstream out(bogus);
        out << "{\"magic\": \"SOMETHING_ELSE\"}";
    }
    CHECK_FALSE(Vault::importFrom(bogus).has_value());
    {
        std::ofstream out(bogus, std::ios::trunc);
        out << "definitely not json";
    }
    CHECK_FALSE(Vault::importFrom(bogus).has_value());
    std::filesystem::remove(bogus);
}

TEST_CASE("audit log is bounded") {
    resetVaultFile();
    Vault vault;
    REQUIRE(vault.create(pw("hunter2hunter2")));
    for (int i = 0; i < 520; ++i)
        vault.appendAudit({i, "c", "s", "sum", "", "v", true});
    CHECK(vault.audit().size() == 500);
    // Newest first.
    CHECK(vault.audit().front().time == 519);
}
