# Development

**Status: v0.3.0 — Usage/Quota Manager implemented as a parallel initiative on top of the Phase 1/2 runtime (see "v0.3.0 audit note" below for why this isn't labeled "Phase 3").** The CLI builds and links against a real, pinned llama.cpp (tag `b10375`, see [architecture.md](architecture.md)) on any machine with normal network access. See "What's verified so far" below for exactly what has and hasn't been run in the sandbox used to build this — this sandbox has never actually run `cmake`/`ctest` against real llama.cpp or a real GGUF model; that validation happened separately, on the project maintainer's own real hardware, for Phase 1 only (see `docs/architecture.md`'s "v0.1.1" section). No sandbox in this project's history has validated Phase 2 or v0.3.0 against real hardware.

## v0.3.0 audit note

Before implementing this, the existing `ROADMAP.md` was audited: it documents Phase 3 as **"Model Registry & Verification,"** not a usage/quota system. The v0.3.0 usage-limit request was implemented as an explicitly-labeled **parallel initiative** in `ROADMAP.md` rather than silently renumbering or replacing Phase 3 — see that file's dedicated row and `docs/usage-model.md` for the full design. This is flagged here rather than assumed, per this project's convention of surfacing discrepancies instead of guessing.

## Toolchain

- CMake >= 3.20
- A C++17-capable compiler (MSVC on Windows; gcc/clang elsewhere)
- Git (used implicitly by CMake's `FetchContent` to pull llama.cpp)
- Network access at configure time, to fetch the pinned llama.cpp source

## Build

```
git clone https://github.com/SHalimoosavi/SYJ-EdgeMind.git
cd SYJ-EdgeMind
cmake -S . -B build
cmake --build build --config Release
```

The first `cmake -S . -B build` fetches llama.cpp (tag `b10375`) via `FetchContent` into the build tree's own dependency area (`build/_deps/llama_cpp-src` by default — **not** into `third_party/llama.cpp`; see `third_party/README.md` for why that directory is deliberately left as an unused placeholder) and configures it as a dependency (`LLAMA_BUILD_EXAMPLES`/`TESTS`/`TOOLS`/`SERVER` are all disabled — SYJ EdgeMind only needs the `llama`/`ggml` libraries). This step requires network access; it is not repeated on subsequent builds unless `build/` is deleted.

## Run tests

```
ctest --test-dir build --output-on-failure
```

## Run inference

```
./build/syj-edgemind --model /path/to/model.gguf --context 1024 --threads 4 --max-tokens 64 --memory-budget 3000 --safety-reserve 300
```

or omit a trailing prompt for interactive mode (`/help`, `/info`, `/memory`, `/reset`, `/quit`). You need to supply your own local GGUF file — SYJ EdgeMind does not download or invent model URLs (Phase 3 adds a model registry/downloader; for now you need to already have a `.gguf` file).

## Directory guide

See [architecture.md](architecture.md) for what each directory under `src/`, `platform/`, `tests/`, and `scripts/` is for.

- `src/core/` — configuration (`config.h/.cpp`) and the top-level RAII `Runtime` (`runtime.h/.cpp`). `runtime.h` also defines `RuntimeError`, the explicit error-classification enum the C API maps from — no string pattern-matching at the ABI boundary.
- `src/tokenizer/` — wrapper over llama.cpp's `llama_tokenize`/`llama_token_to_piece`/`llama_vocab_is_eog`, plus (since v0.1.1) model-native chat-template application (`llama_model_chat_template`/`llama_chat_apply_template`)
- `src/context/` — `ContextManager`: bounded token accounting, independent of the Phase 2 memory-budget system
- `src/inference/` — `InferenceEngine` (model/context lifecycle, the Phase 2 memory-admission gate, streaming generation loop) and `Sampler` (llama.cpp sampler chain wrapper)
- `src/memory/` — Phase 2: `memory_types.h` (pure structs, including the fail-closed `MemoryEstimate::valid` flag), `memory_estimator.*`/`memory_budget.*` (pure formulas/policy, zero llama.cpp dependency, directly unit-tested), `memory_observer.*` (the one file bridging real `llama_model_*` getters and `/proc/meminfo` into those pure types)
- `src/usage/` — v0.3.0: `usage_types.h`/`usage_accounting.*` (pure, no I/O, injectable-clock-based), `usage_state_store.*` (the one file touching the filesystem), `usage_manager.*` (coordinates the two, owns the real-vs-fake clock). See `docs/usage-model.md`.
- `src/api/` — `edge_mind_api.h/.cpp`: the single public C API boundary; the CLI and future platform wrappers use only this header
- `src/cli/` — `syj-edgemind` executable, built only against `api/edge_mind_api.h`
- `src/model/` — still empty; Phase 3

## Corrections applied during Phase 2 review

Three specific issues were identified in an initial Phase 2 draft and fixed before this version:

1. **Boundary-semantics wording.** An earlier draft's documentation said "ties go to UNSAFE," which contradicted both the actual `<=` comparison in `MemoryBudgetPolicy::evaluate()` and the tests. The documentation (`docs/memory-model.md`) now correctly states the usable ceiling is inclusive — an estimate exactly equal to `(budget - reserve)` is SAFE. The code was never wrong here; only the prose was.
2. **Fail-closed estimation.** `MemoryEstimator::estimate_kv_cache_bytes()`/`estimate_compute_buffer_bytes()` used to return a bare `uint64_t`, defaulting to `0` for invalid hyperparameters — indistinguishable from a genuine zero-cost estimate, which could make an invalid/unknown configuration look SAFE. They now return `std::optional<uint64_t>` (`std::nullopt` on invalid/out-of-range/overflow-risking input), and `MemoryEstimate` gained an explicit `valid` flag that `MemoryBudgetPolicy::evaluate()` checks first, unconditionally rejecting `valid == false` regardless of byte contents. See `docs/memory-model.md`'s "Fail-closed estimation" section.
3. **CMake dependency location.** An earlier draft pinned `FetchContent`'s `SOURCE_DIR` to `third_party/llama.cpp`, which would have vendored a full llama.cpp checkout into the repository working tree. `SOURCE_DIR` is no longer set, so llama.cpp lands in `build/_deps/` (CMake's normal dependency area) instead — see the comment block in `CMakeLists.txt`.

A fourth, smaller cleanup: the C API's error-status mapping used to pattern-match substrings out of a human-readable error message (fragile — any wording change could silently misclassify an error). `Runtime` now exposes an explicit `RuntimeError` enum via `last_error()`, and the C API maps it with a single exhaustive `switch` (`to_c_status()` in `edge_mind_api.cpp`).

## What's verified so far (this sandbox)

The development sandbox used to implement these phases has no outbound network access (confirmed: `apt-get install cmake` and any `github.com`/`raw.githubusercontent.com` fetch from that sandbox's shell are blocked by its egress allowlist, and no `.git` directory or real clone of this project exists in it), so a full `cmake -S . -B build && cmake --build build` could not be executed there, and no real GGUF model file is present to test against. What *was* actually done:

1. The real, current llama.cpp public API (tag `b10375`) was fetched and read via web tools (which have separate network access from that sandbox's shell) and used as the basis for every `llama_*` call in this codebase, including Phase 2's model-hyperparameter getters (`llama_model_n_layer/n_embd/n_head/n_head_kv`) and the chat-template functions (`llama_model_chat_template`, `llama_chat_apply_template`) — see `docs/architecture.md`'s verified API surface list. The hyperparameter getter names are long-standing, widely-referenced llama.cpp API (confirmed via third-party binding documentation) rather than freshly re-fetched from `llama.h` directly in every pass — flagged here rather than glossed over.
2. **Every llama.cpp-independent source file compiles clean** with `g++ -std=c++17 -Wall -Wextra -Wpedantic`: `core/config.cpp`, `context/context_manager.cpp`, `memory/memory_estimator.cpp`, `memory/memory_budget.cpp`, and (v0.3.0) `usage/usage_accounting.cpp`, `usage/usage_state_store.cpp`, `usage/usage_manager.cpp`.
3. **All 8 llama.cpp-independent/filesystem-only tests were built and actually run**, and all currently pass: `test_config`, `test_context_manager`, `test_memory_estimator`, `test_memory_budget`, `test_memory_admission`, `test_usage_accounting`, `test_usage_state_store`, `test_usage_manager`. `usage_state_store`/`usage_manager` genuinely exercise the real filesystem (temp files under `/tmp`, cleaned up after each test), not a mock. This process caught and fixed real, distinct bugs during development (not staged for effect — genuinely found by a failing test run each time):
   - `MemoryBudgetPolicy::evaluate()` initially treated a degenerate zero-byte estimate against a zero-byte usable ceiling as "safe" (`0 <= 0`); fixed by requiring a strictly positive usable ceiling.
   - A test asserting that specific extreme-but-sane-range hyperparameters would overflow `uint64_t` was itself wrong — verified by hand that `2 * 2048 * 2^24 * 2^20 * 8 = 2^59`, well under `UINT64_MAX ≈ 2^64 - 1`. The test was rewritten to assert the true, verified behavior (no overflow at the documented sane-range bounds) instead of a false premise.
   - (v0.3.0) `UsageStateStore::load()` zeroed the `version` field to `0` as an internal "not yet loaded" marker for the `NotFound` case — but `UsageManager` then used that same struct as the basis for a *fresh save*, and `validate_state()` correctly rejected `version != 1`, so every save-after-fresh-load silently failed. Caught immediately by `test_usage_manager` (12 failing checks on first run); fixed by letting `UsageState{}`'s own default member initializer supply the correct version for a fresh state, since the `NotFound`/`Corrupted`/`Ok` return code already communicates load status without needing a sentinel value inside the struct.
4. The remaining files (`tokenizer.cpp`, `sampler.cpp`, `inference_engine.cpp`, `memory_observer.cpp`, `edge_mind_api.cpp/h`, `runtime.cpp`, `main.cpp`, and every header, including all of `src/usage/`) were syntax-checked with `g++ -fsyntax-only` against a hand-written stub `llama.h` reproducing the verified declarations from step 1 — this catches real typos and API-usage mistakes but is **not** a substitute for linking against the real `libllama`, and does not prove runtime correctness against an actual GGUF model.
5. `edge_mind_api.h` was additionally confirmed to compile as plain C (`gcc -std=c11`).

**Not done in this sandbox, honestly, and not claimed:** an actual `FetchContent`-driven build against real llama.cpp; an actual generation run against a real GGUF model; the Phase 2 memory-admission path exercised end-to-end; the v0.3.0 quota-admission path exercised end-to-end against a real loaded model and real generation (normal load, `/usage`, a deliberately-exhausted quota, confirming a denied quota never reaches memory admission or model loading); anything on Android/Termux ARM64 specifically. This sandbox is a plain Linux container with no Android toolchain and no network — none of that could be attempted here, let alone verified. The project maintainer separately reported a real-hardware Phase 1 validation (Android/Termux, `SmolLM2-135M-Instruct-Q4_K_M.gguf`, tag `v0.1.1`) — see `docs/architecture.md`. Neither the Phase 2 memory-admission code nor the v0.3.0 usage/quota code has been confirmed against that or any other real hardware/model yet. Please run it and report back — that's the one thing this sandbox genuinely cannot do.

## Testing

Test categories (unit / integration / memory / inference / failure) are described in [ROADMAP.md](../ROADMAP.md) Phase 9 and are built incrementally alongside the features they cover.

- `tests/unit/test_config.cpp`, `tests/unit/test_context_manager.cpp` — Phase 1, extended in Phase 2 with memory-budget config validation coverage
- `tests/memory/test_memory_estimator.cpp` — formula correctness (standard MHA, GQA, context/batch scaling), and fail-closed coverage (invalid hyperparameters, GQA-constraint violation, sane-range rejection, verified-by-hand non-overflow at the sane-range bounds)
- `tests/memory/test_memory_budget.cpp` — accept/reject cases including the inclusive exact-boundary case, one-byte-over rejection, reserve-equals-budget and reserve-exceeds-budget degenerate cases, and the fail-closed invalid-estimate-is-always-UNSAFE case
- `tests/memory/test_memory_admission.cpp` — estimator+policy integration against SmolLM2-135M-shaped hyperparameters (fits at context=1024, rejected at context=8192 against a deliberately tiny budget, context scaling monotonicity, and garbage-hyperparameters-cannot-become-SAFE)
- `tests/usage/test_usage_accounting.cpp` — pure policy logic: rollover detection/execution, per-dimension (time/messages/tokens) accept/exhaust boundaries, clock-rollback safety, multi-limit interaction
- `tests/usage/test_usage_state_store.cpp` — real filesystem round-trip, `NotFound` vs `Corrupted` distinction, 6 distinct corruption scenarios (garbage file, missing field, bad version, negative counter, absurd timestamp, non-numeric value), atomic-write cleanup verification
- `tests/usage/test_usage_manager.cpp` — integration with an injected fake clock (no real sleeping): fresh state, restart persistence (a new `UsageManager` instance reading a file the previous instance wrote), exhaustion, reset-period rollover via clock advancement, session-time enforcement, and fail-closed behavior across all three public methods against corrupted state
- `tests/integration/test_invalid_model_path.cpp` — Phase 1, exercises the real `llama_backend_init`/failed-load path without needing a GGUF fixture
