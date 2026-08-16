# Changelog

All notable changes to this project will be documented in this file.
Format loosely follows [Keep a Changelog](https://keepachangelog.com/).

## [Unreleased]

### Added — Phase 3: Model Registry & Verification

- `src/model/` module: `model_types.h/.cpp` (pure structs/enums — `GgufValidationStatus`, `ModelMetadata`, `ModelIdentity`, `VerificationStatus`/`VerificationResult`, `RegistryEntry`, sanity-ceiling constants), `gguf_reader.{h,cpp}` (parses GGUF header + metadata KV section directly against the public spec — deliberately zero llama.cpp dependency, so a malformed file is rejected before llama.cpp's own parser ever sees it), `model_hash.{h,cpp}` (self-contained streaming SHA-256, FIPS 180-4 — no crypto dependency added), `model_metadata.{h,cpp}` (llama_ftype -> human label table, presentation-only), `model_verifier.{h,cpp}` (composes filesystem checks + GGUF validation + identity + optional checksum comparison), `model_registry.{h,cpp}` (local, percent-encoded, atomically-persisted registry keyed by content hash)
- `Runtime::load()` gained a model-verification gate, inserted between usage/quota admission and memory admission: config validation -> quota admission -> **model verification** -> memory admission -> model loading. A failed/unverified model never reaches `InferenceEngine::load()` or `llama_model_load_from_file()`
- New `RuntimeError::ModelVerificationFailed`; new `RuntimeConfig` fields `expected_model_checksum_sha256` (optional, empty = no checksum comparison) and `model_registry_path` (default `.syj_edgemind_model_registry`)
- New C API: `SYJ_EDGEMIND_ERROR_MODEL_VERIFICATION_FAILED` (same handle-kept-alive-for-diagnostic-retrieval pattern as `MEMORY_BUDGET_EXCEEDED`/`QUOTA_EXCEEDED`), `syj_edgemind_get_verification_report()`, corresponding config fields
- New CLI flags `--checksum`/`--registry-path` and interactive `/verify` command (SYJ EdgeMind's own pre-load GGUF-level report, distinct from `/info`'s post-load llama.cpp-derived view); load now prints "Verifying and loading model: ..."
- Tests: `test_gguf_reader` (12 real byte-correct fixtures, including two adversarial "absurd declared length" cases), `test_model_hash` (real NIST/FIPS 180-4 known-answer vectors + real-file cross-check against system `sha256sum`), `test_model_verifier`, `test_model_registry` (persistence round-trip, corruption detection, import/dedup, lookup) — all under `tests/model/`
- `tests/usage/test_temp_dir.h` promoted to shared `tests/test_temp_dir.h` (now used by both `tests/usage/` and `tests/model/`) rather than duplicated
- Docs: new `docs/model-registry.md`; updates to `docs/architecture.md`, `README.md`, `ROADMAP.md`

### Known limitations (Phase 3)

- No `/models` (list-all) or standalone `/import` (register without loading) CLI command — the acceptance criterion is met by the automatic import-and-verify step inside every model load; a registry-browsing UI is deferred, not silently dropped (see `docs/model-registry.md`)
- `GgufReader` validates the header and metadata KV section only, never the tensor-info section or tensor data itself — llama.cpp's own loader remains the backstop for tensor-level corruption
- `model_ftype_name()`'s quantization-label table was checked against llama.cpp's `master` branch, not byte-for-byte against the pinned tag `b10375` (presentation-only risk, not a verification-correctness risk)
- Not build-verified against real llama.cpp, a real GGUF model, or real hardware in the sandbox used to implement this — llama.cpp-independent components (all of `src/model/`) were compiled and run for real (4/4 new test binaries pass); `Runtime`/C API/CLI integration points were syntax-checked only. See `docs/development.md` and `docs/model-registry.md`'s "Validation status" section.

### Added — v0.3.0: Usage/Quota Manager (parallel initiative, not part of the original phase-numbered roadmap — see ROADMAP.md's discrepancy note)

- `src/usage/` module mirroring the memory subsystem's pure/bridge split: `usage_types.h` (pure structs — `UsagePolicy`, `UsageState`, `UsageDecision`), `usage_accounting.{h,cpp}` (pure logic, injectable clock, zero I/O), `usage_state_store.{h,cpp}` (the sole file touching the filesystem — versioned, validated, atomically written via write-temp-then-`rename()`), `usage_manager.{h,cpp}` (coordinates the two, owns the real-vs-fake clock)
- Fail-closed on corrupted persisted state: `UsageStateLoadResult` distinguishes `Ok`/`NotFound`/`Corrupted`; corrupted state is never treated as unlimited access and produces a diagnostic distinct from legitimate exhaustion
- `Runtime::load()` gained a quota-admission check (before memory admission/model loading) and `Runtime::generate()` gained a re-check plus post-generation accounting (actual tokens generated, counted via the streaming callback — not estimated)
- New `RuntimeConfig` fields: `session_time_limit_seconds`, `daily_message_limit`, `daily_token_limit`, `reset_period_seconds`, `usage_state_path` (all 0/disabled by default), validated
- New C API: `SYJ_EDGEMIND_ERROR_QUOTA_EXCEEDED` (following the same handle-kept-alive-for-diagnostic-retrieval pattern as `MEMORY_BUDGET_EXCEEDED`), `syj_edgemind_get_usage_report()`, corresponding config fields
- New CLI flags `--time-limit-minutes`/`--message-limit`/`--token-limit`/`--reset-period-hours`/`--usage-state-path` and interactive `/usage` command; a quota denial mid-session prints the diagnostic and continues the session (unlike a genuine engine error, which still ends it)
- Tests: `test_usage_accounting`, `test_usage_state_store` (genuinely exercises the real filesystem), `test_usage_manager` (integration, injected fake clock — no real sleeping) — all under `tests/usage/`; `test_config` extended with usage-limit validation coverage
- Docs: new `docs/usage-model.md`; updates to `docs/architecture.md`, `docs/development.md`, `README.md`, `ROADMAP.md`

### Fixed — v0.3.0

- `UsageStateStore::load()` zeroed `UsageState::version` to `0` as an internal "not yet loaded" marker for the `NotFound` case, but `UsageManager` then used that same struct as the basis for a fresh save, and validation correctly rejected `version != 1` — every save-after-fresh-load was silently failing. Caught immediately by `test_usage_manager` (12 failing checks on first run); fixed by letting `UsageState{}`'s own default member initializer supply the correct version, since the return code already communicates load status.

### Known limitations (v0.3.0)

- No automated recovery from corrupted usage state (manual file deletion only).
- No file locking around concurrent processes sharing the same `usage_state_path`.
- A single `reset_period_seconds` window is shared by both the message and token limits.
- Not build-verified against real llama.cpp or tested against a real GGUF model/real hardware in the sandbox used to implement this — pure/filesystem components were compiled and run for real (8/8 tests pass); llama.cpp-touching integration points were syntax-checked only. See `docs/development.md`.

### Added — Phase 2: Memory Safety Engine

- `src/memory/` module implementing the observation -> estimation -> budget-policy -> admission pipeline:
  - `memory_types.h` — pure data structs (`ModelHyperparams`, `MemoryEstimate`, `MemoryDecision`)
  - `memory_estimator.{h,cpp}` — pure KV-cache/compute-buffer formulas, zero llama.cpp dependency
  - `memory_budget.{h,cpp}` — pure admission policy producing a `STATUS: SAFE`/`STATUS: UNSAFE` diagnostic
  - `memory_observer.{h,cpp}` — the only file bridging real `llama_model_n_layer/n_embd/n_head/n_head_kv`/`llama_model_size()` and `/proc/meminfo` into the pure types above
- `InferenceEngine::load()` now gates `llama_init_from_model()` (the call that actually commits KV-cache/compute memory) behind the admission decision; an unsafe configuration frees the (mmap-loaded) model and returns `EngineError::MemoryBudgetExceeded` without ever creating a context
- New `RuntimeConfig` fields `memory_budget_mb` (default 3000) / `safety_reserve_mb` (default 300), validated (reserve must be less than budget)
- New C API: `SYJ_EDGEMIND_ERROR_MEMORY_BUDGET_EXCEEDED` status, `syj_edgemind_get_memory_report()`, `memory_budget_mb`/`safety_reserve_mb` config fields; `syj_edgemind_create()` keeps the runtime handle alive specifically on a budget-exceeded failure so the diagnostic can still be retrieved
- New CLI flags `--memory-budget`/`--safety-reserve` and interactive `/memory` command
- `RuntimeError` (`src/core/runtime.h`): explicit, exhaustive error classification exposed via `Runtime::last_error()`; the C API's `to_c_status()` maps it with a single switch instead of pattern-matching substrings out of a human-readable message
- Tests: `test_memory_estimator`, `test_memory_budget` (unit-level, pure), `test_memory_admission` (integration-level, estimator+policy together against SmolLM2-135M-shaped hyperparameters) — all under `tests/memory/`, matching the Phase 0 test-category layout; `test_config` extended with memory-budget validation coverage
- Docs updated: `docs/memory-model.md` (full rewrite reflecting the real implementation), `docs/architecture.md`, `docs/development.md`, `ROADMAP.md`, `third_party/README.md` (recreated)

### Corrections applied during Phase 2 review

Three issues identified in an initial Phase 2 draft, fixed before this version — full detail in `docs/development.md`:

1. **Boundary-semantics documentation.** Docs incorrectly said "ties go to UNSAFE," contradicting the actual (correct) `<=` comparison and the tests. Fixed the prose, not the code — the usable ceiling was always meant to be inclusive.
2. **Fail-closed estimation.** `MemoryEstimator`'s two functions now return `std::optional<uint64_t>` instead of a bare `uint64_t` defaulting to `0` on invalid input; `MemoryEstimate` gained an explicit `valid` flag; `MemoryBudgetPolicy::evaluate()` rejects `valid == false` unconditionally with a distinct diagnostic ("Memory estimate could not be established safely..."). Sane-range hyperparameter bounds and overflow-checked integer arithmetic (replacing floating-point) were added to `memory_estimator.cpp`.
3. **CMake dependency location.** `FetchContent_Declare`'s `SOURCE_DIR` no longer points at `third_party/llama.cpp` — llama.cpp now fetches into `build/_deps/` (CMake's normal dependency area) instead of being vendored into the repository working tree.

### Fixed — Phase 2 (bugs, not corrections above)

- `MemoryBudgetPolicy::evaluate()` treated a degenerate zero-byte estimate against a zero-byte usable ceiling as "safe" (`0 <= 0`). Caught by `tests/memory/test_memory_budget.cpp` during development; fixed by requiring a strictly positive usable ceiling.
- A test asserted that specific extreme-but-sane-range hyperparameters would cause a `uint64_t` overflow; verified by hand this is false (`2^59` vs. `UINT64_MAX ≈ 2^64 - 1` at the documented sane-range bounds). Rewrote the test to assert the true, verified non-overflow behavior instead.
- `CMakeLists.txt`'s closing `message(STATUS ...)` referenced `SYJ_EDGEMIND_LLAMA_CPP_TAG`, which was only defined inside the `if(NOT SYJ_EDGEMIND_USE_SYSTEM_LLAMA)` block — an undefined-variable reference when building with `-DSYJ_EDGEMIND_USE_SYSTEM_LLAMA=ON`. Moved the variable definition to unconditional top-level scope.

Full pure-test suite rerun clean after each fix.

### Known limitations (Phase 2)

- System-memory observation (`MemoryObserver::observe_system_memory()`) is implemented for Linux only (`/proc/meminfo`) and is **not yet wired into the admission decision** — the check is against the configured budget only, not live available RAM. Windows/iOS detection and live-RAM admission are left for a later phase.
- KV-cache/compute-buffer figures are documented formulas, deliberately biased high, not measurements — they can still diverge from llama.cpp's actual allocation for unusual architectures.
- Not build-verified against the real linked llama.cpp library or a real GGUF model's memory-admission path in the sandbox used to implement this phase (no outbound network access there); pure components (estimator, policy) were compiled and run for real, and all llama.cpp-touching code was syntax-checked against the verified real API. See `docs/development.md`.

### Changed — chat templates reconciled to match real-hardware-validated Phase 1 (v0.1.1)

- `Tokenizer` now owns model-native chat-template discovery/application (`llama_model_chat_template`, `llama_chat_apply_template`) in addition to tokenization; `InferenceEngine::generate()` uses the model's own embedded template when present, with a documented fallback to raw-prompt tokenization when absent. No hard-coded per-model-family prompt formats.

## [0.1.1] — Native llama.cpp Runtime, real-hardware validated

Confirmed by the project maintainer on Android/Termux against `SmolLM2-135M-Instruct-Q4_K_M.gguf`: clean CMake build, 3/3 tests passing, real inference (deterministic one-word test and an open-ended prompt) succeeded. Commit `230fccc`.

### Added — Phase 1: Native llama.cpp Runtime

- Pinned llama.cpp dependency (tag `b10375`, commit `ba360ef`) fetched reproducibly via CMake `FetchContent`, never tracking `main` — see `docs/architecture.md`
- `syj-edgemind-core` static library: `RuntimeConfig`/validation (`src/core/config.*`), RAII `Runtime` (`src/core/runtime.*`), `Tokenizer` wrapper (`src/tokenizer/*`), bounded `ContextManager` (`src/context/*`), `Sampler` chain wrapper and `InferenceEngine` (`src/inference/*`)
- Public C API boundary (`src/api/edge_mind_api.h/.cpp`) — the only header platform code (CLI today, Windows/iOS wrappers later) is meant to use
- `syj-edgemind` CLI (`src/cli/main.cpp`): `--model`, `--context`, `--threads`, `--temperature`, `--top-p`, `--top-k`, `--max-tokens`; single-shot and interactive (`/help`, `/info`, `/reset`, `/quit`) modes; streaming token output
- Tests: `test_config`, `test_context_manager` (unit), `test_invalid_model_path` (integration)
- Docs updated: `docs/architecture.md` (real pin + verified API surface), `docs/development.md`, `docs/troubleshooting.md`, `README.md`

### Known limitations (Phase 1)

- Not build-verified against the real linked llama.cpp library or a real GGUF model in the sandbox used to implement this phase, due to that sandbox having no outbound network access. See `docs/development.md` → "What's verified so far" for exactly what was and wasn't run.
- No memory-budget enforcement yet (Phase 2) — only a basic context-size sanity bound.
- No model registry, verification, or downloader yet (Phase 3).
- No Windows packaging (Phase 5) or iOS bridge (Phase 6/7) yet.

### Added — Phase 0: Architecture & Repository Bootstrap

- Repository structure (`src/`, `docs/`, `platform/`, `tests/`, `scripts/`, `examples/`, `third_party/`, `models/`)
- README, LICENSE (provisional MIT), CONTRIBUTING, SECURITY, CODE_OF_CONDUCT, ROADMAP
- CMake foundation (no llama.cpp integration yet — that is Phase 1)
- Dependency and llama.cpp version-pinning strategy documented in `docs/architecture.md`
