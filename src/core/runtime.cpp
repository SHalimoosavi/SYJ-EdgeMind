#include "core/runtime.h"

namespace syj::edgemind {

Runtime::Runtime() : engine_(std::make_unique<InferenceEngine>()) {}

Runtime::~Runtime() = default;

std::string Runtime::load(const RuntimeConfig& config) {
    ready_ = false;

    const std::string validation_error = validate_config(config);
    if (!validation_error.empty()) {
        return validation_error;
    }

    const EngineError err = engine_->load(config);
    if (err != EngineError::None) {
        std::string msg = "ERROR: ";
        msg += engine_error_message(err);
        if (err == EngineError::ModelFileNotFound) {
            msg += "\n  ";
            msg += config.model_path;
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
        return "ERROR: Runtime is not loaded. Call load() with a valid model first.";
    }

    const EngineError err = engine_->generate(prompt, on_token);
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

} // namespace syj::edgemind
