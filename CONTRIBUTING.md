# Contributing to SYJ EdgeMind

Thanks for your interest. SYJ EdgeMind is currently in early, phase-by-phase bootstrap (see [ROADMAP.md](ROADMAP.md)) and is maintained tightly during this stage — please open an issue before submitting large PRs so effort isn't duplicated or wasted.

## Ground rules

- **One inference core.** Do not add a second, parallel implementation of inference logic for a platform. All platforms call the shared C API in `src/api/`.
- **No heavyweight runtimes in the core.** The engine (`src/`) stays C/C++ against llama.cpp. Python/Node are acceptable only for tooling and scripts (`scripts/`), never for the inference path.
- **Memory discipline.** Any change that loads a model, allocates a KV cache, or grows context must go through `src/memory/` and respect the configured budget. No silent overruns.
- **No fake implementations.** Do not land `// TODO: integrate llama.cpp`-style stubs presented as working code. If a dependency is genuinely missing, say so in the PR description.
- **No invented numbers.** Benchmark results, memory figures, and model URLs must be real and reproducible, not illustrative.
- **Offline-first.** No telemetry, analytics, hidden network calls, or cloud fallbacks, ever.

## Development setup

See [docs/development.md](docs/development.md).

## Pull requests

1. Open an issue describing the change first for anything non-trivial.
2. Keep PRs scoped to a single concern.
3. Include or update tests under `tests/` for the category you touched (unit / integration / memory / inference).
4. Build with warnings enabled (`-Wall -Wextra -Wpedantic` or the MSVC equivalent) and fix what's reasonably fixable.
5. Update relevant docs in `docs/` in the same PR.

## Reporting security issues

Do not open a public issue for a security vulnerability — see [SECURITY.md](SECURITY.md).

## Code of conduct

Participation in this project is governed by [CODE_OF_CONDUCT.md](CODE_OF_CONDUCT.md).
