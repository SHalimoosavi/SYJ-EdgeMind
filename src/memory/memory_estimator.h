#ifndef SYJ_EDGEMIND_MEMORY_MEMORY_ESTIMATOR_H
#define SYJ_EDGEMIND_MEMORY_MEMORY_ESTIMATOR_H

#include <cstdint>
#include <optional>

#include "memory/memory_types.h"

namespace syj::edgemind {

// Pure estimation logic — no llama.cpp dependency, no I/O, no system calls.
// This is deliberate: every number here is a documented FORMULA, not a
// measurement, and keeping it pure makes it directly unit-testable (see
// tests/memory/test_memory_estimator.cpp) independent of whether llama.cpp
// is even available to build against.
//
// FAIL-CLOSED: every function returns std::nullopt — not 0 — when its
// inputs are invalid, out of a sane range, or would cause the underlying
// arithmetic to overflow. A 0-byte estimate and "this could not be safely
// estimated" are different facts; conflating them is exactly what let a
// corrupt/adversarial model's garbage hyperparameters look artificially
// SAFE in an earlier version of this code. Callers (see
// InferenceEngine::load()) must propagate nullopt into
// MemoryEstimate::valid = false, never substitute an invented number.
class MemoryEstimator {
public:
    // KV-cache size in bytes for a context of `n_ctx` tokens, given the
    // model's hyperparameters.
    //
    // Formula (standard GQA-aware KV-cache sizing, matching llama.cpp's own
    // per-layer K/V allocation shape):
    //   n_embd_gqa = hp.n_embd * hp.n_head_kv / hp.n_head
    //   bytes      = 2 (K and V) * hp.n_layer * n_ctx * n_embd_gqa * bytes_per_element
    //
    // `bytes_per_element` defaults to 2 (f16), matching llama.cpp's default
    // KV cache type. This is an ESTIMATE: actual llama.cpp allocation may
    // differ slightly by padding/alignment, which is why the budget policy
    // (memory_budget.h) applies a safety reserve on top rather than trusting
    // this number to the last byte.
    //
    // Returns std::nullopt if any hyperparameter is non-positive, outside a
    // sane range (guards against a corrupt GGUF reporting garbage), if
    // n_head_kv > n_head (violates the GQA constraint), or if the
    // computation would overflow uint64_t.
    static std::optional<uint64_t> estimate_kv_cache_bytes(const ModelHyperparams& hp, int32_t n_ctx,
                                                             int32_t bytes_per_element = 2);

    // Compute/scratch buffer size in bytes for processing a batch of
    // `n_batch` tokens. This is intentionally conservative (biased high):
    // llama.cpp's actual graph-allocator buffer size depends on the model
    // architecture's intermediate tensor shapes, which SYJ EdgeMind does not
    // replicate here. The constant SYJ_EDGEMIND_COMPUTE_BUFFER_MULTIPLIER
    // documents that deliberate over-estimation.
    //
    // Returns std::nullopt under the same invalid-input/overflow conditions
    // as estimate_kv_cache_bytes.
    static std::optional<uint64_t> estimate_compute_buffer_bytes(const ModelHyperparams& hp, int32_t n_batch);

    // Multiplier applied to (n_batch * n_embd * 4 bytes) to approximate the
    // compute buffer. Chosen conservatively high (rather than measured) so
    // that admission decisions err toward "unsafe" rather than "safe" when
    // uncertain — consistent with docs/memory-model.md's safety-first
    // philosophy. Documented here, not silently hard-coded elsewhere.
    static constexpr int32_t SYJ_EDGEMIND_COMPUTE_BUFFER_MULTIPLIER = 4;

    // Sane upper bounds used to reject obviously-garbage hyperparameters
    // (e.g. from a corrupt GGUF) rather than attempting arithmetic on them.
    // Chosen generously above any known real model as of this writing —
    // documented constants, not arbitrary magic numbers.
    static constexpr int32_t SYJ_EDGEMIND_MAX_SANE_LAYERS = 2048;
    static constexpr int32_t SYJ_EDGEMIND_MAX_SANE_EMBD = 1 << 20;      // ~1,000,000
    static constexpr int32_t SYJ_EDGEMIND_MAX_SANE_HEADS = 4096;
    static constexpr int32_t SYJ_EDGEMIND_MAX_SANE_CTX = 1 << 24;        // ~16.7M tokens
    static constexpr int32_t SYJ_EDGEMIND_MAX_SANE_BYTES_PER_ELEMENT = 8; // e.g. f64 upper bound
};

} // namespace syj::edgemind

#endif // SYJ_EDGEMIND_MEMORY_MEMORY_ESTIMATOR_H
