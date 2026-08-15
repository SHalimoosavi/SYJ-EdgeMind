#include "memory/memory_observer.h"

#include "llama.h"

#if defined(__linux__)
#include <fstream>
#include <sstream>
#include <string>
#endif

namespace syj::edgemind {

ModelHyperparams MemoryObserver::observe_model_hyperparams(const llama_model* model) {
    ModelHyperparams hp;
    if (model == nullptr) {
        return hp;
    }

    // Real llama.cpp model-hyperparameter accessors (stable, long-standing
    // public API — see docs/architecture.md's verified API surface list).
    hp.n_layer = llama_model_n_layer(model);
    hp.n_embd = llama_model_n_embd(model);
    hp.n_head = llama_model_n_head(model);
    hp.n_head_kv = llama_model_n_head_kv(model);
    hp.n_ctx_train = llama_model_n_ctx_train(model);
    return hp;
}

uint64_t MemoryObserver::observe_model_size_bytes(const llama_model* model) {
    if (model == nullptr) {
        return 0;
    }
    return llama_model_size(model); // actual measurement, not an estimate
}

SystemMemoryInfo MemoryObserver::observe_system_memory() {
    SystemMemoryInfo info;

#if defined(__linux__)
    std::ifstream meminfo("/proc/meminfo");
    if (!meminfo.is_open()) {
        return info; // available stays false — honest "unknown", not a guess
    }

    uint64_t mem_total_kb = 0;
    uint64_t mem_available_kb = 0;
    bool have_total = false;
    bool have_available = false;

    std::string line;
    while (std::getline(meminfo, line)) {
        std::istringstream iss(line);
        std::string key;
        uint64_t value_kb = 0;
        iss >> key >> value_kb;

        if (key == "MemTotal:") {
            mem_total_kb = value_kb;
            have_total = true;
        } else if (key == "MemAvailable:") {
            mem_available_kb = value_kb;
            have_available = true;
        }

        if (have_total && have_available) {
            break;
        }
    }

    if (have_total && have_available) {
        info.available = true;
        info.total_bytes = mem_total_kb * 1024ULL;
        info.available_bytes = mem_available_kb * 1024ULL;
    }
    // If MemAvailable wasn't present (very old kernels), we deliberately do
    // NOT fall back to a cruder estimate (e.g. MemFree alone) — that would
    // silently understate available memory and is exactly the kind of
    // invented number this project avoids. available stays false instead.
#else
    // Windows/macOS/iOS system-memory detection is not implemented yet.
    // This is a documented Phase 2 limitation, not a silent wrong answer —
    // see docs/memory-model.md and docs/troubleshooting.md.
#endif

    return info;
}

} // namespace syj::edgemind
