// Port of atomicassets src/objects/*.ts. API objects are json (the typed
// *Object interfaces become accessors); action builders return
// Result<Action> through the embedded contract ABIs.
#pragma once

#include <dwarfkit/atomicassets/utility.hpp>

namespace dwarfkit::atomic {

using KitUtilityPtr = std::shared_ptr<KitUtility>;

class Collection {
public:
    static Collection from(const json& collectionObject, const KitUtilityPtr& utility) {
        return Collection(utility, collectionObject);
    }
    Collection(KitUtilityPtr utility, json data)
        : data(std::move(data)), utility(std::move(utility)) {}

    json data;
    KitUtilityPtr utility;

    Name collectionName() const { return Name::from(data.value("collection_name", "")); }
    Name author() const { return Name::from(data.value("author", "")); }
    bool allowNotify() const { return data.value("allow_notify", false); }
    std::vector<Name> authorizedAccounts() const;
    std::vector<Name> notifyAccounts() const;
    double marketFee() const;
    json collectionData() const { return data.value("data", json::object()); }
    std::string name() const;
    std::string image() const;

    Result<Action> addAuth(const Name& account) const;
    Result<Action> removeAuth(const Name& account) const;
    Result<Action> setData(const json& newData) const;
    Result<Action> addNotifyAccount(const Name& account) const;
    Result<Action> removeNotifyAccount(const Name& account) const;
    Result<Action> setMarketFee(double fee) const;
    Result<Action> forbidnotify() const;
};

class Schema {
public:
    static Schema from(const json& schemaObject, const KitUtilityPtr& utility) {
        return Schema(utility, schemaObject,
                      Collection(utility, schemaObject.value("collection", json::object())));
    }
    Schema(KitUtilityPtr utility, json data, Collection collection)
        : data(std::move(data)),
          collection(std::move(collection)),
          utility(std::move(utility)) {}

    json data;
    Collection collection;
    KitUtilityPtr utility;

    Name schemaName() const { return Name::from(data.value("schema_name", "")); }
    uint64_t assets() const;
    json format() const { return data.value("format", json::array()); }

    Result<Action> extendSchema(const Name& authorizedEditor, const json& schemaFormat) const;
};

class Template {
public:
    static Template from(const json& templateObject, const KitUtilityPtr& utility) {
        Collection collection(utility, templateObject.value("collection", json::object()));
        Schema schema(utility, templateObject.value("schema", json::object()), collection);
        return Template(utility, templateObject, std::move(collection), std::move(schema));
    }
    Template(KitUtilityPtr utility, json data, Collection collection, Schema schema)
        : data(std::move(data)),
          collection(std::move(collection)),
          schema(std::move(schema)),
          utility(std::move(utility)) {}

    json data;
    Collection collection;
    Schema schema;
    KitUtilityPtr utility;

    int32_t templateId() const;
    bool transferable() const { return data.value("is_transferable", false); }
    bool burnable() const { return data.value("is_burnable", false); }
    uint32_t issuedSupply() const;
    uint32_t maxSupply() const;
    json immutableData() const { return data.value("immutable_data", json::object()); }

    Result<Action> lock(const Name& authorizedEditor) const;
};

class AtomicAsset {
public:
    static AtomicAsset from(const json& assetObject, const KitUtilityPtr& utility);
    AtomicAsset(KitUtilityPtr utility, json data, Collection collection, Schema schema,
                Template template_);

    json data;
    Collection collection;
    Schema schema;
    Template template_;
    std::vector<ExtendedAsset> backedTokens;
    KitUtilityPtr utility;

    uint64_t assetId() const;
    json immutableData() const { return data.value("immutable_data", json::object()); }
    json mutableData() const { return data.value("mutable_data", json::object()); }
    json assetData() const { return data.value("data", json::object()); }
    std::optional<Name> owner() const;
    bool transferable() const { return data.value("is_transferable", false); }
    bool burnable() const { return data.value("is_burnable", false); }
    std::string name() const;
    std::optional<Name> burnedByAccount() const;

    Result<Action> burn() const;
    Result<Action> back(const Name& payer, const Asset& backedToken) const;
    Result<Action> setData(const Name& authorizedEditor, const json& mutableData) const;
};

class Offer {
public:
    static Offer from(const json& offerObject, const KitUtilityPtr& utility);
    Offer(KitUtilityPtr utility, json data, std::vector<AtomicAsset> senderAssets,
          std::vector<AtomicAsset> recipientAssets)
        : data(std::move(data)),
          sender_assets(std::move(senderAssets)),
          recipient_assets(std::move(recipientAssets)),
          utility(std::move(utility)) {}

    json data;
    std::vector<AtomicAsset> sender_assets;
    std::vector<AtomicAsset> recipient_assets;
    KitUtilityPtr utility;

    uint64_t offerId() const;
    Name senderName() const { return Name::from(data.value("sender_name", "")); }
    Name recipientName() const { return Name::from(data.value("recipient_name", "")); }
    std::string memo() const { return data.value("memo", ""); }
    OfferState state() const;
    bool isSenderContract() const { return data.value("is_sender_contract", false); }
    bool isRecipientContract() const { return data.value("is_recipient_contract", false); }

