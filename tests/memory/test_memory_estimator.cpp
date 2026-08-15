#include <cstdio>
#include <cstdlib>
#include <limits>

#include "memory/memory_estimator.h"

using syj::edgemind::MemoryEstimator;
using syj::edgemind::ModelHyperparams;

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
    // Standard multi-head attention (n_head_kv == n_head): n_embd_gqa reduces
    // to n_embd exactly, so the formula becomes 2 * n_layer * n_ctx * n_embd
    // * bytes_per_element, which we can check by hand.
    {
        ModelHyperparams hp;
        hp.n_layer = 12;
        hp.n_embd = 768;
        hp.n_head = 12;
        hp.n_head_kv = 12; // no GQA
        hp.n_ctx_train = 2048;

        const uint64_t expected = 2ULL * 12 * 1024 * 768 * 2; // n_ctx=1024, f16
        const auto actual = MemoryEstimator::estimate_kv_cache_bytes(hp, /*n_ctx=*/1024, /*bytes_per_element=*/2);
        check(actual.has_value(), "valid MHA hyperparameters produce a valid estimate");
        check(actual.has_value() && *actual == expected, "KV-cache estimate matches hand-computed value for standard MHA");
    }

    // GQA case: n_head_kv < n_head should reduce the estimate proportionally.
    {
        ModelHyperparams hp;
        hp.n_layer = 32;
        hp.n_embd = 4096;
        hp.n_head = 32;
        hp.n_head_kv = 8; // 4x GQA reduction
        hp.n_ctx_train = 8192;

        const auto mha_equivalent = MemoryEstimator::estimate_kv_cache_bytes(
            ModelHyperparams{32, 4096, 32, 32, 8192}, 2048);
        const auto gqa_estimate = MemoryEstimator::estimate_kv_cache_bytes(hp, 2048);

        check(mha_equivalent.has_value() && gqa_estimate.has_value(), "both GQA and MHA cases produce valid estimates");
        if (mha_equivalent.has_value() && gqa_estimate.has_value()) {
            check(*gqa_estimate < *mha_equivalent, "GQA (n_head_kv < n_head) reduces the KV-cache estimate");
            check(*gqa_estimate == *mha_equivalent / 4, "4x GQA reduction quarters the estimate exactly");
        }
    }

    // Doubling context should exactly double the KV-cache estimate (linear
    // in n_ctx).
    {
        ModelHyperparams hp{24, 2048, 16, 16, 4096};
        const auto at_512 = MemoryEstimator::estimate_kv_cache_bytes(hp, 512);
        const auto at_1024 = MemoryEstimator::estimate_kv_cache_bytes(hp, 1024);
        check(at_512.has_value() && at_1024.has_value(), "both context sizes produce valid estimates");
        if (at_512.has_value() && at_1024.has_value()) {
            check(*at_1024 == *at_512 * 2, "KV-cache estimate scales linearly with context size");
        }
    }

    // Invalid hyperparameters: fail CLOSED — must return std::nullopt, NOT
    // a zero-byte "valid" estimate. A caller summing bytes must never be
    // able to mistake "we couldn't tell" for "definitely fits".
    {
        ModelHyperparams zero_hp{};
        check(!MemoryEstimator::estimate_kv_cache_bytes(zero_hp, 1024).has_value(),
              "zeroed hyperparams yield nullopt, not a zero-byte estimate");

        ModelHyperparams valid_hp{12, 768, 12, 12, 2048};
        check(!MemoryEstimator::estimate_kv_cache_bytes(valid_hp, /*n_ctx=*/0).has_value(),
              "zero n_ctx yields nullopt");
        check(!MemoryEstimator::estimate_kv_cache_bytes(valid_hp, /*n_ctx=*/-5).has_value(),
              "negative n_ctx yields nullopt");
        check(!MemoryEstimator::estimate_kv_cache_bytes(valid_hp, 1024, /*bytes_per_element=*/0).has_value(),
              "zero bytes_per_element yields nullopt");
        check(!MemoryEstimator::estimate_kv_cache_bytes(valid_hp, 1024, /*bytes_per_element=*/-1).has_value(),
              "negative bytes_per_element yields nullopt");
        check(!MemoryEstimator::estimate_kv_cache_bytes(valid_hp, 1024, /*bytes_per_element=*/100).has_value(),
              "absurdly large bytes_per_element yields nullopt");

        ModelHyperparams negative_layer{-1, 768, 12, 12, 2048};
        check(!MemoryEstimator::estimate_kv_cache_bytes(negative_layer, 1024).has_value(),
              "negative n_layer yields nullopt");

        ModelHyperparams gqa_violation{12, 768, 8, 12, 2048}; // n_head_kv > n_head: invalid
        check(!MemoryEstimator::estimate_kv_cache_bytes(gqa_violation, 1024).has_value(),
              "n_head_kv > n_head (violates GQA constraint) yields nullopt");

        ModelHyperparams absurd_layers{1'000'000'000, 768, 12, 12, 2048};
        check(!MemoryEstimator::estimate_kv_cache_bytes(absurd_layers, 1024).has_value(),
              "absurdly large n_layer (outside sane range) yields nullopt");
    }

    // Overflow guard, verified honestly: SYJ_EDGEMIND_MAX_SANE_* bounds were
    // deliberately chosen conservatively enough that the KV-cache formula
    // (2 * n_layer * n_ctx * n_embd_gqa * bytes_per_element) cannot overflow
    // uint64_t even at their extremes — verified by hand:
    //   2 * 2048 * 2^24 * 2^20 * 8 = 2^59, vs. UINT64_MAX ~= 2^64 - 1.
    // So estimate_kv_cache_bytes() at the maximum sane bounds must succeed
    // (this is what makes the sane-range gate the PRIMARY defense, with the
    // checked_mul overflow guard in the .cpp as pure defense-in-depth that
    // is not expected to trigger through this public, sane-range-gated
    // API — it exists in case the sane bounds are ever loosened without
    // re-deriving this margin).
    {
        ModelHyperparams at_bounds{
            MemoryEstimator::SYJ_EDGEMIND_MAX_SANE_LAYERS,
            MemoryEstimator::SYJ_EDGEMIND_MAX_SANE_EMBD,
            1, // n_head
            1, // n_head_kv (== n_head, no GQA reduction, maximizes n_embd_gqa)
            2048};
        const auto result = MemoryEstimator::estimate_kv_cache_bytes(
            at_bounds, MemoryEstimator::SYJ_EDGEMIND_MAX_SANE_CTX, MemoryEstimator::SYJ_EDGEMIND_MAX_SANE_BYTES_PER_ELEMENT);
        check(result.has_value(),
              "maximum sane-range bounds stay well within uint64_t and must NOT be rejected as overflow");
    }

    // A value just past the sane range (rather than a true arithmetic
    // overflow, which the bounds above preclude) is correctly rejected by
    // the sane-range gate itself.
    {
        ModelHyperparams past_bounds{
            MemoryEstimator::SYJ_EDGEMIND_MAX_SANE_LAYERS + 1,
            768, 12, 12, 2048};
        check(!MemoryEstimator::estimate_kv_cache_bytes(past_bounds, 1024).has_value(),
              "n_layer one past the sane-range maximum is rejected");
    }

    // Compute-buffer estimate is valid and nonzero for valid inputs, and
    // scales with batch size.
    {
        ModelHyperparams hp{12, 768, 12, 12, 2048};
        const auto at_256 = MemoryEstimator::estimate_compute_buffer_bytes(hp, 256);
        const auto at_512 = MemoryEstimator::estimate_compute_buffer_bytes(hp, 512);
        check(at_256.has_value() && at_512.has_value(), "valid inputs produce valid compute-buffer estimates");
        if (at_256.has_value() && at_512.has_value()) {
            check(*at_256 > 0, "compute-buffer estimate is nonzero for valid inputs");
            check(*at_512 == *at_256 * 2, "compute-buffer estimate scales linearly with batch size");
        }

        check(!MemoryEstimator::estimate_compute_buffer_bytes(hp, /*n_batch=*/0).has_value(),
              "zero n_batch yields nullopt for compute-buffer estimate");
        check(!MemoryEstimator::estimate_compute_buffer_bytes(ModelHyperparams{}, 256).has_value(),
              "invalid hyperparams yield nullopt for compute-buffer estimate");
    }

    if (g_failures > 0) {
        std::fprintf(stderr, "%d check(s) failed.\n", g_failures);
        return EXIT_FAILURE;
    }
    std::printf("test_memory_estimator: all checks passed.\n");
    return EXIT_SUCCESS;
}
