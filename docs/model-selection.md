# Model Selection

**Status: Phase 0 — no models are registered yet.** The model registry (`models/registry.json`) and its tooling land in Phase 3. This document will hold one entry per supported model with:

- Parameter count
- Quantization
- Approximate file size
- Approximate RAM requirement (weights + KV cache + context + scratch + overhead — never just file size)
- Recommended context
- CPU requirements
- License
- Model source (verified official repository only — no invented URLs)
- Limitations

## Candidate model families (not yet verified/registered)

- Qwen3 1.7B (compact Qwen-family)
- Phi-family compact models
- Other genuinely small GGUF models suitable for ~4 GB devices

Q4_K_M quantization is preferred where appropriate, but the runtime will never assume "model file size = total runtime RAM" — see [memory-model.md](memory-model.md) for the full accounting (weights + KV cache + context + scratch buffers + runtime allocations + OS/application overhead).

No model will be added to `models/registry.json` without a verified source and a documented, non-invented set of the fields above.
