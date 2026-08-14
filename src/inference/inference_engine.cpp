#include "inference/inference_engine.h"

#include <fstream>
#include <string>
#include <utility>
#include <vector>

#include "llama.h"
#include "inference/sampler.h"

namespace syj::edgemind {

const char* engine_error_message(EngineError err) {
    switch (err) {
        case EngineError::None:
            return "No error.";

        case EngineError::ModelFileNotFound:
            return "Model file does not exist.";

        case EngineError::ModelLoadFailed:
            return "Failed to load GGUF model.";

        case EngineError::ContextCreateFailed:
            return "Failed to create inference context.";

        case EngineError::TokenizeFailed:
            return "Failed to tokenize prompt.";

        case EngineError::DecodeFailed:
            return "Inference failed.";
    }

    return "Unknown error.";
}

namespace {

bool file_exists(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    return f.good();
}

} // namespace

InferenceEngine::InferenceEngine() = default;

InferenceEngine::~InferenceEngine() {
    /*
     * Reverse of acquisition order:
     *
     * tokenizer
     * context manager
     * llama context
     * llama model
     * llama backend
     */
    tokenizer_.reset();
    context_manager_.reset();

    if (ctx_ != nullptr) {
        llama_free(ctx_);
        ctx_ = nullptr;
    }

    if (model_ != nullptr) {
        llama_model_free(model_);
        model_ = nullptr;
    }

    /*
     * Phase 1 CLI uses one InferenceEngine per process.
     *
     * A future multi-instance runtime should move backend ownership to a
     * process-wide reference-counted lifecycle.
     */
    if (backend_initialized_) {
        llama_backend_free();
        backend_initialized_ = false;
    }
}

EngineError InferenceEngine::load(
    const RuntimeConfig& config) {

    if (!file_exists(config.model_path)) {
        return EngineError::ModelFileNotFound;
    }

    if (!backend_initialized_) {
        llama_backend_init();
        backend_initialized_ = true;
    }

    /*
     * Use llama.cpp's native model defaults.
     *
     * use_mmap defaults to true and is intentionally left under
     * llama.cpp's ownership. SYJ EdgeMind does not implement a
     * second/custom mmap layer.
     */
    llama_model_params model_params =
        llama_model_default_params();

    model_ = llama_model_load_from_file(
        config.model_path.c_str(),
        model_params);

    if (model_ == nullptr) {
        return EngineError::ModelLoadFailed;
    }

    /*
     * Phase 1 is an instruction/chat runtime.
     *
     * Verify that the loaded GGUF actually contains a chat template.
     * This prevents the runtime from silently inventing a prompt format
     * for an instruction model.
     */
    const char* chat_template =
        llama_model_chat_template(
            model_,
            nullptr);

    if (chat_template == nullptr ||
        *chat_template == '\0') {

        llama_model_free(model_);
        model_ = nullptr;

        return EngineError::ModelLoadFailed;
    }

    /*
     * Create the inference context only after the model has passed the
     * chat-template requirement.
     */
    llama_context_params ctx_params =
        llama_context_default_params();

    ctx_params.n_ctx =
        static_cast<uint32_t>(
            config.context_size);

    ctx_params.n_batch =
        static_cast<uint32_t>(
            config.context_size);

    ctx_params.n_threads =
        config.threads;

    ctx_params.n_threads_batch =
        config.threads;

    ctx_ =
        llama_init_from_model(
            model_,
            ctx_params);

    if (ctx_ == nullptr) {
        llama_model_free(model_);
        model_ = nullptr;

        return EngineError::ContextCreateFailed;
    }

    /*
     * Obtain the vocabulary owned by the loaded model.
     */
    const llama_vocab* vocab =
        llama_model_get_vocab(model_);

    tokenizer_ =
        std::make_unique<Tokenizer>(vocab);

    /*
     * Use llama.cpp's ACTUAL context size rather than assuming the
     * requested configuration was accepted unchanged.
     */
    context_manager_ =
        std::make_unique<ContextManager>(
            static_cast<int32_t>(
                llama_n_ctx(ctx_)));

    config_ = config;

    return EngineError::None;
}

void InferenceEngine::reset_context() {
    if (ctx_ != nullptr) {
        llama_memory_t mem =
            llama_get_memory(ctx_);

        if (mem != nullptr) {
            llama_memory_clear(
                mem,
                /*data=*/true);
        }
    }

    if (context_manager_) {
        context_manager_->reset();
    }
}

EngineError InferenceEngine::generate(
    const std::string& prompt,
    const TokenStreamCallback& on_token) {

    if (!is_loaded() ||
        !tokenizer_ ||
        !context_manager_) {

        return EngineError::ContextCreateFailed;
    }

    /*
     * ---------------------------------------------------------------
     * STEP 1 — Apply the model's native chat template
     * ---------------------------------------------------------------
     *
     * Never tokenize the raw user prompt directly for an instruction
     * model. The GGUF's tokenizer.chat_template defines the exact
     * conversation representation expected by the model.
     */
    std::string formatted_prompt;

    std::vector<std::pair<std::string, std::string>> messages;
    messages.emplace_back("user", prompt);

    if (!Tokenizer::apply_chat_template(
            model_,
            messages,
            /*add_assistant_prompt=*/true,
            formatted_prompt)) {

        return EngineError::TokenizeFailed;
    }

    /*
     * ---------------------------------------------------------------
     * STEP 2 — Tokenize the formatted prompt
     * ---------------------------------------------------------------
     *
     * The chat template already emitted the required control tokens,
     * including the assistant-generation prefix.
     *
     * Therefore:
     *
     *     add_special = false
     *
     * prevents llama.cpp from adding another BOS/special-token layer.
     */
    std::vector<llama_token> prompt_tokens;

    if (!tokenizer_->tokenize(
            formatted_prompt,
            /*add_special=*/false,
            /*parse_special=*/true,
            prompt_tokens)) {

        return EngineError::TokenizeFailed;
    }

    if (prompt_tokens.empty()) {
        return EngineError::TokenizeFailed;
    }

    /*
     * Context accounting must use the formatted prompt token count,
     * not the original raw user prompt length.
     */
    if (!context_manager_->can_accept(
            static_cast<int32_t>(
                prompt_tokens.size()))) {

        /*
         * Phase 1 behavior:
         *
         * The context is already full before generation starts.
         * Stop cleanly instead of silently truncating the prompt.
         */
        return EngineError::None;
    }

    /*
     * ---------------------------------------------------------------
     * STEP 3 — Decode the complete formatted prompt
     * ---------------------------------------------------------------
     */
    llama_batch prompt_batch =
        llama_batch_get_one(
            prompt_tokens.data(),
            static_cast<int32_t>(
                prompt_tokens.size()));

    if (llama_decode(
            ctx_,
            prompt_batch) != 0) {

        return EngineError::DecodeFailed;
    }

    context_manager_->consume(
        static_cast<int32_t>(
            prompt_tokens.size()));

    /*
     * ---------------------------------------------------------------
     * STEP 4 — Configure sampling
     * ---------------------------------------------------------------
     */
    SamplingParams sp;

    sp.temperature =
        config_.temperature;

    sp.top_p =
        config_.top_p;

    sp.top_k =
        config_.top_k;

    Sampler sampler(sp);

    if (!sampler.valid()) {
        return EngineError::DecodeFailed;
    }

    /*
     * The logits corresponding to the next generated token are attached
     * to the final token of the prompt batch.
     */
    int32_t last_batch_idx =
        static_cast<int32_t>(
            prompt_tokens.size()) - 1;

    /*
     * ---------------------------------------------------------------
     * STEP 5 — Autoregressive generation
     * ---------------------------------------------------------------
     */
    for (int generated = 0;
         generated < config_.max_tokens;
         ++generated) {

        const llama_token next =
            sampler.sample(
                ctx_,
                last_batch_idx);

        /*
         * Model-specific EOS/EOG handling.
         */
        if (tokenizer_->is_eog(next)) {
            break;
        }

        /*
         * Convert the sampled token into an incremental UTF-8 piece.
         */
        const std::string piece =
            tokenizer_->token_to_piece(
                next,
                /*special=*/false);

        /*
         * Stream the token immediately.
         *
         * Returning false allows the caller to terminate generation.
         */
        if (on_token &&
            !on_token(piece)) {

            break;
        }

        /*
         * Never exceed the configured/actual context capacity.
         */
        if (!context_manager_->can_accept(1)) {
            break;
        }

        llama_token next_mut = next;

        /*
         * Decode one generated token at a time.
         */
        llama_batch step_batch =
            llama_batch_get_one(
                &next_mut,
                1);

        if (llama_decode(
                ctx_,
                step_batch) != 0) {

            return EngineError::DecodeFailed;
        }

        context_manager_->consume(1);

        /*
         * A single-token batch has its logits at index zero.
         */
        last_batch_idx = 0;
    }

    return EngineError::None;
}

InferenceEngine::ModelInfo
InferenceEngine::model_info() const {

    ModelInfo info;

    if (model_ == nullptr ||
        ctx_ == nullptr) {

        return info;
    }

    char desc_buf[256] = {0};

    llama_model_desc(
        model_,
        desc_buf,
        sizeof(desc_buf));

    info.description = desc_buf;

    info.n_params =
        llama_model_n_params(model_);

    info.model_size_bytes =
        llama_model_size(model_);

    info.n_ctx_train =
        llama_model_n_ctx_train(model_);

    info.n_ctx =
        static_cast<int32_t>(
            llama_n_ctx(ctx_));

    info.n_threads =
        llama_n_threads(ctx_);

    return info;
}

} // namespace syj::edgemind
