#include "tokenizer/tokenizer.h"

#include "llama.h"

namespace syj::edgemind {

Tokenizer::Tokenizer(const llama_vocab* vocab) : vocab_(vocab) {}

bool Tokenizer::tokenize(const std::string& text, bool add_special, bool parse_special,
                          std::vector<llama_token>& out_tokens) const {
    out_tokens.clear();

    if (vocab_ == nullptr) {
        return false;
    }

    // llama_tokenize's contract: on success it returns the number of tokens
    // written (>= 0). If the destination buffer is too small, it returns the
    // NEGATIVE of the number of tokens actually required, without writing
    // past the buffer. We therefore probe first, then allocate exactly.
    const int32_t n_needed = -llama_tokenize(
        vocab_,
        text.c_str(),
        static_cast<int32_t>(text.size()),
        /*tokens=*/nullptr,
        /*n_tokens_max=*/0,
        add_special,
        parse_special);

    if (n_needed <= 0) {
        // Empty input tokenizes to zero tokens without add_special; that is
        // not an error condition by itself.
        return true;
    }

    out_tokens.resize(static_cast<size_t>(n_needed));

    const int32_t n_written = llama_tokenize(
        vocab_,
        text.c_str(),
        static_cast<int32_t>(text.size()),
        out_tokens.data(),
        n_needed,
        add_special,
        parse_special);

    if (n_written < 0) {
        // Should not happen given the probe above, but never silently
        // return a partially-filled/garbage buffer.
        out_tokens.clear();
        return false;
    }

    out_tokens.resize(static_cast<size_t>(n_written));
    return true;
}

std::string Tokenizer::token_to_piece(llama_token token, bool special) const {
    if (vocab_ == nullptr) {
        return std::string();
    }

    // Same "negative return = required size" convention as llama_tokenize.
    char small_buf[128];
    int32_t n = llama_token_to_piece(vocab_, token, small_buf, sizeof(small_buf), /*lstrip=*/0, special);

    if (n < 0) {
        const int32_t needed = -n;
        std::string buf(static_cast<size_t>(needed), '\0');
        n = llama_token_to_piece(vocab_, token, buf.data(), needed, /*lstrip=*/0, special);
        if (n < 0) {
            return std::string();
        }
        buf.resize(static_cast<size_t>(n));
        return buf;
    }

    return std::string(small_buf, static_cast<size_t>(n));
}

bool Tokenizer::is_eog(llama_token token) const {
    if (vocab_ == nullptr) {
        return true; // fail safe: treat as end-of-generation rather than loop forever
    }
    return llama_vocab_is_eog(vocab_, token);
}

int32_t Tokenizer::vocab_size() const {
    if (vocab_ == nullptr) {
        return 0;
    }
    return llama_vocab_n_tokens(vocab_);
}

bool Tokenizer::apply_chat_template(const llama_model* model, const std::vector<ChatMessage>& messages,
                                     bool add_assistant_prefix, std::string& out) const {
    out.clear();

    if (model == nullptr || messages.empty()) {
        return false;
    }

    // The GGUF model's own embedded template (tokenizer.chat_template
    // metadata) is the ONLY source of truth here — passing a null name asks
    // llama.cpp for the model's default template rather than a named
    // alternate one. If the model has none, we deliberately do not
    // substitute a hard-coded ChatML/Llama-2/Gemma/etc. format.
    const char* tmpl = llama_model_chat_template(model, /*name=*/nullptr);
    if (tmpl == nullptr) {
        return false;
    }

    std::vector<llama_chat_message> native_messages;
    native_messages.reserve(messages.size());
    for (const auto& m : messages) {
        native_messages.push_back(llama_chat_message{m.role.c_str(), m.content.c_str()});
    }

    // Same "negative/insufficient return means resize" convention used
    // elsewhere in this wrapper (see tokenize(), token_to_piece()).
    std::vector<char> buf(2048);
    int32_t n = llama_chat_apply_template(tmpl, native_messages.data(), native_messages.size(),
                                           add_assistant_prefix, buf.data(), static_cast<int32_t>(buf.size()));
    if (n < 0) {
        return false;
    }
    if (static_cast<size_t>(n) > buf.size()) {
        buf.resize(static_cast<size_t>(n));
        n = llama_chat_apply_template(tmpl, native_messages.data(), native_messages.size(),
                                       add_assistant_prefix, buf.data(), static_cast<int32_t>(buf.size()));
        if (n < 0) {
            return false;
        }
    }

    out.assign(buf.data(), static_cast<size_t>(n));
    return true;
}

} // namespace syj::edgemind
