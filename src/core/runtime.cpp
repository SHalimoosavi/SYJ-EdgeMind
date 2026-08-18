#include "core/runtime.h"

#include <sstream>

#include "model/model_registry.h"

namespace syj::edgemind {

namespace {
// Explicit, exhaustive EngineError -> RuntimeError mapping. This is the
// single place that translation happens, so the C API's status mapping
// never has to guess from a message string.
RuntimeError to_runtime_error(EngineError err) {
    switch (err) {
        case EngineError::None:                 return RuntimeError::None;
        case EngineError::ModelFileNotFound:     return RuntimeError::ModelFileNotFound;
        case EngineError::ModelLoadFailed:       return RuntimeError::ModelLoadFailed;
        case EngineError::ContextCreateFailed:   return RuntimeError::ContextCreateFailed;
        case EngineError::TokenizeFailed:        return RuntimeError::TokenizeFailed;
        case EngineError::DecodeFailed:          return RuntimeError::DecodeFailed;
        case EngineError::MemoryBudgetExceeded:  return RuntimeError::MemoryBudgetExceeded;
    }
    return RuntimeError::ModelLoadFailed; // unreachable if EngineError is exhaustive above
}

UsagePolicy to_usage_policy(const RuntimeConfig& config) {
    UsagePolicy policy;
    policy.session_time_limit_seconds = config.session_time_limit_seconds;
    policy.daily_message_limit = config.daily_message_limit;
    policy.daily_token_limit = config.daily_token_limit;
    policy.reset_period_seconds = config.reset_period_seconds;
    return policy;
}
} // namespace

Runtime::Runtime() : engine_(std::make_unique<InferenceEngine>()) {}

Runtime::~Runtime() = default;

std::string Runtime::load(const RuntimeConfig& config) {
    ready_ = false;
    last_error_ = RuntimeError::None;

    const std::string validation_error = validate_config(config);
    if (!validation_error.empty()) {
        last_error_ = RuntimeError::InvalidConfig;
        return validation_error;
    }

    // v0.5.0 pipeline: config validation -> quota admission -> MODEL
    // RESOLUTION -> model verification -> memory admission (inside
    // engine_->load()) -> model loading. A denied quota check returns here
    // WITHOUT ever touching resolution, verification, InferenceEngine, or
    // the model file — the same non-interference guarantee the v0.3.0
    // pipeline already had, just with one more stage inserted after it.
    usage_manager_ = std::make_unique<UsageManager>(config.usage_state_path);
    const UsagePolicy policy = to_usage_policy(config);
    const UsageDecision quota_decision = usage_manager_->check_admission(policy);
    if (!quota_decision.allowed) {
        last_error_ = RuntimeError::QuotaExceeded;
        std::string msg = "ERROR: Usage quota check failed.\n\n";
        msg += quota_decision.diagnostic;
        return msg;
    }
    // Best-effort: mark the start of this session for session-time-limit
    // accounting. Not a hard gate — the admission check above already
    // passed, so a transient failure to persist a session-start timestamp
    // should not block loading a model that was otherwise allowed.
    usage_manager_->session_start();

    // v0.5.0: resolve exactly one of model_path/model_id into an actual
    // path. resolve_model_path() is pure and read-only (see
    // src/model/model_resolver.h) — it does not verify, hash, or touch the
    // registry; it only answers "which file are we even talking about".
    // Resolving by identity is not a verification shortcut: the resolved
    // path is re-verified below through the exact same pipeline a direct
    // model_path would go through, every time.
    const ModelResolutionResult resolution =
        resolve_model_path(config.model_path, config.model_id, config.model_registry_path);
    if (resolution.status != ModelResolutionStatus::Resolved) {
        last_error_ = RuntimeError::ModelResolutionFailed;
        std::string msg = "ERROR: Model resolution failed.\n\n";
        switch (resolution.status) {
            case ModelResolutionStatus::NeitherProvided:
                msg += "Neither model_path nor model_id was provided.";
                break;
            case ModelResolutionStatus::BothProvided:
                msg += "Both model_path and model_id were provided; exactly one is required.";
                break;
            case ModelResolutionStatus::ModelIdNotFound:
                msg += "No registry entry matches model_id \"" + config.model_id + "\".";
                break;
            case ModelResolutionStatus::RegistryUnreadable:
                msg += "The model registry at \"" + config.model_registry_path + "\" could not be trusted (corrupted).";
                break;
            case ModelResolutionStatus::Resolved:
                break; // unreachable in this branch
        }
        return msg;
    }

    // A local copy of config with model_path set to the resolved path —
    // ModelRegistry::import_model() and InferenceEngine::load() both read
    // config.model_path directly and know nothing about model_id/
    // resolution; they continue to operate exactly as they did before
    // v0.5.0, just fed a path that may have come from a registry lookup
    // instead of directly from the caller.
    RuntimeConfig resolved_config = config;
    resolved_config.model_path = resolution.resolved_path;

    // Phase 3 gate: MODEL IMPORT/DISCOVERY -> GGUF VALIDATION -> MODEL
    // IDENTITY -> VERIFICATION -> REGISTRY, all performed by
    // ModelRegistry::import_model() (which composes ModelVerifier
    // internally — see model_verifier.h). This happens BEFORE
    // engine_->load() is called at all, so an invalid/unverified file
    // never reaches InferenceEngine, memory admission, or
    // llama_model_load_from_file(). A checksum is only compared when
    // config.expected_model_checksum_sha256 is non-empty (see
    // RuntimeConfig's comment); GGUF structural validation is mandatory
    // either way.
    bool verification_was_new_entry = false;
    const VerificationResult verification = ModelRegistry::import_model(
        resolved_config.model_registry_path, resolved_config.model_path,
        resolved_config.expected_model_checksum_sha256, &verification_was_new_entry);
    last_verification_diagnostic_ = verification.diagnostic;
    if (!is_verified(verification.status)) {
        last_error_ = RuntimeError::ModelVerificationFailed;
        std::string msg = "ERROR: Model verification failed.\n\n";
        msg += verification.diagnostic;
        return msg;
    }

    // v0.5.0: engine_->load() now safely releases any previously-loaded
    // model/context at its own start (see InferenceEngine::load's updated
    // comment) — calling load() a second time on this Runtime, or after an
    // explicit unload(), cannot leak the previous model's handles.
    const EngineError err = engine_->load(resolved_config);
    last_error_ = to_runtime_error(err);

    if (err != EngineError::None) {
        std::string msg = "ERROR: ";
        msg += engine_error_message(err);
        if (err == EngineError::ModelFileNotFound) {
            msg += "\n  ";
            msg += resolved_config.model_path;
        }
        if (err == EngineError::MemoryBudgetExceeded) {
            msg += "\n\n";
            msg += engine_->memory_diagnostic();
        }
        return msg;
    }

    config_ = resolved_config;
    ready_ = true;
    return std::string();
}

void Runtime::unload() {
    if (engine_) {
        engine_->unload();
    }
    ready_ = false;
    // Deliberately NOT touching usage_manager_, last_verification_diagnostic_,
    // or config_ — usage/quota state is scoped to the install, not to any
    // one loaded model (see this function's header comment), and the last
    // verification/config remain available for inspection after unload,
    // same as memory_report()/verification_report() already stay
    // inspectable after a failed load.
}

bool Runtime::is_ready() const {
    return ready_ && engine_ && engine_->is_loaded();
}

std::string Runtime::generate(const std::string& prompt, const TokenStreamCallback& on_token) {
    if (!is_ready()) {
        last_error_ = RuntimeError::NotLoaded;
        return "ERROR: Runtime is not loaded. Call load() with a valid model first.";
    }

    const UsagePolicy policy = to_usage_policy(config_);

    // Re-check admission: quota may have been exhausted by an earlier
    // generate() call in the same interactive session since load() only
    // checked once. This is a pre-check gate, separate from inference
    // itself — a denied quota check never calls engine_->generate() at all.
    if (usage_manager_) {
        const UsageDecision quota_decision = usage_manager_->check_admission(policy);
        if (!quota_decision.allowed) {
            last_error_ = RuntimeError::QuotaExceeded;
            std::string msg = "ERROR: Usage quota check failed.\n\n";
            msg += quota_decision.diagnostic;
            return msg;
        }
    }

    // Count tokens actually generated (one callback invocation per
    // generated token — see InferenceEngine::generate()) so accounting
    // reflects what actually happened, not a guess.
    int64_t tokens_generated = 0;
    const TokenStreamCallback counting_callback = [&](const std::string& piece) -> bool {
        ++tokens_generated;
        return on_token ? on_token(piece) : true;
    };

    const EngineError err = engine_->generate(prompt, counting_callback);
    last_error_ = to_runtime_error(err);

    // Record usage for whatever was actually generated, even on a
    // mid-generation error — those tokens were genuinely produced and
    // genuinely cost resources, and reflecting them here is what "usage
    // accounting reflects what actually happened" means in practice.
    if (usage_manager_) {
        usage_manager_->record_generation(policy, tokens_generated);
    }

    if (err != EngineError::None) {
        std::string msg = "ERROR: ";
        msg += engine_error_message(err);
        return msg;
    }
    return std::string();
}

void Runtime::reset_context() {
    if (engine_) {
        engine_->reset_context();
    }
}

InferenceEngine::ModelInfo Runtime::model_info() const {
    if (!engine_) {
        return InferenceEngine::ModelInfo{};
    }
    return engine_->model_info();
}

std::string Runtime::memory_report() const {
    if (!engine_) {
        return std::string();
    }
    return engine_->memory_diagnostic();
}

std::string Runtime::usage_report() const {
    if (!usage_manager_) {
        return "No usage data available yet — call load() first.";
    }
    return usage_manager_->usage_report(to_usage_policy(config_));
}

std::string Runtime::context_report() const {
    if (!engine_) {
        return "No context data available yet — call load() first.";
    }
    const InferenceEngine::ContextState state = engine_->context_state();
    if (!state.available) {
        return "No model is currently loaded.";
    }
    std::ostringstream oss;
    oss << "Context capacity: " << state.n_ctx << " tokens\n";
    oss << "Used:             " << state.n_used << " tokens\n";
    oss << "Remaining:        " << state.n_remaining << " tokens\n";
    return oss.str();
}

} // namespace syj::edgemind
