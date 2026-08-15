#ifndef SYJ_EDGEMIND_MEMORY_MEMORY_OBSERVER_H
#define SYJ_EDGEMIND_MEMORY_MEMORY_OBSERVER_H

#include <cstdint>

#include "memory/memory_types.h"

struct llama_model; // fwd-declared from llama.h

namespace syj::edgemind {

// Reports on system memory availability. `available` is false when this
// platform's detection isn't implemented — callers must not treat that as
// "unlimited memory available", only as "unknown; admission decisions fall
// back to the configured budget alone, without a live system-memory check."
struct SystemMemoryInfo {
    bool available = false;
    uint64_t total_bytes = 0;
    uint64_t available_bytes = 0;
};

// The ONLY component in src/memory/ that touches llama.cpp (real
// llama_model_* getters) or the OS (reading available system memory). Every
// other memory/ file is pure logic that can be unit tested without either.
class MemoryObserver {
public:
    // Reads real hyperparameters from an already-loaded model via
    // llama_model_n_embd/n_layer/n_head/n_head_kv/n_ctx_train. These are
    // actual llama.cpp accessors, not estimates.
    static ModelHyperparams observe_model_hyperparams(const llama_model* model);

    // Returns the model's ACTUAL, measured weight size in bytes via
    // llama_model_size() — an authoritative runtime measurement, not a
    // guess, which is why MemoryEstimate::weights_bytes_is_observed is set
    // true by whoever calls this (see InferenceEngine::load()).
    static uint64_t observe_model_size_bytes(const llama_model* model);

    // Best-effort system memory observation. Currently implemented for
    // Linux (reads /proc/meminfo's MemTotal/MemAvailable). Returns
    // SystemMemoryInfo{available=false, ...} on any other platform or on
    // any read failure — this is a documented Phase 2 limitation (see
    // docs/memory-model.md), not a silent wrong answer: Windows and iOS
    // detection are not implemented yet (Phase 5/6/7 territory).
    static SystemMemoryInfo observe_system_memory();
};

} // namespace syj::edgemind

#endif // SYJ_EDGEMIND_MEMORY_MEMORY_OBSERVER_H
