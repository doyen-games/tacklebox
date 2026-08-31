#include <dwarfkit/plugins/transact/cosigner.hpp>

namespace dwarfkit {

TransactPluginCosigner::TransactPluginCosigner(const CosignerOptions& options)
    : actor(options.actor), permission(options.permission), privateKey(options.privateKey) {
    if (options.contract) {
        contract = *options.contract;
    }
    if (options.action) {
        action = *options.action;
    }
}

void TransactPluginCosigner::register_(TransactContext& context) {
    context.addHook(
        TransactHookTypes::beforeSign,
        [this](SigningRequest request, TransactContext& ctx) -> Result<TransactHookResponseType> {
            // Modify request data to prepend noop action
            DK_TRY(modifiedRequest, prependNoop(request));
            // Sign Transaction
            DK_TRY(resolved, ctx.resolve(modifiedRequest));
            DK_TRY(chainId, request.getChainId());
            DK_TRY(signature,
                   privateKey.signDigest(resolved.transaction.signingDigest(chainId)));
            // Create a new request that is a 'Transaction', not an 'Action[]'
            TransactArgs args;
            args.transaction = Serializer::objectify(resolved.transaction);
            DK_TRY(newRequest, ctx.createRequest(args));
            // Return modified request and new signature
            return TransactHookResponseType{
                TransactHookResponse{std::move(newRequest), {signature}}};
        });
}

Result<SigningRequest> TransactPluginCosigner::prependNoop(const SigningRequest& request) const {
    // Create noop action to assume resource costs
    Action newAction;
    newAction.account = contract;
    newAction.name = action;
    newAction.authorization = {PermissionLevel{actor, permission}};
    newAction.data = Bytes();
    // Prepend this action to the request
    return prependAction(request, newAction);
}

}  // namespace dwarfkit
