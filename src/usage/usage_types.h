#ifndef SYJ_EDGEMIND_USAGE_USAGE_TYPES_H
#define SYJ_EDGEMIND_USAGE_USAGE_TYPES_H

#include <cstdint>
#include <string>

namespace syj::edgemind {

// Configured limits. A limit of 0 means "disabled" (no cap on that
// dimension) — this is deliberately NOT a commercial licensing system;
// it's a local, user-configured usage guard. Every field here is meant to
// be settable via CLI flags or the C API config struct.
struct UsagePolicy {
    // 0 = disabled. Session time is measured from UsageManager::session_start()
    // to "now" at each admission check — it does not require polling.
    int64_t session_time_limit_seconds = 0;

    // 0 = disabled. Counts individual generate() calls within the current
    // reset period.
    int64_t daily_message_limit = 0;

    // 0 = disabled. Counts tokens actually generated (streamed pieces),
    // not prompt tokens, within the current reset period.
    int64_t daily_token_limit = 0;

    // How often the daily_* counters roll over, in seconds. Default: 86400
    // (24h). Exposed as a policy field (not hard-coded) so a future
    // subscription tier could use a different window without a redesign —
    // per the prompt's "future extensibility for licensing/subscription
    // tiers" requirement, without actually implementing licensing now.
    int64_t reset_period_seconds = 86400;

    static constexpr int64_t SYJ_EDGEMIND_MAX_SANE_LIMIT = 1'000'000'000; // guards against absurd config values
    static constexpr int64_t SYJ_EDGEMIND_MIN_RESET_PERIOD_SECONDS = 60;  // a reset period under a minute is not meaningful
};

// Persisted, mutable, untrusted state — this struct is what
// UsageStateStore reads from and writes to disk. Every field here must be
// treated as attacker/corruption-controlled input on load: validated,
// range-checked, never assumed benign. See usage_state_store.cpp.
struct UsageState {
    // Bumped whenever the on-disk format changes. A file with an
    // unrecognized version is treated as corrupt (fail closed), not
    // silently reinterpreted.
    static constexpr int32_t SYJ_EDGEMIND_USAGE_STATE_VERSION = 1;
    int32_t version = SYJ_EDGEMIND_USAGE_STATE_VERSION;

    // Unix seconds. 0 means "no period established yet" (fresh state).
    int64_t period_start_unix = 0;

    int64_t messages_used_this_period = 0;
    int64_t tokens_used_this_period = 0;

    // Unix seconds of the most recent session_start() call. 0 = no active
    // session. Session time is NOT persisted as an accumulated duration —
    // it's derived from (now - session_start_unix) at admission-check time,
    // which is simpler and can't drift from missed accounting calls.
    int64_t session_start_unix = 0;

    static constexpr int64_t SYJ_EDGEMIND_MAX_SANE_TIMESTAMP = 4102444800; // year 2100, generous sanity ceiling
    static constexpr int64_t SYJ_EDGEMIND_MAX_SANE_COUNTER = 1'000'000'000'000'000LL; // 1e15, generic corruption/overflow guard
};

enum class UsageOutcome {
    Allowed,
    ExhaustedTime,
    ExhaustedMessages,
    ExhaustedTokens,
    StateCorrupted, // fail-closed: distinct from legitimate exhaustion, see usage_manager.h
};

// The result of a single admission check — mirrors MemoryDecision's shape
// (always populated, always has a human-readable diagnostic) so the two
// subsystems read consistently to a caller.
struct UsageDecision {
    bool allowed = false;
    UsageOutcome outcome = UsageOutcome::StateCorrupted;

    int64_t seconds_remaining = 0;  // meaningful only if session_time_limit_seconds > 0
    int64_t messages_remaining = 0; // meaningful only if daily_message_limit > 0
    int64_t tokens_remaining = 0;   // meaningful only if daily_token_limit > 0
    int64_t period_reset_unix = 0;  // when the current reset period ends

    std::string diagnostic;
};

} // namespace syj::edgemind

#endif // SYJ_EDGEMIND_USAGE_USAGE_TYPES_H
