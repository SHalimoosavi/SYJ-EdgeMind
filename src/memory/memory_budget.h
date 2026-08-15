#ifndef SYJ_EDGEMIND_MEMORY_MEMORY_BUDGET_H
#define SYJ_EDGEMIND_MEMORY_MEMORY_BUDGET_H

#include <cstdint>

#include "memory/memory_types.h"

namespace syj::edgemind {

// Pure admission-policy logic — no llama.cpp dependency, no I/O.
// This is the ONLY place that decides SAFE vs UNSAFE; every other component
// (memory_estimator, memory_observer, InferenceEngine) feeds this policy
// rather than making its own pass/fail judgment, per the Phase 2 pipeline:
//   observation -> estimation -> budget policy -> admission decision -> load
class MemoryBudgetPolicy {
public:
    // Evaluates `estimate` against `budget_bytes` (the configured SYJ
    // EdgeMind memory budget) minus `safety_reserve_bytes` (headroom held
    // back deliberately, never allocated toward).
    //
    // SAFE iff ALL of:
    //   - estimate.valid is true (see MemoryEstimate — a component that
    //     could not be established safely, e.g. from invalid/out-of-range
    //     model hyperparameters, makes the WHOLE estimate untrusted,
    //     regardless of what its byte fields happen to contain)
    //   - usable_ceiling = (budget_bytes - safety_reserve_bytes) is
    //     strictly positive (a reserve >= budget leaves nothing usable)
    //   - estimate.total_bytes() <= usable_ceiling
    //
    // The usable ceiling is INCLUSIVE: an estimate exactly equal to it is
    // SAFE. The safety reserve is what already provides the required
    // headroom below the raw budget — there is no additional margin applied
    // at the comparison itself, and none should be: doing so would double
    // the reserve implicitly and make the configured safety_reserve_bytes
    // value a lie.
    //
    // Always returns a fully-populated MemoryDecision, including a
    // human-readable diagnostic (see format_diagnostic), for every outcome
    // — safe, unsafe-over-budget, and unsafe-invalid-estimate alike.
    static MemoryDecision evaluate(const MemoryEstimate& estimate, uint64_t budget_bytes,
                                    uint64_t safety_reserve_bytes);

private:
    static std::string format_diagnostic(const MemoryDecision& decision);
};

} // namespace syj::edgemind

#endif // SYJ_EDGEMIND_MEMORY_MEMORY_BUDGET_H
