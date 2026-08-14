#ifndef SYJ_EDGEMIND_CONTEXT_CONTEXT_MANAGER_H
#define SYJ_EDGEMIND_CONTEXT_CONTEXT_MANAGER_H

#include <cstdint>

namespace syj::edgemind {

// Tracks how many tokens have been consumed against a fixed-size llama.cpp
// context and enforces a hard stop before overflow.
//
// This is intentionally simple for Phase 1: it only prevents unbounded
// growth (Phase 1 spec §8/§10 — generation must terminate on max token
// count/context limit). It does NOT implement history truncation,
// summarization, or automatic new-context rollover — those are Phase 2/
// later refinements once the memory-budget engine exists to reason about
// the cost of doing so.
class ContextManager {
public:
    explicit ContextManager(int32_t n_ctx);

    // Returns true if `additional_tokens` more tokens can be added without
    // exceeding the configured context size.
    bool can_accept(int32_t additional_tokens) const;

    // Records that `count` tokens were consumed (prompt or generated).
    // No-op (does not go negative or wrap) if this would exceed n_ctx —
    // callers must check can_accept() first.
    void consume(int32_t count);

    void reset();

    int32_t n_ctx() const { return n_ctx_; }
    int32_t n_used() const { return n_used_; }
    int32_t n_remaining() const { return n_ctx_ - n_used_; }

private:
    int32_t n_ctx_;
    int32_t n_used_ = 0;
};

} // namespace syj::edgemind

#endif // SYJ_EDGEMIND_CONTEXT_CONTEXT_MANAGER_H
