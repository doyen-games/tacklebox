// Port of atomicassets src/endpoints/types.ts state enums. The typed response
// Structs become json responses (see DIVERGENCES.md).
#pragma once

#include <dwarfkit/antelope.hpp>

namespace dwarfkit::atomic {

enum class OfferState : int {
    PENDING = 0,
    INVALID = 1,
    UNKNOWN = 2,
    ACCEPTED = 3,
    DECLINED = 4,
    CANCELLED = 5,
};

enum class LinkState : int {
    WAITING = 0,
    CREATED = 1,
    CANCELED = 2,
    CLAIMED = 3,
};

enum class AuctionState : int {
    WAITING = 0,
    LISTED = 1,
    CANCELED = 2,
    SOLD = 3,
    INVALID = 4,
};

enum class SaleState : int {
    WAITING = 0,
    LISTED = 1,
    CANCELED = 2,
    SOLD = 3,
    INVALID = 4,
};

enum class BuyofferState : int {
    PENDING = 0,
    DECLINED = 1,
    CANCELED = 2,
    ACCEPTED = 3,
    INVALID = 4,
};

enum class TemplateBuyofferState : int {
    LISTED = 0,
    CANCELED = 1,
    SOLD = 2,
    INVALID = 3,
};

}  // namespace dwarfkit::atomic
