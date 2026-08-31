#include <dwarfkit/plugins/wallet/cloudwallet/utils.hpp>

#include <algorithm>

namespace dwarfkit::cloudwallet {

Result<void> validateModifications(const Transaction& original, const Transaction& modified) {
    // Ensure all the original actions exist within the modified transaction
    const bool originalsExist = std::all_of(
        original.actions.begin(), original.actions.end(), [&](const Action& action) {
            return std::any_of(modified.actions.begin(), modified.actions.end(),
                               [&](const Action& modifiedAction) {
                                   return action.equals(modifiedAction);
                               });
        });
    if (!originalsExist) {
        return err(ErrorKind::Invalid,
                   "The modified transaction does not contain all the original actions.");
    }

    // Iterate and validate each action newly added to this transaction
    for (const Action& newAction : modified.actions) {
        const bool isOriginal =
            std::any_of(original.actions.begin(), original.actions.end(),
                        [&](const Action& originalAction) {
                            return newAction.equals(originalAction);
                        });
        if (isOriginal) {
            continue;
        }
        // Determine if a new action has the authorization of the original
        // actor
        const Name originalActor = original.actions.empty()
                                       ? Name()
                                       : original.actions[0].authorization.empty()
                                             ? Name()
                                             : original.actions[0].authorization[0].actor;
        const bool authByUser =
            std::any_of(newAction.authorization.begin(), newAction.authorization.end(),
                        [&](const PermissionLevel& auth) { return auth.actor == originalActor; });
        if (!authByUser) {
            continue;
        }
        // Ensure if a transaction fee is being paid by the user, it is going
        // to the correct account
        if (newAction.account == Name::from("eosio.token") &&
            newAction.name == Name::from("transfer")) {
            DK_TRY(data,
                   Serializer::decode(newAction.data.array, "transfer", validationAbi()));
            if (data.value("to", "") == "txfee.wam" &&
                data.value("memo", "").starts_with("WAX fee for")) {
                continue;
            }
        }
        // Ensure if a RAM purchase is occurring during a modification, it is
        // going to the user
        if (newAction.account == Name::from("eosio") &&
            newAction.name == Name::from("buyrambytes")) {
            DK_TRY(data,
                   Serializer::decode(newAction.data.array, "buyrambytes", validationAbi()));
            if (data.value("receiver", "") == originalActor.toString()) {
                continue;
            }
        }
        // If not passing the above rules, error
        return err(ErrorKind::Invalid,
                   "The modified transaction contains one or more actions that are not "
                   "allowed.");
    }
    return {};
}

}  // namespace dwarfkit::cloudwallet
