#include "inference/sampler.h"

#include "llama.h"

namespace syj::edgemind {

static_assert(0xFFFFFFFFu == LLAMA_DEFAULT_SEED,
              "SamplingParams::seed default must match llama.cpp's LLAMA_DEFAULT_SEED");

Sampler::Sampler(const SamplingParams& params) {
    llama_sampler_chain_params chain_params = llama_sampler_chain_default_params();
    chain_ = llama_sampler_chain_init(chain_params);
    if (chain_ == nullptr) {
        return;
    }

    // Order matches llama.cpp's own common/sampling.cpp convention:
    // top_k -> top_p -> temperature -> final distribution sample.
    if (params.top_k > 0) {
        llama_sampler_chain_add(chain_, llama_sampler_init_top_k(params.top_k));
    }
    if (params.top_p < 1.0f) {
        llama_sampler_chain_add(chain_, llama_sampler_init_top_p(params.top_p, /*min_keep=*/1));
    }
    llama_sampler_chain_add(chain_, llama_sampler_init_temp(params.temperature));
    llama_sampler_chain_add(chain_, llama_sampler_init_dist(params.seed));
}

Sampler::~Sampler() {
    if (chain_ != nullptr) {
        llama_sampler_free(chain_);
        chain_ = nullptr;
    }
}

Sampler::Sampler(Sampler&& other) noexcept : chain_(other.chain_) {
    other.chain_ = nullptr;
}

Sampler& Sampler::operator=(Sampler&& other) noexcept {
    if (this != &other) {
        if (chain_ != nullptr) {
            llama_sampler_free(chain_);
        }
        chain_ = other.chain_;
        other.chain_ = nullptr;
    }
    return *this;
}

llama_token Sampler::sample(llama_context* ctx, int32_t idx) {
    if (chain_ == nullptr || ctx == nullptr) {
        return LLAMA_TOKEN_NULL;
    }
    const llama_token token = llama_sampler_sample(chain_, ctx, idx);
    llama_sampler_accept(chain_, token);
    return token;
}

} // namespace syj::edgemind
