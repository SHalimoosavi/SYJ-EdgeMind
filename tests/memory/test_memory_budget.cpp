#include <cstdio>
#include <cstdlib>

#include "memory/memory_budget.h"

using syj::edgemind::MemoryBudgetPolicy;
using syj::edgemind::MemoryEstimate;
using syj::edgemind::MemoryDecision;

namespace {
int g_failures = 0;

void check(bool condition, const char* description) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", description);
        ++g_failures;
    }
}

uint64_t mb(uint64_t n) { return n * 1024ULL * 1024ULL; }
} // namespace

int main() {
    // Comfortably-under-budget configuration is accepted.
    {
        MemoryEstimate est;
        est.weights_bytes = mb(1800);
        est.weights_bytes_is_observed = true;
        est.kv_cache_bytes = mb(400);
        est.compute_buffer_bytes = mb(100);
        est.runtime_overhead_bytes = mb(180);
        // total = 2480 MB

        const MemoryDecision d = MemoryBudgetPolicy::evaluate(est, mb(3000), mb(300));
        check(d.safe, "estimate well under (budget - reserve) is accepted");
        check(!d.diagnostic.empty(), "diagnostic is always populated");
        check(d.diagnostic.find("STATUS: SAFE") != std::string::npos, "diagnostic reports SAFE");
    }

    // Over-budget configuration is rejected.
    {
        MemoryEstimate est;
        est.weights_bytes = mb(3800);
        est.weights_bytes_is_observed = true;
        est.kv_cache_bytes = mb(900);
        est.compute_buffer_bytes = mb(200);
        est.runtime_overhead_bytes = mb(180);
        // total = 5080 MB

        const MemoryDecision d = MemoryBudgetPolicy::evaluate(est, mb(3000), mb(300));
        check(!d.safe, "estimate exceeding (budget - reserve) is rejected");
        check(d.diagnostic.find("STATUS: UNSAFE") != std::string::npos, "diagnostic reports UNSAFE");
        check(d.diagnostic.find("Recommended action") != std::string::npos,
              "UNSAFE diagnostic includes a recommended action");
    }

    // Exact-boundary case: total == usable ceiling is SAFE. The usable
    // ceiling is INCLUSIVE — this is the corrected, spec-mandated behavior
    // (the reserve already provides the required headroom; there is no
    // additional implicit margin at the comparison itself).
    {
        MemoryEstimate est;
        est.weights_bytes = mb(2700); // total will equal exactly 2700 MB
        const MemoryDecision d = MemoryBudgetPolicy::evaluate(est, mb(3000), mb(300)); // ceiling = 2700 MB
        check(d.safe, "estimate exactly equal to the usable ceiling is SAFE (inclusive boundary)");
    }

    // One byte over the boundary is rejected — no silent rounding leniency.
    {
        MemoryEstimate est;
        est.weights_bytes = mb(2700) + 1;
        const MemoryDecision d = MemoryBudgetPolicy::evaluate(est, mb(3000), mb(300));
        check(!d.safe, "one byte over the usable ceiling is rejected");
    }

    // Clearly over budget — sanity check on an unambiguous case.
    {
        MemoryEstimate est;
        est.weights_bytes = mb(10000);
        const MemoryDecision d = MemoryBudgetPolicy::evaluate(est, mb(3000), mb(300));
        check(!d.safe, "an estimate far exceeding the budget is rejected");
    }

    // Degenerate case: reserve == budget means zero usable ceiling — must
    // not underflow, and must reject even a zero-byte estimate (0 <= 0
    // would otherwise silently read as "fits").
    {
        MemoryEstimate est; // all zero, valid == true
        const MemoryDecision d = MemoryBudgetPolicy::evaluate(est, mb(300), mb(300));
        check(!d.safe, "reserve == budget leaves zero usable ceiling: reject, even a zero estimate");
    }

    // Degenerate case: reserve > budget must not underflow into a
    // false-safe decision.
    {
        MemoryEstimate est;
        const MemoryDecision d = MemoryBudgetPolicy::evaluate(est, mb(100), mb(300));
        check(!d.safe, "reserve exceeding budget must not underflow into a false-safe decision");
    }

    // Fail-closed: an INVALID estimate is UNSAFE unconditionally, even if
    // its byte fields (left at 0 / whatever) would otherwise look like they
    // fit comfortably within a huge budget. This is the core Correction #2
    // behavior — invalid data must never be able to produce a SAFE result.
    {
        MemoryEstimate est;
        est.valid = false;
        est.weights_bytes = mb(1); // deliberately tiny/plausible-looking
        est.kv_cache_bytes = 0;
        est.compute_buffer_bytes = 0;
        est.runtime_overhead_bytes = 0;

        const MemoryDecision d = MemoryBudgetPolicy::evaluate(est, mb(1'000'000), mb(1)); // enormous budget
        check(!d.safe, "an invalid estimate is UNSAFE even against an enormous budget");
        check(d.diagnostic.find("STATUS: UNSAFE") != std::string::npos,
              "invalid-estimate diagnostic reports UNSAFE");
        check(d.diagnostic.find("could not be established safely") != std::string::npos,
              "invalid-estimate diagnostic explains why, distinctly from an over-budget message");
        check(d.diagnostic.find("Recommended action") != std::string::npos,
              "invalid-estimate diagnostic still includes a recommended action");
    }

    if (g_failures > 0) {
        std::fprintf(stderr, "%d check(s) failed.\n", g_failures);
        return EXIT_FAILURE;
    }
    std::printf("test_memory_budget: all checks passed.\n");
    return EXIT_SUCCESS;
}
