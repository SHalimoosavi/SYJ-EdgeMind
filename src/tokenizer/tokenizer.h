#ifndef SYJ_EDGEMIND_TOKENIZER_TOKENIZER_H
#define SYJ_EDGEMIND_TOKENIZER_TOKENIZER_H

#include <cstdint>
#include <string>
#include <vector>

struct llama_vocab;
struct llama_model;

using llama_token = int32_t;

namespace syj::edgemind {

// Wraps the model's tokenizer and chat-template functionality through
// llama.cpp's public C API.
//
// SYJ EdgeMind does not implement a tokenizer algorithm or a model-specific
// prompt format. The GGUF model remains authoritative for both tokenization
// and its embedded chat template.
class Tokenizer {
public:
    explicit Tokenizer(const llama_vocab* vocab);

    // Tokenizes `text`. Returns true on success and fills `out_tokens`.
    bool tokenize(const std::string& text,
                  bool add_special,
                  bool parse_special,
                  std::vector<llama_token>& out_tokens) const;

    // Converts a single token into a UTF-8 text fragment suitable for
    // incremental streaming output.
    std::string token_to_piece(llama_token token, bool special) const;

    // True if `token` is an end-of-generation token for this vocabulary.
    bool is_eog(llama_token token) const;

    int32_t vocab_size() const;

    // Returns the model's embedded default chat template, or nullptr when
    // the model does not provide one.
    static const char* model_chat_template(const llama_model* model);

    // Formats chat messages using the model's embedded chat template.
    //
    // `add_assistant_prompt=true` requests the assistant-generation prefix.
    // Returns false if the model has no template or llama.cpp cannot apply it.
    static bool apply_chat_template(
        const llama_model* model,
        const std::vector<std::pair<std::string, std::string>>& messages,
        bool add_assistant_prompt,
        std::string& out);

private:
    const llama_vocab* vocab_;
};

} // namespace syj::edgemind

#endif // SYJ_EDGEMIND_TOKENIZER_TOKENIZER_H
