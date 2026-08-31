// Port of wharfkit/transact-plugin-autocorrect. The upstream race between the
// "Checking transaction" prompt and the correction becomes a status update
// (see DIVERGENCES.md).
#pragma once

#include <dwarfkit/resources.hpp>
#include <dwarfkit/session.hpp>

namespace dwarfkit {

class TransactPluginAutoCorrect : public AbstractTransactPlugin {
public:
    struct Powerup {
        DK_STRUCT("powerup")
        Name payer;
        Name receiver;
        uint32_t days = 0;
        int64_t net_frac = 0;
        int64_t cpu_frac = 0;
        Asset max_payment;
        DK_FIELDS(payer, receiver, days, net_frac, cpu_frac, max_payment)
    };

    struct Buyrambytes {
        DK_STRUCT("buyrambytes")
        Name payer;
        Name receiver;
        uint32_t bytes = 0;
        DK_FIELDS(payer, receiver, bytes)
    };

    std::string id() const override { return "transact-plugin-autocorrect"; }
    LocaleDefinitions translations() const override;
    void register_(TransactContext& context) override;

    Result<TransactHookResponseType> run(SigningRequest request, TransactContext& context);

private:
    Result<SigningRequest> correct(const SigningRequest& request, TransactContext& context,
                                   api::v1::AccountObject& account);
    Result<SigningRequest> buyram(TransactContext& context,
                                  const ResolvedSigningRequest& resolved,
                                  api::v1::AccountObject& account, const Resources& resources,
                                  double needed);
    Result<SigningRequest> powerup(TransactContext& context,
                                   const ResolvedSigningRequest& resolved,
                                   api::v1::AccountObject& account, const Resources& resources,
                                   double cpu, double net);

    std::optional<SampleUsage> sample_;
    std::optional<Asset> price_;
    std::vector<std::string> resources_;
    int iterations_ = 0;
};

// The exception object from a compute_transaction response, or null.
json getException(const json& response);

}  // namespace dwarfkit
