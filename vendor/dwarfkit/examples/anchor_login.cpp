// Console Anchor login: create a SessionKit with WalletPluginAnchor, print the
// identity request as a terminal QR code, wait for the wallet's buoy callback
// and perform a small eosio.token transfer on Jungle 4.
//
//   anchor_login [to] [quantity] [memo]
//
// Scan the QR with Anchor on a mobile device (or paste the esr: link into
// Anchor Desktop) and approve both requests.
#include <iostream>

#include <dwarfkit/plugins/wallet/anchor.hpp>
#include <dwarfkit/session.hpp>
#include <dwarfkit/transport/curl_fetch_provider.hpp>
#include <dwarfkit/transport/curl_websocket_provider.hpp>

#include <qrcodegen.hpp>

namespace dk = dwarfkit;

namespace {

// Render two module rows per text line with half blocks.
void printQr(const std::string& text) {
    using qrcodegen::QrCode;
    const QrCode qr = QrCode::encodeText(text.c_str(), QrCode::Ecc::LOW);
    const int size = qr.getSize();
    const int border = 2;
    for (int y = -border; y < size + border; y += 2) {
        std::string line;
        for (int x = -border; x < size + border; x++) {
            const bool top = qr.getModule(x, y);
            const bool bottom = qr.getModule(x, y + 1);
            if (top && bottom) {
                line += "\xE2\x96\x88";  // full block
            } else if (top) {
                line += "\xE2\x96\x80";  // upper half
            } else if (bottom) {
                line += "\xE2\x96\x84";  // lower half
            } else {
                line += " ";
            }
        }
        std::cout << line << "\n";
    }
}

// A console UI that renders qr prompt elements as scannable codes.
class QrConsoleUserInterface final : public dk::ConsoleUserInterface {
public:
    dk::Result<dk::PromptResponse> prompt(const dk::PromptArgs& args,
                                          dk::CancelToken token) override {
        std::cout << "\n== " << args.title << " ==\n";
        if (args.body) {
            std::cout << *args.body << "\n";
        }
        for (const auto& element : args.elements) {
            if (element.type == dk::PromptElementType::qr && element.data.is_string()) {
                printQr(element.data.get<std::string>());
                std::cout << element.data.get<std::string>() << "\n";
            } else if (element.type == dk::PromptElementType::link &&
                       element.data.is_object() && element.data.contains("href")) {
                if (element.label) {
                    std::cout << *element.label << ": ";
                }
                std::cout << element.data["href"].get<std::string>() << "\n";
            }
        }
        (void)token;
        return dk::PromptResponse{};
    }
};

}  // namespace

int main(int argc, char** argv) {
    const std::string to = argc > 1 ? argv[1] : "teamgreymass";
    const std::string quantity = argc > 2 ? argv[2] : "0.0001 EOS";
    const std::string memo = argc > 3 ? argv[3] : "sent with dwarfkit";

    auto buoyWs = std::make_shared<dk::CurlWebSocketProvider>();
    auto anchor = std::make_shared<dk::WalletPluginAnchor>(
        dk::WalletPluginAnchorOptions{.buoyWs = buoyWs});

    dk::SessionKitArgs args;
    args.appName = "dwarfkit-example";
    args.chains = {dk::Chains::Jungle4()};
    args.ui = std::make_shared<QrConsoleUserInterface>();
    args.walletPlugins = {anchor};

    dk::SessionKitOptions options;
    options.fetch = std::make_shared<dk::CurlFetchProvider>();

    dk::SessionKit kit(args, options);

    std::cout << "Logging in with Anchor on Jungle 4...\n";
    auto login = kit.login({});
    if (!login) {
        std::cerr << "login failed: " << login.error().message << "\n";
        return 1;
    }
    auto session = login->session;
    std::cout << "Logged in as " << session->permissionLevel.toString() << "\n";

    const dk::json action = {
        {"account", "eosio.token"},
        {"name", "transfer"},
        {"authorization",
         dk::json::array({{{"actor", session->actor().toString()},
                           {"permission", session->permission().toString()}}})},
        {"data",
         {{"from", session->actor().toString()},
          {"to", to},
          {"quantity", quantity},
          {"memo", memo}}}};

    std::cout << "Requesting signature for a " << quantity << " transfer to " << to << "...\n";
    const auto result = session->transact({.action = action});
    if (!result) {
        std::cerr << "transact failed: " << result.error().message << "\n";
        return 1;
    }
    if (result->response && result->response->contains("transaction_id")) {
        std::cout << "Transaction broadcast: "
                  << (*result->response)["transaction_id"].get<std::string>() << "\n";
    } else {
        std::cout << "Transaction signed with " << result->signatures.size()
                  << " signature(s)\n";
    }
    return 0;
}
