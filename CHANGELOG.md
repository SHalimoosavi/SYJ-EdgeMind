# Changelog

All notable changes to this project will be documented in this file.
Format loosely follows [Keep a Changelog](https://keepachangelog.com/).

## [Unreleased]

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
