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

    if (g_failures > 0) {
        std::fprintf(stderr, "%d check(s) failed.\n", g_failures);
        return EXIT_FAILURE;
    }
    std::printf("test_config: all checks passed.\n");
    return EXIT_SUCCESS;
}
