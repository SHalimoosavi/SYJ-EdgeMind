#ifndef SYJ_EDGEMIND_CORE_RUNTIME_H
#define SYJ_EDGEMIND_CORE_RUNTIME_H

#include <memory>
#include <string>

#include "core/config.h"
#include "inference/inference_engine.h"
#include "usage/usage_manager.h"

namespace syj::edgemind {

// Explicit, stable error classification for Runtime-level operations —
// this is what the C API (edge_mind_api.cpp) maps 1:1 to
// syj_edgemind_status, instead of pattern-matching substrings out of a
// human-readable message string. RuntimeError is a superset of EngineError
// (adds InvalidConfig, for failures caught by validate_config() before the
// engine is ever touched; NotLoaded, for operations attempted on a Runtime
// that never successfully loaded; and QuotaExceeded, for the v0.3.0 usage/
// quota admission gate — evaluated BEFORE memory admission/model loading,
// per the documented pipeline in docs/architecture.md).
enum class RuntimeError {
    None,
    InvalidConfig,
    ModelFileNotFound,
    ModelLoadFailed,
    ContextCreateFailed,
    TokenizeFailed,
    DecodeFailed,
    MemoryBudgetExceeded,
    NotLoaded,
    QuotaExceeded,
};

// The top-level object platform code (CLI today; Windows/iOS wrappers in
// later phases) is expected to hold. Wraps config validation + InferenceEngine
// lifecycle behind one RAII type so callers never touch llama.cpp types.
class Runtime {
public:
    Runtime();
    ~Runtime();

    Runtime(const Runtime&) = delete;
    Runtime& operator=(const Runtime&) = delete;

    // Validates `config`, checks usage/quota admission (see UsageManager),
    // then loads the model. On any failure, returns a human-readable error
    // and the Runtime remains unloaded (is_ready() returns false) — it
    // never leaves a half-initialized runtime in place. Order of checks:
    // config validation -> quota admission -> memory admission (inside
    // InferenceEngine::load()) -> model loading. A denied quota check never
    // reaches memory admission or touches the model at all.
    // See last_error() for a machine-readable classification of the same
    // outcome.
    std::string load(const RuntimeConfig& config);

    bool is_ready() const;

    // Re-checks quota admission, then streams a response to `prompt` if
    // allowed, then records the actual usage (one message + however many
    // tokens were actually generated) — accounting happens AFTER
    // generation, reflecting what actually occurred, not what was
    // requested. Returns a human-readable error string (empty on success).
    // See InferenceEngine::generate for callback semantics.
    std::string generate(const std::string& prompt, const TokenStreamCallback& on_token);

    void reset_context();

    InferenceEngine::ModelInfo model_info() const;
    const RuntimeConfig& config() const { return config_; }

    // Phase 2: the memory-budget diagnostic from the most recent load()
    // attempt. Populated even when load() failed due to the budget being
    // exceeded — this is precisely the deterministic, human-readable error
    // state Phase 2 requires callers to be able to inspect.
    std::string memory_report() const;

    // v0.3.0: the usage/quota diagnostic — current usage, remaining quota,
    // and reset time, formatted for a CLI /usage command. Safe to call
    // whether or not the Runtime is currently loaded (it reads the local
    // usage-state file, which is independent of model state); if load()
    // was never called, this reports against config()'s default policy.
    std::string usage_report() const;

    // Machine-readable classification of the outcome of the most recent
    // load() or generate() call — the explicit, non-string-matching
    // source of truth the C API status mapping is built from.
    RuntimeError last_error() const { return last_error_; }

private:
    std::unique_ptr<InferenceEngine> engine_;
    std::unique_ptr<UsageManager> usage_manager_;
    RuntimeConfig config_;
    bool ready_ = false;
    RuntimeError last_error_ = RuntimeError::None;
};

} // namespace syj::edgemind

#endif // SYJ_EDGEMIND_CORE_RUNTIME_H
