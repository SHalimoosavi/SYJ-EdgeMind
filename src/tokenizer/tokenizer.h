#ifndef SYJ_EDGEMIND_TOKENIZER_TOKENIZER_H
#define SYJ_EDGEMIND_TOKENIZER_TOKENIZER_H

#include <string>
#include <vector>
#include <cstdint>

struct llama_vocab; // fwd-declared from llama.h; we don't include llama.h in headers
using llama_token = int32_t;

namespace syj::edgemind {

// Wraps the model's own tokenizer via llama.cpp's llama_vocab functions.
// This class does NOT implement any tokenizer algorithm itself — per the
// Phase 1 spec (§9), tokenization is entirely delegated to llama.cpp/the
// model's vocab. This is only a convenience layer for SYJ EdgeMind's use of
// that API (allocation, error surfacing, piece-decoding for streaming).
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

private:
    const llama_vocab* vocab_;
};

} // namespace syj::edgemind

#endif // SYJ_EDGEMIND_TOKENIZER_TOKENIZER_H
