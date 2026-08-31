// Port of @wharfkit/abicache src/abi.ts. The TS pending promise map becomes a
// mutex (BLUEPRINT.md 6.3): concurrent getAbi calls for the same account are
// serialized rather than shared.
#pragma once

#include <map>
#include <memory>
#include <mutex>
#include <set>

#include <dwarfkit/antelope/api/client.hpp>
#include <dwarfkit/signing_request/abi_provider.hpp>

namespace dwarfkit {

// Given an APIClient instance, this class provides an AbiProvider interface
// for retrieving and caching ABIs.
class ABICache : public AbiProvider {
public:
    explicit ABICache(std::shared_ptr<APIClient> client) : client(std::move(client)) {}

    std::shared_ptr<APIClient> client;

    Result<ABI> getAbi(const Name& account) override;

    void setAbi(const Name& account, const ABI& abi, bool merge = false);

    // Snapshot accessors for tests and inspection.
    size_t cacheSize() const;
    bool cacheHas(const Name& account) const;
    std::optional<ABI> cacheGet(const Name& account) const;

private:
    static ABI merge(const ABI& base, const ABI& addition, const std::string& version);

    mutable std::mutex mutex_;
    std::map<std::string, ABI> cache_;
    // Keys whose cached ABI came only from merged partial (action-synthesized)
    // ABIs and is not yet reconciled with chain.
    std::set<std::string> partial_;
};

}  // namespace dwarfkit
