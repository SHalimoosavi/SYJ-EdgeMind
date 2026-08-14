# Roadmap

SYJ EdgeMind is built one phase at a time. A phase is not started until the previous phase's acceptance criterion is met. Status is updated at the end of each phase.

| Phase | Name | Acceptance Criterion | Status |
|---|---|---|---|
| 0 | Architecture & Repository Bootstrap | Repo skeleton, docs skeleton, CMake foundation, and dependency strategy exist | **Done** |
| 1 | Native llama.cpp Runtime | A local GGUF model can be loaded and can generate text | **Implemented; not yet build-verified against real llama.cpp/a real model (no sandbox network access) — see docs/development.md** |
| 2 | Memory Safety Engine | Runtime refuses unsafe configurations rather than crashing or exhausting RAM | Not started |
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
