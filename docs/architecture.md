# Architecture

## Overview

```
        SYJ EdgeMind
              |
     +--------+--------+
     |                 |
Windows CLI        iOS App/Core
     |                 |
     +--------+--------+
              |
        Native C API        <- src/api/
              |
       SYJ Edge Runtime      <- src/core, src/memory, src/model,
              |                 src/inference, src/context, src/tokenizer
          llama.cpp           <- fetched via CMake FetchContent (build/_deps), pinned tag
              |
    GGUF Quantized Model
              |
       CPU / RAM / mmap
```

There is exactly one inference core. Platform layers (`platform/windows`, `platform/ios`) are thin wrappers that call `src/api/edge_mind_api.h` and must never contain their own inference logic or duplicate anything in `src/inference/`.

## Components (Phase 0: interfaces/placeholders only, no implementation yet)

- `src/core/` — runtime lifecycle and configuration (`runtime.h/.cpp`, `config.h/.cpp`). `runtime.h` defines `RuntimeError`, an explicit, exhaustive error classification that `Runtime::last_error()` exposes — this is what the C API maps to `syj_edgemind_status`, not string inspection.
- `src/memory/` — memory budget + estimator (Phase 2 — **implemented**: `memory_types.h`, `memory_estimator.*`, `memory_budget.*` are pure/llama.cpp-independent; `memory_observer.*` bridges real `llama_model_*` getters and `/proc/meminfo` into those pure types)
- `src/usage/` — usage/quota manager (v0.3.0, parallel initiative — **implemented**: `usage_types.h`, `usage_accounting.*` are pure/no-I/O; `usage_state_store.*` is the sole file touching the filesystem; `usage_manager.*` coordinates them and owns the injectable clock. See `docs/usage-model.md`.)
- `src/model/` — model registry + verification, self-contained SHA-256 (Phase 3 — **implemented**: `model_types.h`, `gguf_reader.*`, and `model_hash.*` are pure/llama.cpp-independent (`gguf_reader.*` parses the GGUF header+metadata directly against the public spec, never via llama.cpp); `model_verifier.*` composes filesystem checks with those; `model_registry.*` is the sole file persisting import history. See `docs/model-registry.md`.)
- `src/inference/` — llama.cpp integration, sampler (Phase 1)
- `src/context/` — context/token accounting and truncation strategy (Phase 1/2)
- `src/tokenizer/` — tokenizer wrapper (Phase 1)
- `src/api/` — the single stable C API every platform wrapper calls (Phase 1). `edge_mind_api.cpp`'s `to_c_status()` maps `RuntimeError` -> `syj_edgemind_status` via an exhaustive switch, kept in sync with `RuntimeError` by the compiler (missing a case is a warning under this project's strict-warnings build config).
- `src/cli/` — Windows/desktop CLI entry point (Phase 1, hardened in Phase 4)
- `platform/windows/` — Windows-specific build notes and packaging (Phase 5)
- `platform/ios/` — Objective-C++/Swift bridge over the C API (Phase 6/7)

## Dependency strategy

Kept deliberately minimal:

- **llama.cpp** — the only inference dependency. Fetched reproducibly via CMake `FetchContent` at a pinned tag (see below), landing in the build tree's own dependency area (`build/_deps/`), never tracked against `main`, and never vendored into the repository working tree — see `third_party/README.md` for why `third_party/llama.cpp` is deliberately left as an unused, gitignored placeholder rather than the fetch target.
- **C/C++ standard library** and **CMake** — build tooling.
- No Electron, React, Next.js, Python runtime, or Node runtime in the inference core. Python/Node may appear only as external tooling in `scripts/` (e.g. a model downloader) — never on the inference path.
- SwiftUI is permitted only in the optional iOS UI layer (Phase 7), which calls the same C API.

## llama.cpp version pinning

**Status (Phase 1): pinned.**

```
llama.cpp repository: https://github.com/ggml-org/llama.cpp
llama.cpp tag:         b10375
llama.cpp commit:      ba360ef  (short SHA, as published on the tag's release page)
Pinned on:              2026-08-14
Verified via:           https://github.com/ggml-org/llama.cpp/releases (tag b10375, "chat: tighten
                        bare function parsing for Qwen models (#26793)", published 2026-08-12)
Reason for pin:         Most recent tagged release available at the time SYJ EdgeMind Phase 1 was
                        implemented. llama.cpp does not use semantic versioning; it ships sequential
                        build-tagged releases (b-numbers) via GitHub Actions, so a tag is the
                        reproducible unit to pin against rather than an arbitrary `main` commit.
```

