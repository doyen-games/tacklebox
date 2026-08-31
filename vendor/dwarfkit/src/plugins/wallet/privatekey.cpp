#include <dwarfkit/plugins/wallet/privatekey.hpp>

namespace dwarfkit {

Result<std::shared_ptr<WalletPluginPrivateKey>> WalletPluginPrivateKey::make(
    std::string_view privateKeyData) {
    DK_TRY(privateKey, PrivateKey::from(privateKeyData));
    return std::make_shared<WalletPluginPrivateKey>(privateKey);
}

WalletPluginPrivateKey::WalletPluginPrivateKey(const PrivateKey& privateKey) {
    config_ = {.requiresChainSelect = true, .requiresPermissionSelect = true};
    data_["privateKey"] = privateKey.toString();
    const auto publicKey = privateKey.toPublic();
    const std::string publicKeyString = publicKey ? publicKey->toString() : "";
    metadata_.name = "Private Key Signer";
    metadata_.publicKey = publicKeyString;
    // the upstream description reads this.data.publicKey, which is never set;
    // it templates "undefined" into the string. Use the actual key instead.
    metadata_.description =
        "An unsecured wallet that can sign for authorities using the " +
        (publicKeyString.size() > 15
             ? publicKeyString.substr(0, 11) + "..." +
                   publicKeyString.substr(publicKeyString.size() - 4)
             : publicKeyString) +
        " public key.";
}

Result<WalletPluginLoginResponse> WalletPluginPrivateKey::login(LoginContext& context) {
    Checksum256 chain;
    if (context.chain) {
        chain = context.chain->id;
    } else if (!context.chains.empty()) {
        chain = context.chains[0].id;
    }
    if (!context.permissionLevel) {
        return err(ErrorKind::Plugin,
                   "Calling login() without a permissionLevel is not supported by the "
                   "WalletPluginPrivateKey plugin.");
    }
    WalletPluginLoginResponse response;
    response.chain = chain;
    response.permissionLevel = *context.permissionLevel;
    return response;
}

Result<WalletPluginSignResponse> WalletPluginPrivateKey::sign(
    const ResolvedSigningRequest& resolved, TransactContext& context) {
    const Transaction& transaction = resolved.transaction;
    const Checksum256 digest = transaction.signingDigest(context.chain.id);
    DK_TRY(privateKey, PrivateKey::from(data_.value("privateKey", "")));
    DK_TRY(signature, privateKey.signDigest(digest));
    WalletPluginSignResponse response;
    response.signatures = {signature};
    return response;
}

}  // namespace dwarfkit
