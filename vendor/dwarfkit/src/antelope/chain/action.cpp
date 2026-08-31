#include <dwarfkit/antelope/chain/action.hpp>

namespace dwarfkit {

namespace {

Result<Action> buildAction(const json& anyAction, Bytes data) {
    json object = anyAction;
    object["data"] = data.hexString();
    DK_TRY(action, structFrom<Action>(object));
    return action;
}

// Bytes.isBytes: hex strings and arrays of numbers count as already-encoded
bool isBytesValue(const json& data) {
    if (data.is_string()) return true;
    if (data.is_array()) {
        for (const auto& item : data) {
            if (!item.is_number()) return false;
        }
        return true;
    }
    return false;
}

json normalizeBytesValue(const json& anyAction) {
    if (anyAction.contains("data") && anyAction.at("data").is_array()) {
        std::vector<uint8_t> bytes;
        for (const auto& item : anyAction.at("data")) {
            bytes.push_back(item.get<uint8_t>());
        }
        json object = anyAction;
        object["data"] = Bytes(std::move(bytes)).hexString();
        return object;
    }
    return anyAction;
}

}  // namespace

Result<Action> Action::from(const json& anyAction) {
    if (!anyAction.contains("data") || !isBytesValue(anyAction.at("data"))) {
        return err(ErrorKind::Invalid,
                   "Missing ABI definition when creating action with untyped action data");
    }
    return structFrom<Action>(normalizeBytesValue(anyAction));
}

Result<Action> Action::from(const json& anyAction, const ABI& withAbi) {
    Result<Action> action = [&]() -> Result<Action> {
        const json& data = anyAction.contains("data") ? anyAction.at("data") : json(nullptr);
        if (isBytesValue(data)) {
            // already encoded
            return structFrom<Action>(normalizeBytesValue(anyAction));
        }
        const std::string actionName =
            anyAction.contains("name") ? anyAction.at("name").get<std::string>() : "";
        const auto type = withAbi.getActionType(actionName);
        if (!type) {
            return err(ErrorKind::Invalid, "The action \"" + actionName +
                                               "\" does not exist on the ABI provided.");
        }
        DK_TRY(encoded, Serializer::encode(data, *type, withAbi));
        return buildAction(anyAction, encoded);
    }();
    if (action) {
        action->abi = std::make_shared<ABI>(withAbi);
    }
    return action;
}

Result<bool> Action::equals(const json& other) const {
    Result<Action> otherAction =
        abi ? Action::from(other, *abi) : Action::from(other);
    DK_CHECK(otherAction);
    return equals(*otherAction);
}

Result<json> Action::decodeData(const ABI& withAbi) const {
    const auto type = withAbi.getActionType(name);
    if (!type) {
        return err(ErrorKind::Invalid,
                   "Action " + name.toString() + " does not exist in provided ABI");
    }
    return Serializer::decode(data, *type, withAbi);
}

Result<json> Action::decoded() const {
    if (!abi) {
        return err(ErrorKind::Invalid, "Missing ABI definition when decoding action data");
    }
    json rv = dkStructToJSON(*this);
    DK_TRY(decodedData, decodeData(*abi));
    rv["data"] = std::move(decodedData);
    return rv;
}

}  // namespace dwarfkit
