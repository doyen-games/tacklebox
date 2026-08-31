// Port of wharfkit/msigs (@wharfkit/msigs): the typed client for the Greymass
// msig proposal API. Responses stay json, like the loose upstream interfaces.
#pragma once

#include <memory>
#include <optional>
#include <string>

#include <dwarfkit/antelope.hpp>

namespace dwarfkit {

struct GetProposalOptions {
    std::optional<uint64_t> globalseq;
    std::optional<bool> version_history;
};

struct GetProposalHistoryOptions {
    std::optional<std::string> status;
    std::optional<int> limit;
    std::optional<int> offset;
};

struct GetProposalsOptions {
    std::optional<std::string> status;
    std::optional<int> limit;
    std::optional<int> offset;
};

struct DebugProposalOptions {
    std::optional<uint64_t> globalseq;
};

struct GetApprovalsOptions {
    std::optional<uint64_t> globalseq;
    std::optional<int> limit;
    std::optional<int> offset;
};

struct GetActivityOptions {
    std::optional<int> limit;
    std::optional<int> offset;
    std::optional<std::string> action_type;
};

struct GetApproverProposalsOptions {
    std::optional<std::string> status;
    std::optional<bool> include_approved;
    std::optional<int> limit;
    std::optional<int> offset;
};

struct SearchProposalsOptions {
    std::optional<std::string> status;
    std::optional<int> limit;
    std::optional<int> offset;
};

struct MsigsClientOptions {
    std::optional<int> maxProposalLimit;
    std::optional<int> maxApprovalLimit;
};

class MsigsClient {
public:
    explicit MsigsClient(std::shared_ptr<APIClient> client,
                         const MsigsClientOptions& options = {});

    Result<int> getMaxProposalLimit();
    Result<int> getMaxApprovalLimit();

    Result<json> get_proposal(const Name& proposer, const Name& proposalName,
                              const GetProposalOptions& options = {});
    Result<json> get_proposal_history(const Name& proposer, const Name& proposalName,
                                      const GetProposalHistoryOptions& options = {});
    Result<json> get_proposals(const Name& proposer, const GetProposalsOptions& options = {});
    Result<json> get_approvals(const Name& proposer, const Name& proposalName,
                               const GetApprovalsOptions& options = {});
    Result<json> get_activity(const Name& account, const GetActivityOptions& options = {});
    Result<json> get_approver_proposals(const Name& approver,
                                        const GetApproverProposalsOptions& options = {});
    Result<json> search_proposals(const std::string& query,
                                  const SearchProposalsOptions& options = {});
    Result<json> get_status();
    Result<json> debug_proposal(const Name& proposer, const Name& proposalName,
                                const DebugProposalOptions& options = {});

private:
    void initializeLimits();

    std::shared_ptr<APIClient> client_;
    std::optional<int> maxProposalLimit_;
    std::optional<int> maxApprovalLimit_;
    bool limitsInitialized_ = false;
};

struct PaginationInfo {
    int currentPage = 0;
    int pageSize = 0;
    int totalResults = 0;
    int totalPages = 0;
    bool hasMore = false;
    bool hasPrevious = false;
    std::optional<int> nextOffset;
    std::optional<int> previousOffset;
};

PaginationInfo getPaginationInfo(int offset, int limit, int total, bool more);

}  // namespace dwarfkit