    Result<Action> cancel() const;
    Result<Action> decline() const;
    Result<Action> accept() const;
    Result<Action> payram(const Name& payer) const;
};

class Sale {
public:
    static Result<Sale> from(const json& saleObject, const KitUtilityPtr& utility);
    Sale(KitUtilityPtr utility, json data, Collection collection,
         std::vector<AtomicAsset> assets, ExtendedAsset price)
        : data(std::move(data)),
          collection(std::move(collection)),
          assets(std::move(assets)),
          price(std::move(price)),
          utility(std::move(utility)) {}

    json data;
    Collection collection;
    std::vector<AtomicAsset> assets;
    ExtendedAsset price;
    KitUtilityPtr utility;

    uint64_t saleId() const;
    Name seller() const { return Name::from(data.value("seller", "")); }
    std::optional<Name> buyer() const;
    Asset listingPrice() const;
    Asset::Symbol listingSymbol() const { return price.quantity.symbol; }
    Name makerMarketplace() const;
    std::optional<Name> takerMarketplace() const;
    SaleState state() const;
    bool isSellerContract() const { return data.value("is_seller_contract", false); }

    Result<Action> assert_() const;
    Result<Action> cancel() const;
    Result<Action> payram(const Name& payer) const;
    Result<Action> purchase(const Name& buyer, uint64_t intendedDelphiMedian,
                            const Name& takerMarketplace) const;
};

class Auction {
public:
    static Result<Auction> from(const json& auctionObject, const KitUtilityPtr& utility);
    Auction(KitUtilityPtr utility, json data, Collection collection,
            std::vector<AtomicAsset> assets, ExtendedAsset price)
        : data(std::move(data)),
          collection(std::move(collection)),
          assets(std::move(assets)),
          price(std::move(price)),
          utility(std::move(utility)) {}

    json data;
    Collection collection;
    std::vector<AtomicAsset> assets;
    ExtendedAsset price;
    KitUtilityPtr utility;

    uint64_t auctionId() const;
    Name seller() const { return Name::from(data.value("seller", "")); }
    std::optional<Name> buyer() const;
    json endTime() const { return data.value("end_time", json()); }
    json bids() const { return data.value("bids", json::array()); }
    AuctionState state() const;
    bool claimedBySeller() const { return data.value("claimed_by_seller", false); }
    bool claimedByBuyer() const { return data.value("claimed_by_buyer", false); }
    Name makerMarketplace() const;
    std::optional<Name> takerMarketplace() const;
    bool isSellerContract() const { return data.value("is_seller_contract", false); }

    Result<Action> assert_() const;
    Result<Action> claimBuy() const;
    Result<Action> claimSell() const;
    Result<Action> bid(const Name& bidder, const Asset& bid, const Name& takerMarketplace) const;
    Result<Action> cancel() const;
    Result<Action> payram(const Name& payer) const;
};

class Buyoffer {
public:
    static Result<Buyoffer> from(const json& buyofferObject, const KitUtilityPtr& utility);
    Buyoffer(KitUtilityPtr utility, json data, Collection collection,
             std::vector<AtomicAsset> assets, ExtendedAsset price)
        : data(std::move(data)),
          collection(std::move(collection)),
          assets(std::move(assets)),
          price(std::move(price)),
          utility(std::move(utility)) {}

    json data;
    Collection collection;
    std::vector<AtomicAsset> assets;
    ExtendedAsset price;
    KitUtilityPtr utility;

    uint64_t buyofferId() const;
    std::optional<Name> seller() const;
    Name buyer() const { return Name::from(data.value("buyer", "")); }
    Name makerMarketplace() const;
    std::optional<Name> takerMarketplace() const;
    std::string memo() const { return data.value("memo", ""); }
    std::string declineMemo() const;
    BuyofferState state() const;

    Result<Action> accept(const Name& takerMarketplace) const;
    Result<Action> cancel() const;
    Result<Action> decline(const std::string& memo) const;
    Result<Action> payram(const Name& payer) const;
};

class Link {
public:
    static Result<Link> from(const json& linkObject, const KitUtilityPtr& utility);
    Link(KitUtilityPtr utility, json data, std::vector<AtomicAsset> assets, PublicKey publicKey)
        : data(std::move(data)),
          assets(std::move(assets)),
          publicKey(publicKey),
          utility(std::move(utility)) {}

    json data;
    std::vector<AtomicAsset> assets;
    PublicKey publicKey;
    KitUtilityPtr utility;

    uint64_t linkId() const;
    Name creator() const { return Name::from(data.value("creator", "")); }
    std::optional<Name> claimer() const;
    LinkState state() const;
    std::string memo() const { return data.value("memo", ""); }

    Result<Action> cancel() const;
    Result<Action> claim(const Name& claimer, const Signature& claimerSignature) const;
};

}  // namespace dwarfkit::atomic
