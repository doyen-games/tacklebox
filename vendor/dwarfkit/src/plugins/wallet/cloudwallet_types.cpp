#include <dwarfkit/plugins/wallet/cloudwallet/types.hpp>

namespace dwarfkit::cloudwallet {

Result<WAXCloudWalletLoginResponse> WAXCloudWalletLoginResponse::from(const json& value) {
    if (!value.is_object()) {
        return err(ErrorKind::Invalid, "Invalid Cloud Wallet login response");
    }
    WAXCloudWalletLoginResponse rv;
    rv.verified = value.value("verified", false);
    if (value.contains("pubKeys") && value["pubKeys"].is_array()) {
        for (const auto& key : value["pubKeys"]) {
            if (key.is_string()) {
                rv.pubKeys.push_back(key.get<std::string>());
            }
        }
    }
    rv.userAccount = value.value("userAccount", "");
    if (value.contains("permission") && value["permission"].is_string()) {
        rv.permission = value["permission"].get<std::string>();
    }
    if (value.contains("proof")) {
        rv.proof = value["proof"];
    }
    return rv;
}

namespace {

Result<Bytes> parseSerializedTransaction(const json& value) {
    if (value.is_string()) {
        return Bytes::from(value.get<std::string>());
    }
    if (value.is_array()) {
        std::vector<uint8_t> bytes;
        bytes.reserve(value.size());
        for (const auto& item : value) {
            if (!item.is_number()) {
                return err(ErrorKind::Invalid, "Invalid serializedTransaction byte");
            }
            bytes.push_back(static_cast<uint8_t>(item.get<uint32_t>()));
        }
        return Bytes(std::move(bytes));
    }
    // postMessage structured clones of a Uint8Array can arrive as an object
    // of index keys
    if (value.is_object()) {
        std::vector<uint8_t> bytes(value.size());
        for (const auto& [key, item] : value.items()) {
            const size_t index = static_cast<size_t>(std::stoul(key));
            if (index >= bytes.size() || !item.is_number()) {
                return err(ErrorKind::Invalid, "Invalid serializedTransaction byte");
            }
            bytes[index] = static_cast<uint8_t>(item.get<uint32_t>());
        }
        return Bytes(std::move(bytes));
    }
    return err(ErrorKind::Invalid, "Invalid serializedTransaction");
}

}  // namespace

Result<WAXCloudWalletSigningResponse> WAXCloudWalletSigningResponse::from(const json& value) {
    if (!value.is_object()) {
        return err(ErrorKind::Invalid, "Invalid Cloud Wallet signing response");
    }
    WAXCloudWalletSigningResponse rv;
    rv.verified = value.value("verified", false);
    rv.type = value.value("type", "");
    if (value.contains("signatures") && value["signatures"].is_array()) {
        for (const auto& sig : value["signatures"]) {
            if (sig.is_string()) {
                DK_TRY(signature, Signature::from(sig.get<std::string>()));
                rv.signatures.push_back(signature);
            }
        }
    }
    if (value.contains("serializedTransaction") &&
        !value["serializedTransaction"].is_null()) {
        DK_TRY(bytes, parseSerializedTransaction(value["serializedTransaction"]));
        rv.serializedTransaction = bytes;
    }
    return rv;
}

const ABI& validationAbi() {
    static const ABI abi = [] {
        const json def = {
            {"version", "eosio::abi/1.2"},
            {"structs",
             json::array(
                 {{{"name", "buyrambytes"},
                   {"base", ""},
                   {"fields",
                    json::array({{{"name", "payer"}, {"type", "name"}},
                                 {{"name", "receiver"}, {"type", "name"}},
                                 {{"name", "bytes"}, {"type", "uint32"}}})}},
                  {{"name", "transfer"},
                   {"base", ""},
                   {"fields",
                    json::array({{{"name", "from"}, {"type", "name"}},
                                 {{"name", "to"}, {"type", "name"}},
                                 {{"name", "quantity"}, {"type", "asset"}},
                                 {{"name", "memo"}, {"type", "string"}}})}}})}};
        const auto parsed = ABI::from(def);
        return parsed ? *parsed : ABI();
    }();
    return abi;
}

}  // namespace dwarfkit::cloudwallet
