#include "api/edge_mind_api.h"

#include <cstring>

#include "core/runtime.h"

using syj::edgemind::Runtime;
using syj::edgemind::RuntimeConfig;

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
    return config;
}

// Best-effort classification of a Runtime::load()/generate() error string
// into a stable status code for C callers. The authoritative mapping lives
// in InferenceEngine/EngineError; this exists only because the C API must
// not leak the C++ EngineError enum or std::string across the boundary.
syj_edgemind_status classify_error(const std::string& msg) {
    if (msg.empty()) {
        return SYJ_EDGEMIND_OK;
    }
    if (msg.find("Model path must not be empty") != std::string::npos ||
        msg.find("Context size") != std::string::npos ||
        msg.find("Thread count") != std::string::npos ||
        msg.find("Temperature") != std::string::npos ||
        msg.find("top_p") != std::string::npos ||
        msg.find("top_k") != std::string::npos ||
        msg.find("max_tokens") != std::string::npos) {
        return SYJ_EDGEMIND_ERROR_INVALID_CONFIG;
    }
    if (msg.find("does not exist") != std::string::npos) {
        return SYJ_EDGEMIND_ERROR_MODEL_NOT_FOUND;
    }
    if (msg.find("Failed to load GGUF model") != std::string::npos) {
        return SYJ_EDGEMIND_ERROR_MODEL_LOAD_FAILED;
    }
    if (msg.find("Failed to create inference context") != std::string::npos) {
        return SYJ_EDGEMIND_ERROR_CONTEXT_CREATE_FAILED;
    }
    if (msg.find("Failed to tokenize") != std::string::npos) {
        return SYJ_EDGEMIND_ERROR_TOKENIZE_FAILED;
    }
    if (msg.find("Inference failed") != std::string::npos) {
        return SYJ_EDGEMIND_ERROR_DECODE_FAILED;
    }
    if (msg.find("not loaded") != std::string::npos) {
        return SYJ_EDGEMIND_ERROR_NOT_LOADED;
    }
    return SYJ_EDGEMIND_ERROR_MODEL_LOAD_FAILED; // conservative fallback, never silently OK
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
}

syj_edgemind_runtime* syj_edgemind_create(const syj_edgemind_config* config,
                                           syj_edgemind_status* out_status) {
    auto* handle = new syj_edgemind_runtime();
    const std::string err = handle->runtime.load(to_cpp_config(config));
    const syj_edgemind_status status = classify_error(err);
    if (out_status != nullptr) {
        *out_status = status;
    }
    if (status != SYJ_EDGEMIND_OK) {
        delete handle;
        return nullptr;
    }
    return handle;
}

syj_edgemind_status syj_edgemind_generate(syj_edgemind_runtime* runtime,
                                           const char* prompt,
                                           syj_edgemind_token_callback on_token,
                                           void* user_data) {
    if (runtime == nullptr) {
        return SYJ_EDGEMIND_ERROR_NOT_LOADED;
    }
    const std::string prompt_str = (prompt != nullptr) ? prompt : "";

    const std::string err = runtime->runtime.generate(prompt_str,
        [on_token, user_data](const std::string& piece) -> bool {
            if (on_token == nullptr) {
                return true;
            }
            return on_token(piece.c_str(), user_data) != 0;
        });

    return classify_error(err);
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
    }
    return "Unknown status.";
}

void syj_edgemind_destroy(syj_edgemind_runtime* runtime) {
    delete runtime;
}
