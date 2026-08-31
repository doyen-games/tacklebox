#include <dwarfkit/msigs.hpp>

#include <cmath>

namespace dwarfkit {

MsigsClient::MsigsClient(std::shared_ptr<APIClient> client, const MsigsClientOptions& options)
    : client_(std::move(client)),
      maxProposalLimit_(options.maxProposalLimit),
      maxApprovalLimit_(options.maxApprovalLimit) {}

void MsigsClient::initializeLimits() {
    if (limitsInitialized_) {
        return;
    }
    if (maxProposalLimit_ && maxApprovalLimit_) {
        limitsInitialized_ = true;
        return;
    }
    const auto status = get_status();
    if (status) {
        if (!maxProposalLimit_) {
            maxProposalLimit_ = status->value("max_proposal_results", 20);
        }
        if (!maxApprovalLimit_) {
            maxApprovalLimit_ = status->value("max_approval_results", 100);
        }
    } else {
        if (!maxProposalLimit_) {
            maxProposalLimit_ = 20;
        }
        if (!maxApprovalLimit_) {
            maxApprovalLimit_ = 100;
        }
    }
    limitsInitialized_ = true;
}

Result<int> MsigsClient::getMaxProposalLimit() {
    initializeLimits();
    return *maxProposalLimit_;
}

Result<int> MsigsClient::getMaxApprovalLimit() {
    initializeLimits();
    return *maxApprovalLimit_;
}

Result<json> MsigsClient::get_proposal(const Name& proposer, const Name& proposalName,
                                       const GetProposalOptions& options) {
    json params = {{"proposer", proposer.toString()},
                   {"proposal_name", proposalName.toString()}};
    if (options.globalseq) {
        params["globalseq"] = *options.globalseq;
    }
    if (options.version_history) {
        params["version_history"] = *options.version_history;
    }
    return client_->call({.path = "/v1/proposals/get_proposal", .params = params});
}

Result<json> MsigsClient::get_proposal_history(const Name& proposer, const Name& proposalName,
                                               const GetProposalHistoryOptions& options) {
    json params = {{"proposer", proposer.toString()},
                   {"proposal_name", proposalName.toString()}};
    if (options.status) {
        params["status"] = *options.status;
    }
    if (options.limit) {
        initializeLimits();
        if (*options.limit > *maxProposalLimit_) {
            return err(ErrorKind::Invalid,
                       "Limit cannot exceed " + std::to_string(*maxProposalLimit_));
        }
        params["limit"] = *options.limit;
    }
    if (options.offset) {
        params["offset"] = *options.offset;
    }
    return client_->call({.path = "/v1/proposals/get_proposal_history", .params = params});
}

Result<json> MsigsClient::get_proposals(const Name& proposer, const GetProposalsOptions& options) {
    json params = {{"proposer", proposer.toString()}};
    if (options.status) {
        params["status"] = *options.status;
    }
    if (options.limit) {
        initializeLimits();
        if (*options.limit > *maxProposalLimit_) {
            return err(ErrorKind::Invalid,
                       "Limit cannot exceed " + std::to_string(*maxProposalLimit_));
        }
        params["limit"] = *options.limit;
    }
    if (options.offset) {
        params["offset"] = *options.offset;
    }
    return client_->call({.path = "/v1/proposals/get_proposals", .params = params});
}

Result<json> MsigsClient::get_approvals(const Name& proposer, const Name& proposalName,
                                        const GetApprovalsOptions& options) {
    json params = {{"proposer", proposer.toString()},
                   {"proposal_name", proposalName.toString()}};
    if (options.globalseq) {
        params["globalseq"] = *options.globalseq;
    }
    if (options.limit) {
        initializeLimits();
        if (*options.limit > *maxApprovalLimit_) {
            return err(ErrorKind::Invalid,
                       "Limit cannot exceed " + std::to_string(*maxApprovalLimit_));
        }
        params["limit"] = *options.limit;
    }
    if (options.offset) {
        params["offset"] = *options.offset;
    }
    return client_->call({.path = "/v1/proposals/get_approvals", .params = params});
}

Result<json> MsigsClient::get_activity(const Name& account, const GetActivityOptions& options) {
    json params = {{"account", account.toString()}};
    if (options.limit) {
        initializeLimits();
        if (*options.limit > *maxProposalLimit_) {
            return err(ErrorKind::Invalid,
                       "Limit cannot exceed " + std::to_string(*maxProposalLimit_));
        }
        params["limit"] = *options.limit;
    }
    if (options.offset) {
        params["offset"] = *options.offset;
    }
    if (options.action_type) {
        params["action_type"] = *options.action_type;
    }
    return client_->call({.path = "/v1/proposals/get_activity", .params = params});
}

Result<json> MsigsClient::get_approver_proposals(const Name& approver,
                                                 const GetApproverProposalsOptions& options) {
    json params = {{"approver", approver.toString()}};
    if (options.status) {
        params["status"] = *options.status;
    }
    if (options.include_approved) {
        params["include_approved"] = *options.include_approved;
    }
    if (options.limit) {
        initializeLimits();
        if (*options.limit > *maxProposalLimit_) {
            return err(ErrorKind::Invalid,
                       "Limit cannot exceed " + std::to_string(*maxProposalLimit_));
        }
        params["limit"] = *options.limit;
    }
    if (options.offset) {
        params["offset"] = *options.offset;
    }
    return client_->call({.path = "/v1/proposals/get_approver_proposals", .params = params});
}

Result<json> MsigsClient::search_proposals(const std::string& query,
                                           const SearchProposalsOptions& options) {
    json params = {{"query", query}};
    if (options.status) {
        params["status"] = *options.status;
    }
    if (options.limit) {
        initializeLimits();
        if (*options.limit > *maxProposalLimit_) {
            return err(ErrorKind::Invalid,
                       "Limit cannot exceed " + std::to_string(*maxProposalLimit_));
        }
        params["limit"] = *options.limit;
    }
    if (options.offset) {
        params["offset"] = *options.offset;
    }
    return client_->call({.path = "/v1/proposals/search_proposals", .params = params});
}

Result<json> MsigsClient::get_status() {
    return client_->call({.path = "/v1/proposals/get_status", .params = json::object()});
}

Result<json> MsigsClient::debug_proposal(const Name& proposer, const Name& proposalName,
                                         const DebugProposalOptions& options) {
    json params = {{"proposer", proposer.toString()},
                   {"proposal_name", proposalName.toString()}};
    if (options.globalseq) {
        params["globalseq"] = *options.globalseq;
    }
    return client_->call({.path = "/v1/proposals/debug_proposal", .params = params});
}

PaginationInfo getPaginationInfo(int offset, int limit, int total, bool more) {
    PaginationInfo rv;
    rv.currentPage = offset / limit + 1;
    rv.pageSize = limit;
    rv.totalResults = total;
    rv.totalPages = static_cast<int>(
        std::ceil(static_cast<double>(total) / static_cast<double>(limit)));
    rv.hasMore = more;
    rv.hasPrevious = offset > 0;
    if (rv.hasMore) {
        rv.nextOffset = offset + limit;
    }
    if (rv.hasPrevious) {
        rv.previousOffset = std::max(0, offset - limit);
    }
    return rv;
}

}  // namespace dwarfkit
