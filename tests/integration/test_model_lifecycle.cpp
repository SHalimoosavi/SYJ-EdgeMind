// Phase 6 lifecycle test: load -> unload -> reload, against the REAL
// production Runtime — no mocks, no test-only hooks, no alternate
// lifecycle implementation. This exercises exactly the same
// Runtime::load()/unload()/generate() a real caller would use.
//
// WHY THIS TEST IS GATED ON AN ENVIRONMENT VARIABLE:
// No fixture in this repository can honestly exercise this test's
// positive path. tests/model/test_gguf_fixture.h's synthetic GGUF is
// documented (from a real prior Android/Termux run) as NOT loadable by
// real llama.cpp — its `context_length` field is encoded as GGUF UINT64
// while that llama.cpp build expects UINT32, a fixture/backend encoding
// mismatch, not an EdgeMind defect. Using it here would either never
// reach a successful load (making it impossible to prove reload doesn't
// leak state from a *successful* first load — the actual point of this
// test) or risk an unverified failure mode inside llama.cpp's own parser
// that cannot be safely predicted without a real link. No real .gguf is
// or ever will be committed to this repository (see .gitignore).
//
// So: set SYJ_EDGEMIND_TEST_MODEL_PATH to a real, locally-available GGUF
// file to run the actual lifecycle for real. Left unset, this test SKIPS
// (CTest exit code 77, registered via SKIP_RETURN_CODE in
// tests/CMakeLists.txt) rather than silently reporting success — a skip
// is reported distinctly from a pass by ctest, so an environment without
// a real model can never be mistaken for a validated lifecycle.

#include <cstdio>
#include <cstdlib>
#include <string>

#include "core/runtime.h"

using syj::edgemind::Runtime;
using syj::edgemind::RuntimeConfig;

namespace {
int g_failures = 0;

void check(bool condition, const char* description) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", description);
        ++g_failures;
    }
}

// The traditional Automake/CTest "skip" convention — matches the
// SKIP_RETURN_CODE registered for this test in tests/CMakeLists.txt.
constexpr int kSkipExitCode = 77;
} // namespace

int main() {
    const char* model_path_env = std::getenv("SYJ_EDGEMIND_TEST_MODEL_PATH");
    if (model_path_env == nullptr || model_path_env[0] == '\0') {
        std::printf("test_model_lifecycle: SKIPPED — no real GGUF model available.\n");
        std::printf("  Set SYJ_EDGEMIND_TEST_MODEL_PATH=/path/to/model.gguf to run the\n");
        std::printf("  real load -> unload -> reload lifecycle against real llama.cpp.\n");
        std::printf("  This is a deliberate skip, not a pass — nothing about the lifecycle\n");
        std::printf("  was actually exercised this run. See this file's header comment.\n");
        return kSkipExitCode;
    }

    const std::string model_path = model_path_env;
    Runtime runtime;
    RuntimeConfig config;
    config.model_path = model_path;

    // --- 1. First load ---
    const std::string load_err = runtime.load(config);
    check(load_err.empty(), "first load() succeeds against the real model");
    check(runtime.is_ready(), "runtime reports ready after first load");

    // --- 2. Confirm usable: a real, minimal generation ---
    if (runtime.is_ready()) {
        const std::string gen_err = runtime.generate("Hi", [](const std::string&) { return true; });
        check(gen_err.empty(), "generate() succeeds after first load");
    }

    // --- 3. Unload ---
    runtime.unload();
    check(!runtime.is_ready(), "runtime reports NOT ready after unload()");

    // --- 4. Reload the SAME model — this is the actual point of the test:
    //     a successful first load must not leave the Runtime/
    //     InferenceEngine in a state (leaked handles, stale pointers,
    //     partially-freed resources) that prevents a clean second load. ---
    const std::string reload_err = runtime.load(config);
    check(reload_err.empty(), "second load() (reload of the SAME model) succeeds — no leaked or "
                               "corrupted state from the first load/unload cycle");
    check(runtime.is_ready(), "runtime reports ready after reload");

    // --- 5. Confirm usable again — proves the reload actually produced a
    //     working model/context, not merely a non-error return value ---
    if (runtime.is_ready()) {
        const std::string gen_err2 = runtime.generate("Hi again", [](const std::string&) { return true; });
        check(gen_err2.empty(), "generate() succeeds after reload");
    }

    // --- 6. Final clean unload / shutdown ---
    runtime.unload();
    check(!runtime.is_ready(), "runtime reports NOT ready after final unload()");

    if (g_failures == 0) {
        std::printf("test_model_lifecycle: all checks passed (real model: %s, real llama.cpp).\n",
                     model_path.c_str());
        return 0;
    }
    std::fprintf(stderr, "test_model_lifecycle: %d check(s) failed.\n", g_failures);
    return 1;
}
