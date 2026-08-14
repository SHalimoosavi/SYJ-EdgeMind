#include "core/config.h"

#include <sstream>

namespace syj::edgemind {

std::string validate_config(const RuntimeConfig& config) {
    if (config.model_path.empty()) {
        return "Model path must not be empty.";
    }

    if (config.context_size < RuntimeConfig::SYJ_EDGEMIND_MIN_CONTEXT ||
        config.context_size > RuntimeConfig::SYJ_EDGEMIND_MAX_CONTEXT) {
        std::ostringstream oss;
        oss << "Context size " << config.context_size
            << " is out of the supported range ["
            << RuntimeConfig::SYJ_EDGEMIND_MIN_CONTEXT << ", "
            << RuntimeConfig::SYJ_EDGEMIND_MAX_CONTEXT << "]. "
            << "Formal memory-budget-based context limits arrive in Phase 2; "
            << "this is only a basic sanity bound.";
        return oss.str();
    }

    if (config.threads <= 0) {
        return "Thread count must be a positive integer.";
    }

    if (config.temperature < 0.0f) {
        return "Temperature must be non-negative.";
    }

    if (config.top_p <= 0.0f || config.top_p > 1.0f) {
        return "top_p must be in the range (0.0, 1.0].";
    }

    if (config.top_k < 0) {
        return "top_k must be non-negative (0 disables top-k filtering).";
    }

    if (config.max_tokens <= 0) {
        return "max_tokens must be a positive integer.";
    }

    return std::string();
}

} // namespace syj::edgemind
