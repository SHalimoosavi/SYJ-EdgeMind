#include "memory/memory_estimator.h"

#include <limits>

namespace syj::edgemind {

namespace {

// Overflow-checked uint64_t multiply: returns false (leaving *out
// unspecified) if a*b would overflow, true with the product in *out
// otherwise. Avoids floating-point rounding at the boundary between
// "fits" and "doesn't fit" that a double-based computation would risk.
bool checked_mul(uint64_t a, uint64_t b, uint64_t* out) {
    if (a == 0 || b == 0) {
        *out = 0;
        return true;
    }
    if (a > std::numeric_limits<uint64_t>::max() / b) {
        return false; // would overflow
    }
    *out = a * b;
    return true;
}

bool hyperparams_in_sane_range(const ModelHyperparams& hp) {
    if (hp.n_layer <= 0 || hp.n_layer > MemoryEstimator::SYJ_EDGEMIND_MAX_SANE_LAYERS) return false;
    if (hp.n_embd <= 0 || hp.n_embd > MemoryEstimator::SYJ_EDGEMIND_MAX_SANE_EMBD) return false;
    if (hp.n_head <= 0 || hp.n_head > MemoryEstimator::SYJ_EDGEMIND_MAX_SANE_HEADS) return false;
    if (hp.n_head_kv <= 0 || hp.n_head_kv > MemoryEstimator::SYJ_EDGEMIND_MAX_SANE_HEADS) return false;
    if (hp.n_head_kv > hp.n_head) return false; // violates the GQA constraint (kv heads <= query heads)

    // llama.cpp models use whole attention heads. Reject malformed metadata
    // rather than silently truncating n_embd / n_head below.
    if (hp.n_embd % hp.n_head != 0) return false;

    // Grouped-query attention must divide the query heads evenly into KV
    // groups. Reject malformed metadata rather than estimating a misleading
    // cache size from integer truncation.
    if (hp.n_head % hp.n_head_kv != 0) return false;

    return true;
}

} // namespace

std::optional<uint64_t> MemoryEstimator::estimate_kv_cache_bytes(const ModelHyperparams& hp, int32_t n_ctx,
                                                                   int32_t bytes_per_element) {
    if (!hyperparams_in_sane_range(hp)) {
        return std::nullopt;
    }
    if (n_ctx <= 0 || n_ctx > SYJ_EDGEMIND_MAX_SANE_CTX) {
        return std::nullopt;
    }
    if (bytes_per_element <= 0 || bytes_per_element > SYJ_EDGEMIND_MAX_SANE_BYTES_PER_ELEMENT) {
        return std::nullopt;
    }

    // n_embd_gqa = n_embd * n_head_kv / n_head — the GQA-aware embedding
    // dimension actually stored per K/V tensor per layer. Computed with
    // integer arithmetic (checked for overflow) rather than double, so the
    // exact-boundary behavior the budget policy relies on isn't at the
    // mercy of floating-point rounding. Integer division here matches
    // llama.cpp's own integer-dimension bookkeeping; any truncation is at
    // most a few bytes per layer, immaterial next to the safety reserve.
    uint64_t embd_x_headkv = 0;
    if (!checked_mul(static_cast<uint64_t>(hp.n_embd), static_cast<uint64_t>(hp.n_head_kv), &embd_x_headkv)) {
        return std::nullopt;
    }
    const uint64_t n_embd_gqa = embd_x_headkv / static_cast<uint64_t>(hp.n_head);

    // bytes = 2 (K and V) * n_layer * n_ctx * n_embd_gqa * bytes_per_element
    uint64_t bytes = 2;
    const uint64_t factors[4] = {
        static_cast<uint64_t>(hp.n_layer),
        static_cast<uint64_t>(n_ctx),
        n_embd_gqa,
        static_cast<uint64_t>(bytes_per_element),
    };
    for (uint64_t f : factors) {
        if (!checked_mul(bytes, f, &bytes)) {
            return std::nullopt;
        }
    }

    return bytes;
}

std::optional<uint64_t> MemoryEstimator::estimate_compute_buffer_bytes(const ModelHyperparams& hp, int32_t n_batch) {
    if (!hyperparams_in_sane_range(hp)) {
        return std::nullopt;
    }
    if (n_batch <= 0 || n_batch > SYJ_EDGEMIND_MAX_SANE_CTX) {
        return std::nullopt;
    }

    // base = n_batch * n_embd * 4 (f32); result = base * multiplier
    uint64_t bytes = 1;
    const uint64_t factors[4] = {
        static_cast<uint64_t>(n_batch),
        static_cast<uint64_t>(hp.n_embd),
        4, // f32
        static_cast<uint64_t>(SYJ_EDGEMIND_COMPUTE_BUFFER_MULTIPLIER),
    };
    for (uint64_t f : factors) {
        if (!checked_mul(bytes, f, &bytes)) {
            return std::nullopt;
        }
    }

    return bytes;
}

} // namespace syj::edgemind
