// Port of atomicassets src/kits/{atomicassets,atomicmarket,atomictools}.ts.
// The typed ActionParams arguments are json (encoded through the embedded
// contract ABIs).
#pragma once

#include <dwarfkit/atomicassets/objects.hpp>

namespace dwarfkit::atomic {

class AtomicAssetsKit {
public:
    AtomicAssetsKit(const std::string& url, const ChainDefinition& chain,
                    const KitOptions& options = {})
        : utility(std::make_shared<KitUtility>(url, chain, options)) {}

    KitUtilityPtr utility;

    Result<Collection> loadCollection(const Name& collectionName) const;
    Result<Action> createCollection(const json& value) const;
    Result<Schema> loadSchema(const Name& collectionName, const Name& schemaName) const;
    Result<Action> createSchema(const json& value) const;
    Result<Template> loadTemplate(const Name& collectionName, int32_t templateId) const;
    Result<Action> createTemplate(const json& value) const;
    Result<AtomicAsset> loadAsset(uint64_t assetId) const;
    Result<Action> mintAsset(const json& value) const;
    Result<Offer> loadOffer(uint64_t offerId) const;
    Result<Action> createOffer(const json& value) const;
    Result<Action> transfer(const json& value) const;
    Result<Action> withdraw(const json& value) const;
    Result<Action> addConfToken(const json& value) const;
    Result<Action> adminColEdit(const json& value) const;
    Result<Action> announceDepo(const json& value) const;
    Result<Action> setVersion(const json& value) const;
};

class AtomicMarketKit {
public:
    AtomicMarketKit(const std::string& url, const ChainDefinition& chain,
                    const KitOptions& options = {})
        : utility(std::make_shared<KitUtility>(url, chain, options)) {}

    KitUtilityPtr utility;

    Result<Auction> loadAuction(uint64_t auctionId) const;
    Result<Action> announceAuction(const json& value) const;
    Result<Sale> loadSale(uint64_t saleId) const;
    Result<Action> announceSale(const json& value) const;
    Result<Buyoffer> loadBuyoffer(uint64_t buyofferId) const;
    Result<Action> createBuyo(const json& value) const;
    Result<Action> addBonusfeeCounter(const json& value) const;
    Result<Action> addBonusfee(const json& value) const;
    Result<Action> delBonusfee(const json& value) const;
    Result<Action> stopBonusfee(const json& value) const;
    Result<Action> addConfToken(const json& value) const;
    Result<Action> addDelphi(const json& value) const;
    Result<Action> registerMarketplace(const json& value) const;
    Result<Action> setMarketfee(const json& value) const;
    Result<Action> setMinbidinc(const json& value) const;
    Result<Action> setVersion(const json& value) const;
    Result<Action> withdraw(const json& value) const;
};

class AtomicToolsKit {
public:
    AtomicToolsKit(const std::string& url, const ChainDefinition& chain,
                   const KitOptions& options = {})
        : utility(std::make_shared<KitUtility>(url, chain, options)) {}

    KitUtilityPtr utility;

    Result<Link> loadLink(uint64_t linkId) const;
    Result<Action> announceLink(const json& value) const;
};

}  // namespace dwarfkit::atomic
