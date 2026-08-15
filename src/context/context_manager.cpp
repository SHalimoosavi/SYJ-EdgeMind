#include "context/context_manager.h"

#include <algorithm>

namespace syj::edgemind {

ContextManager::ContextManager(int32_t n_ctx) : n_ctx_(n_ctx) {}

bool ContextManager::can_accept(int32_t additional_tokens) const {
    if (additional_tokens < 0) {
        return false;
    }

    // Avoid signed-int overflow in n_used_ + additional_tokens by comparing
    // against the remaining capacity first.
    if (n_used_ > n_ctx_) {
        return false;
    }

    return additional_tokens <= (n_ctx_ - n_used_);
}

void ContextManager::consume(int32_t count) {
    if (count <= 0) {
        return;
    }
    if (!can_accept(count)) {
        return; // caller must check can_accept() first; never silently overflow
    }
    n_used_ += count;
}

void ContextManager::reset() {
    n_used_ = 0;
}

} // namespace syj::edgemind
