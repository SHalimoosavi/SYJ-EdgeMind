# Security

See [SECURITY.md](../SECURITY.md) at the repo root for how to report a vulnerability. This page holds the fuller design-level detail as it's implemented.

## Principles (enforced from Phase 1 onward)

- No telemetry, no analytics, no hidden networking, no cloud fallback.
- No arbitrary remote execution; no unsafe shell execution.
- No secrets or API keys anywhere in the repository or runtime.
- Model files are verified (SHA-256) before load; a failed or missing verification is a hard error.
- All allocations related to model loading and the KV cache are bounded by the configured memory budget (see [memory-model.md](memory-model.md)) — no unbounded growth.
- Input validation on configuration and CLI arguments; invalid configuration is rejected with a clear error, never silently coerced.
- The model *downloader* is architecturally separate from the *inference runtime* — inference never triggers network access.

## Status

Phase 0: this document describes intended guarantees. None of the enforcement code exists yet — it lands with Phase 1 (basic input/file handling), Phase 2 (memory bounds), and Phase 3 (model verification), and is specifically exercised by the "failure tests" category in Phase 9.
