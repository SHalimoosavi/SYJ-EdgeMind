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

    const EngineError err = engine_->generate(prompt, on_token);
    last_error_ = to_runtime_error(err);

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

} // namespace syj::edgemind
