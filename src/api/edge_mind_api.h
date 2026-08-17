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
//
// ---------------------------------------------------------------------
// ABI COMPATIBILITY (read before adding or reordering any struct field)
// ---------------------------------------------------------------------
// syj_edgemind_config and syj_edgemind_runtime are plain structs/opaque
// pointers with no struct_size or version field of their own — this API
// has never had a formal versioning scheme. That makes it:
//
//   SOURCE-compatible: any caller that recompiles against this header
//   after a field is appended is unaffected — untouched fields keep their
//   meaning, and syj_edgemind_default_config() zero-initializes the new
//   field to its "unset" default.
//
//   NOT safely binary-ABI-stable across versions: a caller compiled
//   against an OLDER version of this header, then dynamically linked
//   against a NEWER shared library, allocated syj_edgemind_config using
//   the OLD (smaller) sizeof(). If the newer library reads a field that
//   was appended after that caller was built, it reads past the end of
//   memory the caller never allocated for that struct — undefined
//   behavior, not simply "sees the default". This is not yet a real
//   distribution scenario (the only current consumer, src/cli/main.cpp,
//   is always built in the same CMake invocation as this library), but it
//   is exactly the failure mode the Windows/iOS platform wrappers this
//   header exists for would eventually hit.
//
//   Retrofitting a struct_size/version field as the FIRST member of
//   syj_edgemind_config was considered and rejected for v0.5.0: it would
//   shift every existing field's offset, which would itself silently
//   break the one real external artifact that already exists
//   (v0.4.0-alpha), the opposite of what this note is trying to prevent.
//
// Given that, the smallest available mechanism (no struct layout change,
// nothing speculative) is: syj_edgemind_abi_version() below, an explicit,
// checkable integer a dynamically-linked caller can compare against what
// it was compiled with, BEFORE trusting any field added after the version
// it recognizes. New fields must always be APPENDED to the end of
// syj_edgemind_config (never inserted/reordered) and
// SYJ_EDGEMIND_ABI_VERSION bumped in the same change.
#define SYJ_EDGEMIND_ABI_VERSION 2 // 1 = v0.4.0-alpha's struct shape (no model_id); 2 = adds model_id (v0.5.0)

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
    // Phase 3: the model file failed verification (missing, not a regular
    // file, empty, invalid GGUF magic/version/metadata, or checksum
    // mismatch — see syj_edgemind_get_verification_report() for which).
    // Evaluated before memory admission and before the model is ever
    // handed to llama.cpp.
    SYJ_EDGEMIND_ERROR_MODEL_VERIFICATION_FAILED = 10,
    // v0.5.0: resolving model_path/model_id into an actual path failed —
    // neither was set, both were set, model_id had no matching registry
    // entry, or the registry could not be trusted. See
    // syj::edgemind::ModelResolutionStatus (src/model/model_resolver.h,
    // internal — not exposed through this C API) for the granular reason;
    // this status only needs to answer "did resolution succeed", same
    // posture as SYJ_EDGEMIND_ERROR_MODEL_VERIFICATION_FAILED.
    SYJ_EDGEMIND_ERROR_MODEL_RESOLUTION_FAILED = 11,
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

    // Phase 3: model verification. NULL/empty means "no checksum
    // configured" — GGUF structural verification is still mandatory
    // either way (see syj_edgemind_create's verification behavior).
    const char* expected_model_checksum_sha256;
    const char* model_registry_path; // NULL means "use SYJ EdgeMind's default"

    // v0.5.0. APPENDED FIELD — see the ABI COMPATIBILITY note at the top
    // of this file before touching this struct further. NULL/empty means
    // "resolve via model_path instead" — exactly one of model_path/
    // model_id must be set (see syj::edgemind::validate_config). Existing
    // callers that only ever set model_path are completely unaffected:
    // this field defaults to the same "unset" state
    // syj_edgemind_default_config() already zero-initializes every
    // pointer field to.
    const char* model_id;
} syj_edgemind_config;

// Fills `out_config` with SYJ EdgeMind's built-in Phase 1 defaults
// (conservative context, sane sampling values). Callers should call this
// first, then override only the fields they care about (typically just
// model_path).
void syj_edgemind_default_config(syj_edgemind_config* out_config);

