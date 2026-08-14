#include "tokenizer/tokenizer.h"

#include <limits>
#include <utility>
#include <vector>

#include "llama.h"

namespace syj::edgemind {

Tokenizer::Tokenizer(const llama_vocab* vocab)
    : vocab_(vocab) {
}

bool Tokenizer::tokenize(
    const std::string& text,
    bool add_special,
    bool parse_special,
    std::vector<llama_token>& out_tokens) const {

    out_tokens.clear();

    if (vocab_ == nullptr) {
        return false;
    }

    /*
     * llama_tokenize() returns:
     *
     *   >= 0 : number of tokens written
     *   <  0 : negative number of tokens required
     *
     * Probe first so the destination buffer can be sized exactly.
     */
    const int32_t probe = llama_tokenize(
        vocab_,
        text.c_str(),
        static_cast<int32_t>(text.size()),
        nullptr,
        0,
        add_special,
        parse_special
    );

    if (probe == 0) {
        return true;
    }

    /*
     * Protect the negation below from signed integer overflow.
     */
    if (probe == std::numeric_limits<int32_t>::min()) {
        return false;
    }

    const int32_t n_needed = probe < 0 ? -probe : probe;

    if (n_needed <= 0) {
        return false;
    }

    out_tokens.resize(static_cast<size_t>(n_needed));

    const int32_t n_written = llama_tokenize(
        vocab_,
        text.c_str(),
        static_cast<int32_t>(text.size()),
        out_tokens.data(),
        n_needed,
        add_special,
        parse_special
    );

    if (n_written < 0 || n_written > n_needed) {
        out_tokens.clear();
        return false;
    }

    out_tokens.resize(static_cast<size_t>(n_written));
    return true;
}

std::string Tokenizer::token_to_piece(
    llama_token token,
    bool special) const {

    if (vocab_ == nullptr) {
        return {};
    }

    /*
     * Most token pieces are comfortably below this size.
     * If llama.cpp reports that more space is required, allocate exactly
     * what it requests.
     */
    char small_buf[128];

    int32_t n = llama_token_to_piece(
        vocab_,
        token,
        small_buf,
        static_cast<int32_t>(sizeof(small_buf)),
        0,
        special
    );

    if (n < 0) {
        if (n == std::numeric_limits<int32_t>::min()) {
            return {};
        }

        const int32_t needed = -n;

        if (needed <= 0) {
            return {};
        }

        std::string buf(static_cast<size_t>(needed), '\0');

        n = llama_token_to_piece(
            vocab_,
            token,
            buf.data(),
            needed,
            0,
            special
        );

        if (n < 0 || n > needed) {
            return {};
        }

        buf.resize(static_cast<size_t>(n));
        return buf;
    }

    if (n <= 0) {
        return {};
    }

    return std::string(
        small_buf,
        static_cast<size_t>(n)
    );
}

bool Tokenizer::is_eog(llama_token token) const {
    if (vocab_ == nullptr) {
        /*
         * Fail safe: an invalid tokenizer must never allow an
         * uncontrolled generation loop.
         */
        return true;
    }

    return llama_vocab_is_eog(vocab_, token);
}

int32_t Tokenizer::vocab_size() const {
    if (vocab_ == nullptr) {
        return 0;
    }

    return llama_vocab_n_tokens(vocab_);
}

const char* Tokenizer::model_chat_template(
    const llama_model* model) {

    if (model == nullptr) {
        return nullptr;
    }

    return llama_model_chat_template(
        model,
        nullptr
    );
}

bool Tokenizer::apply_chat_template(
    const llama_model* model,
    const std::vector<std::pair<std::string, std::string>>& messages,
    bool add_assistant_prompt,
    std::string& out) {

    out.clear();

    if (model == nullptr || messages.empty()) {
        return false;
    }

    const char* tmpl = model_chat_template(model);

    if (tmpl == nullptr || tmpl[0] == '\0') {
        return false;
    }

    /*
     * The std::string pairs own the role/content strings, so their c_str()
     * pointers remain valid for both llama_chat_apply_template() calls.
     */
    std::vector<llama_chat_message> chat;
    chat.reserve(messages.size());

    for (const auto& message : messages) {
        llama_chat_message item{};

        item.role = message.first.c_str();
        item.content = message.second.c_str();

        chat.push_back(item);
    }

    /*
     * First call: ask llama.cpp how many bytes are required.
     */
    const int32_t required = llama_chat_apply_template(
        tmpl,
        chat.data(),
        chat.size(),
        add_assistant_prompt,
        nullptr,
        0
    );

    if (required < 0) {
        return false;
    }

    if (required == 0) {
        return true;
    }

    /*
     * Allocate exactly the number of bytes reported by llama.cpp.
     *
     * IMPORTANT:
     * The second call receives the exact same capacity. We do not claim
     * required + 1 bytes unless the string actually owns that capacity.
     */
    std::string buffer(
        static_cast<size_t>(required),
        '\0'
    );

    const int32_t written = llama_chat_apply_template(
        tmpl,
        chat.data(),
        chat.size(),
        add_assistant_prompt,
        buffer.data(),
        required
    );

    if (written < 0 || written > required) {
        return false;
    }

    buffer.resize(static_cast<size_t>(written));
    out = std::move(buffer);

    return true;
}

} // namespace syj::edgemind
