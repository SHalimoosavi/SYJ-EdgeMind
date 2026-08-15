#ifndef SYJ_EDGEMIND_TOKENIZER_TOKENIZER_H
#define SYJ_EDGEMIND_TOKENIZER_TOKENIZER_H

#include <string>
#include <vector>
#include <cstdint>

struct llama_vocab;  // fwd-declared from llama.h; we don't include llama.h in headers
struct llama_model;
using llama_token = int32_t;

namespace syj::edgemind {

// One turn of a chat conversation, mirroring llama.cpp's llama_chat_message.
struct ChatMessage {
    std::string role;    // "system" | "user" | "assistant"
    std::string content;
};

// Wraps the model's own tokenizer AND its own chat template, both via
// llama.cpp's public API. This class does NOT implement any tokenizer
// algorithm or hard-code any model family's prompt format itself — per the
// Phase 1 spec (§9) and the Phase 1.1 chat-template revision, both
// tokenization and prompt formatting are entirely delegated to
// llama.cpp/the GGUF model's own metadata (tokenizer.chat_template). This is
// only a convenience layer for SYJ EdgeMind's use of that API.
class Tokenizer {
public:
    explicit Tokenizer(const llama_vocab* vocab);

    // Tokenizes `text`. Returns true on success and fills `out_tokens`.
    // Returns false (with out_tokens cleared) if llama_tokenize fails.
    bool tokenize(const std::string& text, bool add_special, bool parse_special,
                  std::vector<llama_token>& out_tokens) const;

    // Converts a single token back to a UTF-8 text fragment ("piece"),
    // suitable for incremental/streaming output. Returns an empty string on
    // failure (e.g. invalid token).
    std::string token_to_piece(llama_token token, bool special) const;

    // True if `token` is an end-of-generation token for this vocab
    // (EOS or any model-specific EOG token).
    bool is_eog(llama_token token) const;

    int32_t vocab_size() const;

    // Formats `messages` using the GIVEN model's own embedded chat template
    // (read from GGUF metadata via llama_model_chat_template()) via
    // llama_chat_apply_template(). Writes the formatted, model-native prompt
    // to `out` and returns true on success.
    //
    // Returns false if the model has no embedded chat template, or if
    // template application fails — callers MUST handle this by falling back
    // to raw-prompt tokenization rather than treating it as fatal, since not
    // every GGUF model ships a chat template. This function never hard-codes
    // a specific format (ChatML/Llama-2/Gemma/etc.) — the GGUF metadata is
    // always the source of truth.
    bool apply_chat_template(const llama_model* model, const std::vector<ChatMessage>& messages,
                              bool add_assistant_prefix, std::string& out) const;

private:
    const llama_vocab* vocab_;
};

} // namespace syj::edgemind

#endif // SYJ_EDGEMIND_TOKENIZER_TOKENIZER_H
