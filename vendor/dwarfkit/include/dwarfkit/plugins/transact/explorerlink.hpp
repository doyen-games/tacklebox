// Port of wharfkit/transact-plugin-explorerlink.
#pragma once

#include <dwarfkit/session.hpp>

namespace dwarfkit {

class TransactPluginExplorerLink : public AbstractTransactPlugin {
public:
    std::string id() const override { return "transact-plugin-explorer-link"; }
    LocaleDefinitions translations() const override;
    void register_(TransactContext& context) override;

    Result<std::string> getExplorerLink(TransactContext& context, const ChainDefinition& chain,
                                        const std::string& transaction) const;
};

}  // namespace dwarfkit
