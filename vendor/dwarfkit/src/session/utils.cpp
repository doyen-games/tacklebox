#include <dwarfkit/session/utils.hpp>

namespace dwarfkit {

namespace {

enum class Placement { append, prepend };

Result<SigningRequest> insertAction(const SigningRequest& request, const Action& newAction,
                                    Placement placement) {
    SigningRequest cloned = request.clone();
    Result<void> outcome = {};
    std::visit(
        [&](auto& d) {
            auto& req = d.req;
            if (const Action* action = req.template get_if<Action>()) {
                std::vector<Action> pair = placement == Placement::append
                                               ? std::vector<Action>{*action, newAction}
                                               : std::vector<Action>{newAction, *action};
                req.value = std::move(pair);
            } else if (auto* actions = req.template get_if<std::vector<Action>>()) {
                if (placement == Placement::append) {
                    actions->push_back(newAction);
                } else {
                    actions->insert(actions->begin(), newAction);
                }
            } else if (auto* tx = req.template get_if<Transaction>()) {
                if (placement == Placement::append) {
                    tx->actions.push_back(newAction);
                } else {
                    tx->actions.insert(tx->actions.begin(), newAction);
                }
            } else {
                outcome = err(ErrorKind::Invalid, "unknown data req type");
            }
        },
        cloned.data);
    DK_CHECK(std::move(outcome));
    return cloned;
}

}  // namespace

Result<SigningRequest> appendAction(const SigningRequest& request, const Action& action) {
    return insertAction(request, action, Placement::append);
}

Result<SigningRequest> appendAction(const SigningRequest& request, const json& action) {
    DK_TRY(typed, Action::from(action));
    return insertAction(request, typed, Placement::append);
}

Result<SigningRequest> prependAction(const SigningRequest& request, const Action& action) {
    return insertAction(request, action, Placement::prepend);
}

Result<SigningRequest> prependAction(const SigningRequest& request, const json& action) {
    DK_TRY(typed, Action::from(action));
    return insertAction(request, typed, Placement::prepend);
}

}  // namespace dwarfkit
