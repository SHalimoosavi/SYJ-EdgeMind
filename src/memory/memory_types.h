#ifndef SYJ_EDGEMIND_MEMORY_MEMORY_TYPES_H
#define SYJ_EDGEMIND_MEMORY_MEMORY_TYPES_H

#include <cstdint>
#include <limits>
#include <string>

namespace syj::edgemind {

// The model hyperparameters needed to estimate KV-cache and compute-buffer
// memory. Deliberately a plain data struct with no llama.cpp dependency —
// the bridge from real llama_model_* getters to this struct lives in
// memory_observer.h/.cpp, which is the ONLY file in src/memory/ allowed to
// include llama.h. Everything else here (memory_estimator, memory_budget)
// is pure, llama.cpp-independent logic, so it can be unit tested without a
// model or the pinned dependency being available.
struct ModelHyperparams {
    int32_t n_layer = 0;
    int32_t n_embd = 0;
    int32_t n_head = 0;
    int32_t n_head_kv = 0;
    int32_t n_ctx_train = 0;
};

// The breakdown of estimated/observed memory a loaded-and-running model
// would consume, matching docs/memory-model.md's categories:
//   1. Model weight memory       (weights_bytes)
//   2. Context/KV-cache memory   (kv_cache_bytes)
//   3. Compute-buffer memory     (compute_buffer_bytes)
//   4. Runtime overhead          (runtime_overhead_bytes)
//
// FAIL-CLOSED DESIGN: `valid` is the explicit signal that every component
// below was actually established. A component that could not be computed
// safely (invalid hyperparameters, out-of-sane-range values, or arithmetic
// that would overflow) is NOT represented as 0 — a zero byte count is
// indistinguishable from "definitely fits" to a naive sum, which is exactly
// the false-safe failure mode this design avoids. Whoever builds a
// MemoryEstimate (see MemoryEstimator, InferenceEngine::load()) MUST set
// valid = false and MUST NOT populate byte fields with invented numbers
// when any input component was invalid/unavailable. MemoryBudgetPolicy
// treats valid == false as UNSAFE unconditionally, regardless of the byte
// fields' contents.
struct MemoryEstimate {
    bool valid = true;

    // Actual, OBSERVED value (llama_model_size() post-load) — not an
    // estimate. This is llama.cpp's own reported model size; it is NOT
    // equivalent to process RSS or currently-resident physical RAM (mmap'd
    // pages not yet touched aren't resident). Kept in this struct anyway so
    // the budget policy has one place to sum from — see memory_observer.cpp
    // for the source and docs/memory-model.md for the observed-vs-estimated
    // distinction in full.
    uint64_t weights_bytes = 0;
    bool weights_bytes_is_observed = false;

    // ESTIMATED via memory_estimator.cpp's documented formulas — never
    // claimed as a measured value.
    uint64_t kv_cache_bytes = 0;
    uint64_t compute_buffer_bytes = 0;

    // Fixed, documented default (not measured) covering process/runtime
    // bookkeeping outside the model/context allocations themselves.
    uint64_t runtime_overhead_bytes = 0;

    // Overflow-safe sum: unsigned wraparound is detected and saturates to
    // UINT64_MAX rather than silently wrapping to a small number that could
    // read as SAFE. In real use this saturation should never trigger — all
    // four components are individually bounded (weights by an actual file
    // size; kv_cache/compute_buffer by MemoryEstimator's own overflow
    // checks; runtime_overhead by a fixed small constant) — this exists as
    // a defense-in-depth guard, not because it's expected to fire.
    uint64_t total_bytes() const {
        uint64_t sum = 0;
        const uint64_t parts[4] = {weights_bytes, kv_cache_bytes, compute_buffer_bytes, runtime_overhead_bytes};
        for (uint64_t p : parts) {
            const uint64_t next = sum + p;
            if (next < sum) {
                return std::numeric_limits<uint64_t>::max(); // overflow: saturate, never wrap
            }
            sum = next;
        }
        return sum;
    }
};

// The admission decision produced by evaluating a MemoryEstimate against a
// configured budget + safety reserve.
struct MemoryDecision {
    bool safe = false;
    MemoryEstimate estimate;
    uint64_t budget_bytes = 0;
    uint64_t safety_reserve_bytes = 0;
    // Human-readable, deterministic diagnostic (see memory_budget.cpp's
    // format_diagnostic) — always populated, safe or not.
    std::string diagnostic;
};

} // namespace syj::edgemind

#endif // SYJ_EDGEMIND_MEMORY_MEMORY_TYPES_H
