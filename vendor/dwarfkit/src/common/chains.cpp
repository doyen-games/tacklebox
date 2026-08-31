#include <dwarfkit/common/chains.hpp>

namespace dwarfkit {

ChainDefinition ChainDefinition::from(Args args) {
    ChainDefinition rv;
    rv.id = args.id;
    rv.url = std::move(args.url);
    rv.logo = std::move(args.logo);
    rv.explorer = std::move(args.explorer);
    rv.coinType = args.coinType;
    if (args.systemTokenContract && args.systemTokenSymbol) {
        rv.systemToken = TokenIdentifier{rv.id, *args.systemTokenContract,
                                         *args.systemTokenSymbol};
    }
    if (args.systemToken) {
        rv.systemToken = std::move(args.systemToken);
    }
    return rv;
}

Result<ChainDefinition> ChainDefinition::from(const json& value) {
    Args args;
    DK_TRY(id, Checksum256::from(value.value("id", "")));
    args.id = id;
    args.url = value.value("url", "");
    if (value.contains("logo") && !value.at("logo").is_null()) {
        DK_TRY(logo, Logo::from(value.at("logo")));
        args.logo = std::move(logo);
    }
    if (value.contains("explorer") && !value.at("explorer").is_null()) {
        DK_TRY(explorer, structFrom<ExplorerDefinition>(value.at("explorer")));
        args.explorer = std::move(explorer);
    }
    if (value.contains("coinType") && value.at("coinType").is_number()) {
        args.coinType = value.at("coinType").get<int>();
    }
    if (value.contains("systemToken") && !value.at("systemToken").is_null()) {
        DK_TRY(token, structFrom<TokenIdentifier>(value.at("systemToken")));
        args.systemToken = std::move(token);
    }
    if (value.contains("systemTokenSymbol") && value.contains("systemTokenContract")) {
        DK_TRY(symbol, Asset::Symbol::from(value.at("systemTokenSymbol").get<std::string>()));
        args.systemTokenSymbol = symbol;
        args.systemTokenContract = Name::from(value.at("systemTokenContract").get<std::string>());
    }
    return from(std::move(args));
}

std::string ChainDefinition::name() const {
    const auto& indices = chainIdsToIndices();
    const auto indice = indices.find(id.hexString());
    if (indice == indices.end()) {
        return "Unknown blockchain";
    }
    const auto& names = ChainNames();
    const auto found = names.find(indice->second);
    return found != names.end() ? found->second : "Unknown blockchain";
}

std::optional<Logo> ChainDefinition::getLogo() const {
    if (logo) {
        return logo;
    }
    const auto& logos = chainLogos();
    const auto found = logos.find(id.hexString());
    if (found != logos.end()) {
        return Logo::from(std::string_view(found->second));
    }
    return std::nullopt;
}

json ChainDefinition::toJSON() const {
    json rv = {{"id", id.hexString()}, {"url", url}};
    if (logo) rv["logo"] = Serializer::objectify(*logo);
    if (explorer) rv["explorer"] = Serializer::objectify(*explorer);
    if (systemToken) rv["systemToken"] = Serializer::objectify(*systemToken);
    return rv;
}

namespace Chains {

namespace {

ChainDefinition make(std::string_view id, std::string url, std::optional<int> coinType,
                     std::string_view symbol, std::string_view contract) {
    ChainDefinition::Args args;
    args.id = Checksum256::from(id).value();
    args.url = std::move(url);
    args.coinType = coinType;
    args.systemTokenSymbol = Asset::Symbol::from(symbol).value();
    args.systemTokenContract = Name::from(contract);
    return ChainDefinition::from(std::move(args));
}

}  // namespace

#define DK_CHAIN(fn, id, url, coinType, symbol, contract)                     \
    const ChainDefinition& fn() {                                             \
        static const ChainDefinition def = make(id, url, coinType, symbol, contract); \
        return def;                                                           \
    }

DK_CHAIN(EOS, "aca376f206b8fc25a6ed44dbdc66547c36c6c33e3a119ffbeaef943642f0e906",
         "https://eos.greymass.com", 194, "4,EOS", "eosio.token")
DK_CHAIN(FIO, "21dcae42c0182200e93f954a074011f9048a7624c6fe81d3c9541a614a88bd1c",
         "https://fio.greymass.com", 235, "9,FIO", "eosio.token")
DK_CHAIN(FIOTestnet, "b20901380af44ef59c5918439a1f9a41d83669020319a80574b804a5f95cbd7e",
         "https://fiotestnet.greymass.com", std::nullopt, "9,FIO", "fio.token")
DK_CHAIN(Jungle4, "73e4385a2708e6d7048834fbc1079f2fabb17b3c125b146af438971e90716c4d",
         "https://jungle4.greymass.com", 194, "4,EOS", "eosio.token")
DK_CHAIN(KylinTestnet, "5fff1dae8dc8e2fc4d5b23b2c7665c97f9e9d8edf2b6485a86ba311c25639191",
         "https://kylintestnet.greymass.com", 194, "4,EOS", "eosio.token")
DK_CHAIN(Libre, "38b1d7815474d0c60683ecbea321d723e83f5da6ae5f1c1f9fecc69d9ba96465",
         "https://libre.greymass.com", std::nullopt, "4,LIBRE", "eosio.token")
DK_CHAIN(LibreTestnet, "b64646740308df2ee06c6b72f34c0f7fa066d940e831f752db2006fcc2b78dee",
         "https://libretestnet.greymass.com", std::nullopt, "4,LIBRE", "eosio.token")
DK_CHAIN(Proton, "384da888112027f0321850a169f737c33e53b388aad48b5adace4bab97f437e0",
         "https://proton.greymass.com", std::nullopt, "4,XPR", "eosio.token")
DK_CHAIN(ProtonTestnet, "71ee83bcf52142d61019d95f9cc5427ba6a0d7ff8accd9e2088ae2abeaf3d3dd",
         "https://proton-testnet.greymass.com", std::nullopt, "4,XPR", "eosio.token")
DK_CHAIN(Telos, "4667b205c6838ef70ff7988f6e8257e8be0e1284a2f59699054a018f743b1d11",
         "https://telos.greymass.com", 977, "4,TLOS", "eosio.token")
DK_CHAIN(TelosTestnet, "1eaa0824707c8c16bd25145493bf062aecddfeb56c736f6ba6397f3195f33c9f",
         "https://telostestnet.greymass.com", 977, "4,TLOS", "eosio.token")
DK_CHAIN(UX, "8fc6dce7942189f842170de953932b1f66693ad3788f766e777b6f9d22335c02",
         "https://api.uxnetwork.io", std::nullopt, "4,UTX", "eosio.token")
DK_CHAIN(Vaulta, "aca376f206b8fc25a6ed44dbdc66547c36c6c33e3a119ffbeaef943642f0e906",
         "https://eos.greymass.com", 194, "4,A", "core.vaulta")
DK_CHAIN(WAX, "1064487b3cd1a897ce03ae5b6a865651747e2e152090f99c1d19d44e01aea5a4",
         "https://wax.greymass.com", 14001, "8,WAX", "eosio.token")
DK_CHAIN(WAXTestnet, "f16b1833c747c43682f4386fca9cbb327929334a762755ebec17f6f23c9b8a12",
         "https://waxtestnet.greymass.com", 14001, "8,WAX", "eosio.token")
DK_CHAIN(XPR, "384da888112027f0321850a169f737c33e53b388aad48b5adace4bab97f437e0",
         "https://proton.greymass.com", std::nullopt, "4,XPR", "eosio.token")
DK_CHAIN(XPRTestnet, "71ee83bcf52142d61019d95f9cc5427ba6a0d7ff8accd9e2088ae2abeaf3d3dd",
         "https://proton-testnet.greymass.com", std::nullopt, "4,XPR", "eosio.token")

#undef DK_CHAIN

const ChainDefinition* byIndice(std::string_view indice) {
    static const std::map<std::string, const ChainDefinition& (*)(), std::less<>> lookup = {
        {"EOS", EOS},         {"FIO", FIO},
        {"FIOTestnet", FIOTestnet}, {"Jungle4", Jungle4},
        {"KylinTestnet", KylinTestnet}, {"Libre", Libre},
        {"LibreTestnet", LibreTestnet}, {"Proton", Proton},
        {"ProtonTestnet", ProtonTestnet}, {"Telos", Telos},
        {"TelosTestnet", TelosTestnet}, {"UX", UX},
        {"Vaulta", Vaulta},   {"WAX", WAX},
        {"WAXTestnet", WAXTestnet}, {"XPR", XPR},
        {"XPRTestnet", XPRTestnet},
    };
    const auto found = lookup.find(indice);
    return found != lookup.end() ? &found->second() : nullptr;
}

}  // namespace Chains

const std::map<std::string, std::string, std::less<>>& ChainNames() {
    static const std::map<std::string, std::string, std::less<>> names = {
        {"EOS", "EOS"},
        {"FIO", "FIO"},
        {"FIOTestnet", "FIO (Testnet)"},
        {"Jungle4", "Jungle 4 (Testnet)"},
        {"KylinTestnet", "Kylin (Testnet)"},
        {"Libre", "Libre"},
        {"LibreTestnet", "Libre (Testnet)"},
        {"Proton", "XPR Network"},
        {"ProtonTestnet", "XPR Network (Testnet)"},
        {"Telos", "Telos"},
        {"TelosTestnet", "Telos (Testnet)"},
        {"UX", "UX Network"},
        {"Vaulta", "Vaulta"},
        {"WAX", "WAX"},
        {"WAXTestnet", "WAX (Testnet)"},
        {"XPR", "XPR Network"},
        {"XPRTestnet", "XPR Network (Testnet)"},
    };
    return names;
}

const std::map<std::string, std::string, std::less<>>& chainIdsToIndices() {
    static const std::map<std::string, std::string, std::less<>> indices = {
        {"21dcae42c0182200e93f954a074011f9048a7624c6fe81d3c9541a614a88bd1c", "FIO"},
        {"b20901380af44ef59c5918439a1f9a41d83669020319a80574b804a5f95cbd7e", "FIOTestnet"},
        {"73e4385a2708e6d7048834fbc1079f2fabb17b3c125b146af438971e90716c4d", "Jungle4"},
        {"5fff1dae8dc8e2fc4d5b23b2c7665c97f9e9d8edf2b6485a86ba311c25639191", "KylinTestnet"},
        {"38b1d7815474d0c60683ecbea321d723e83f5da6ae5f1c1f9fecc69d9ba96465", "Libre"},
        {"b64646740308df2ee06c6b72f34c0f7fa066d940e831f752db2006fcc2b78dee", "LibreTestnet"},
        {"384da888112027f0321850a169f737c33e53b388aad48b5adace4bab97f437e0", "XPR"},
        {"71ee83bcf52142d61019d95f9cc5427ba6a0d7ff8accd9e2088ae2abeaf3d3dd", "XPRTestnet"},
        {"4667b205c6838ef70ff7988f6e8257e8be0e1284a2f59699054a018f743b1d11", "Telos"},
        {"1eaa0824707c8c16bd25145493bf062aecddfeb56c736f6ba6397f3195f33c9f", "TelosTestnet"},
        {"8fc6dce7942189f842170de953932b1f66693ad3788f766e777b6f9d22335c02", "UX"},
        {"aca376f206b8fc25a6ed44dbdc66547c36c6c33e3a119ffbeaef943642f0e906", "Vaulta"},
        {"1064487b3cd1a897ce03ae5b6a865651747e2e152090f99c1d19d44e01aea5a4", "WAX"},
        {"f16b1833c747c43682f4386fca9cbb327929334a762755ebec17f6f23c9b8a12", "WAXTestnet"},
    };
    return indices;
}

const std::map<std::string, std::string, std::less<>>& chainLogos() {
    static const std::map<std::string, std::string, std::less<>> logos = {
        {"21dcae42c0182200e93f954a074011f9048a7624c6fe81d3c9541a614a88bd1c",
         "https://assets.wharfkit.com/chain/fio.png"},
        {"b20901380af44ef59c5918439a1f9a41d83669020319a80574b804a5f95cbd7e",
         "https://assets.wharfkit.com/chain/fio.png"},
        {"2a02a0053e5a8cf73a56ba0fda11e4d92e0238a4a2aa74fccf46d5a910746840",
         "https://assets.wharfkit.com/chain/jungle.png"},
        {"73e4385a2708e6d7048834fbc1079f2fabb17b3c125b146af438971e90716c4d",
         "https://assets.wharfkit.com/chain/jungle.png"},
        {"38b1d7815474d0c60683ecbea321d723e83f5da6ae5f1c1f9fecc69d9ba96465",
         "https://assets.wharfkit.com/chain/libre.png"},
        {"b64646740308df2ee06c6b72f34c0f7fa066d940e831f752db2006fcc2b78dee",
         "https://assets.wharfkit.com/chain/libre.png"},
        {"384da888112027f0321850a169f737c33e53b388aad48b5adace4bab97f437e0",
         "https://assets.wharfkit.com/chain/xprnetwork.png"},
        {"71ee83bcf52142d61019d95f9cc5427ba6a0d7ff8accd9e2088ae2abeaf3d3dd",
         "https://assets.wharfkit.com/chain/xprnetwork.png"},
        {"4667b205c6838ef70ff7988f6e8257e8be0e1284a2f59699054a018f743b1d11",
         "https://assets.wharfkit.com/chain/telos.png"},
        {"1eaa0824707c8c16bd25145493bf062aecddfeb56c736f6ba6397f3195f33c9f",
         "https://assets.wharfkit.com/chain/telos.png"},
        {"aca376f206b8fc25a6ed44dbdc66547c36c6c33e3a119ffbeaef943642f0e906",
         "https://assets.wharfkit.com/chain/vaulta.png"},
        {"8fc6dce7942189f842170de953932b1f66693ad3788f766e777b6f9d22335c02",
         "https://assets.wharfkit.com/chain/ux.png"},
        {"1064487b3cd1a897ce03ae5b6a865651747e2e152090f99c1d19d44e01aea5a4",
         "https://assets.wharfkit.com/chain/wax.png"},
        {"f16b1833c747c43682f4386fca9cbb327929334a762755ebec17f6f23c9b8a12",
         "https://assets.wharfkit.com/chain/wax.png"},
    };
    return logos;
}

}  // namespace dwarfkit
