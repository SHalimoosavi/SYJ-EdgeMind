# Memory Model

**Status: Phase 2 — implemented.** Memory-budget admission is enforced in `InferenceEngine::load()`. See `docs/architecture.md` for the pipeline this document describes in more detail, and `docs/development.md` for what has and hasn't been validated where.

## Pipeline

```
Memory observation  (src/memory/memory_observer.*  — the only file touching llama.cpp/OS)
        |
Memory estimation   (src/memory/memory_estimator.*  — pure formulas, fail-closed)
        |
Budget policy        (src/memory/memory_budget.*     — pure admission decision, fail-closed)
        |
Admission decision    (safe / unsafe, always with a diagnostic)
        |
Runtime/model loading (InferenceEngine::load() acts on the decision)
```

Each stage is a separate, independently-testable component. `memory_estimator.*` and `memory_budget.*` have zero llama.cpp dependency by design, specifically so they can be unit-tested without the pinned dependency or a GGUF file being available (see `tests/memory/`).

## What's observed vs. estimated

| Quantity | Source | Kind |
|---|---|---|
| Model weight size | `llama_model_size()` after `llama_model_load_from_file()` | **Observed** — but see caveat below |
| Model hyperparameters (n_layer, n_embd, n_head, n_head_kv) | `llama_model_n_layer/n_embd/n_head/n_head_kv()` | **Observed** (actual accessors) |
| KV-cache size | `MemoryEstimator::estimate_kv_cache_bytes()` — `2 * n_layer * n_ctx * n_embd_gqa * bytes_per_element`, where `n_embd_gqa = n_embd * n_head_kv / n_head` | **Estimated** (documented formula, not measured) |
| Compute-buffer size | `MemoryEstimator::estimate_compute_buffer_bytes()` — `n_batch * n_embd * 4 bytes * 4x` (deliberately conservative multiplier) | **Estimated**, deliberately biased high |
| Runtime overhead | fixed 180 MB default | **Documented constant**, not measured |
| System memory available | `/proc/meminfo` (Linux only) | **Observed where implemented**; not currently wired into the admission decision (see Known limitations) |

**Caveat on "observed" weight size:** `llama_model_size()` is llama.cpp's own reported model size — a real measurement, not a guess. It is **not** equivalent to process RSS or currently-resident physical RAM: mmap'd weight pages that haven't been touched yet aren't necessarily resident. Treat it as "the size llama.cpp itself accounts for this model," not as a live RSS reading.

## Fail-closed estimation (Correction #2)

`MemoryEstimator::estimate_kv_cache_bytes()` and `estimate_compute_buffer_bytes()` return `std::optional<uint64_t>`, **not** a bare `uint64_t`. They return `std::nullopt` — never a fabricated `0` — when:

- any hyperparameter is non-positive or outside a documented sane range (`SYJ_EDGEMIND_MAX_SANE_LAYERS`, `_EMBD`, `_HEADS`, `_CTX`, `_BYTES_PER_ELEMENT` in `memory_estimator.h`), which also guards against a corrupt/adversarial GGUF reporting garbage metadata
- `n_head_kv > n_head` (violates the GQA constraint)
- the underlying arithmetic would overflow `uint64_t` (checked multiplication throughout, not floating-point — see `checked_mul` in `memory_estimator.cpp`)

Why this matters: a `0`-byte estimate and "this could not be safely estimated" are different facts. If invalid input silently produced `0`, it would look identical to "this genuinely takes no memory" when summed — making an unsafe or unknown configuration falsely appear SAFE. `MemoryEstimate::valid` (default `true`) is the explicit signal: whoever assembles an estimate from `nullopt` results **must** set `valid = false` and must not substitute an invented byte count. `MemoryBudgetPolicy::evaluate()` treats `valid == false` as UNSAFE unconditionally, regardless of what the byte fields contain, with a diagnostic distinct from the ordinary over-budget message:

```
STATUS: UNSAFE

Memory estimate could not be established safely because required
model hyperparameters were invalid or unavailable.

Recommended action:
Verify that the loaded GGUF model is compatible with the supported
llama.cpp API.
```

`MemoryEstimate::total_bytes()` is itself overflow-safe (saturates to `UINT64_MAX` on unsigned wraparound rather than silently wrapping to a small number) as defense-in-depth, though in practice this should never trigger given the individually-bounded components above.

**Verified honestly:** the current `SYJ_EDGEMIND_MAX_SANE_*` bounds were chosen conservatively enough that the KV-cache formula cannot actually overflow `uint64_t` even at their extremes (`2 * 2048 * 2^24 * 2^20 * 8 = 2^59`, vs. `UINT64_MAX ≈ 2^64 - 1`) — confirmed by hand and exercised in `tests/memory/test_memory_estimator.cpp`. The overflow-checked arithmetic is genuine defense-in-depth for if those bounds are ever loosened, not something the current bounds can trigger through the public API.

## Admission policy (Correction #1 — boundary semantics)

