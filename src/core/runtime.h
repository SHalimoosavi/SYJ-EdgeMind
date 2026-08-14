#ifndef SYJ_EDGEMIND_CORE_RUNTIME_H
#define SYJ_EDGEMIND_CORE_RUNTIME_H

#include <memory>
#include <string>

#include "core/config.h"
#include "inference/inference_engine.h"

namespace syj::edgemind {

// The top-level object platform code (CLI today; Windows/iOS wrappers in
// later phases) is expected to hold. Wraps config validation + InferenceEngine
// lifecycle behind one RAII type so callers never touch llama.cpp types.
class Runtime {
public:
    Runtime();
    ~Runtime();

    Runtime(const Runtime&) = delete;
    Runtime& operator=(const Runtime&) = delete;

    // Validates `config`, then loads the model. On failure, returns a
    // human-readable error and the Runtime remains unloaded (is_ready()
    // returns false) — it never leaves a half-initialized runtime in place.
    std::string load(const RuntimeConfig& config);

    bool is_ready() const;

    // Streams a response to `prompt`. Returns a human-readable error string
    // (empty on success). See InferenceEngine::generate for callback semantics.
    std::string generate(const std::string& prompt, const TokenStreamCallback& on_token);

    void reset_context();

    InferenceEngine::ModelInfo model_info() const;
    const RuntimeConfig& config() const { return config_; }

private:
    std::unique_ptr<InferenceEngine> engine_;
    RuntimeConfig config_;
    bool ready_ = false;
};

} // namespace syj::edgemind

#endif // SYJ_EDGEMIND_CORE_RUNTIME_H
