#ifndef SYJ_EDGEMIND_INFERENCE_INFERENCE_ENGINE_H
#define SYJ_EDGEMIND_INFERENCE_INFERENCE_ENGINE_H

#include <functional>
#include <memory>
#include <string>

#include "core/config.h"
#include "context/context_manager.h"
#include "tokenizer/tokenizer.h"

struct llama_model;
struct llama_context;

namespace syj::edgemind {

enum class EngineError {
    None,
    ModelFileNotFound,
    ModelLoadFailed,
    ContextCreateFailed,
    TokenizeFailed,
    DecodeFailed,
    MemoryBudgetExceeded,
};

const char* engine_error_message(EngineError err);

// Called once per generated token piece during streaming generation.
// Return false from the callback to request early stop (e.g. user
// interruption), matching the Phase 1 spec §10 termination conditions.
using TokenStreamCallback = std::function<bool(const std::string& piece)>;

// Owns the llama.cpp model + context lifecycle end-to-end:
//   backend init -> model load -> context create -> tokenize -> decode ->
//   sample -> stream -> shutdown -> resource release.
// RAII: llama_free/llama_model_free/llama_backend_free are called from the
// destructor; there is no manual "destroy" the caller must remember to call
// beyond letting the object go out of scope (the C API in edge_mind_api.h
// wraps this for callers that need explicit create/destroy semantics).
class InferenceEngine {
public:
    InferenceEngine();
    ~InferenceEngine();

    InferenceEngine(const InferenceEngine&) = delete;
    InferenceEngine& operator=(const InferenceEngine&) = delete;

    // Loads the model at config.model_path and creates an inference context
    // sized config.context_size, using config.threads for both generation
    // and batch processing. Returns EngineError::None on success.
    //
    // Does not throw. All failure modes are reported via the return value,
    // per the Phase 1 spec §14 ("do not swallow errors, do not continue with
    // an invalid runtime").
    EngineError load(const RuntimeConfig& config);

    // Tokenizes `prompt`, runs it through the context, then streams
    // generated tokens to `on_token` one piece at a time until: EOS/EOG,
    // config.max_tokens is reached, the context fills, on_token returns
    // false, or a decode error occurs (reported via the return value).
    EngineError generate(const std::string& prompt, const TokenStreamCallback& on_token);

    // Clears the KV cache / context accounting so a new, unrelated prompt
    // can be generated without reloading the model. Does not reload weights.
    void reset_context();

    bool is_loaded() const { return model_ != nullptr && ctx_ != nullptr; }

    // Basic, ACTUALLY-observed (not estimated) facts about the loaded model,
    // for the CLI's /info command and Phase 1's "memory observation"
    // requirement (§21). This is not the Phase 2 memory-budget engine.
    struct ModelInfo {
        std::string description;
        uint64_t n_params = 0;
        uint64_t model_size_bytes = 0;
        int32_t n_ctx_train = 0;
        int32_t n_ctx = 0;
        int32_t n_threads = 0;
    };
    ModelInfo model_info() const;

    // The full Phase 2 memory-budget diagnostic from the most recent load()
    // attempt (populated whether the load succeeded or was rejected for
    // exceeding the budget). Empty if load() has never been called.
    const std::string& memory_diagnostic() const { return memory_diagnostic_; }

private:
    llama_model* model_ = nullptr;
    llama_context* ctx_ = nullptr;
    std::unique_ptr<Tokenizer> tokenizer_;
    std::unique_ptr<ContextManager> context_manager_;
    RuntimeConfig config_;
    bool backend_initialized_ = false;
    std::string memory_diagnostic_;
};

} // namespace syj::edgemind

#endif // SYJ_EDGEMIND_INFERENCE_INFERENCE_ENGINE_H
