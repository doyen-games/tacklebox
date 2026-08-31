#include <dwarfkit/atomicassets/objects.hpp>

namespace dwarfkit::atomic {

namespace {

// API numeric fields arrive as strings or numbers
uint64_t toU64(const json& value) {
    if (value.is_string()) {
        return std::stoull(value.get<std::string>());
    }
    if (value.is_number()) {
        return value.get<uint64_t>();
    }
    return 0;
}

int64_t toI64(const json& value) {
    if (value.is_string()) {
        return std::stoll(value.get<std::string>());
    }
    if (value.is_number()) {
        return value.get<int64_t>();
    }
    return 0;
}

int stateOf(const json& data) {
    return static_cast<int>(toI64(data.value("state", json(0))));
}

std::optional<Name> optionalName(const json& data, const char* key) {
    if (data.contains(key) && data[key].is_string() &&
        !data[key].get_ref<const std::string&>().empty()) {
        return Name::from(data[key].get<std::string>());
    }
    return std::nullopt;
}

// {token_contract, token_symbol, token_precision, amount} -> ExtendedAsset
Result<ExtendedAsset> tokenAmountToExtendedAsset(const json& token) {
    DK_TRY(symbol,
           Asset::Symbol::from(std::to_string(toI64(token.value("token_precision", json(0)))) +
                               "," + token.value("token_symbol", "")));
    ExtendedAsset rv;
    rv.quantity = Asset::fromUnits(toI64(token.value("amount", json(0))), symbol);
    rv.contract = Name::from(token.value("token_contract", ""));
    return rv;
}

}  // namespace

// ---- Collection ------------------------------------------------------------

std::vector<Name> Collection::authorizedAccounts() const {
    std::vector<Name> rv;
    for (const auto& account : data.value("authorized_accounts", json::array())) {
        rv.push_back(Name::from(account.get<std::string>()));
    }
    return rv;
}

std::vector<Name> Collection::notifyAccounts() const {
    std::vector<Name> rv;
    for (const auto& account : data.value("notify_accounts", json::array())) {
        rv.push_back(Name::from(account.get<std::string>()));
    }
    return rv;
}

double Collection::marketFee() const {
    const json fee = data.value("market_fee", json(0.0));
    if (fee.is_string()) {
        return std::strtod(fee.get_ref<const std::string&>().c_str(), nullptr);
    }
    return fee.is_number() ? fee.get<double>() : 0.0;
}

std::string Collection::name() const {
    // null when the collection metadata omits it
    const json value = data.value("name", json());
    return value.is_string() ? value.get<std::string>() : "";
}

std::string Collection::image() const {
    const json value = data.value("img", json());
    return value.is_string() ? value.get<std::string>() : "";
}

Result<Action> Collection::addAuth(const Name& account) const {
    return utility->assetsContract.action(
        Name::from("addcolauth"), json{{"collection_name", collectionName().toString()},
                                       {"account_to_add", account.toString()}});
}

Result<Action> Collection::removeAuth(const Name& account) const {
    return utility->assetsContract.action(
        Name::from("remcolauth"), json{{"collection_name", collectionName().toString()},
                                       {"account_to_remove", account.toString()}});
}

Result<Action> Collection::setData(const json& newData) const {
    return utility->assetsContract.action(
        Name::from("setcoldata"),
        json{{"collection_name", collectionName().toString()}, {"data", newData}});
}

Result<Action> Collection::addNotifyAccount(const Name& account) const {
    return utility->assetsContract.action(
        Name::from("addnotifyacc"), json{{"collection_name", collectionName().toString()},
                                         {"account_to_add", account.toString()}});
}

Result<Action> Collection::removeNotifyAccount(const Name& account) const {
    return utility->assetsContract.action(
        Name::from("remnotifyacc"), json{{"collection_name", collectionName().toString()},
                                         {"account_to_remove", account.toString()}});
}

Result<Action> Collection::setMarketFee(double fee) const {
    return utility->assetsContract.action(
        Name::from("setmarketfee"),
        json{{"collection_name", collectionName().toString()}, {"market_fee", fee}});
}

Result<Action> Collection::forbidnotify() const {
    return utility->assetsContract.action(
        Name::from("forbidnotify"), json{{"collection_name", collectionName().toString()}});
}

// ---- Schema ----------------------------------------------------------------

uint64_t Schema::assets() const {
    return toU64(data.value("assets", json(0)));
}

Result<Action> Schema::extendSchema(const Name& authorizedEditor,
                                    const json& schemaFormat) const {
    return utility->assetsContract.action(
        Name::from("extendschema"),
        json{{"authorized_editor", authorizedEditor.toString()},
             {"collection_name", collection.collectionName().toString()},
             {"schema_name", schemaName().toString()},
             {"schema_format_extension", schemaFormat}});
}

// ---- Template --------------------------------------------------------------

int32_t Template::templateId() const {
    return static_cast<int32_t>(toI64(data.value("template_id", json(0))));
}

uint32_t Template::issuedSupply() const {
    return static_cast<uint32_t>(toU64(data.value("issued_supply", json(0))));
}

uint32_t Template::maxSupply() const {
    return static_cast<uint32_t>(toU64(data.value("max_supply", json(0))));
}

Result<Action> Template::lock(const Name& authorizedEditor) const {
    return utility->assetsContract.action(
        Name::from("locktemplate"),
        json{{"authorized_editor", authorizedEditor.toString()},
             {"collection_name", collection.collectionName().toString()},
             {"template_id", templateId()}});
}

// ---- AtomicAsset -----------------------------------------------------------

AtomicAsset AtomicAsset::from(const json& assetObject, const KitUtilityPtr& utility) {
    Collection collection(utility, assetObject.value("collection", json::object()));
    Schema schema(utility, assetObject.value("schema", json::object()), collection);
    Template template_(utility, assetObject.value("template", json()), collection, schema);
    return AtomicAsset(utility, assetObject, std::move(collection), std::move(schema),
                       std::move(template_));
}

AtomicAsset::AtomicAsset(KitUtilityPtr utilityIn, json dataIn, Collection collectionIn,
                         Schema schemaIn, Template templateIn)
    : data(std::move(dataIn)),
      collection(std::move(collectionIn)),
      schema(std::move(schemaIn)),
      template_(std::move(templateIn)),
      utility(std::move(utilityIn)) {
    for (const auto& token : data.value("backed_tokens", json::array())) {
        const auto extended = tokenAmountToExtendedAsset(token);
        if (extended) {
            backedTokens.push_back(*extended);
        }
    }
}

uint64_t AtomicAsset::assetId() const {
    return toU64(data.value("asset_id", json(0)));
}

std::optional<Name> AtomicAsset::owner() const {
    return optionalName(data, "owner");
}

std::string AtomicAsset::name() const {
    return data.value("name", "");
}

std::optional<Name> AtomicAsset::burnedByAccount() const {
    return optionalName(data, "burned_by_account");
}

Result<Action> AtomicAsset::burn() const {
    return utility->assetsContract.action(
        Name::from("burnasset"),
        json{{"asset_owner", owner() ? owner()->toString() : ""},
             {"asset_id", assetId()}});
}

Result<Action> AtomicAsset::back(const Name& payer, const Asset& backedToken) const {
    return utility->assetsContract.action(
        Name::from("backasset"), json{{"payer", payer.toString()},
                                      {"asset_owner", owner() ? owner()->toString() : ""},
                                      {"asset_id", assetId()},
                                      {"token_to_back", backedToken.toString()}});
}

Result<Action> AtomicAsset::setData(const Name& authorizedEditor,
                                    const json& mutableData) const {
    return utility->assetsContract.action(
        Name::from("setassetdata"),
        json{{"authorized_editor", authorizedEditor.toString()},
             {"asset_owner", owner() ? owner()->toString() : ""},
             {"asset_id", assetId()},
             {"new_mutable_data", mutableData}});
}

// ---- Offer -----------------------------------------------------------------

Offer Offer::from(const json& offerObject, const KitUtilityPtr& utility) {
    std::vector<AtomicAsset> senderAssets;
    std::vector<AtomicAsset> recipientAssets;
    for (const auto& assetObject : offerObject.value("sender_assets", json::array())) {
        senderAssets.push_back(AtomicAsset::from(assetObject, utility));
    }
    for (const auto& assetObject : offerObject.value("recipient_assets", json::array())) {
        recipientAssets.push_back(AtomicAsset::from(assetObject, utility));
    }
    return Offer(utility, offerObject, std::move(senderAssets), std::move(recipientAssets));
}

uint64_t Offer::offerId() const {
    return toU64(data.value("offer_id", json(0)));
}

OfferState Offer::state() const {
    return static_cast<OfferState>(stateOf(data));
}

Result<Action> Offer::cancel() const {
    return utility->assetsContract.action(Name::from("canceloffer"),
                                          json{{"offer_id", offerId()}});
}

Result<Action> Offer::decline() const {
    return utility->assetsContract.action(Name::from("declineoffer"),
                                          json{{"offer_id", offerId()}});
}

Result<Action> Offer::accept() const {
    return utility->assetsContract.action(Name::from("acceptoffer"),
                                          json{{"offer_id", offerId()}});
}

Result<Action> Offer::payram(const Name& payer) const {
    return utility->assetsContract.action(
        Name::from("payofferram"),
        json{{"payer", payer.toString()}, {"offer_id", offerId()}});
}

// ---- Sale ------------------------------------------------------------------

Result<Sale> Sale::from(const json& saleObject, const KitUtilityPtr& utility) {
    Collection collection(utility, saleObject.value("collection", json::object()));
    std::vector<AtomicAsset> assets;
    for (const auto& assetObject : saleObject.value("assets", json::array())) {
        assets.push_back(AtomicAsset::from(assetObject, utility));
    }
    DK_TRY(price, tokenAmountToExtendedAsset(saleObject.value("price", json::object())));
    return Sale(utility, saleObject, std::move(collection), std::move(assets), price);
}

uint64_t Sale::saleId() const {
    return toU64(data.value("sale_id", json(0)));
}

std::optional<Name> Sale::buyer() const {
    return optionalName(data, "buyer");
}

Asset Sale::listingPrice() const {
    return Asset::fromUnits(toI64(data.value("listing_price", json(0))), listingSymbol());
}

Name Sale::makerMarketplace() const {
    return Name::from(data.value("maker_marketplace", ""));
}

std::optional<Name> Sale::takerMarketplace() const {
    return optionalName(data, "taker_marketplace");
}

SaleState Sale::state() const {
    return static_cast<SaleState>(stateOf(data));
}

Result<Action> Sale::assert_() const {
    json assetIds = json::array();
    for (const auto& asset : assets) {
        assetIds.push_back(asset.assetId());
    }
    return utility->marketContract.action(
        Name::from("assertsale"),
        json{{"sale_id", saleId()},
             {"asset_ids_to_assert", assetIds},
             {"listing_price_to_assert", listingPrice().toString()},
             {"settlement_symbol_to_assert", listingSymbol().toString()}});
}

Result<Action> Sale::cancel() const {
    return utility->marketContract.action(Name::from("cancelsale"),
                                          json{{"sale_id", saleId()}});
}

Result<Action> Sale::payram(const Name& payer) const {
    return utility->marketContract.action(
        Name::from("paysaleram"), json{{"payer", payer.toString()}, {"sale_id", saleId()}});
}

Result<Action> Sale::purchase(const Name& buyerName, uint64_t intendedDelphiMedian,
                              const Name& takerMarketplaceName) const {
    return utility->marketContract.action(
        Name::from("purchasesale"),
        json{{"buyer", buyerName.toString()},
             {"sale_id", saleId()},
             {"intended_delphi_median", intendedDelphiMedian},
             {"taker_marketplace", takerMarketplaceName.toString()}});
}

// ---- Auction ---------------------------------------------------------------

Result<Auction> Auction::from(const json& auctionObject, const KitUtilityPtr& utility) {
    Collection collection(utility, auctionObject.value("collection", json::object()));
    std::vector<AtomicAsset> assets;
    for (const auto& assetObject : auctionObject.value("assets", json::array())) {
        assets.push_back(AtomicAsset::from(assetObject, utility));
    }
    DK_TRY(price, tokenAmountToExtendedAsset(auctionObject.value("price", json::object())));
    return Auction(utility, auctionObject, std::move(collection), std::move(assets), price);
}

uint64_t Auction::auctionId() const {
    return toU64(data.value("auction_id", json(0)));
}

std::optional<Name> Auction::buyer() const {
    return optionalName(data, "buyer");
}

AuctionState Auction::state() const {
    return static_cast<AuctionState>(stateOf(data));
}

Name Auction::makerMarketplace() const {
    return Name::from(data.value("maker_marketplace", ""));
}

std::optional<Name> Auction::takerMarketplace() const {
    return optionalName(data, "taker_marketplace");
}

Result<Action> Auction::assert_() const {
    json assetIds = json::array();
    for (const auto& asset : assets) {
        assetIds.push_back(asset.assetId());
    }
    return utility->marketContract.action(
        Name::from("assertauct"),
        json{{"auction_id", auctionId()}, {"asset_ids_to_assert", assetIds}});
}

Result<Action> Auction::claimBuy() const {
    return utility->marketContract.action(Name::from("auctclaimbuy"),
                                          json{{"auction_id", auctionId()}});
}

Result<Action> Auction::claimSell() const {
    return utility->marketContract.action(Name::from("auctclaimsel"),
                                          json{{"auction_id", auctionId()}});
}

Result<Action> Auction::bid(const Name& bidder, const Asset& bidAmount,
                            const Name& takerMarketplaceName) const {
    return utility->marketContract.action(
        Name::from("auctionbid"),
        json{{"bidder", bidder.toString()},
             {"auction_id", auctionId()},
             {"bid", bidAmount.toString()},
             {"taker_marketplace", takerMarketplaceName.toString()}});
}

Result<Action> Auction::cancel() const {
    return utility->marketContract.action(Name::from("cancelauct"),
                                          json{{"auction_id", auctionId()}});
}

Result<Action> Auction::payram(const Name& payer) const {
    return utility->marketContract.action(
        Name::from("payauctram"),
        json{{"payer", payer.toString()}, {"auction_id", auctionId()}});
}

// ---- Buyoffer --------------------------------------------------------------

Result<Buyoffer> Buyoffer::from(const json& buyofferObject, const KitUtilityPtr& utility) {
    Collection collection(utility, buyofferObject.value("collection", json::object()));
    std::vector<AtomicAsset> assets;
    for (const auto& assetObject : buyofferObject.value("assets", json::array())) {
        assets.push_back(AtomicAsset::from(assetObject, utility));
    }
    DK_TRY(price, tokenAmountToExtendedAsset(buyofferObject.value("price", json::object())));
    return Buyoffer(utility, buyofferObject, std::move(collection), std::move(assets), price);
}

uint64_t Buyoffer::buyofferId() const {
    return toU64(data.value("buyoffer_id", json(0)));
}

std::optional<Name> Buyoffer::seller() const {
    return optionalName(data, "seller");
}

Name Buyoffer::makerMarketplace() const {
    return Name::from(data.value("maker_marketplace", ""));
}

std::optional<Name> Buyoffer::takerMarketplace() const {
    return optionalName(data, "taker_marketplace");
}

std::string Buyoffer::declineMemo() const {
    return data.value("decline_memo", "");
}

BuyofferState Buyoffer::state() const {
    return static_cast<BuyofferState>(stateOf(data));
}

Result<Action> Buyoffer::accept(const Name& takerMarketplaceName) const {
    json assetIds = json::array();
    for (const auto& asset : assets) {
        assetIds.push_back(asset.assetId());
    }
    return utility->marketContract.action(
        Name::from("acceptbuyo"),
        json{{"buyoffer_id", buyofferId()},
             {"expected_asset_ids", assetIds},
             {"expected_price", price.quantity.toString()},
             {"taker_marketplace", takerMarketplaceName.toString()}});
}

Result<Action> Buyoffer::cancel() const {
    return utility->marketContract.action(Name::from("cancelbuyo"),
                                          json{{"buyoffer_id", buyofferId()}});
}

Result<Action> Buyoffer::decline(const std::string& declineMemoText) const {
    return utility->marketContract.action(
        Name::from("declinebuyo"),
        json{{"buyoffer_id", buyofferId()}, {"decline_memo", declineMemoText}});
}

Result<Action> Buyoffer::payram(const Name& payer) const {
    return utility->marketContract.action(
        Name::from("paybuyoram"),
        json{{"payer", payer.toString()}, {"buyoffer_id", buyofferId()}});
}

// ---- Link ------------------------------------------------------------------

Result<Link> Link::from(const json& linkObject, const KitUtilityPtr& utility) {
    std::vector<AtomicAsset> assets;
    for (const auto& assetObject : linkObject.value("assets", json::array())) {
        assets.push_back(AtomicAsset::from(assetObject, utility));
    }
    DK_TRY(publicKey, PublicKey::from(linkObject.value("public_key", "")));
    return Link(utility, linkObject, std::move(assets), publicKey);
}

uint64_t Link::linkId() const {
    return toU64(data.value("link_id", json(0)));
}

std::optional<Name> Link::claimer() const {
    return optionalName(data, "claimer");
}

LinkState Link::state() const {
    return static_cast<LinkState>(stateOf(data));
}

Result<Action> Link::cancel() const {
    return utility->toolsContract.action(Name::from("cancellink"),
                                         json{{"link_id", linkId()}});
}

Result<Action> Link::claim(const Name& claimerName, const Signature& claimerSignature) const {
    return utility->toolsContract.action(
        Name::from("claimlink"), json{{"link_id", linkId()},
                                      {"claimer", claimerName.toString()},
                                      {"claimer_signature", claimerSignature.toString()}});
}

}  // namespace dwarfkit::atomic
