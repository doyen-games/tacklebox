// Port of wallet-plugin-cleos src/index.ts: sign nothing, print the cleos
// command instead. Development tool. The copy-to-clipboard button becomes a
// label-only element (see DIVERGENCES.md).
#pragma once

#include <dwarfkit/session/login.hpp>
#include <dwarfkit/session/transact.hpp>
#include <dwarfkit/session/wallet.hpp>

namespace dwarfkit {

class WalletPluginCleos final : public AbstractWalletPlugin {
public:
    WalletPluginCleos();

    std::string id() const override { return "cleos"; }

    Result<WalletPluginLoginResponse> login(LoginContext& context) override;
    Result<WalletPluginSignResponse> sign(const ResolvedSigningRequest& resolved,
                                          TransactContext& context) override;
};

}  // namespace dwarfkit
