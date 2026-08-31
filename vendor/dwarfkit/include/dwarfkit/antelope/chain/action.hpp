// Port of antelope src/chain/action.ts
#pragma once

#include <memory>

#include <dwarfkit/antelope/chain/abi.hpp>
#include <dwarfkit/antelope/chain/permission_level.hpp>
#include <dwarfkit/antelope/serializer.hpp>

namespace dwarfkit {

struct Action {
    DK_STRUCT("action")
    // The account (a.k.a. contract) to run action on.
    Name account;
    // The name of the action.
    Name name;
    // The permissions authorizing the action.
    std::vector<PermissionLevel> authorization;
    // The ABI-encoded action data.
    Bytes data;
    DK_FIELDS(account, name, authorization, data)

    // Retained ABI (not serialized); set when the action is created with one.
    std::shared_ptr<const ABI> abi;

    // From json whose data is already ABI-encoded (hex string).
    static Result<Action> from(const json& anyAction);
    // From json whose data is encoded using the provided ABI; the ABI is retained.
    static Result<Action> from(const json& anyAction, const ABI& abi);
    static Action from(const Action& action) { return action; }

    // From typed action data; an ABI is synthesized from the type and retained.
    template <DkStruct T>
    static Result<Action> from(const json& base, const T& typedData) {
        DK_TRY(encoded, Serializer::encode(typedData));
        json object = base;
        object["data"] = encoded.hexString();
        DK_TRY(action, structFrom<Action>(object));
        ABI synthesized = Serializer::synthesize<T>();
        synthesized.actions.push_back({action.name, std::string(T::abiName), ""});
        action.abi = std::make_shared<ABI>(std::move(synthesized));
        return action;
    }

    bool equals(const Action& other) const {
        return account == other.account && name == other.name &&
               authorization == other.authorization && data == other.data;
    }
    // Compare against an untyped action, resolving it with this action's ABI.
    Result<bool> equals(const json& other) const;

    // Return action data decoded as given type.
    template <class T>
    Result<T> decodeData() const {
        return Serializer::decode<T>(data);
    }
    // Return action data decoded using an ABI.
    Result<json> decodeData(const ABI& withAbi) const;

    // The action with its data decoded using the retained ABI.
    Result<json> decoded() const;
};

}  // namespace dwarfkit
