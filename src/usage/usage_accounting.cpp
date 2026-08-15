#include "usage/usage_accounting.h"

#include <algorithm>
#include <sstream>

namespace syj::edgemind {

bool UsageAccounting::period_has_rolled_over(const UsagePolicy& policy, const UsageState& state, int64_t now) {
    if (state.period_start_unix <= 0) {
        return true; // fresh state: establish the first period
    }
    if (now < state.period_start_unix) {
        // Clock moved backwards relative to the persisted period start —
        // treat as needing a fresh period rather than computing a negative
        // elapsed time. This does mean a clock rollback can grant a fresh
        // period early; the alternative (refusing to roll over) risks
        // getting permanently stuck if period_start_unix is ever ahead of
        // the real clock, which is the worse failure mode for a local,
        // non-adversarial usage guard.
        return true;
    }
    const int64_t elapsed = now - state.period_start_unix;
    return elapsed >= policy.reset_period_seconds;
}

UsageState UsageAccounting::roll_over_period(const UsageState& state, int64_t now) {
    UsageState next = state;
    next.period_start_unix = now;
    next.messages_used_this_period = 0;
    next.tokens_used_this_period = 0;
    // session_start_unix is deliberately untouched — session tracking is
    // independent of the daily-period rollover.
    return next;
}

UsageDecision UsageAccounting::evaluate(const UsagePolicy& policy, const UsageState& state, int64_t now) {
    UsageDecision decision;
    decision.allowed = true;
    decision.outcome = UsageOutcome::Allowed;

    // Session time.
    if (policy.session_time_limit_seconds > 0 && state.session_start_unix > 0 && now >= state.session_start_unix) {
        const int64_t elapsed = now - state.session_start_unix;
        decision.seconds_remaining = std::max<int64_t>(0, policy.session_time_limit_seconds - elapsed);
        if (elapsed >= policy.session_time_limit_seconds) {
            decision.allowed = false;
            decision.outcome = UsageOutcome::ExhaustedTime;
        }
    } else if (policy.session_time_limit_seconds > 0) {
        // Limit configured but no session started yet (or now < start,
        // handled defensively): report the full limit as remaining.
        decision.seconds_remaining = policy.session_time_limit_seconds;
    }

    // Messages.
    if (policy.daily_message_limit > 0) {
        decision.messages_remaining = std::max<int64_t>(0, policy.daily_message_limit - state.messages_used_this_period);
        if (state.messages_used_this_period >= policy.daily_message_limit && decision.allowed) {
            decision.allowed = false;
            decision.outcome = UsageOutcome::ExhaustedMessages;
        }
    }

    // Tokens.
    if (policy.daily_token_limit > 0) {
        decision.tokens_remaining = std::max<int64_t>(0, policy.daily_token_limit - state.tokens_used_this_period);
        if (state.tokens_used_this_period >= policy.daily_token_limit && decision.allowed) {
            decision.allowed = false;
            decision.outcome = UsageOutcome::ExhaustedTokens;
        }
    }

    decision.period_reset_unix =
        (state.period_start_unix > 0) ? (state.period_start_unix + policy.reset_period_seconds) : (now + policy.reset_period_seconds);

    decision.diagnostic = format_diagnostic(decision, policy);
    return decision;
}

std::string UsageAccounting::format_diagnostic(const UsageDecision& decision, const UsagePolicy& policy) {
    std::ostringstream oss;

    oss << "Session time remaining: ";
    if (policy.session_time_limit_seconds > 0) {
        oss << decision.seconds_remaining << "s (limit: " << policy.session_time_limit_seconds << "s)\n";
    } else {
        oss << "unlimited\n";
    }

    oss << "Messages remaining:     ";
    if (policy.daily_message_limit > 0) {
        oss << decision.messages_remaining << " (limit: " << policy.daily_message_limit << ")\n";
    } else {
        oss << "unlimited\n";
    }

    oss << "Tokens remaining:       ";
    if (policy.daily_token_limit > 0) {
        oss << decision.tokens_remaining << " (limit: " << policy.daily_token_limit << ")\n";
    } else {
        oss << "unlimited\n";
    }

    oss << "Quota resets at (unix): " << decision.period_reset_unix << "\n\n";

    switch (decision.outcome) {
        case UsageOutcome::Allowed:
            oss << "STATUS: ALLOWED\n";
            break;
        case UsageOutcome::ExhaustedTime:
            oss << "STATUS: DENIED\n";
            oss << "Reason: session time limit reached.\n";
            break;
        case UsageOutcome::ExhaustedMessages:
            oss << "STATUS: DENIED\n";
            oss << "Reason: daily message limit reached.\n";
            break;
        case UsageOutcome::ExhaustedTokens:
            oss << "STATUS: DENIED\n";
            oss << "Reason: daily token limit reached.\n";
            break;
        case UsageOutcome::StateCorrupted:
            // evaluate() never sets this outcome itself (see header comment
            // — that's the caller's responsibility before reaching here),
            // but format_diagnostic is also reused by UsageManager for the
            // corrupted-state path, so this case is handled for completeness.
            oss << "STATUS: DENIED\n";
            oss << "Reason: usage state could not be read safely (see diagnostic detail).\n";
            break;
    }

    return oss.str();
}

} // namespace syj::edgemind