The build (`CMakeLists.txt`) fetches exactly `GIT_TAG b10375` via CMake `FetchContent` into the build tree's own dependency area (`build/_deps/llama_cpp-src` by default) — it never tracks `main` and is never vendored into the repository working tree. Bumping this pin in the future is a deliberate, reviewed change to this file and to `CMakeLists.txt`, not an automatic update. `-DSYJ_EDGEMIND_USE_SYSTEM_LLAMA=ON` is reserved for a manually-vendored/offline build using `third_party/llama.cpp` instead (see `third_party/README.md`); this is not the default path.

**API surface verified against this tag** (via the upstream `include/llama.h`, `src/llama-sampling.cpp`, `common/sampling.cpp`) and used by SYJ EdgeMind's wrappers in `src/inference/`, `src/tokenizer/`, and `src/context/`:

- Backend: `llama_backend_init`, `llama_backend_free`
- Model: `llama_model_default_params`, `llama_model_load_from_file`, `llama_model_free`, `llama_model_get_vocab`, `llama_model_desc`, `llama_model_size`, `llama_model_n_params`
- Context: `llama_context_default_params`, `llama_init_from_model`, `llama_free`, `llama_n_ctx`
- Vocab/tokenizer: `llama_vocab_n_tokens`, `llama_tokenize`, `llama_token_to_piece`, `llama_vocab_is_eog`
- Batch/decode: `llama_batch_get_one`, `llama_batch_init`, `llama_batch_free`, `llama_decode`, `llama_get_logits_ith`
- Sampling: `llama_sampler_chain_default_params`, `llama_sampler_chain_init`, `llama_sampler_chain_add`, `llama_sampler_init_top_k`, `llama_sampler_init_top_p`, `llama_sampler_init_temp`, `llama_sampler_init_dist`, `llama_sampler_sample`, `llama_sampler_accept`, `llama_sampler_free`
- Chat templates (added v0.1.1): `llama_model_chat_template`, `llama_chat_apply_template`, `llama_chat_message`

## v0.1.1 — model-native chat templates

As of commit `230fccc` (tag `v0.1.1`), prompt formatting is delegated entirely to the GGUF model's own embedded chat template rather than any SYJ EdgeMind-side prompt construction:

```
RAW USER PROMPT
      -> llama_model_chat_template()   (reads tokenizer.chat_template from GGUF metadata)
      -> Tokenizer::apply_chat_template()
      -> formatted, model-native prompt
      -> Tokenizer::tokenize()
      -> llama_decode() -> Sampler -> streaming output
```

`Tokenizer` now owns model chat-template discovery/application in addition to tokenization, token-to-piece conversion, EOG detection, and vocab info. The earlier `apply_model_chat_template()` helper (which risked drifting toward hard-coded per-family prompt formats) was removed; there are no hard-coded ChatML/Llama-2/Gemma/SmolLM/Phi formats anywhere in this codebase. If a GGUF model has no embedded template, `InferenceEngine::generate()` falls back to tokenizing the raw prompt directly — a documented fallback, not a silent failure.

**Real-hardware validation (Android/Termux, reported by the project maintainer):** a full `cmake`/`ctest` run passed (3/3 tests), and real inference against `SmolLM2-135M-Instruct-Q4_K_M.gguf` (GGUF V3, ~98.87 MiB, 8192 training context) produced correct output for a deterministic one-word prompt and a coherent (if not strictly constraint-following, as expected from a 135M model) response to an open-ended prompt. This is the first real end-to-end confirmation of the Phase 1 runtime outside of this sandbox's syntax-level checks.

Notes on this API generation (relevant to why the code below is written the way it is):
- `llama_load_model_from_file` / `llama_new_context_with_model` are deprecated in favor of `llama_model_load_from_file` / `llama_init_from_model` — SYJ EdgeMind uses the non-deprecated names.
- `llama_model_params.use_mmap` defaults to `true` via `llama_model_default_params()`; SYJ EdgeMind does not implement a custom mmap layer — it relies on this upstream default, consistent with the Phase 0 requirement to use llama.cpp's real facilities rather than a fake one.

## Dependency licensing

Deferred to the Phase 10 release-candidate audit (see [ROADMAP.md](../ROADMAP.md)). llama.cpp is upstream MIT-licensed at the time of writing, which is why LICENSE provisionally uses MIT — but this repository does not claim full license compatibility until the Phase 10 audit is complete, and no model weights are redistributed under this license (see `models/README.md`).
