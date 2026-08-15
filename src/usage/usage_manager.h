#ifndef SYJ_EDGEMIND_USAGE_USAGE_MANAGER_H
#define SYJ_EDGEMIND_USAGE_USAGE_MANAGER_H

#include <functional>
#include <string>

#include "usage/usage_types.h"

namespace syj::edgemind {

// Injectable clock, matching UsageAccounting's "now is always passed in"
// convention one layer up — UsageManager is the ONE place that defaults to
// reading the real wall clock (std::time), but tests construct it with a
// fake clock instead, so no test ever needs to sleep for real.
using ClockFn = std::function<int64_t()>;

// Coordinates UsageStateStore (persistence) and UsageAccounting (pure
// policy logic) behind the single interface Runtime actually calls. This is
// the "UsageManager" the v0.3.0 usage-limit requirement names, playing the
// same role InferenceEngine plays for the memory subsystem: it owns the
// pipeline, the pure components underneath it don't know about each other.
class UsageManager {
public:
    // `state_path` is the local file UsageStateStore reads/writes.
    // `clock` defaults to the real wall clock; tests pass a fake one.
    explicit UsageManager(std::string state_path, ClockFn clock = nullptr);

    // Loads persisted state (or starts fresh if none exists), rolls the
    // period over if it has expired, and evaluates `policy` against the
    // result — WITHOUT recording any new usage. This is the admission
    // check: call before allowing a generate() to proceed.
    //
    // FAIL-CLOSED: if the persisted state file exists but is corrupted,
    // this returns UsageDecision{allowed=false, outcome=StateCorrupted,
    // diagnostic="..."} — corrupted state is NEVER treated as "unlimited
    // access", and is diagnostically distinct from legitimate exhaustion
    // (see UsageOutcome).
    UsageDecision check_admission(const UsagePolicy& policy);

    // Marks the start of a new session (resets the session-time clock).
    // Does not touch the daily period counters. Persists immediately.
    // Returns false if persistence failed (I/O error) — callers should
    // treat that as equivalent to StateCorrupted for admission purposes on
    // the next check, since the write may not have gone through.
    bool session_start();

    // Records one generated message with `tokens_generated` tokens against
    // the current period, and persists immediately. Call this AFTER a
    // successful generation, not before — accounting reflects what
    // actually happened, matching the pipeline's documented ordering
    // (admission before inference, accounting after). `policy` is needed
    // here (not just at check_admission time) so a period that expired
    // between the admission check and this call is rolled over correctly
    // rather than incorrectly adding to a stale period's counters.
    //
    // Returns false if persistence failed.
    bool record_generation(const UsagePolicy& policy, int64_t tokens_generated);

    // Human-readable status for the current state/policy pair, for a CLI
    // /usage command — does not mutate anything.
    std::string usage_report(const UsagePolicy& policy);

private:
    int64_t now() const;

    std::string state_path_;
    ClockFn clock_;
};

} // namespace syj::edgemind

#endif // SYJ_EDGEMIND_USAGE_USAGE_MANAGER_H
