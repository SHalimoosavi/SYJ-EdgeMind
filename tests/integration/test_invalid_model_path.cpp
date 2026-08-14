// Integration test: this is the one Phase 1 test that touches the real
// InferenceEngine/llama.cpp path (llama_backend_init + a failed
// llama_model_load_from_file), without requiring an actual GGUF fixture —
// exercising exactly the "Missing model" error path required by Phase 1
// spec §14, and confirming the runtime refuses to continue afterward
// (Runtime::is_ready() stays false).

#include <cstdio>
#include <cstdlib>
#include <string>

#include "core/runtime.h"

using syj::edgemind::Runtime;
using syj::edgemind::RuntimeConfig;

int main() {
    Runtime runtime;

    RuntimeConfig config;
    config.model_path = "/nonexistent/path/does-not-exist.gguf";

    const std::string err = runtime.load(config);

    if (err.empty()) {
        std::fprintf(stderr, "FAIL: loading a nonexistent model path did not produce an error.\n");
        return EXIT_FAILURE;
    }

    if (err.find("does not exist") == std::string::npos) {
        std::fprintf(stderr, "FAIL: error message did not mention the missing file: %s\n", err.c_str());
        return EXIT_FAILURE;
    }

    if (runtime.is_ready()) {
        std::fprintf(stderr, "FAIL: runtime reports ready() after a failed load.\n");
        return EXIT_FAILURE;
    }

    // A second load() attempt with a bad config must also fail cleanly
    // rather than crash or use a half-initialized engine.
    RuntimeConfig bad_config;
    bad_config.model_path = ""; // triggers config validation failure, not engine failure
    const std::string err2 = runtime.load(bad_config);
    if (err2.empty()) {
        std::fprintf(stderr, "FAIL: loading an empty model_path did not produce an error.\n");
        return EXIT_FAILURE;
    }

    std::printf("test_invalid_model_path: all checks passed.\n");
    return EXIT_SUCCESS;
}
