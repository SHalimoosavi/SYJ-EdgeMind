#include "core/runtime.h"

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

    // v0.3.0 pipeline: config validation -> quota admission -> memory
    // admission (inside engine_->load()) -> model loading. A denied quota
    // check returns here WITHOUT ever touching InferenceEngine, memory
    // observation, or the model file — it must not interfere with the
    // Phase 2 memory-admission pipeline in either direction.
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

    const EngineError err = engine_->load(config);
    last_error_ = to_runtime_error(err);

    if (err != EngineError::None) {
        std::string msg = "ERROR: ";
        msg += engine_error_message(err);
        if (err == EngineError::ModelFileNotFound) {
            msg += "\n  ";
            msg += config.model_path;
        }
        if (err == EngineError::MemoryBudgetExceeded) {
            msg += "\n\n";
            msg += engine_->memory_diagnostic();
        }
        return msg;
    }

    config_ = config;
    ready_ = true;
    return std::string();
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

} // namespace syj::edgemind
