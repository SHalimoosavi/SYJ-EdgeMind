#include "api/edge_mind_api.h"

#include <cstring>

#include "core/runtime.h"

using syj::edgemind::Runtime;
using syj::edgemind::RuntimeConfig;
using syj::edgemind::RuntimeError;

struct syj_edgemind_runtime {
    Runtime runtime;
};

namespace {

RuntimeConfig to_cpp_config(const syj_edgemind_config* c) {
    RuntimeConfig config;
    if (c == nullptr) {
        return config;
    }
    if (c->model_path != nullptr) {
        config.model_path = c->model_path;
    }
    if (c->context_size > 0) {
        config.context_size = c->context_size;
    }
    if (c->threads > 0) {
        config.threads = c->threads;
    }
    config.temperature = c->temperature;
    config.top_p = c->top_p;
    if (c->top_k >= 0) {
        config.top_k = c->top_k;
    }
    if (c->max_tokens > 0) {
        config.max_tokens = c->max_tokens;
    }
    if (c->memory_budget_mb > 0) {
        config.memory_budget_mb = c->memory_budget_mb;
    }
    if (c->safety_reserve_mb > 0) {
        config.safety_reserve_mb = c->safety_reserve_mb;
    }
    return config;
}

// Explicit, exhaustive RuntimeError -> syj_edgemind_status mapping — a
// direct enum-to-enum switch, not string inspection. Every RuntimeError
// value is handled; there is no fallback branch to fall silently through,
// so adding a new RuntimeError without updating this function is a compiler
// warning (and, with warnings-as-strict per the project's build config, a
// build failure), not a silent misclassification.
syj_edgemind_status to_c_status(RuntimeError err) {
    switch (err) {
        case RuntimeError::None:                 return SYJ_EDGEMIND_OK;
        case RuntimeError::InvalidConfig:        return SYJ_EDGEMIND_ERROR_INVALID_CONFIG;
        case RuntimeError::ModelFileNotFound:    return SYJ_EDGEMIND_ERROR_MODEL_NOT_FOUND;
        case RuntimeError::ModelLoadFailed:      return SYJ_EDGEMIND_ERROR_MODEL_LOAD_FAILED;
        case RuntimeError::ContextCreateFailed:  return SYJ_EDGEMIND_ERROR_CONTEXT_CREATE_FAILED;
        case RuntimeError::TokenizeFailed:       return SYJ_EDGEMIND_ERROR_TOKENIZE_FAILED;
        case RuntimeError::DecodeFailed:         return SYJ_EDGEMIND_ERROR_DECODE_FAILED;
        case RuntimeError::MemoryBudgetExceeded: return SYJ_EDGEMIND_ERROR_MEMORY_BUDGET_EXCEEDED;
        case RuntimeError::NotLoaded:            return SYJ_EDGEMIND_ERROR_NOT_LOADED;
    }
    return SYJ_EDGEMIND_ERROR_MODEL_LOAD_FAILED; // unreachable if RuntimeError is exhaustive above
}

} // namespace

void syj_edgemind_default_config(syj_edgemind_config* out_config) {
    if (out_config == nullptr) {
        return;
    }
    RuntimeConfig defaults;
    out_config->model_path = nullptr;
    out_config->context_size = defaults.context_size;
    out_config->threads = defaults.threads;
    out_config->temperature = defaults.temperature;
    out_config->top_p = defaults.top_p;
    out_config->top_k = defaults.top_k;
    out_config->max_tokens = defaults.max_tokens;
    out_config->memory_budget_mb = defaults.memory_budget_mb;
    out_config->safety_reserve_mb = defaults.safety_reserve_mb;
}

