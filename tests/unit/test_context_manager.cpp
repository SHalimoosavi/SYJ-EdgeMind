#include <cstdio>
#include <cstdlib>

#include "context/context_manager.h"

using syj::edgemind::ContextManager;

namespace {
int g_failures = 0;

void check(bool condition, const char* description) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", description);
        ++g_failures;
    }
}
} // namespace

int main() {
    {
        ContextManager cm(1024);
        check(cm.n_ctx() == 1024, "n_ctx() reflects constructor argument");
        check(cm.n_used() == 0, "freshly constructed manager has zero used tokens");
        check(cm.can_accept(1024), "can accept exactly the full context up front");
        check(!cm.can_accept(1025), "cannot accept more than the full context up front");
    }

    {
        ContextManager cm(10);
        cm.consume(7);
        check(cm.n_used() == 7, "consume() tracks cumulative usage");
        check(cm.can_accept(3), "can accept exactly up to the limit");
        check(!cm.can_accept(4), "cannot accept past the limit");

        // Attempting to consume more than remaining must NOT overflow/wrap —
        // this is the "never silently proceed past the budget" guarantee.
        cm.consume(4);
        check(cm.n_used() == 7, "consume() beyond the limit is a no-op, not a silent overflow");
    }

    {
        ContextManager cm(10);
        cm.consume(10);
        check(cm.n_remaining() == 0, "n_remaining() reaches zero at the limit");
        cm.reset();
        check(cm.n_used() == 0, "reset() clears usage back to zero");
        check(cm.can_accept(10), "manager is reusable after reset()");
    }

    {
        ContextManager cm(5);
        check(!cm.can_accept(-1), "negative token counts are never acceptable");
    }

    if (g_failures > 0) {
        std::fprintf(stderr, "%d check(s) failed.\n", g_failures);
        return EXIT_FAILURE;
    }
    std::printf("test_context_manager: all checks passed.\n");
    return EXIT_SUCCESS;
}
