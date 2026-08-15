#include "memory/memory_budget.h"

#include <sstream>
#include <iomanip>

namespace syj::edgemind {

namespace {
double to_mb(uint64_t bytes) {
    return static_cast<double>(bytes) / (1024.0 * 1024.0);
}
} // namespace

MemoryDecision MemoryBudgetPolicy::evaluate(const MemoryEstimate& estimate, uint64_t budget_bytes,
                                             uint64_t safety_reserve_bytes) {
    MemoryDecision decision;
    decision.estimate = estimate;
    decision.budget_bytes = budget_bytes;
    decision.safety_reserve_bytes = safety_reserve_bytes;

    // If the reserve alone exceeds (or equals) the budget, nothing can ever
    // be safe — treat the usable ceiling as zero rather than underflowing.
    const uint64_t usable_ceiling = (safety_reserve_bytes < budget_bytes) ? (budget_bytes - safety_reserve_bytes) : 0;

    // Fail-closed: an invalid estimate is UNSAFE unconditionally, regardless
    // of what total_bytes() would compute to. This must be checked before
    // (and independently of) the numeric comparison below.
    if (!estimate.valid) {
        decision.safe = false;
    } else {
        // Inclusive boundary: total_bytes() == usable_ceiling is SAFE.
        decision.safe = (usable_ceiling > 0) && (estimate.total_bytes() <= usable_ceiling);
    }

    decision.diagnostic = format_diagnostic(decision);
    return decision;
}

std::string MemoryBudgetPolicy::format_diagnostic(const MemoryDecision& decision) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(1);

    if (!decision.estimate.valid) {
        oss << "STATUS: UNSAFE\n\n";
        oss << "Memory estimate could not be established safely because required\n";
        oss << "model hyperparameters were invalid or unavailable.\n\n";
        oss << "Recommended action:\n";
        oss << "Verify that the loaded GGUF model is compatible with the supported\n";
        oss << "llama.cpp API.\n";
        return oss.str();
    }

    oss << "Model weights"
        << (decision.estimate.weights_bytes_is_observed ? " (observed): " : " (estimate): ")
        << to_mb(decision.estimate.weights_bytes) << " MB\n";
    oss << "KV cache estimate:             " << to_mb(decision.estimate.kv_cache_bytes) << " MB\n";
    oss << "Compute buffer estimate:       " << to_mb(decision.estimate.compute_buffer_bytes) << " MB\n";
    oss << "Runtime overhead estimate:     " << to_mb(decision.estimate.runtime_overhead_bytes) << " MB\n";
    oss << "\n";
    oss << "Estimated total:               " << to_mb(decision.estimate.total_bytes()) << " MB\n";
    oss << "Safety reserve:                " << to_mb(decision.safety_reserve_bytes) << " MB\n";
    oss << "Memory budget:                 " << to_mb(decision.budget_bytes) << " MB\n";
    oss << "\n";

    if (decision.safe) {
        oss << "STATUS: SAFE\n";
    } else {
        oss << "STATUS: UNSAFE\n";
        oss << "The selected model/context configuration exceeds the configured\n";
        oss << "memory budget (after the safety reserve).\n\n";
        oss << "Recommended action:\n";
        oss << "Reduce --context, or increase --memory-budget if this device genuinely\n";
        oss << "has more RAM available than the current budget assumes.\n";
    }

    return oss.str();
}

} // namespace syj::edgemind
