#include <dwarfkit/atomicassets/kits.hpp>

namespace dwarfkit::atomic {

// ---- AtomicAssetsKit -------------------------------------------------------

Result<Collection> AtomicAssetsKit::loadCollection(const Name& collectionName) const {
    DK_TRY(response, utility->atomicClient->atomicassets.v1.get_collection(collectionName));
    return Collection::from(response.value("data", json::object()), utility);
}

Result<Action> AtomicAssetsKit::createCollection(const json& value) const {
    return utility->assetsContract.action(Name::from("createcol"), value);
}

Result<Schema> AtomicAssetsKit::loadSchema(const Name& collectionName,
                                           const Name& schemaName) const {
    DK_TRY(response,
           utility->atomicClient->atomicassets.v1.get_schema(collectionName, schemaName));
    return Schema::from(response.value("data", json::object()), utility);
}

Result<Action> AtomicAssetsKit::createSchema(const json& value) const {
    return utility->assetsContract.action(Name::from("createschema"), value);
}

Result<Template> AtomicAssetsKit::loadTemplate(const Name& collectionName,
                                               int32_t templateId) const {
    DK_TRY(response,
           utility->atomicClient->atomicassets.v1.get_template(collectionName, templateId));
    return Template::from(response.value("data", json::object()), utility);
}

Result<Action> AtomicAssetsKit::createTemplate(const json& value) const {
    return utility->assetsContract.action(Name::from("createtempl"), value);
}

Result<AtomicAsset> AtomicAssetsKit::loadAsset(uint64_t assetId) const {
    DK_TRY(response, utility->atomicClient->atomicassets.v1.get_asset(assetId));
    return AtomicAsset::from(response.value("data", json::object()), utility);
}

Result<Action> AtomicAssetsKit::mintAsset(const json& value) const {
    return utility->assetsContract.action(Name::from("mintasset"), value);
}

Result<Offer> AtomicAssetsKit::loadOffer(uint64_t offerId) const {
    DK_TRY(response, utility->atomicClient->atomicassets.v1.get_offer(offerId));
    return Offer::from(response.value("data", json::object()), utility);
}

Result<Action> AtomicAssetsKit::createOffer(const json& value) const {
    return utility->assetsContract.action(Name::from("createoffer"), value);
}

Result<Action> AtomicAssetsKit::transfer(const json& value) const {
    return utility->assetsContract.action(Name::from("transfer"), value);
}

Result<Action> AtomicAssetsKit::withdraw(const json& value) const {
    return utility->assetsContract.action(Name::from("withdraw"), value);
}

Result<Action> AtomicAssetsKit::addConfToken(const json& value) const {
    return utility->assetsContract.action(Name::from("addconftoken"), value);
}

Result<Action> AtomicAssetsKit::adminColEdit(const json& value) const {
    return utility->assetsContract.action(Name::from("admincoledit"), value);
}

Result<Action> AtomicAssetsKit::announceDepo(const json& value) const {
    return utility->assetsContract.action(Name::from("announcedepo"), value);
}

Result<Action> AtomicAssetsKit::setVersion(const json& value) const {
    return utility->assetsContract.action(Name::from("setversion"), value);
}

// ---- AtomicMarketKit -------------------------------------------------------

Result<Auction> AtomicMarketKit::loadAuction(uint64_t auctionId) const {
    DK_TRY(response, utility->atomicClient->atomicmarket.v1.get_auction(auctionId));
    return Auction::from(response.value("data", json::object()), utility);
}

Result<Action> AtomicMarketKit::announceAuction(const json& value) const {
    return utility->marketContract.action(Name::from("announceauct"), value);
}

Result<Sale> AtomicMarketKit::loadSale(uint64_t saleId) const {
    DK_TRY(response, utility->atomicClient->atomicmarket.v1.get_sale(saleId));
    return Sale::from(response.value("data", json::object()), utility);
}

Result<Action> AtomicMarketKit::announceSale(const json& value) const {
    return utility->marketContract.action(Name::from("announcesale"), value);
}

Result<Buyoffer> AtomicMarketKit::loadBuyoffer(uint64_t buyofferId) const {
    DK_TRY(response, utility->atomicClient->atomicmarket.v1.get_buyoffer(buyofferId));
    return Buyoffer::from(response.value("data", json::object()), utility);
}

Result<Action> AtomicMarketKit::createBuyo(const json& value) const {
    return utility->marketContract.action(Name::from("createbuyo"), value);
}

Result<Action> AtomicMarketKit::addBonusfeeCounter(const json& value) const {
    return utility->marketContract.action(Name::from("addafeectr"), value);
}

Result<Action> AtomicMarketKit::addBonusfee(const json& value) const {
    return utility->marketContract.action(Name::from("addbonusfee"), value);
}

Result<Action> AtomicMarketKit::delBonusfee(const json& value) const {
    return utility->marketContract.action(Name::from("delbonusfee"), value);
}

Result<Action> AtomicMarketKit::stopBonusfee(const json& value) const {
    return utility->marketContract.action(Name::from("stopbonusfee"), value);
}

Result<Action> AtomicMarketKit::addConfToken(const json& value) const {
    return utility->marketContract.action(Name::from("addconftoken"), value);
}

Result<Action> AtomicMarketKit::addDelphi(const json& value) const {
    return utility->marketContract.action(Name::from("adddelphi"), value);
}

Result<Action> AtomicMarketKit::registerMarketplace(const json& value) const {
    return utility->marketContract.action(Name::from("regmarket"), value);
}

Result<Action> AtomicMarketKit::setMarketfee(const json& value) const {
    return utility->marketContract.action(Name::from("setmarketfee"), value);
}

Result<Action> AtomicMarketKit::setMinbidinc(const json& value) const {
    return utility->marketContract.action(Name::from("setminbidinc"), value);
}

Result<Action> AtomicMarketKit::setVersion(const json& value) const {
    return utility->marketContract.action(Name::from("setversion"), value);
}

Result<Action> AtomicMarketKit::withdraw(const json& value) const {
    return utility->marketContract.action(Name::from("withdraw"), value);
}

// ---- AtomicToolsKit --------------------------------------------------------

Result<Link> AtomicToolsKit::loadLink(uint64_t linkId) const {
    DK_TRY(response, utility->atomicClient->atomictools.v1.get_link(linkId));
    return Link::from(response.value("data", json::object()), utility);
}

Result<Action> AtomicToolsKit::announceLink(const json& value) const {
    return utility->toolsContract.action(Name::from("announcelink"), value);
}

}  // namespace dwarfkit::atomic
