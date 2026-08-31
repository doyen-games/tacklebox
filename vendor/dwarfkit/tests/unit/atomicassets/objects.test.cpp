// Port of atomicassets test/{asset,collection,schema,template,offer,sale,
// auction,buyoffer,link}.ts: kit loads over the recorded fixtures, getters
// against the raw data, and action builders decoded through the embedded
// contract ABIs.
#include <doctest/doctest.h>

#include <dwarfkit/atomicassets.hpp>
#include <dwarfkit/atomicassets/contracts/atomicassets.gen.hpp>
#include <dwarfkit/atomicassets/contracts/atomicmarket.gen.hpp>
#include <dwarfkit/atomicassets/contracts/atomictoolsx.gen.hpp>
#include <dwarfkit/signing_request.hpp>

#include "../../util/mock_session.hpp"

using namespace dwarfkit;
using namespace dwarfkit::test;
using namespace dwarfkit::atomic;

namespace {

constexpr uint64_t assetId = 1099851897196ull;
constexpr uint64_t assetIdBurned = 1099959414679ull;

KitOptions mockKitOptions() {
    KitOptions options;
    options.client = makeClient(DK_FIXTURE_DIR "/atomicassets/data", "https://wax.greymass.com");
    options.atomicClient = std::make_shared<AtomicAssetsAPIClient>(
        makeClient(DK_FIXTURE_DIR "/atomicassets/data", "https://wax-atomic.alcor.exchange"));
    return options;
}

AtomicAssetsKit assetsKit() {
    return AtomicAssetsKit("https://wax-atomic.alcor.exchange", Chains::WAX(),
                           mockKitOptions());
}

AtomicMarketKit marketKit() {
    return AtomicMarketKit("https://wax-atomic.alcor.exchange", Chains::WAX(),
                           mockKitOptions());
}

AtomicToolsKit toolsKit() {
    return AtomicToolsKit("https://wax-atomic.alcor.exchange", Chains::WAX(), mockKitOptions());
}

json decode(const Action& action, const std::string& type, const ABI& abi) {
    return Serializer::decode(action.data.array, type, abi).value();
}

// uint64 fields decode as numbers or strings depending on magnitude
uint64_t asU64(const json& value) {
    return value.is_string() ? std::stoull(value.get<std::string>()) : value.get<uint64_t>();
}

}  // namespace

