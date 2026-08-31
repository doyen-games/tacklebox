// Port of wharfkit/transact-plugin-resource-provider (src/index.ts and
// src/utils.ts). The upstream Transfer/BuyRAMBytes exports become nested
// types; the 120s prompt expiry timer is not replicated (see DIVERGENCES.md).
#pragma once

#include <map>

#include <dwarfkit/resources.hpp>
#include <dwarfkit/session.hpp>

namespace dwarfkit {

// True when every original action appears unmodified in the modified
// transaction (account, name, first authorizer actor and data all match).
bool hasOriginalActions(const Transaction& original, const Transaction& modified);

// All actions in the modified transaction that were not in the original.
std::vector<Action> getNewActions(const Transaction& original, const Transaction& modified);

struct ResourceProviderOptions {
    std::optional<bool> allowFees;
    // chain id (hex) to resource provider endpoint
    std::map<std::string, std::string> endpoints;
    std::optional<Asset> maxFee;
};

class TransactPluginResourceProvider : public AbstractTransactPlugin {
public:
    struct Transfer {
        DK_STRUCT("transfer")
        Name from;
        Name to;
        Asset quantity;
        std::string memo;
        DK_FIELDS(from, to, quantity, memo)
    };

    struct BuyRAMBytes {
        DK_STRUCT("buyrambytes")
        Name payer;
        Name receiver;
        uint32_t bytes = 0;
        DK_FIELDS(payer, receiver, bytes)
    };

    static const std::map<std::string, std::string>& defaultEndpoints();

    explicit TransactPluginResourceProvider(const ResourceProviderOptions& options = {});

    std::string id() const override { return "transact-plugin-resource-provider"; }
    LocaleDefinitions translations() const override;
    void register_(TransactContext& context) override;

    bool allowFees = true;
    std::optional<Asset> maxFee;
    std::map<std::string, std::string> endpoints;

    std::string getEndpoint(const ChainDefinition& chain) const;

    Result<TransactHookResponseType> request(SigningRequest request, TransactContext& context);

    Result<Transaction> getModifiedTransaction(const json& response) const;
    Result<SigningRequest> createRequest(const json& response, TransactContext& context) const;
    Result<void> validateRequest(const SigningRequest& request, TransactContext& context) const;
    Result<void> validateResponseData(const json& response) const;
};

}  // namespace dwarfkit
