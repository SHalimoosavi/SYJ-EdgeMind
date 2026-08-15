// Integration test for UsageManager: real filesystem persistence (a temp
// file), fully deterministic time via an injected fake clock — no real
// sleeping anywhere in this file.

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>

#include "usage/usage_manager.h"
#include "test_temp_dir.h"

using syj::edgemind::UsageManager;
using syj::edgemind::UsageOutcome;
using syj::edgemind::UsagePolicy;

namespace {
int g_failures = 0;

void check(bool condition, const char* description) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", description);
        ++g_failures;
    }
}

std::string temp_path(const char* suffix) {
    return syj::edgemind::test::writable_temp_dir() +
           "/syj_edgemind_test_usage_manager_" + suffix + ".txt";
}

void remove_if_exists(const std::string& path) {
    std::remove(path.c_str());
    std::remove((path + ".tmp").c_str());
}

// A simple controllable fake clock: starts at a fixed epoch and advances
// only when the test explicitly moves it forward.
class FakeClock {
public:
    explicit FakeClock(int64_t start) : now_(start) {}
    int64_t operator()() const { return now_; }
    void advance(int64_t seconds) { now_ += seconds; }

private:
    int64_t now_;
};
} // namespace

int main() {
    // Fresh state (no file yet): admission is allowed under any configured
    // limit, since nothing has been used.
    {
        const std::string path = temp_path("fresh");
        remove_if_exists(path);

        FakeClock clock(1'700'000'000);
        UsageManager manager(path, [&clock] { return clock(); });

        UsagePolicy policy;
        policy.daily_message_limit = 5;

        const auto decision = manager.check_admission(policy);
        check(decision.allowed, "fresh state with a message limit configured is allowed (nothing used yet)");
        check(decision.messages_remaining == 5, "fresh state reports the full limit as remaining");

        remove_if_exists(path);
    }

    // record_generation persists usage; a subsequent check_admission (new
    // UsageManager instance, same file — simulating a process restart)
    // reflects it.
    {
        const std::string path = temp_path("restart_persistence");
        remove_if_exists(path);

        FakeClock clock(1'700'000'000);
        UsagePolicy policy;
        policy.daily_message_limit = 3;
        policy.daily_token_limit = 100;

        {
            UsageManager manager(path, [&clock] { return clock(); });
            check(manager.check_admission(policy).allowed, "first check_admission is allowed");
            check(manager.record_generation(policy, /*tokens_generated=*/40), "first record_generation persists");
        }

        // New UsageManager instance — simulates the process restarting,
        // proving persistence actually survives across instances/restarts.
        {
            UsageManager manager(path, [&clock] { return clock(); });
            const auto decision = manager.check_admission(policy);
            check(decision.allowed, "usage after 1 message/40 tokens is still allowed");
            check(decision.messages_remaining == 2, "restart-loaded state correctly shows 2 messages remaining");
            check(decision.tokens_remaining == 60, "restart-loaded state correctly shows 60 tokens remaining");
        }

        remove_if_exists(path);
    }

    // Recording enough generations exhausts the message limit; the next
    // admission check denies.
    {
        const std::string path = temp_path("exhaustion");
        remove_if_exists(path);

        FakeClock clock(1'700'000'000);
        UsageManager manager(path, [&clock] { return clock(); });

        UsagePolicy policy;
        policy.daily_message_limit = 2;

        check(manager.check_admission(policy).allowed, "1st admission check allowed");
        check(manager.record_generation(policy, 10), "1st record_generation succeeds");
        check(manager.check_admission(policy).allowed, "2nd admission check still allowed (1/2 used)");
        check(manager.record_generation(policy, 10), "2nd record_generation succeeds");

        const auto decision = manager.check_admission(policy);
        check(!decision.allowed, "3rd admission check is denied (2/2 messages used)");
        check(decision.outcome == UsageOutcome::ExhaustedMessages, "denial outcome is ExhaustedMessages");

        remove_if_exists(path);
    }

    // Reset-period rollover: after the configured period elapses, usage
    // resets and admission is allowed again, entirely via the injected
    // fake clock — no real waiting.
    {
        const std::string path = temp_path("reset_period");
        remove_if_exists(path);

        FakeClock clock(1'700'000'000);
        UsageManager manager(path, [&clock] { return clock(); });

        UsagePolicy policy;
        policy.daily_message_limit = 1;
        policy.reset_period_seconds = 3600; // 1 hour for this test

        check(manager.check_admission(policy).allowed, "1st admission check allowed");
        check(manager.record_generation(policy, 5), "1st record_generation succeeds");
        check(!manager.check_admission(policy).allowed, "2nd admission check denied within the same period");

        clock.advance(3700); // past the 1-hour reset period

        const auto decision = manager.check_admission(policy);
        check(decision.allowed, "admission is allowed again after the reset period elapses");
        check(decision.messages_remaining == 1, "post-rollover remaining count is back to the full limit");

        remove_if_exists(path);
    }

    // Session time limit enforcement via session_start() + fake clock
    // advancement.
    {
        const std::string path = temp_path("session_time");
        remove_if_exists(path);

        FakeClock clock(1'700'000'000);
        UsageManager manager(path, [&clock] { return clock(); });

        UsagePolicy policy;
        policy.session_time_limit_seconds = 600; // 10 minutes

        check(manager.session_start(), "session_start persists successfully");
        check(manager.check_admission(policy).allowed, "admission allowed right after session start");

        clock.advance(700); // past the 10-minute session limit

        const auto decision = manager.check_admission(policy);
        check(!decision.allowed, "admission denied after the session time limit elapses");
        check(decision.outcome == UsageOutcome::ExhaustedTime, "denial outcome is ExhaustedTime");

        remove_if_exists(path);
    }

    // Corrupted state fails closed: check_admission, record_generation,
    // and session_start all refuse rather than silently proceeding.
    {
        const std::string path = temp_path("corrupted_fail_closed");
        remove_if_exists(path);
        {
            std::ofstream out(path);
            out << "not a valid usage state file\n";
        }

        FakeClock clock(1'700'000'000);
        UsageManager manager(path, [&clock] { return clock(); });
        UsagePolicy policy; // no limits configured at all — proves this isn't about the policy

        const auto decision = manager.check_admission(policy);
        check(!decision.allowed, "corrupted state denies admission even with NO limits configured");
        check(decision.outcome == UsageOutcome::StateCorrupted, "corrupted state outcome is StateCorrupted, distinct from exhaustion");
        check(decision.diagnostic.find("could not be read safely") != std::string::npos,
              "corrupted-state diagnostic explains the reason distinctly from an exhaustion message");

        check(!manager.session_start(), "session_start() also fails closed against corrupted state");
        check(!manager.record_generation(policy, 1), "record_generation() also fails closed against corrupted state");

        remove_if_exists(path);
    }

    if (g_failures > 0) {
        std::fprintf(stderr, "%d check(s) failed.\n", g_failures);
        return EXIT_FAILURE;
    }
    std::printf("test_usage_manager: all checks passed.\n");
    return EXIT_SUCCESS;
}
