// Port of wharfkit/wallet-plugin-privatekey: an unsecured wallet that signs
// for authorities with a plain private key. The TS plugin keeps the key in
// data.privateKey (a string); this port keeps the same serialized shape.
#pragma once

#include <dwarfkit/session/login.hpp>
#include <dwarfkit/session/session.hpp>

namespace dwarfkit {

class WalletPluginPrivateKey final : public AbstractWalletPlugin {
public:
    // Errors when the private key string cannot be parsed.
    static Result<std::shared_ptr<WalletPluginPrivateKey>> make(std::string_view privateKey);
    explicit WalletPluginPrivateKey(const PrivateKey& privateKey);

    std::string id() const override { return "wallet-plugin-privatekey"; }
    Result<WalletPluginLoginResponse> login(LoginContext& context) override;
    Result<WalletPluginSignResponse> sign(const ResolvedSigningRequest& resolved,
                                          TransactContext& context) override;
};

}  // namespace dwarfkit
