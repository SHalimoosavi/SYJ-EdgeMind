#ifndef SYJ_EDGEMIND_INFERENCE_SAMPLER_H
#define SYJ_EDGEMIND_INFERENCE_SAMPLER_H

#include <cstdint>

struct llama_sampler; // fwd-declared from llama.h
struct llama_context;
using llama_token = int32_t;

namespace syj::edgemind {

struct SamplingParams {
    float temperature = 0.7f;
    float top_p = 0.9f;
    int top_k = 40;
    // Matches llama.cpp's LLAMA_DEFAULT_SEED (0xFFFFFFFF, i.e. "random seed").
    // Not including llama.h here to keep this header llama.cpp-implementation-free;
    // sampler.cpp verifies this literal matches LLAMA_DEFAULT_SEED via a static_assert.
    uint32_t seed = 0xFFFFFFFFu;
};

// Owns a llama_sampler chain (top_k -> top_p -> temp -> dist), per the
// Phase 1 spec §11: "keep sampling simple", built only from the parameters
// SYJ EdgeMind actually exposes (temperature, top_p, max_tokens covered
// elsewhere). RAII: the chain is freed in the destructor via llama_sampler_free.
class Sampler {
public:
    explicit Sampler(const SamplingParams& params);
    ~Sampler();

    Sampler(const Sampler&) = delete;
    Sampler& operator=(const Sampler&) = delete;
    Sampler(Sampler&& other) noexcept;
    Sampler& operator=(Sampler&& other) noexcept;

    // Samples the next token from the logits at batch index `idx` of `ctx`
    // (the context that just ran llama_decode). Also calls llama_sampler_accept
    // internally so any stateful samplers in the chain stay consistent.
    llama_token sample(llama_context* ctx, int32_t idx);

    bool valid() const { return chain_ != nullptr; }

private:
    llama_sampler* chain_ = nullptr;
};

} // namespace syj::edgemind

#endif // SYJ_EDGEMIND_INFERENCE_SAMPLER_H
