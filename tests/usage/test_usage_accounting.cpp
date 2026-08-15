#include <cstdio>
#include <cstdlib>

#include "usage/usage_accounting.h"

using syj::edgemind::UsageAccounting;
using syj::edgemind::UsageDecision;
using syj::edgemind::UsageOutcome;
using syj::edgemind::UsagePolicy;
using syj::edgemind::UsageState;

namespace {
int g_failures = 0;

void check(bool condition, const char* description) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", description);
        ++g_failures;
    }
}
} // namespace

int main() {
    // Fresh state (period_start_unix == 0) always needs a rollover.
    {
        UsagePolicy policy;
        UsageState state; // all zero
        check(UsageAccounting::period_has_rolled_over(policy, state, /*now=*/1000),
              "fresh (all-zero) state always needs a rollover to establish the first period");
    }

    // A period well within its window does not need a rollover.
    {
        UsagePolicy policy;
        policy.reset_period_seconds = 86400;
        UsageState state;
        state.period_start_unix = 1000;
        check(!UsageAccounting::period_has_rolled_over(policy, state, /*now=*/1000 + 3600),
              "a period 1 hour into an 86400s window has not rolled over");
        check(UsageAccounting::period_has_rolled_over(policy, state, /*now=*/1000 + 90000),
              "a period past its 86400s window has rolled over");
    }

    // Clock-rollback safety: now < period_start_unix must not compute a
    // negative elapsed time — must be treated as needing a rollover.
    {
        UsagePolicy policy;
        UsageState state;
        state.period_start_unix = 100000;
        check(UsageAccounting::period_has_rolled_over(policy, state, /*now=*/50000),
              "a clock that appears to have moved backwards is treated as needing a rollover, not a negative-elapsed crash");
    }

    // roll_over_period resets counters and period_start, leaves session
    // tracking untouched.
    {
        UsageState state;
        state.period_start_unix = 1000;
        state.messages_used_this_period = 5;
        state.tokens_used_this_period = 500;
        state.session_start_unix = 999;

        const UsageState rolled = UsageAccounting::roll_over_period(state, /*now=*/2000);
        check(rolled.period_start_unix == 2000, "roll_over_period sets period_start_unix to now");
        check(rolled.messages_used_this_period == 0, "roll_over_period resets message counter");
        check(rolled.tokens_used_this_period == 0, "roll_over_period resets token counter");
        check(rolled.session_start_unix == 999, "roll_over_period does not touch session_start_unix");
    }

    // No limits configured (all zero/disabled): always allowed.
    {
        UsagePolicy policy; // everything 0 except reset_period_seconds default
        UsageState state;
        state.period_start_unix = 1;
        const UsageDecision d = UsageAccounting::evaluate(policy, state, /*now=*/100);
        check(d.allowed, "no limits configured means always allowed");
        check(d.outcome == UsageOutcome::Allowed, "outcome is Allowed when no limits are configured");
    }

    // Message limit: under, at, and over.
    {
        UsagePolicy policy;
        policy.daily_message_limit = 10;
        UsageState state;
        state.period_start_unix = 1;

        state.messages_used_this_period = 5;
        auto d = UsageAccounting::evaluate(policy, state, 100);
        check(d.allowed, "5/10 messages used is still allowed");
        check(d.messages_remaining == 5, "5/10 messages used leaves 5 remaining");

        state.messages_used_this_period = 10;
        d = UsageAccounting::evaluate(policy, state, 100);
        check(!d.allowed, "10/10 messages used is exhausted");
        check(d.outcome == UsageOutcome::ExhaustedMessages, "exhausted message outcome is ExhaustedMessages");
        check(d.messages_remaining == 0, "exhausted message limit leaves 0 remaining, not negative");

        state.messages_used_this_period = 15; // somehow over — must not go negative
        d = UsageAccounting::evaluate(policy, state, 100);
        check(!d.allowed, "over the message limit is still denied");
        check(d.messages_remaining == 0, "remaining never goes negative even if usage exceeds the limit");
    }

    // Token limit: under, at, and over.
    {
        UsagePolicy policy;
        policy.daily_token_limit = 1000;
        UsageState state;
        state.period_start_unix = 1;

        state.tokens_used_this_period = 999;
        auto d = UsageAccounting::evaluate(policy, state, 100);
        check(d.allowed, "999/1000 tokens used is still allowed");
        check(d.tokens_remaining == 1, "999/1000 tokens used leaves 1 remaining");

        state.tokens_used_this_period = 1000;
        d = UsageAccounting::evaluate(policy, state, 100);
        check(!d.allowed, "1000/1000 tokens used is exhausted");
        check(d.outcome == UsageOutcome::ExhaustedTokens, "exhausted token outcome is ExhaustedTokens");
    }

    // Session time limit: under and at/over.
    {
        UsagePolicy policy;
        policy.session_time_limit_seconds = 3600;
        UsageState state;
        state.session_start_unix = 1000;

        auto d = UsageAccounting::evaluate(policy, state, /*now=*/1000 + 1800); // 30 min in
        check(d.allowed, "30 minutes into a 60-minute session limit is still allowed");
        check(d.seconds_remaining == 1800, "30 minutes into a 60-minute limit leaves 1800s remaining");

        d = UsageAccounting::evaluate(policy, state, /*now=*/1000 + 3600); // exactly at the limit
        check(!d.allowed, "exactly at the session time limit is exhausted (inclusive boundary)");
        check(d.outcome == UsageOutcome::ExhaustedTime, "exhausted time outcome is ExhaustedTime");

        d = UsageAccounting::evaluate(policy, state, /*now=*/1000 + 7200); // well past
        check(!d.allowed, "well past the session time limit is exhausted");
        check(d.seconds_remaining == 0, "seconds_remaining never goes negative");
    }

    // Session limit configured but no session started yet: report full
    // limit as remaining, don't deny (there's nothing to be exhausted yet).
    {
        UsagePolicy policy;
        policy.session_time_limit_seconds = 3600;
        UsageState state; // session_start_unix == 0
        const UsageDecision d = UsageAccounting::evaluate(policy, state, 100);
        check(d.allowed, "a session-time limit with no session started yet is allowed");
        check(d.seconds_remaining == 3600, "no session started yet reports the full limit as remaining");
    }

    // Multiple simultaneous limits: the decision must reflect ANY exhausted
    // dimension, and diagnostics/remaining values for OTHER dimensions must
    // still be computed correctly even when one dimension is what denies.
    {
        UsagePolicy policy;
        policy.daily_message_limit = 10;
        policy.daily_token_limit = 1000;
        UsageState state;
        state.period_start_unix = 1;
        state.messages_used_this_period = 3;      // fine
        state.tokens_used_this_period = 1000;     // exhausted

        const UsageDecision d = UsageAccounting::evaluate(policy, state, 100);
        check(!d.allowed, "token exhaustion denies even though messages are fine");
        check(d.outcome == UsageOutcome::ExhaustedTokens, "outcome correctly identifies tokens as the exhausted dimension");
        check(d.messages_remaining == 7, "messages_remaining is still correctly computed even when tokens is what denies");
    }

    // Diagnostic text sanity: always non-empty, contains STATUS:.
    {
        UsagePolicy policy;
        UsageState state;
        const UsageDecision d = UsageAccounting::evaluate(policy, state, 100);
        check(!d.diagnostic.empty(), "diagnostic is always populated");
        check(d.diagnostic.find("STATUS:") != std::string::npos, "diagnostic always contains a STATUS: line");
    }

    if (g_failures > 0) {
        std::fprintf(stderr, "%d check(s) failed.\n", g_failures);
        return EXIT_FAILURE;
    }
    std::printf("test_usage_accounting: all checks passed.\n");
    return EXIT_SUCCESS;
}
