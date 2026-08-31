// Port of the session kit's nodejs use case as a console example: establish a
// session with WalletPluginPrivateKey and perform an eosio.token transfer on
// Jungle 4.
//
//   transfer_privatekey <actor@permission> <private key> [to] [quantity] [memo]
//
// Create a free test account (and its key) at https://monitor.jungletestnet.io
#include <iostream>

#include <dwarfkit/plugins/wallet/privatekey.hpp>
#include <dwarfkit/session.hpp>
#include <dwarfkit/transport/curl_fetch_provider.hpp>

namespace dk = dwarfkit;

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "usage: transfer_privatekey <actor@permission> <private key> [to] "
                     "[quantity] [memo]\n";
        return 1;
    }
    const auto permission = dk::PermissionLevel::from(argv[1]);
    if (!permission) {
        std::cerr << "invalid permission: " << permission.error().message << "\n";
        return 1;
    }
    const auto wallet = dk::WalletPluginPrivateKey::make(argv[2]);
    if (!wallet) {
        std::cerr << "invalid private key: " << wallet.error().message << "\n";
        return 1;
    }
    const std::string to = argc > 3 ? argv[3] : "teamgreymass";
    const std::string quantity = argc > 4 ? argv[4] : "0.0001 EOS";
    const std::string memo = argc > 5 ? argv[5] : "sent with dwarfkit";

    dk::SessionArgs args;
    args.chain = dk::Chains::Jungle4();
    args.permissionLevel = *permission;
    args.walletPlugin = *wallet;

    dk::SessionOptions options;
    options.fetch = std::make_shared<dk::CurlFetchProvider>();

    dk::Session session(args, options);

    const dk::json action = {
        {"account", "eosio.token"},
        {"name", "transfer"},
        {"authorization", dk::json::array({{{"actor", session.actor().toString()},
                                            {"permission", session.permission().toString()}}})},
        {"data",
         {{"from", session.actor().toString()},
          {"to", to},
          {"quantity", quantity},
          {"memo", memo}}}};

    std::cout << "Sending " << quantity << " from " << session.actor().toString() << " to " << to
              << " on Jungle 4...\n";
    const auto result = session.transact({.action = action});
    if (!result) {
        std::cerr << "transact failed: " << result.error().message << "\n";
        return 1;
    }
    if (result->response) {
        std::cout << "Transaction id: " << result->response->value("transaction_id", "") << "\n";
    }
    for (const auto& signature : result->signatures) {
        std::cout << "Signature: " << signature.toString() << "\n";
    }
    return 0;
}
