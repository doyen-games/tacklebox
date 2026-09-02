// Pure helpers behind Autopilot's dynamic amounts and programmable memos.
// No I/O: the controller fetches balances, these compute.
#pragma once

#include <string>

#include <dwarfkit/core/json.hpp>
#include <dwarfkit/core/result.hpp>

#include "vault/vault.hpp"

namespace tb::autopilot {

// floor(percent% of `balance`), optionally leaving `reserve` untouched.
//   balance  "12.3456 WAX" (fresh liquid balance)
//   percent  (0, 100]
//   reserve  "" or "1.0000 WAX" (must match the balance's symbol+precision)
// Exact integer basis-point math, floor rounding: never sends more than
// intended, never loses precision to floating point.
// Errors when the result is zero or inputs are malformed.
dwarfkit::Result<std::string> computePercentAmount(const std::string& balance, double percent,
                                                   const std::string& reserve);

// Placeholder substitution over every string value in `data` (recursive):
//   {actor} {amount} {balance} {date} {time}
// Unknown braces are left untouched.
struct TemplateContext {
    std::string actor;
    std::string amount;   // the computed (or fixed) quantity, may be empty
    std::string balance;  // pre-send balance string, may be empty
    int64_t unixSec = 0;  // stamp for {date}/{time}
};
dwarfkit::json applyTemplates(dwarfkit::json data, const TemplateContext& context);

// --- schedule timing ---------------------------------------------------------
// Local-time parsing for the schedule editor. Malformed text errors so the
// editor can refuse the save instead of silently misfiring.
dwarfkit::Result<int64_t> parseDateTimeLocal(const std::string& text);  // "YYYY-MM-DD HH:MM[:SS]"
dwarfkit::Result<int> parseTimeOfDay(const std::string& text);          // "HH:MM[:SS]"
std::string formatDateTimeLocal(int64_t unixSec);
std::string formatTimeOfDay(int secOfDay);

// The next moment this schedule should fire, strictly after `now`. Honors
// the timing mode, the startAt/endAt window and lastRunAt. Returns 0 when
// no future run is possible (past endAt) - the caller disables the schedule.
int64_t computeNextRun(const Schedule& schedule, int64_t now);

}  // namespace tb::autopilot