syj_edgemind_runtime* syj_edgemind_create(const syj_edgemind_config* config,
                                           syj_edgemind_status* out_status) {
    auto* handle = new syj_edgemind_runtime();
    handle->runtime.load(to_cpp_config(config)); // message discarded; status comes from last_error()
    const syj_edgemind_status status = to_c_status(handle->runtime.last_error());
    if (out_status != nullptr) {
        *out_status = status;
    }
    if (status == SYJ_EDGEMIND_OK) {
        return handle;
    }
    if (status == SYJ_EDGEMIND_ERROR_MEMORY_BUDGET_EXCEEDED) {
        // Kept alive on purpose — see header comment on syj_edgemind_create.
        return handle;
    }
    delete handle;
    return nullptr;
}

size_t syj_edgemind_get_memory_report(const syj_edgemind_runtime* runtime, char* out_buf, size_t buf_size) {
    if (runtime == nullptr) {
        if (buf_size > 0 && out_buf != nullptr) {
            out_buf[0] = '\0';
        }
        return 0;
    }
    const std::string report = runtime->runtime.memory_report();
    if (buf_size > 0 && out_buf != nullptr) {
        const size_t to_copy = (report.size() < buf_size - 1) ? report.size() : (buf_size - 1);
        std::memcpy(out_buf, report.data(), to_copy);
        out_buf[to_copy] = '\0';
    }
    return report.size();
}

syj_edgemind_status syj_edgemind_generate(syj_edgemind_runtime* runtime,
                                           const char* prompt,
                                           syj_edgemind_token_callback on_token,
                                           void* user_data) {
    if (runtime == nullptr) {
        return SYJ_EDGEMIND_ERROR_NOT_LOADED;
    }
    const std::string prompt_str = (prompt != nullptr) ? prompt : "";

    runtime->runtime.generate(prompt_str,
        [on_token, user_data](const std::string& piece) -> bool {
            if (on_token == nullptr) {
                return true;
            }
            return on_token(piece.c_str(), user_data) != 0;
        });

    return to_c_status(runtime->runtime.last_error());
}

void syj_edgemind_reset(syj_edgemind_runtime* runtime) {
    if (runtime != nullptr) {
        runtime->runtime.reset_context();
    }
}

int syj_edgemind_get_model_info(const syj_edgemind_runtime* runtime, syj_edgemind_model_info* out_info) {
    if (runtime == nullptr || out_info == nullptr) {
        return 1;
    }
    if (!runtime->runtime.is_ready()) {
        return 1;
    }
    const auto info = runtime->runtime.model_info();
    std::memset(out_info, 0, sizeof(*out_info));
    std::strncpy(out_info->description, info.description.c_str(), sizeof(out_info->description) - 1);
    out_info->n_params = info.n_params;
    out_info->model_size_bytes = info.model_size_bytes;
    out_info->n_ctx_train = info.n_ctx_train;
    out_info->n_ctx = info.n_ctx;
    out_info->n_threads = info.n_threads;
    return 0;
}

const char* syj_edgemind_status_message(syj_edgemind_status status) {
    switch (status) {
        case SYJ_EDGEMIND_OK:                          return "OK";
        case SYJ_EDGEMIND_ERROR_INVALID_CONFIG:        return "Invalid configuration.";
        case SYJ_EDGEMIND_ERROR_MODEL_NOT_FOUND:       return "Model file does not exist.";
        case SYJ_EDGEMIND_ERROR_MODEL_LOAD_FAILED:     return "Failed to load GGUF model.";
        case SYJ_EDGEMIND_ERROR_CONTEXT_CREATE_FAILED: return "Failed to create inference context.";
        case SYJ_EDGEMIND_ERROR_TOKENIZE_FAILED:       return "Failed to tokenize prompt.";
        case SYJ_EDGEMIND_ERROR_DECODE_FAILED:         return "Inference failed.";
        case SYJ_EDGEMIND_ERROR_NOT_LOADED:            return "Runtime is not loaded.";
        case SYJ_EDGEMIND_ERROR_MEMORY_BUDGET_EXCEEDED: return "Configuration exceeds the configured memory budget.";
    }
    return "Unknown status.";
}

void syj_edgemind_destroy(syj_edgemind_runtime* runtime) {
    delete runtime;
}
