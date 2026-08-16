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

    if (config.memory_budget_mb < RuntimeConfig::SYJ_EDGEMIND_MIN_MEMORY_BUDGET_MB ||
        config.memory_budget_mb > RuntimeConfig::SYJ_EDGEMIND_MAX_MEMORY_BUDGET_MB) {
        std::ostringstream oss;
        oss << "memory_budget_mb " << config.memory_budget_mb << " is out of the supported range ["
            << RuntimeConfig::SYJ_EDGEMIND_MIN_MEMORY_BUDGET_MB << ", "
            << RuntimeConfig::SYJ_EDGEMIND_MAX_MEMORY_BUDGET_MB << "].";
        return oss.str();
    }

    if (config.safety_reserve_mb < 0) {
        return "safety_reserve_mb must be non-negative.";
    }

    if (config.safety_reserve_mb >= config.memory_budget_mb) {
        return "safety_reserve_mb must be less than memory_budget_mb (nothing would ever fit).";
    }

    if (config.session_time_limit_seconds < 0 || config.session_time_limit_seconds > RuntimeConfig::SYJ_EDGEMIND_MAX_SANE_LIMIT) {
        return "session_time_limit_seconds must be non-negative and within the supported sane range (0 disables it).";
    }
    if (config.daily_message_limit < 0 || config.daily_message_limit > RuntimeConfig::SYJ_EDGEMIND_MAX_SANE_LIMIT) {
        return "daily_message_limit must be non-negative and within the supported sane range (0 disables it).";
    }
    if (config.daily_token_limit < 0 || config.daily_token_limit > RuntimeConfig::SYJ_EDGEMIND_MAX_SANE_LIMIT) {
        return "daily_token_limit must be non-negative and within the supported sane range (0 disables it).";
    }
    if (config.reset_period_seconds < RuntimeConfig::SYJ_EDGEMIND_MIN_RESET_PERIOD_SECONDS) {
        std::ostringstream oss;
        oss << "reset_period_seconds must be at least " << RuntimeConfig::SYJ_EDGEMIND_MIN_RESET_PERIOD_SECONDS
            << " seconds (a shorter reset window is not meaningful).";
        return oss.str();
    }
    if (config.usage_state_path.empty()) {
        return "usage_state_path must not be empty.";
    }

    if (config.model_registry_path.empty()) {
        return "model_registry_path must not be empty.";
    }
    // expected_model_checksum_sha256 is intentionally NOT format-validated
    // here (e.g. requiring exactly 64 hex characters) — an empty string is
    // the common, valid "no checksum configured" case, and a malformed
    // non-empty value simply fails to match during verification
    // (VerificationStatus::ChecksumMismatch), which is itself a safe,
    // fail-closed outcome. Rejecting it at config-validation time would add
    // a second place that has to agree with ModelVerifier's comparison
    // logic for no real safety benefit.

    return std::string();
}

} // namespace syj::edgemind