// Returns the ABI version this library was built with (see the ABI
// COMPATIBILITY note at the top of this file). A caller dynamically
// linking against this library — as opposed to being rebuilt alongside it
// in the same CMake invocation — should compare this against the
// SYJ_EDGEMIND_ABI_VERSION it was compiled with before trusting any
// syj_edgemind_config field introduced after the version it recognizes.
int32_t syj_edgemind_abi_version(void);

// Creates and loads a runtime. Returns NULL on failure; if out_status is
// non-NULL, it is set to the specific failure reason.
//
// SPECIAL CASE: if the failure is SYJ_EDGEMIND_ERROR_MEMORY_BUDGET_EXCEEDED,
// SYJ_EDGEMIND_ERROR_QUOTA_EXCEEDED, or
// SYJ_EDGEMIND_ERROR_MODEL_VERIFICATION_FAILED, the returned pointer is NOT
// NULL — the runtime handle is kept alive specifically so the caller can
// retrieve the detailed diagnostic (syj_edgemind_get_memory_report(),
// syj_edgemind_get_usage_report(), or syj_edgemind_get_verification_report()
// respectively) before calling syj_edgemind_destroy(). is_ready()-equivalent
// operations (generate, model info) will still fail with
// SYJ_EDGEMIND_ERROR_NOT_LOADED on this handle; it exists only to carry the
// diagnostic out. SYJ_EDGEMIND_ERROR_MODEL_RESOLUTION_FAILED returns NULL
// (there is no path/model to report a diagnostic about yet — resolution
// happens before verification even begins). Every other failure reason
// returns NULL as usual.
syj_edgemind_runtime* syj_edgemind_create(const syj_edgemind_config* config,
                                           syj_edgemind_status* out_status);

// v0.5.0: releases the loaded model/context, returning `runtime` to an
// unloaded state (subsequent syj_edgemind_generate()/model-info calls fail
// with SYJ_EDGEMIND_ERROR_NOT_LOADED until a fresh syj_edgemind_create()).
// Idempotent — safe to call on a handle with nothing loaded, and safe to
// call more than once. Does not destroy `runtime` itself — that remains
// syj_edgemind_destroy()'s job. There is currently no "reload a new model
// into an existing handle" entry point at the C API layer; that would be a
// deliberate, separate future addition (see docs/model-registry.md), not
// something this function implies.
void syj_edgemind_unload(syj_edgemind_runtime* runtime);

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

// Writes the Phase 3 model-verification diagnostic (STATUS: VERIFIED/
// REJECTED, plus architecture/quantization/identity when available — see
// ModelVerifier::verify's diagnostic format) from the most recent load
// attempt into `out_buf`, with the same truncation/NUL-termination/
// snprintf-like-return-value contract as syj_edgemind_get_memory_report().
// Safe to call on a handle returned due to
// SYJ_EDGEMIND_ERROR_MODEL_VERIFICATION_FAILED (see syj_edgemind_create).
size_t syj_edgemind_get_verification_report(const syj_edgemind_runtime* runtime, char* out_buf, size_t buf_size);

// v0.5.0: writes a human-readable listing of every entry in the model
// registry at `registry_path` (NULL means "use SYJ EdgeMind's default",
// same default as syj_edgemind_config::model_registry_path), one line per
// entry (model_id, display_name, architecture, quantization, verification
// status), with the same truncation/NUL-termination/snprintf-like-
// return-value contract as syj_edgemind_get_memory_report(). This is a
// static registry query — it does NOT require or touch a
// syj_edgemind_runtime handle, and is usable before any model is loaded
// (see docs/model-registry.md). Writes nothing (returns 0) if the registry
// doesn't exist yet or is empty; a corrupted registry also currently
// writes nothing rather than a distinct error — this function intentionally
// does not expose that distinction (see syj_edgemind_status for a richer
// per-model-load status if it matters for a specific model you're trying
// to load, as opposed to browsing what's registered).
size_t syj_edgemind_list_models(const char* registry_path, char* out_buf, size_t buf_size);

// Human-readable string for a status code. Owned by SYJ EdgeMind; do not free.
const char* syj_edgemind_status_message(syj_edgemind_status status);

void syj_edgemind_destroy(syj_edgemind_runtime* runtime);

#ifdef __cplusplus
}
#endif

#endif // SYJ_EDGEMIND_API_EDGE_MIND_API_H
