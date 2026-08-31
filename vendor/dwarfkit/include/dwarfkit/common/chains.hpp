// Port of common src/common/chains.ts
#pragma once

#include <map>

#include <dwarfkit/antelope/api/v1/types.hpp>
#include <dwarfkit/common/explorer.hpp>
#include <dwarfkit/common/logo.hpp>
#include <dwarfkit/common/token.hpp>

namespace dwarfkit {

// The information required to interact with a given chain. Chain-specific
// account data types (Telos, WAX) are template parameters at the get_account
// call sites rather than a stored class reference (see DIVERGENCES.md).
class ChainDefinition {
public:
    // The chain ID.
    Checksum256 id;
    // The base URL of the chain's API endpoint (e.g. https://jungle4.greymass.com).
    std::string url;
    // The absolute URL(s) to the chain's logo.
    std::optional<Logo> logo;
    // The explorer definition for the chain.
    std::optional<ExplorerDefinition> explorer;
    // The SLIP-44 coin type for the chain.
    std::optional<int> coinType;
    // The system token for the chain.
    std::optional<TokenIdentifier> systemToken;

    ChainDefinition() = default;

    struct Args {
        Checksum256 id;
        std::string url;
        std::optional<Logo> logo;
        std::optional<ExplorerDefinition> explorer;
        std::optional<int> coinType;
        std::optional<TokenIdentifier> systemToken;
        std::optional<Asset::Symbol> systemTokenSymbol;
        std::optional<Name> systemTokenContract;
    };

    static ChainDefinition from(Args args);
    static Result<ChainDefinition> from(const json& value);

    // Human readable chain name, "Unknown blockchain" when unknown.
    std::string name() const;

    std::optional<Logo> getLogo() const;

    bool equals(const ChainDefinition& other) const {
        return id == other.id && url == other.url;
    }
    bool operator==(const ChainDefinition& other) const { return equals(other); }

    json toJSON() const;
};

struct TelosAccountVoterInfo : api::v1::AccountVoterInfo {
    DK_STRUCT_BASE("telos_account_voter_info", api::v1::AccountVoterInfo)
    int64_t last_stake = 0;
    DK_FIELDS(last_stake)
};

using TelosAccountObject = api::v1::BasicAccountObject<TelosAccountVoterInfo>;

struct WAXAccountVoterInfo : api::v1::AccountVoterInfo {
    DK_STRUCT_BASE("wax_account_voter_info", api::v1::AccountVoterInfo)
    double unpaid_voteshare = 0;
    TimePoint unpaid_voteshare_last_updated;
    double unpaid_voteshare_change_rate = 0;
    TimePoint last_claim_time;
    DK_FIELDS(unpaid_voteshare, unpaid_voteshare_last_updated, unpaid_voteshare_change_rate,
              last_claim_time)
};

using WAXAccountObject = api::v1::BasicAccountObject<WAXAccountVoterInfo>;

// An exported list of ChainDefinition entries for select chains.
namespace Chains {
const ChainDefinition& EOS();
const ChainDefinition& FIO();
const ChainDefinition& FIOTestnet();
const ChainDefinition& Jungle4();
const ChainDefinition& KylinTestnet();
const ChainDefinition& Libre();
const ChainDefinition& LibreTestnet();
const ChainDefinition& Proton();
const ChainDefinition& ProtonTestnet();
const ChainDefinition& Telos();
const ChainDefinition& TelosTestnet();
const ChainDefinition& UX();
const ChainDefinition& Vaulta();
const ChainDefinition& WAX();
const ChainDefinition& WAXTestnet();
const ChainDefinition& XPR();
const ChainDefinition& XPRTestnet();

// Chains[indice] lookup
const ChainDefinition* byIndice(std::string_view indice);
}  // namespace Chains

// List of human readable chain names keyed by indice.
const std::map<std::string, std::string, std::less<>>& ChainNames();

// A list of chain IDs and their indices for reference lookups.
const std::map<std::string, std::string, std::less<>>& chainIdsToIndices();

// A list of known chain IDs and their logos.
const std::map<std::string, std::string, std::less<>>& chainLogos();

}  // namespace dwarfkit
