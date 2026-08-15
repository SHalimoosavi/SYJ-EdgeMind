# Security

See [SECURITY.md](../SECURITY.md) at the repo root for how to report a vulnerability. This page holds the fuller design-level detail as it's implemented.

## Principles

- No telemetry, no analytics, no hidden networking, no cloud fallback, no cloud authentication.
- No arbitrary remote execution; no unsafe shell execution.
- No secrets or API keys anywhere in the repository or runtime.
- Input validation on configuration and CLI arguments; invalid configuration is rejected with a clear error, never silently coerced (`RuntimeConfig::validate_config()`).
- The model *downloader* (Phase 3, not yet implemented) is architecturally separate from the *inference runtime* — inference never triggers network access.

## Implemented and tested

- **Memory bounds (Phase 2).** Model weight, KV-cache, and compute-buffer memory is estimated before a context is created; a configuration whose total exceeds the configured budget is refused, and no `llama_context` is ever created for it. Estimation is fail-closed: invalid or out-of-sane-range model hyperparameters (e.g. from a corrupt/adversarial GGUF) produce an explicitly *invalid* estimate rather than a fabricated `0`, which `MemoryBudgetPolicy` rejects unconditionally. See [memory-model.md](memory-model.md).
- **Untrusted local persisted state (v0.3.0).** The usage/quota state file is treated as untrusted mutable input on every load: version, timestamps, and counters are all range-checked (`UsageStateStore::validate_state()`); negative counters, absurd (year-2100+) timestamps, unrecognized versions, missing fields, and malformed lines are all rejected as `Corrupted` rather than partially trusted. A corrupted file is never interpreted as "unlimited access" — `UsageManager` fails closed and denies admission. Writes are atomic (write-temp-then-`rename()`) so a process interrupted mid-write can't leave a torn file that a later read might partially trust. See [usage-model.md](usage-model.md).
- **Overflow guards.** `MemoryEstimator`'s formulas use checked integer multiplication (not floating-point) and return "invalid" rather than a wrapped value on overflow. `UsageManager::record_generation()` saturates counters rather than wrapping past a sane ceiling.

## Not yet implemented

- **Model verification (Phase 3).** SHA-256 verification of downloaded/imported GGUF files does not exist yet — there is no model downloader at all yet, so this section of the original threat model is still aspirational, not enforced.
- **Live system-memory checks.** The memory-budget admission decision checks the estimate against the *configured* budget only, not live available RAM (Linux-only `/proc/meminfo` reading exists in `MemoryObserver` but isn't wired into the decision — see `memory-model.md`'s Known Limitations).
- **File locking on the usage-state file.** No protection against two processes racing to write it concurrently (the atomic rename means neither process can observe a torn file, but one process's write can still be silently lost to the other's).

## Verification status of the above

Everything under "Implemented and tested" has real, executed test coverage in this project's development sandboxes for its llama.cpp-independent logic (`tests/memory/`, `tests/usage/` — see `docs/development.md` for exactly what ran and what didn't). Neither has been exercised against a real, linked llama.cpp build or on real target hardware from within this conversation; the project maintainer separately validated Phase 1 (not Phase 2, not v0.3.0) on real Android/Termux hardware. See `docs/development.md` and `docs/troubleshooting.md` for the precise, current verification status — this file describes design intent and what's been locally tested, not a claim of production hardening.
