#include "usage/usage_manager.h"

#include <algorithm>
#include <ctime>

#include "usage/usage_accounting.h"
#include "usage/usage_state_store.h"

namespace syj::edgemind {

namespace {
UsageDecision corrupted_decision(const std::string& state_path) {
    UsageDecision decision;
    decision.allowed = false;
    decision.outcome = UsageOutcome::StateCorrupted;
    decision.diagnostic =
        "STATUS: DENIED\n"
        "Reason: usage state file could not be read safely (missing fields, unrecognized\n"
        "version, or out-of-range values).\n\n"
        "Path: " + state_path + "\n\n"
        "This is treated as a fail-closed condition, distinct from a legitimately\n"
        "exhausted quota: SYJ EdgeMind will not silently grant unlimited usage when\n"
        "its own local state cannot be trusted. If you know this state file is safe\n"
        "to discard (e.g. after a manual edit gone wrong), remove it to start fresh.\n";
    return decision;
}
} // namespace

UsageManager::UsageManager(std::string state_path, ClockFn clock)
    : state_path_(std::move(state_path)), clock_(std::move(clock)) {}

int64_t UsageManager::now() const {
    if (clock_) {
        return clock_();
    }
    return static_cast<int64_t>(std::time(nullptr));
}

UsageDecision UsageManager::check_admission(const UsagePolicy& policy) {
    UsageState state;
    const UsageStateLoadResult result = UsageStateStore::load(state_path_, &state);

    if (result == UsageStateLoadResult::Corrupted) {
        return corrupted_decision(state_path_);
    }
    // result == NotFound leaves `state` as a fresh, zeroed UsageState — a
    // fresh install is not a corruption event, it just means "no usage yet".

    const int64_t current_time = now();

    if (UsageAccounting::period_has_rolled_over(policy, state, current_time)) {
        state = UsageAccounting::roll_over_period(state, current_time);
        // Best-effort persist of the rollover. If this write fails (I/O
        // error), we still proceed with the in-memory rolled-over state for
        // THIS decision — refusing usage entirely because of a transient
        // save failure on a rollover (as opposed to on an actual
        // exhaustion or corruption) would be a worse failure mode for a
        // local single-user tool. The next successful save call catches
        // the persisted state up.
        UsageStateStore::save(state_path_, state);
    }

    return UsageAccounting::evaluate(policy, state, current_time);
}

bool UsageManager::session_start() {
    UsageState state;
    const UsageStateLoadResult result = UsageStateStore::load(state_path_, &state);
    if (result == UsageStateLoadResult::Corrupted) {
        return false; // fail closed: do not overwrite corrupted state implicitly
    }

    state.session_start_unix = now();
    return UsageStateStore::save(state_path_, state);
}

bool UsageManager::record_generation(const UsagePolicy& policy, int64_t tokens_generated) {
    if (tokens_generated < 0) {
        return false; // never record a negative usage amount
    }

    UsageState state;
    const UsageStateLoadResult result = UsageStateStore::load(state_path_, &state);
    if (result == UsageStateLoadResult::Corrupted) {
        return false; // fail closed: cannot safely account against corrupted state
    }

    const int64_t current_time = now();
    if (UsageAccounting::period_has_rolled_over(policy, state, current_time)) {
        state = UsageAccounting::roll_over_period(state, current_time);
    }

    // Overflow guard: never let a counter wrap. Saturate rather than wrap,
    // consistent with MemoryEstimate::total_bytes()'s saturating-add
    // convention — a saturated (very large) counter reads as "exhausted"
    // to any sane policy, which is the safe failure direction.
    if (state.messages_used_this_period < UsageState::SYJ_EDGEMIND_MAX_SANE_COUNTER) {
        state.messages_used_this_period += 1;
    }
    const int64_t remaining_headroom = UsageState::SYJ_EDGEMIND_MAX_SANE_COUNTER - state.tokens_used_this_period;
    if (remaining_headroom > 0) {
        state.tokens_used_this_period += std::min<int64_t>(tokens_generated, remaining_headroom);
    }

    return UsageStateStore::save(state_path_, state);
}

std::string UsageManager::usage_report(const UsagePolicy& policy) {
    UsageState state;
    const UsageStateLoadResult result = UsageStateStore::load(state_path_, &state);
    if (result == UsageStateLoadResult::Corrupted) {
        return corrupted_decision(state_path_).diagnostic;
    }

    const int64_t current_time = now();
    if (UsageAccounting::period_has_rolled_over(policy, state, current_time)) {
        state = UsageAccounting::roll_over_period(state, current_time);
        // Report-only call: deliberately does NOT persist the rollover —
        // /usage should never have a mutating side effect a user wouldn't
        // expect from a status query.
    }

    return UsageAccounting::evaluate(policy, state, current_time).diagnostic;
}

} // namespace syj::edgemind
