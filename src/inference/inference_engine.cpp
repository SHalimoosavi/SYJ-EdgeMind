#include "inference/inference_engine.h"

#include <fstream>
#include <optional>
#include <thread>

#include "llama.h"
#include "inference/sampler.h"
#include "memory/memory_budget.h"
#include "memory/memory_estimator.h"
#include "memory/memory_observer.h"

namespace syj::edgemind {

const char* engine_error_message(EngineError err) {
    switch (err) {
        case EngineError::None:               return "No error.";
        case EngineError::ModelFileNotFound:   return "Model file does not exist.";
        case EngineError::ModelLoadFailed:     return "Failed to load GGUF model.";
        case EngineError::ContextCreateFailed: return "Failed to create inference context.";
        case EngineError::TokenizeFailed:      return "Failed to tokenize prompt.";
        case EngineError::DecodeFailed:        return "Inference failed.";
        case EngineError::MemoryBudgetExceeded: return "Configuration exceeds the configured memory budget.";
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
    unload();

    // NOTE: assumes a single InferenceEngine is used at a time within the
    // process (true for the Phase 1 CLI). A multi-instance runtime would
    // need a process-wide refcount around llama_backend_init/free instead
    // of tying it to one engine's lifetime.
    if (backend_initialized_) {
        llama_backend_free();
        backend_initialized_ = false;
    }
}

void InferenceEngine::unload() {
    // Reverse of the acquisition order documented in inference_engine.h.
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

    config_ = RuntimeConfig{};
}

EngineError InferenceEngine::load(const RuntimeConfig& config) {
    // Every load attempt starts from a clean model/context state. The
    // backend itself remains initialized so repeated loads do not repeatedly
    // initialize/free llama.cpp's process-wide backend.
    unload();
    memory_diagnostic_.clear();

    if (!file_exists(config.model_path)) {
        return EngineError::ModelFileNotFound;
    }

    if (!backend_initialized_) {
        llama_backend_init();
        backend_initialized_ = true;
    }

    llama_model_params model_params = llama_model_default_params();
    // model_params.use_mmap defaults to true from llama_model_default_params();
    // SYJ EdgeMind relies on that upstream mmap path rather than a custom one
    // (Phase 0/1 requirement — no fake mmap layer).

    model_ = llama_model_load_from_file(config.model_path.c_str(), model_params);
    if (model_ == nullptr) {
        return EngineError::ModelLoadFailed;
    }

    // ------------------------------------------------------------------
    // Phase 2: memory observation -> estimation -> budget policy -> admission.
    //
    // We deliberately gate context creation (llama_init_from_model), not
    // model loading itself, behind this check: llama_model_load_from_file
    // uses mmap for weights (Phase 0/1 requirement — no fake mmap layer),
    // so inspecting the model's real hyperparameters and measured size here
    // does not itself commit the full weight size to resident memory. It is
    // llama_init_from_model's KV-cache and compute-buffer allocations that
    // actually commit RAM, so that is what we refuse to do when unsafe.
    // ------------------------------------------------------------------
    const ModelHyperparams hyperparams = MemoryObserver::observe_model_hyperparams(model_);

    MemoryEstimate estimate;
    estimate.weights_bytes = MemoryObserver::observe_model_size_bytes(model_); // actual measurement
    estimate.weights_bytes_is_observed = true;

    const std::optional<uint64_t> kv_cache = MemoryEstimator::estimate_kv_cache_bytes(hyperparams, config.context_size);
    const std::optional<uint64_t> compute_buffer =
        MemoryEstimator::estimate_compute_buffer_bytes(hyperparams, config.context_size);

    if (!kv_cache.has_value() || !compute_buffer.has_value()) {
        // Fail-closed: at least one component could not be established
        // safely (invalid/out-of-range hyperparameters, or the arithmetic
        // would overflow). Do NOT substitute 0 or any other invented
        // number — mark the whole estimate invalid so
        // MemoryBudgetPolicy::evaluate() rejects it unconditionally,
        // regardless of what weights_bytes alone would suggest.
        estimate.valid = false;
        estimate.kv_cache_bytes = 0;
        estimate.compute_buffer_bytes = 0;
        estimate.runtime_overhead_bytes = 0;
    } else {
        // Both derived components were established successfully, so the
        // estimate is now explicitly trusted by the fail-closed policy.
        estimate.valid = true;
        estimate.kv_cache_bytes = *kv_cache;
        estimate.compute_buffer_bytes = *compute_buffer;
        // Fixed, documented runtime-overhead default (process/runtime
        // bookkeeping outside the model/context allocations) — matches the
        // illustrative figure in docs/memory-model.md. Not measured; not
        // claimed to be exact.
        estimate.runtime_overhead_bytes = 180ULL * 1024 * 1024;
    }

    const uint64_t budget_bytes = static_cast<uint64_t>(config.memory_budget_mb) * 1024ULL * 1024ULL;
    const uint64_t reserve_bytes = static_cast<uint64_t>(config.safety_reserve_mb) * 1024ULL * 1024ULL;

    const MemoryDecision decision = MemoryBudgetPolicy::evaluate(estimate, budget_bytes, reserve_bytes);
    memory_diagnostic_ = decision.diagnostic;

    if (!decision.safe) {
        llama_model_free(model_);
        model_ = nullptr;
        return EngineError::MemoryBudgetExceeded;
    }

    llama_context_params ctx_params = llama_context_default_params();
    ctx_params.n_ctx = static_cast<uint32_t>(config.context_size);
    ctx_params.n_batch = static_cast<uint32_t>(config.context_size);
    ctx_params.n_threads = config.threads;
    ctx_params.n_threads_batch = config.threads;

    ctx_ = llama_init_from_model(model_, ctx_params);
    if (ctx_ == nullptr) {
        llama_model_free(model_);
        model_ = nullptr;
        return EngineError::ContextCreateFailed;
    }

    const llama_vocab* vocab = llama_model_get_vocab(model_);
    tokenizer_ = std::make_unique<Tokenizer>(vocab);

    // Use the context's ACTUAL n_ctx (per llama.h's own guidance: the
    // requested value in llama_context_params may differ from what the
    // context actually uses) rather than assuming config.context_size held.
    context_manager_ = std::make_unique<ContextManager>(static_cast<int32_t>(llama_n_ctx(ctx_)));

    config_ = config;
    return EngineError::None;
}

void InferenceEngine::reset_context() {
    if (ctx_ != nullptr) {
        llama_memory_t mem = llama_get_memory(ctx_);
        if (mem != nullptr) {
            llama_memory_clear(mem, /*data=*/true);
        }
    }
    if (context_manager_) {
        context_manager_->reset();
    }
}

EngineError InferenceEngine::generate(const std::string& prompt, const TokenStreamCallback& on_token) {
    if (!is_loaded() || !tokenizer_ || !context_manager_) {
        return EngineError::ContextCreateFailed;
    }

    std::vector<llama_token> prompt_tokens;

    // Model-native chat-template path is the default per the v0.1.1
    // architecture revision: RAW USER PROMPT -> tokenizer.chat_template (via
    // GGUF metadata) -> Tokenizer::apply_chat_template() -> formatted prompt
    // -> Tokenizer::tokenize(). We never hard-code ChatML/Llama-2/Gemma/etc.
    //
    // Fallback: not every GGUF model embeds a chat template. When
    // apply_chat_template() reports none is available, we fall back to
    // tokenizing the raw prompt directly (add_special=true still adds BOS).
    // This is a documented, deliberate fallback — not a silent failure.
    std::string formatted_prompt;
    const bool used_chat_template =
        tokenizer_->apply_chat_template(model_, {ChatMessage{"user", prompt}}, /*add_assistant_prefix=*/true,
                                         formatted_prompt);

    const std::string& prompt_to_tokenize = used_chat_template ? formatted_prompt : prompt;
    const bool add_special = !used_chat_template; // template output already includes any special tokens

    if (!tokenizer_->tokenize(prompt_to_tokenize, add_special, /*parse_special=*/true, prompt_tokens)) {
        return EngineError::TokenizeFailed;
    }

    if (prompt_tokens.empty()) {
        return EngineError::TokenizeFailed;
    }

    if (!context_manager_->can_accept(static_cast<int32_t>(prompt_tokens.size()))) {
        // Context limit reached before generation even starts — a clean,
        // explicit stop per Phase 1 spec §8/§10, not a crash or silent
        // truncation.
        return EngineError::None;
    }

    llama_batch prompt_batch = llama_batch_get_one(prompt_tokens.data(),
                                                     static_cast<int32_t>(prompt_tokens.size()));
    if (llama_decode(ctx_, prompt_batch) != 0) {
        return EngineError::DecodeFailed;
    }
    context_manager_->consume(static_cast<int32_t>(prompt_tokens.size()));

    SamplingParams sp;
    sp.temperature = config_.temperature;
    sp.top_p = config_.top_p;
    sp.top_k = config_.top_k;
    Sampler sampler(sp);
    if (!sampler.valid()) {
        return EngineError::DecodeFailed;
    }

    int32_t last_batch_idx = static_cast<int32_t>(prompt_tokens.size()) - 1;

    for (int generated = 0; generated < config_.max_tokens; ++generated) {
        const llama_token next = sampler.sample(ctx_, last_batch_idx);

        if (tokenizer_->is_eog(next)) {
            break; // clean termination: EOS/EOG (Phase 1 spec §10)
        }

        const std::string piece = tokenizer_->token_to_piece(next, /*special=*/false);
        if (on_token && !on_token(piece)) {
            break; // clean termination: caller/user requested stop
        }

        if (!context_manager_->can_accept(1)) {
            break; // clean termination: context full (Phase 1 spec §8/§10)
        }

        llama_token next_mut = next; // llama_batch_get_one takes a non-const pointer
        llama_batch step_batch = llama_batch_get_one(&next_mut, 1);
        if (llama_decode(ctx_, step_batch) != 0) {
            return EngineError::DecodeFailed;
        }
        context_manager_->consume(1);
        last_batch_idx = 0; // single-token batch: its logits are at index 0
    }

    return EngineError::None;
}

InferenceEngine::ModelInfo InferenceEngine::model_info() const {
    ModelInfo info;
    if (model_ == nullptr || ctx_ == nullptr) {
        return info;
    }

    char desc_buf[256] = {0};
    llama_model_desc(model_, desc_buf, sizeof(desc_buf));
    info.description = desc_buf;

    info.n_params = llama_model_n_params(model_);
    info.model_size_bytes = llama_model_size(model_);
    info.n_ctx_train = llama_model_n_ctx_train(model_);
    info.n_ctx = static_cast<int32_t>(llama_n_ctx(ctx_));
    info.n_threads = llama_n_threads(ctx_);
    return info;
}

} // namespace syj::edgemind
