#ifndef SYJ_EDGEMIND_CORE_RUNTIME_H
#define SYJ_EDGEMIND_CORE_RUNTIME_H

#include <memory>
#include <string>

#include "core/config.h"
#include "inference/inference_engine.h"
#include "model/model_types.h"
#include "model/model_resolver.h"
#include "usage/usage_manager.h"

namespace syj::edgemind {

// Explicit, stable error classification for Runtime-level operations —
// this is what the C API (edge_mind_api.cpp) maps 1:1 to
// syj_edgemind_status, instead of pattern-matching substrings out of a
// human-readable message string. RuntimeError is a superset of EngineError
// (adds InvalidConfig, for failures caught by validate_config() before the
// engine is ever touched; NotLoaded, for operations attempted on a Runtime
// that never successfully loaded; QuotaExceeded, for the v0.3.0 usage/
// quota admission gate; ModelVerificationFailed, for the Phase 3 model
// verification gate; and ModelResolutionFailed, for the v0.5.0 model_id ->
// path resolution step, evaluated BEFORE verification — see
// resolve_model_path() in src/model/model_resolver.h). Both
// ModelResolutionFailed and ModelVerificationFailed are deliberately single
// values covering several distinct sub-outcomes each — the granular
// classification lives in ModelResolutionStatus/VerificationStatus
// respectively (available via Runtime::verification_report() for the
// latter), mirroring how MemoryBudgetExceeded is one RuntimeError value
// even though MemoryBudgetPolicy's own diagnostic is more detailed.
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
    ModelVerificationFailed,
    ModelResolutionFailed,
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
    // verifies the model file (see ModelVerifier/ModelRegistry — Phase 3),
    // then loads the model. On any failure, returns a human-readable error
    // and the Runtime remains unloaded (is_ready() returns false) — it
    // never leaves a half-initialized runtime in place. Order of checks:
    // config validation -> quota admission -> model verification -> memory
    // admission (inside InferenceEngine::load()) -> model loading. A denied
    // quota check never reaches model verification; a failed verification
    // never reaches memory admission, model loading, or llama.cpp at all —
    // this is the gate that keeps an unverified/malformed file from ever
    // being handed to llama_model_load_from_file().
    // See last_error() for a machine-readable classification of the same
    // outcome, and verification_report() for the detailed
    // VerificationStatus-level diagnostic.
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

    // v0.5.0: releases the loaded model and inference context, returning
    // the Runtime to an unloaded state (is_ready() becomes false). Safe to
    // call when nothing is loaded (idempotent — a no-op in that case) and
    // safe to call more than once in a row. Does NOT touch usage/quota
    // state (usage_manager_ persists across unload — quota is scoped to
    // the local install, not to any one model) or the model registry.
    // After unload(), load() may be called again — either with the same
    // config to reload the same model, or a different one to switch models
    // within this Runtime instance — and will run the exact same
    // resolve -> verify -> admit -> load pipeline as any other load() call.
    void unload();

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

    // Phase 3: the model-verification diagnostic from the most recent
    // load() attempt (STATUS: VERIFIED/REJECTED, plus metadata/identity
    // when available — see ModelVerifier::verify's diagnostic format).
    // Populated even when load() failed due to verification specifically,
    // same "inspectable regardless of outcome" posture as memory_report().
    std::string verification_report() const { return last_verification_diagnostic_; }

    // Phase 4. Read-only context-usage report for the CLI's /context
    // command — forwards InferenceEngine::context_state() (which itself
    // only forwards ContextManager, never recomputing anything). Reports
    // "not loaded" if the Runtime never successfully loaded a model or has
    // since been unload()ed — same posture as model_info().
    std::string context_report() const;

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
    std::string last_verification_diagnostic_;
};

} // namespace syj::edgemind

#endif // SYJ_EDGEMIND_CORE_RUNTIME_H
