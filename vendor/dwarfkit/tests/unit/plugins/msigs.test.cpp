// Port of msigs test/api.ts (representative subset: the client configuration
// suite, one call per endpoint against the recorded fixtures, and the
// pagination helper; the remaining upstream cases repeat the same shapes).
#include <doctest/doctest.h>

#include <dwarfkit/msigs.hpp>

#include "../../util/mock_fetch_provider.hpp"

using namespace dwarfkit;
using namespace dwarfkit::test;

namespace {

MsigsClient makeMsigs(const MsigsClientOptions& options = {}) {
    return MsigsClient(makeClient(DK_FIXTURE_DIR "/msigs/data", "http://localhost"), options);
}

}  // namespace

TEST_SUITE("msigs") {
    TEST_CASE("get_status returns configured limits") {
        auto msigs = makeMsigs();
        const auto status = msigs.get_status().value();
        CHECK(status["max_proposal_results"] == 20);
        CHECK(status["max_approval_results"] == 100);
        CHECK(status.contains("last_account_action_seq"));
    }

    TEST_CASE("client fetches limits dynamically on first use") {
        auto msigs = makeMsigs();
        CHECK(msigs.getMaxProposalLimit().value() == 20);
    }

    TEST_CASE("custom maxProposalLimit can be configured") {
        auto msigs = makeMsigs({.maxProposalLimit = 50});
        CHECK(msigs.getMaxProposalLimit().value() == 50);
    }

    TEST_CASE("limit validation") {
        auto msigs = makeMsigs();
        SUBCASE("get_proposals") {
            const auto result = msigs.get_proposals(Name::from("alice"), {.limit = 50});
            REQUIRE_FALSE(result.has_value());
            CHECK(result.error().message.find("Limit cannot exceed 20") != std::string::npos);
        }
        SUBCASE("get_activity") {
            const auto result = msigs.get_activity(Name::from("alice"), {.limit = 50});
            REQUIRE_FALSE(result.has_value());
            CHECK(result.error().message.find("Limit cannot exceed 20") != std::string::npos);
        }
        SUBCASE("get_approver_proposals") {
            const auto result = msigs.get_approver_proposals(Name::from("bob"), {.limit = 50});
            REQUIRE_FALSE(result.has_value());
            CHECK(result.error().message.find("Limit cannot exceed 20") != std::string::npos);
        }
        SUBCASE("get_proposal_history") {
            const auto result = msigs.get_proposal_history(Name::from("alice"),
                                                           Name::from("upgrade"), {.limit = 50});
            REQUIRE_FALSE(result.has_value());
            CHECK(result.error().message.find("Limit cannot exceed 20") != std::string::npos);
        }
    }

    TEST_CASE("get_proposals (alice proposals)") {
        auto msigs = makeMsigs();
        const auto res = msigs.get_proposals(Name::from("alice")).value();
        REQUIRE(res["proposals"].is_array());
        CHECK(res.contains("total"));
        CHECK(res.contains("more"));
        for (const auto& p : res["proposals"]) {
            CHECK(p["proposer"] == "alice");
            CHECK(p.contains("proposal_name"));
            CHECK(p.contains("globalseq"));
            CHECK(p.contains("status"));
        }
    }

    TEST_CASE("get_proposals (filter by status)") {
        auto msigs = makeMsigs();
        const auto res =
            msigs.get_proposals(Name::from("alice"), {.status = "proposed", .limit = 10}).value();
        REQUIRE(res["proposals"].is_array());
        for (const auto& p : res["proposals"]) {
            CHECK(p["proposer"] == "alice");
            CHECK(p["status"] == "proposed");
        }
    }

    TEST_CASE("get_proposal (alice/upgrade)") {
        auto msigs = makeMsigs();
        const auto res = msigs.get_proposal(Name::from("alice"), Name::from("upgrade")).value();
        CHECK(res["proposer"] == "alice");
        CHECK(res["proposal_name"] == "upgrade");
        CHECK(res.contains("actions_count"));
    }

    TEST_CASE("getPaginationInfo") {
        const auto info = getPaginationInfo(20, 10, 45, true);
        CHECK(info.currentPage == 3);
        CHECK(info.pageSize == 10);
        CHECK(info.totalResults == 45);
        CHECK(info.totalPages == 5);
        CHECK(info.hasMore == true);
        CHECK(info.hasPrevious == true);
        CHECK(info.nextOffset == 30);
        CHECK(info.previousOffset == 10);
        const auto first = getPaginationInfo(0, 10, 45, true);
        CHECK(first.currentPage == 1);
        CHECK_FALSE(first.hasPrevious);
        CHECK_FALSE(first.previousOffset.has_value());
    }
}
