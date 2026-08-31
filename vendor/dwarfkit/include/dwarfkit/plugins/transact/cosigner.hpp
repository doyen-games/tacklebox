// Port of wharfkit/transact-plugin-cosigner.
#pragma once

#include <dwarfkit/session.hpp>

namespace dwarfkit {

struct CosignerOptions {
    Name actor;
    Name permission;
    PrivateKey privateKey;
    std::optional<Name> contract;
    std::optional<Name> action;
};

class TransactPluginCosigner : public AbstractTransactPlugin {
public:
    explicit TransactPluginCosigner(const CosignerOptions& options);

    std::string id() const override { return "transact-plugin-cosigner"; }
    void register_(TransactContext& context) override;

    Name actor;
    Name permission;
    PrivateKey privateKey;
    Name contract = Name::from("greymassnoop");
    Name action = Name::from("noop");

    // Prepend the noop action that assumes the resource costs.
    Result<SigningRequest> prependNoop(const SigningRequest& request) const;
};

}  // namespace dwarfkit
