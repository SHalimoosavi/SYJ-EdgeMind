// Unit tests for syj::edgemind::validate_config.
// No test framework: each check prints on failure and the process exits
// non-zero if any check failed, so CTest reports pass/fail correctly.

#include <cstdio>
#include <cstdlib>
#include <string>

#include "core/config.h"

using syj::edgemind::RuntimeConfig;
using syj::edgemind::validate_config;

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
    // A fully valid, default-ish config (aside from model_path) should pass.
    {
        RuntimeConfig cfg;
        cfg.model_path = "model.gguf";
        check(validate_config(cfg).empty(), "valid config should have no validation error");
    }

    // Empty model path is rejected.
    {
        RuntimeConfig cfg;
        cfg.model_path = "";
        check(!validate_config(cfg).empty(), "empty model_path should be rejected");
    }

    // Context size below the sanity minimum is rejected.
    {
        RuntimeConfig cfg;
        cfg.model_path = "model.gguf";
        cfg.context_size = 1;
        check(!validate_config(cfg).empty(), "context_size below minimum should be rejected");
    }

    // Context size above the Phase 1 sanity ceiling is rejected (this is
    // NOT the Phase 2 memory-budget check — just the "prevent unbounded
    // context allocation" guard required in Phase 1).
    {
        RuntimeConfig cfg;
        cfg.model_path = "model.gguf";
        cfg.context_size = 1'000'000;
        check(!validate_config(cfg).empty(), "absurd context_size should be rejected");
    }

    // Non-positive thread count is rejected.
    {
        RuntimeConfig cfg;
        cfg.model_path = "model.gguf";
        cfg.threads = 0;
        check(!validate_config(cfg).empty(), "threads == 0 should be rejected");
    }

    // top_p out of (0, 1] is rejected.
    {
        RuntimeConfig cfg;
        cfg.model_path = "model.gguf";
        cfg.top_p = 1.5f;
        check(!validate_config(cfg).empty(), "top_p > 1.0 should be rejected");
    }
    {
        RuntimeConfig cfg;
        cfg.model_path = "model.gguf";
        cfg.top_p = 0.0f;
        check(!validate_config(cfg).empty(), "top_p == 0.0 should be rejected");
    }

    // Non-positive max_tokens is rejected.
    {
        RuntimeConfig cfg;
        cfg.model_path = "model.gguf";
        cfg.max_tokens = 0;
        check(!validate_config(cfg).empty(), "max_tokens == 0 should be rejected");
    }

    // memory_budget_mb out of the supported sane range is rejected.
    {
        RuntimeConfig cfg;
        cfg.model_path = "model.gguf";
        cfg.memory_budget_mb = 1; // below SYJ_EDGEMIND_MIN_MEMORY_BUDGET_MB
        check(!validate_config(cfg).empty(), "memory_budget_mb below the minimum should be rejected");
    }
    {
        RuntimeConfig cfg;
        cfg.model_path = "model.gguf";
        cfg.memory_budget_mb = 999'999'999; // absurdly large
        check(!validate_config(cfg).empty(), "memory_budget_mb above the maximum should be rejected");
    }

    // safety_reserve_mb must be non-negative.
    {
        RuntimeConfig cfg;
        cfg.model_path = "model.gguf";
        cfg.safety_reserve_mb = -1;
        check(!validate_config(cfg).empty(), "negative safety_reserve_mb should be rejected");
    }

    // safety_reserve_mb must be strictly less than memory_budget_mb —
    // reserve == budget and reserve > budget are both rejected at the
    // config layer (MemoryBudgetPolicy defends against this reaching it
    // anyway, but the config validation is the first line of defense).
    {
        RuntimeConfig cfg;
        cfg.model_path = "model.gguf";
        cfg.memory_budget_mb = 3000;
        cfg.safety_reserve_mb = 3000; // equal
        check(!validate_config(cfg).empty(), "safety_reserve_mb == memory_budget_mb should be rejected");
    }
    {
        RuntimeConfig cfg;
        cfg.model_path = "model.gguf";
        cfg.memory_budget_mb = 3000;
        cfg.safety_reserve_mb = 4000; // greater
        check(!validate_config(cfg).empty(), "safety_reserve_mb > memory_budget_mb should be rejected");
    }

    // A valid, non-default memory-budget configuration passes.
    {
        RuntimeConfig cfg;
        cfg.model_path = "model.gguf";
        cfg.memory_budget_mb = 2048;
        cfg.safety_reserve_mb = 256;
        check(validate_config(cfg).empty(), "a sane, valid memory-budget configuration should pass");
    }

    // v0.3.0 usage-limit validation.
    {
        RuntimeConfig cfg;
        cfg.model_path = "model.gguf";
        cfg.session_time_limit_seconds = -1;
        check(!validate_config(cfg).empty(), "negative session_time_limit_seconds should be rejected");
    }
    {
        RuntimeConfig cfg;
        cfg.model_path = "model.gguf";
        cfg.daily_message_limit = -1;
        check(!validate_config(cfg).empty(), "negative daily_message_limit should be rejected");
    }
    {
        RuntimeConfig cfg;
        cfg.model_path = "model.gguf";
        cfg.daily_token_limit = -1;
        check(!validate_config(cfg).empty(), "negative daily_token_limit should be rejected");
    }
    {
        RuntimeConfig cfg;
        cfg.model_path = "model.gguf";
        cfg.reset_period_seconds = 10; // below the 60s minimum
        check(!validate_config(cfg).empty(), "reset_period_seconds below the minimum should be rejected");
    }
    {
        RuntimeConfig cfg;
        cfg.model_path = "model.gguf";
        cfg.usage_state_path = "";
        check(!validate_config(cfg).empty(), "empty usage_state_path should be rejected");
    }
    {
        // 0 (disabled) for every usage limit is the default and must be valid.
        RuntimeConfig cfg;
        cfg.model_path = "model.gguf";
        check(validate_config(cfg).empty(), "default (all usage limits disabled) configuration should pass");
    }
    {
        RuntimeConfig cfg;
        cfg.model_path = "model.gguf";
        cfg.session_time_limit_seconds = 3600;
        cfg.daily_message_limit = 50;
        cfg.daily_token_limit = 10000;
        cfg.reset_period_seconds = 3600;
        check(validate_config(cfg).empty(), "a sane, fully-configured usage-limit configuration should pass");
    }

    if (g_failures > 0) {
        std::fprintf(stderr, "%d check(s) failed.\n", g_failures);
        return EXIT_FAILURE;
    }
    std::printf("test_config: all checks passed.\n");
    return EXIT_SUCCESS;
}
