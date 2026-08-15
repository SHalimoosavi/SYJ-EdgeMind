// Integration test for the memory subsystem's pure components working
// together (estimator -> policy), using hyperparameters representative of a
// real small model (matching the SmolLM2-135M-Instruct GGUF used for Phase
// 1's real-hardware inference test, per its published architecture: 30
// layers, 576 hidden size, 9 heads, 3 KV heads, 8192 training context).
//
// This does NOT require llama.cpp or an actual GGUF file — it exercises
// exactly the estimation + admission logic InferenceEngine::load() calls,
// with hand-supplied hyperparameters standing in for MemoryObserver's real
// llama_model_* calls (which do require the real library and aren't
// available in this sandbox — see docs/development.md).

#include <cstdio>
#include <cstdlib>

#include "memory/memory_budget.h"
#include "memory/memory_estimator.h"
#include "memory/memory_types.h"

using namespace syj::edgemind;

namespace {
int g_failures = 0;

void check(bool condition, const char* description) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", description);
        ++g_failures;
    }
}

uint64_t mb(uint64_t n) { return n * 1024ULL * 1024ULL; }

// Mirrors exactly what InferenceEngine::load() does: build a MemoryEstimate
// from the (possibly-nullopt) estimator results, failing closed to
// valid=false rather than inventing a number when either component is
// unavailable.
MemoryEstimate build_estimate(const ModelHyperparams& hp, int32_t n_ctx, uint64_t weights_bytes) {
    MemoryEstimate est;
    est.weights_bytes = weights_bytes;
    est.weights_bytes_is_observed = true;

    const auto kv = MemoryEstimator::estimate_kv_cache_bytes(hp, n_ctx);
    const auto compute = MemoryEstimator::estimate_compute_buffer_bytes(hp, n_ctx);

    if (!kv.has_value() || !compute.has_value()) {
        est.valid = false;
        return est;
    }

    est.kv_cache_bytes = *kv;
    est.compute_buffer_bytes = *compute;
    est.runtime_overhead_bytes = mb(180);
    est.valid = true;
    return est;
}
} // namespace

int main() {
    // SmolLM2-135M-Instruct-shaped hyperparameters.
    ModelHyperparams hp;
    hp.n_layer = 30;
    hp.n_embd = 576;
    hp.n_head = 9;
    hp.n_head_kv = 3;
    hp.n_ctx_train = 8192;

    // Case 1: a conservative context (1024, the Phase 1 default) on a
    // ~99 MB Q4_K_M model comfortably fits a 3000 MB / 300 MB reserve
    // budget — this must be admitted.
    {
        const MemoryEstimate est = build_estimate(hp, /*n_ctx=*/1024, mb(99));
        check(est.valid, "SmolLM2-shaped hyperparameters at context=1024 produce a valid estimate");
        const MemoryDecision d = MemoryBudgetPolicy::evaluate(est, mb(3000), mb(300));
        check(d.safe, "a ~99MB model at context=1024 fits the default 4GB-device budget");
    }

    // Case 2: an unreasonably large context on the SAME small model, against
    // a deliberately tiny budget, must be rejected — proving the admission
    // gate actually blocks unsafe configurations rather than only ever
    // accepting.
    {
        const MemoryEstimate est = build_estimate(hp, /*n_ctx=*/8192, mb(99));
        check(est.valid, "context=8192 still produces a valid (just larger) estimate");
        const MemoryDecision d = MemoryBudgetPolicy::evaluate(est, mb(128), mb(32)); // 96 MB usable ceiling
        check(!d.safe, "the same model against a deliberately tiny 128MB/32MB-reserve budget is rejected");
    }

    // Case 3: sanity check that increasing context strictly increases the
    // estimate for a fixed model+budget — i.e., the admission decision
    // actually responds to --context rather than being constant.
    {
        const auto total_at = [&](int32_t n_ctx) {
            return build_estimate(hp, n_ctx, mb(99)).total_bytes();
        };
        check(total_at(2048) > total_at(512), "estimated total memory increases with context size");
    }

    // Case 4: invalid observation/estimation data can NEVER become SAFE,
    // even against a deliberately enormous budget. This directly exercises
    // the fail-closed path an InferenceEngine::load() would hit if
    // MemoryObserver ever returned garbage hyperparameters (e.g. from a
    // malformed GGUF's metadata).
    {
        ModelHyperparams garbage; // all zero — never a real model's hyperparameters
        const MemoryEstimate est = build_estimate(garbage, /*n_ctx=*/1024, mb(99));
        check(!est.valid, "garbage (all-zero) hyperparameters produce an invalid estimate");

        const MemoryDecision d = MemoryBudgetPolicy::evaluate(est, mb(1'000'000), mb(1)); // enormous budget
        check(!d.safe, "an invalid estimate from garbage hyperparameters is UNSAFE against any budget");
    }

    if (g_failures > 0) {
        std::fprintf(stderr, "%d check(s) failed.\n", g_failures);
        return EXIT_FAILURE;
    }
    std::printf("test_memory_admission: all checks passed.\n");
    return EXIT_SUCCESS;
}