TEST_SUITE("atomicassets-objects") {
    TEST_CASE("asset") {
        const auto kit = assetsKit();
        const auto asset = kit.loadAsset(assetId).value();
        SUBCASE("getters") {
            CHECK(asset.assetId() == assetId);
            CHECK(asset.collection.collectionName().toString() ==
                  asset.data["collection"]["collection_name"].get<std::string>());
            CHECK(asset.schema.schemaName().toString() == asset.data["schema"]["schema_name"].get<std::string>());
            CHECK(asset.template_.templateId() ==
                  std::stoi(asset.data["template"]["template_id"].get<std::string>()));
            REQUIRE(asset.owner().has_value());
            CHECK(asset.owner()->toString() == asset.data["owner"].get<std::string>());
            CHECK(asset.transferable() == asset.data["is_transferable"].get<bool>());
            CHECK(asset.burnable() == asset.data["is_burnable"].get<bool>());
            CHECK(asset.name() == asset.data["name"].get<std::string>());
            CHECK_FALSE(asset.burnedByAccount().has_value());
        }
        SUBCASE("burned asset") {
            const auto burned = kit.loadAsset(assetIdBurned).value();
            CHECK(burned.burnedByAccount().has_value());
        }
        SUBCASE("burn action") {
            const auto action = asset.burn().value();
            CHECK(action.account == Name::from("atomicassets"));
            CHECK(action.name == Name::from("burnasset"));
            CHECK(action.authorization[0] == PlaceholderAuth);
            const auto decoded = decode(action, "burnasset", gen::atomicassets::abi());
            CHECK(decoded["asset_owner"] == asset.owner()->toString());
            CHECK(asU64(decoded["asset_id"]) == asset.assetId());
        }
        SUBCASE("back action") {
            const auto action =
                asset.back(Name::from("test.gm"), Asset::from("0.00000001 WAX").value())
                    .value();
            const auto decoded = decode(action, "backasset", gen::atomicassets::abi());
            CHECK(decoded["payer"] == "test.gm");
            CHECK(decoded["token_to_back"] == "0.00000001 WAX");
        }
        SUBCASE("setData action") {
            const json mutableData = json::array(
                {{{"key", "name"}, {"value", json::array({"string", "new name"})}}});
            const auto action = asset.setData(Name::from("test.gm"), mutableData).value();
            const auto decoded = decode(action, "setassetdata", gen::atomicassets::abi());
            CHECK(decoded["authorized_editor"] == "test.gm");
            CHECK(decoded["new_mutable_data"][0]["key"] == "name");
        }
        SUBCASE("backed tokens") {
            CHECK(asset.backedTokens.size() == asset.data["backed_tokens"].size());
        }
    }

    TEST_CASE("collection") {
        const auto kit = assetsKit();
        const auto collection = kit.loadCollection(Name::from("taco")).value();
        SUBCASE("getters") {
            CHECK(collection.collectionName() == Name::from("taco"));
            CHECK(collection.author().toString() == collection.data["author"].get<std::string>());
            CHECK(collection.allowNotify() == collection.data["allow_notify"].get<bool>());
            CHECK(collection.authorizedAccounts().size() ==
                  collection.data["authorized_accounts"].size());
            CHECK(collection.notifyAccounts().size() ==
                  collection.data["notify_accounts"].size());
            CHECK(collection.name() == collection.data["name"].get<std::string>());
        }
        SUBCASE("img optional") {
            const auto noImg = kit.loadCollection(Name::from("testfighters")).value();
            CHECK(noImg.image().empty());
        }
        SUBCASE("actions") {
            const auto add = collection.addAuth(Name::from("test.gm")).value();
            CHECK(add.name == Name::from("addcolauth"));
            const auto addDecoded = decode(add, "addcolauth", gen::atomicassets::abi());
            CHECK(addDecoded["collection_name"] == "taco");
            CHECK(addDecoded["account_to_add"] == "test.gm");

            const auto remove = collection.removeAuth(Name::from("test.gm")).value();
            CHECK(decode(remove, "remcolauth", gen::atomicassets::abi())["account_to_remove"] ==
                  "test.gm");

            const auto fee = collection.setMarketFee(0.05).value();
            const auto feeDecoded = decode(fee, "setmarketfee", gen::atomicassets::abi());
            // float64 fields serialize to json as decimal strings
            const json& feeValue = feeDecoded["market_fee"];
            const double feeNumber =
                feeValue.is_string()
                    ? std::strtod(feeValue.get_ref<const std::string&>().c_str(), nullptr)
                    : feeValue.get<double>();
            CHECK(feeNumber == doctest::Approx(0.05));

            const auto notify = collection.addNotifyAccount(Name::from("test.gm")).value();
            CHECK(notify.name == Name::from("addnotifyacc"));
            const auto forbid = collection.forbidnotify().value();
            CHECK(forbid.name == Name::from("forbidnotify"));
        }
    }

    TEST_CASE("schema") {
        const auto kit = assetsKit();
        const auto schema = kit.loadSchema(Name::from("taco"), Name::from("cmbz.res")).value();
        CHECK(schema.schemaName() == Name::from("cmbz.res"));
        CHECK(schema.collection.collectionName() == Name::from("taco"));
        CHECK(schema.format().is_array());
        const auto action =
            schema
                .extendSchema(Name::from("test.gm"),
                              json::array({{{"name", "extra"}, {"type", "string"}}}))
                .value();
        CHECK(action.name == Name::from("extendschema"));
        const auto decoded = decode(action, "extendschema", gen::atomicassets::abi());
        CHECK(decoded["schema_name"] == "cmbz.res");
        CHECK(decoded["schema_format_extension"][0]["name"] == "extra");
    }

    TEST_CASE("template") {
        const auto kit = assetsKit();
        const auto template_ = kit.loadTemplate(Name::from("taco"), 750150).value();
        CHECK(template_.templateId() == 750150);
        CHECK(template_.collection.collectionName() == Name::from("taco"));
        CHECK(template_.issuedSupply() > 0);
        const auto action = template_.lock(Name::from("test.gm")).value();
        CHECK(action.name == Name::from("locktemplate"));
        const auto decoded = decode(action, "locktemplate", gen::atomicassets::abi());
        CHECK(decoded["template_id"] == 750150);
    }

    TEST_CASE("offer") {
        const auto kit = assetsKit();
        const auto offer = kit.loadOffer(22820296ull).value();
        CHECK(offer.offerId() == 22820296ull);
        CHECK(offer.senderName().toString() == offer.data["sender_name"].get<std::string>());
        CHECK(offer.recipientName().toString() == offer.data["recipient_name"].get<std::string>());
        CHECK(!offer.sender_assets.empty());
        const auto cancel = offer.cancel().value();
        CHECK(cancel.name == Name::from("canceloffer"));
        const auto accept = offer.accept().value();
        CHECK(asU64(decode(accept, "acceptoffer", gen::atomicassets::abi())["offer_id"]) ==
              22820296ull);
        const auto payram = offer.payram(Name::from("test.gm")).value();
        CHECK(decode(payram, "payofferram", gen::atomicassets::abi())["payer"] == "test.gm");
    }

    TEST_CASE("sale") {
        const auto kit = marketKit();
        const auto sale = kit.loadSale(89024803ull).value();
        CHECK(sale.saleId() == 89024803ull);
        CHECK(sale.seller().toString() == sale.data["seller"].get<std::string>());
        CHECK(!sale.assets.empty());
        CHECK(sale.listingSymbol().toString().find("WAX") != std::string::npos);
        const auto assertAction = sale.assert_().value();
        CHECK(assertAction.account == Name::from("atomicmarket"));
        const auto decoded = decode(assertAction, "assertsale", gen::atomicmarket::abi());
        CHECK(asU64(decoded["sale_id"]) == 89024803ull);
        CHECK(decoded["listing_price_to_assert"] == sale.listingPrice().toString());
        const auto purchase =
            sale.purchase(Name::from("test.gm"), 0, Name::from("")).value();
        CHECK(decode(purchase, "purchasesale", gen::atomicmarket::abi())["buyer"] ==
              "test.gm");
    }

    TEST_CASE("auction") {
        const auto kit = marketKit();
        const auto auction = kit.loadAuction(1301765ull).value();
        CHECK(auction.auctionId() == 1301765ull);
        CHECK(auction.seller().toString() == auction.data["seller"].get<std::string>());
        const auto bid = auction
                             .bid(Name::from("test.gm"),
                                  Asset::from("1.00000000 WAX").value(), Name::from(""))
                             .value();
        const auto decoded = decode(bid, "auctionbid", gen::atomicmarket::abi());
        CHECK(decoded["bidder"] == "test.gm");
        CHECK(decoded["bid"] == "1.00000000 WAX");
        const auto cancel = auction.cancel().value();
        CHECK(cancel.name == Name::from("cancelauct"));
    }

    TEST_CASE("buyoffer") {
        const auto kit = marketKit();
        const auto buyoffer = kit.loadBuyoffer(2432258ull).value();
        CHECK(buyoffer.buyofferId() == 2432258ull);
        CHECK(buyoffer.buyer().toString() == buyoffer.data["buyer"].get<std::string>());
        const auto accept = buyoffer.accept(Name::from("")).value();
        const auto decoded = decode(accept, "acceptbuyo", gen::atomicmarket::abi());
        CHECK(asU64(decoded["buyoffer_id"]) == 2432258ull);
        CHECK(decoded["expected_price"] == buyoffer.price.quantity.toString());
        const auto decline = buyoffer.decline("no thanks").value();
        CHECK(decode(decline, "declinebuyo", gen::atomicmarket::abi())["decline_memo"] ==
              "no thanks");
    }

    TEST_CASE("link") {
        const auto kit = toolsKit();
        const auto link = kit.loadLink(1451754ull).value();
        CHECK(link.linkId() == 1451754ull);
        CHECK(link.creator().toString() == link.data["creator"].get<std::string>());
        CHECK(link.publicKey.toString().starts_with("PUB_"));
        const auto cancel = link.cancel().value();
        CHECK(cancel.account == Name::from("atomictoolsx"));
        CHECK(asU64(decode(cancel, "cancellink", gen::atomictoolsx::abi())["link_id"]) ==
              1451754ull);
    }

    TEST_CASE("kit action builders") {
        const auto kit = assetsKit();
        const auto transfer =
            kit.transfer(json{{"from", "test.gm"},
                              {"to", "teamgreymass"},
                              {"asset_ids", json::array({assetId})},
                              {"memo", "dwarfkit"}})
                .value();
        CHECK(transfer.account == Name::from("atomicassets"));
        CHECK(transfer.name == Name::from("transfer"));
        const auto decoded = decode(transfer, "transfer", gen::atomicassets::abi());
        CHECK(decoded["from"] == "test.gm");
        CHECK(decoded["memo"] == "dwarfkit");
    }
}
