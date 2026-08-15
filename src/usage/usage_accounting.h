#ifndef SYJ_EDGEMIND_USAGE_USAGE_ACCOUNTING_H
#define SYJ_EDGEMIND_USAGE_USAGE_ACCOUNTING_H

#include <cstdint>

#include "usage/usage_types.h"

namespace syj::edgemind {

// Pure quota logic — no I/O, no OS clock calls, no filesystem access.
// Mirrors the design of src/memory/memory_estimator.h and memory_budget.h:
// kept independently unit-testable (tests/usage/test_usage_accounting.cpp)
// without needing real time to pass or a real state file to exist.
//
// "now" is always passed in explicitly (unix seconds) rather than read
// internally — this is what makes tests deterministic; the one caller that
// reads the real clock is UsageManager (usage_manager.cpp), which is the
// bridge layer, matching MemoryObserver's role for the memory subsystem.
class UsageAccounting {
public:
    // Returns true if `state`'s period_start_unix is stale relative to
    // `now` given `policy.reset_period_seconds` — i.e. the daily counters
    // should be rolled over before this check/accounting proceeds.
    // A period_start_unix of 0 (fresh state) always counts as needing a
    // roll-over (to establish the first period).
    static bool period_has_rolled_over(const UsagePolicy& policy, const UsageState& state, int64_t now);

    // Returns a copy of `state` with the period counters reset and
    // period_start_unix set to `now`. Does not touch session_start_unix —
    // session and daily-period tracking are independent axes.
    static UsageState roll_over_period(const UsageState& state, int64_t now);

    // The core admission decision. Assumes `state` has already been
    // validated (see UsageStateStore) and rolled over if needed (see
    // period_has_rolled_over/roll_over_period) — this function does not
    // roll over on its own, to keep "decide" and "mutate" separate and
    // explicit, matching MemoryBudgetPolicy's pure-evaluation style.
    //
    // Fail-closed is the CALLER's responsibility here: if state validity
    // is in question, the caller must not reach this function at all and
    // must instead produce a UsageDecision with outcome = StateCorrupted
    // directly (see UsageManager). This function assumes valid input and
    // answers only "does valid state satisfy valid policy".
    static UsageDecision evaluate(const UsagePolicy& policy, const UsageState& state, int64_t now);

private:
    static std::string format_diagnostic(const UsageDecision& decision, const UsagePolicy& policy);
};

} // namespace syj::edgemind

#endif // SYJ_EDGEMIND_USAGE_USAGE_ACCOUNTING_H
