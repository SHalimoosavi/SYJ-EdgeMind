# Roadmap

SYJ EdgeMind is built one phase at a time. A phase is not started until the previous phase's acceptance criterion is met. Status is updated at the end of each phase.

| Phase | Name | Acceptance Criterion | Status |
|---|---|---|---|
| 0 | Architecture & Repository Bootstrap | Repo skeleton, docs skeleton, CMake foundation, and dependency strategy exist | **Done** |
| 1 | Native llama.cpp Runtime | A local GGUF model can be loaded and can generate text | **Done — confirmed on real hardware (Android/Termux, SmolLM2-135M-Instruct-Q4_K_M.gguf) by the project maintainer; commit `230fccc`, tag `v0.1.1`** |
| 2 | Memory Safety Engine | Runtime refuses unsafe configurations rather than crashing or exhausting RAM | **Implemented, including a fail-closed estimation design (invalid/unavailable hyperparameters reject the configuration, never default to a 0-byte estimate) and 3 corrections applied after initial review — see docs/development.md's "Corrections applied" section. Pure estimator/policy components build-verified and unit-tested for real in this sandbox (5/5 tests pass); llama.cpp-integration points (memory_observer, InferenceEngine wiring) syntax-checked only — not yet confirmed against real llama.cpp/real hardware.** |
| — | **Usage/Quota Manager Foundation (v0.3.0, parallel initiative)** | **A user-configured local usage limit (session time / daily messages / daily tokens) is enforced, persists across restarts, and fails closed on corrupted state** | **Implemented — see docs/usage-model.md. Note: this row was not part of the original phase-numbered plan below; it was introduced by explicit request as parallel work alongside (not instead of) Phase 3. Flagging this discrepancy rather than silently renumbering the existing phases — see docs/development.md's v0.3.0 audit note. 8/8 pure/filesystem tests build-verified and run for real in this sandbox; Runtime/CLI/C API integration syntax-checked only.** |
| 3 | Model Registry & Verification | A user can safely acquire/import a GGUF model and verify it before inference | Not started |
| 4 | Production CLI | A technically non-expert user can run local inference with minimal configuration | Not started |
| 5 | Windows Packaging | A clean Windows machine with the required toolchain can build and run the project reproducibly | Not started |
| 6 | iOS Native Bridge | The same inference core can be invoked from iOS | Not started |
| 7 | iOS Minimal UI | Minimal SwiftUI app: model selection, chat, stop generation, context reset, memory info, offline indicator | Not started |
| 8 | Performance & 4 GB Optimization | Application behaves predictably under the 4 GB memory target, measured on real hardware | Not started |
| 9 | Testing & Hardening | Full unit/integration/memory/corruption/config/platform test coverage; sanitizers and static analysis run | Not started |
| 10 | Release Candidate (v0.1.0-rc1) | Dependency, license, security, README, build-reproducibility, model-registry, and cross-platform audits all pass | Not started |
| 11 | v0.1.0 | CHANGELOG, release notes, supported-model table, benchmark table, known limitations, release published | Not started |

## Non-negotiable constraints throughout all phases

- One inference core (C/C++ + llama.cpp), no duplicated per-platform inference logic.
- CPU-only by default; no GPU/cloud dependency required.
- Offline after model acquisition; zero telemetry.
- No invented benchmark numbers, model URLs, or API functions.
- No placeholder implementations presented as working code.
