#ifndef SYJ_EDGEMIND_API_EDGE_MIND_API_H
#define SYJ_EDGEMIND_API_EDGE_MIND_API_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// The single stable C API boundary between SYJ EdgeMind's core (src/core,
// src/inference, src/tokenizer, src/context — all C++, all built on
// llama.cpp) and platform wrappers (Windows CLI today; a future iOS
// Objective-C++/Swift bridge). Platform code must use only this header and
// must never include llama.h or any SYJ EdgeMind internal header directly —
// that is what "llama.cpp internals are not leaked" (Phase 1 spec §15/§25)
// means in practice.

typedef struct syj_edgemind_runtime syj_edgemind_runtime;

// Error codes mirror syj::edgemind::EngineError plus a config-validation
// case; see edge_mind_api.cpp for the mapping. 0 always means success.
typedef enum syj_edgemind_status {
    SYJ_EDGEMIND_OK = 0,
    SYJ_EDGEMIND_ERROR_INVALID_CONFIG = 1,
    SYJ_EDGEMIND_ERROR_MODEL_NOT_FOUND = 2,
    SYJ_EDGEMIND_ERROR_MODEL_LOAD_FAILED = 3,
    SYJ_EDGEMIND_ERROR_CONTEXT_CREATE_FAILED = 4,
    SYJ_EDGEMIND_ERROR_TOKENIZE_FAILED = 5,
    SYJ_EDGEMIND_ERROR_DECODE_FAILED = 6,
    SYJ_EDGEMIND_ERROR_NOT_LOADED = 7,
    SYJ_EDGEMIND_ERROR_MEMORY_BUDGET_EXCEEDED = 8,
    SYJ_EDGEMIND_ERROR_QUOTA_EXCEEDED = 9,
} syj_edgemind_status;

typedef struct syj_edgemind_config {
    const char* model_path;
    int32_t context_size;   // e.g. 1024; see docs/memory-model.md profiles
    int32_t threads;
    float temperature;
    float top_p;
    int32_t top_k;
    int32_t max_tokens;
    int64_t memory_budget_mb;   // Phase 2; 0 means "use SYJ EdgeMind's default"
    int64_t safety_reserve_mb;  // Phase 2; 0 means "use SYJ EdgeMind's default"

    // v0.3.0 usage/quota limits — 0 means "disabled" for each of the three
    // limits (this is a genuine value, not "use the default", since the
    // default itself is disabled/unlimited; see RuntimeConfig).
    int64_t session_time_limit_seconds;
    int64_t daily_message_limit;
    int64_t daily_token_limit;
    int64_t reset_period_seconds; // 0 means "use SYJ EdgeMind's default" (86400)
    const char* usage_state_path; // NULL means "use SYJ EdgeMind's default"
} syj_edgemind_config;

// Fills `out_config` with SYJ EdgeMind's built-in Phase 1 defaults
// (conservative context, sane sampling values). Callers should call this
// first, then override only the fields they care about (typically just
// model_path).
void syj_edgemind_default_config(syj_edgemind_config* out_config);

// Creates and loads a runtime. Returns NULL on failure; if out_status is
// non-NULL, it is set to the specific failure reason.
//
// SPECIAL CASE: if the failure is SYJ_EDGEMIND_ERROR_MEMORY_BUDGET_EXCEEDED
// or SYJ_EDGEMIND_ERROR_QUOTA_EXCEEDED, the returned pointer is NOT NULL —
// the runtime handle is kept alive specifically so the caller can retrieve
// the detailed diagnostic (syj_edgemind_get_memory_report() or
// syj_edgemind_get_usage_report() respectively) before calling
// syj_edgemind_destroy(). is_ready()-equivalent operations (generate, model
// info) will still fail with SYJ_EDGEMIND_ERROR_NOT_LOADED on this handle;
// it exists only to carry the diagnostic out. Every other failure reason
// returns NULL as usual.
syj_edgemind_runtime* syj_edgemind_create(const syj_edgemind_config* config,
                                           syj_edgemind_status* out_status);

// Streams a response to `prompt`. `on_token` is invoked once per generated
// UTF-8 text fragment; `user_data` is passed through unchanged. Return 0
// from `on_token` to request early stop, non-zero to continue — mirroring
// the C++ TokenStreamCallback's bool-return convention without requiring
// C callers to know about std::function.
typedef int (*syj_edgemind_token_callback)(const char* piece, void* user_data);

syj_edgemind_status syj_edgemind_generate(syj_edgemind_runtime* runtime,
                                           const char* prompt,
                                           syj_edgemind_token_callback on_token,
                                           void* user_data);

// Clears the context/KV cache so a new prompt can start fresh without
// reloading the model.
void syj_edgemind_reset(syj_edgemind_runtime* runtime);

// Basic, actually-observed model facts (see InferenceEngine::ModelInfo).
typedef struct syj_edgemind_model_info {
    char description[256];
    uint64_t n_params;
    uint64_t model_size_bytes;
    int32_t n_ctx_train;
    int32_t n_ctx;
    int32_t n_threads;
} syj_edgemind_model_info;

// Returns 0 on success, non-zero if runtime is NULL/not loaded.
int syj_edgemind_get_model_info(const syj_edgemind_runtime* runtime, syj_edgemind_model_info* out_info);

// Writes the Phase 2 memory-budget diagnostic (human-readable, matches the
// STATUS: SAFE/UNSAFE format in docs/memory-model.md) from the most recent
// load attempt into `out_buf` (truncated to fit `buf_size`, always
// NUL-terminated if buf_size > 0). Returns the number of bytes that would
// have been written (excluding the NUL terminator), like snprintf — 0 if no
// report is available yet.
size_t syj_edgemind_get_memory_report(const syj_edgemind_runtime* runtime, char* out_buf, size_t buf_size);

// Writes the v0.3.0 usage/quota diagnostic (current usage, remaining quota,
// configured limits, reset time — matching the STATUS: ALLOWED/DENIED
// format in docs/usage-model.md) into `out_buf`, with the same
// truncation/NUL-termination/snprintf-like-return-value contract as
// syj_edgemind_get_memory_report(). Safe to call even on a handle returned
// due to SYJ_EDGEMIND_ERROR_QUOTA_EXCEEDED (see syj_edgemind_create).
size_t syj_edgemind_get_usage_report(const syj_edgemind_runtime* runtime, char* out_buf, size_t buf_size);

// Human-readable string for a status code. Owned by SYJ EdgeMind; do not free.
const char* syj_edgemind_status_message(syj_edgemind_status status);

void syj_edgemind_destroy(syj_edgemind_runtime* runtime);

#ifdef __cplusplus
}
#endif

#endif // SYJ_EDGEMIND_API_EDGE_MIND_API_H