A configuration is **SAFE** iff ALL of:

1. `estimate.valid` is `true`
2. `usable_ceiling = (memory_budget_mb - safety_reserve_mb) * 1MB` is strictly positive
3. `estimate.total_bytes() <= usable_ceiling`

**The usable ceiling is INCLUSIVE.** An estimate exactly equal to the ceiling is SAFE — the safety reserve is what already provides the required headroom below the raw budget; there is no additional implicit margin applied at the comparison itself. (An earlier draft of this document said "ties go to UNSAFE," which contradicted the implementation and the tests and has been corrected.) One byte over the ceiling is UNSAFE — this boundary has no leniency in either direction.

Separately, `RuntimeConfig::validate_config()` rejects `safety_reserve_mb >= memory_budget_mb` outright at the configuration layer, so a zero-or-negative usable ceiling shouldn't reach the policy in normal use — the policy still refuses to be silently permissive about that degenerate case if it ever does (a zero usable ceiling is never SAFE, even for an all-zero-but-technically-`valid` estimate).

Defaults: `memory_budget_mb = 3000`, `safety_reserve_mb = 300`.

Example diagnostic (real output format from `MemoryBudgetPolicy::format_diagnostic`):

```
Model weights (observed): 99.0 MB
KV cache estimate:             12.7 MB
Compute buffer estimate:       9.0 MB
Runtime overhead estimate:     180.0 MB

Estimated total:               300.7 MB
Safety reserve:                300.0 MB
Memory budget:                 3000.0 MB

STATUS: SAFE
```

## Where the admission check sits in the load sequence

`InferenceEngine::load()` follows this exact order:

1. Validate configuration.
2. Initialize the llama.cpp backend if not already done.
3. Load the model via `llama_model_load_from_file()` (mmap-based, per Phase 0/1's no-fake-mmap requirement).
4. Observe model hyperparameters and size (`MemoryObserver`).
5. Estimate KV-cache and compute-buffer memory (`MemoryEstimator`; fails closed to `valid = false` on any invalid input).
6. Evaluate `MemoryBudgetPolicy`.
7. Store the diagnostic (retrievable via `Runtime::memory_report()` / `syj_edgemind_get_memory_report()` even on failure).
8. If UNSAFE: free the model, **do not create a context**, return `MemoryBudgetExceeded`.
9. If SAFE: call `llama_init_from_model()` and continue normal Phase 1 initialization.

This ordering is deliberate: reading a model's metadata via mmap doesn't force its full weight size into resident memory, so the observation step itself stays cheap even on a tight budget; it's context creation that actually commits KV-cache/compute-buffer RAM, so that's what's gated behind the admission decision.

## Context profiles (Phase 1, unchanged)

| Profile | Context |
|---|---|
| ULTRA_LOW | 512 |
| LOW | 1024 |
| BALANCED | 2048 |
| ADVANCED | 4096 |

`RuntimeConfig` still enforces its own basic sanity bound (16–8192 tokens) independent of the memory-budget system — the sanity bound catches obviously-wrong input before any model is even loaded; the budget check catches configurations that are individually reasonable but don't fit *this* model on *this* budget.

## C API error classification

`syj_edgemind_status` is derived from an explicit `RuntimeError` enum (`src/core/runtime.h`) via a total switch (`to_c_status()` in `edge_mind_api.cpp`) — **not** by pattern-matching substrings out of a human-readable error message. `Runtime::last_error()` is the single source of truth the C API reads; the message string returned by `Runtime::load()`/`generate()` is for humans, the enum is for the ABI boundary.

## Known limitations

- **System memory observation is Linux-only** (`/proc/meminfo`) and, as of Phase 2, is **not yet factored into the admission decision** — `MemoryObserver::observe_system_memory()` exists and is unit-testable in isolation, but `InferenceEngine::load()` currently checks the estimate against the *configured* budget only, not against live available RAM. A configured budget that's larger than what's actually free will not be caught until the OS itself intervenes. Wiring live system-memory observation into the admission decision (and doing it for Windows/iOS) is left for a future phase.
- KV-cache and compute-buffer figures are **formulas, not measurements** — they're deliberately conservative (biased toward overestimating) but can still diverge from llama.cpp's actual allocation, especially for architectures with unusual attention/FFN shapes this project hasn't specifically accounted for.
- The `runtime_overhead_bytes` constant (180 MB) is a documented default, not something measured per-platform.
- Automatic context-size reduction on rejection ("recommended action: reduce context") is suggested in the diagnostic text but **not applied automatically** — the runtime refuses and reports; it does not retry with a smaller context on the caller's behalf.
- The sane-range hyperparameter bounds (`SYJ_EDGEMIND_MAX_SANE_*`) are chosen to comfortably exceed any known real model as of this writing, not derived from any formal limit — a legitimate future model could in principle exceed them and be incorrectly rejected as "invalid" rather than "too large for the budget." This trade-off (reject unknown-shaped input rather than risk silent overflow) is deliberate.
