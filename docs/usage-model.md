# Usage / Quota Model

**Status: v0.3.0 — implemented as a parallel initiative alongside the phase-numbered roadmap.** See `ROADMAP.md`'s discrepancy note: this was introduced by explicit request, not by the original Phase 0 plan, which had Phase 3 as "Model Registry & Verification." Both are legitimate, independent pieces of work; this document covers only the usage/quota system.

This is a **local, offline, user-configured usage guard** — not a commercial licensing or subscription system, no cloud authentication, no telemetry, no internet requirement. `UsagePolicy::reset_period_seconds` and the overall Policy/State/Accounting/Enforcement separation are deliberately structured so a future licensing tier *could* build on this without a redesign, but nothing here implements licensing today.

## Pipeline

```
Configuration
        |
Usage/quota admission   (UsageManager::check_admission — src/usage/*)
        |
Memory admission          (existing Phase 2 pipeline, untouched)
        |
Model loading / Context / Inference   (existing, untouched)
        |
Usage accounting          (UsageManager::record_generation, after generation)
```

Quota admission is checked in `Runtime::load()` **before** `InferenceEngine::load()` is ever called — a denied quota check never reaches memory observation, model loading, or touches llama.cpp at all. It is re-checked in `Runtime::generate()` before each generation (quota can be exhausted by an earlier call within the same interactive session), and usage is recorded **after** generation completes (or partially completes — see "What counts as usage" below), reflecting what actually happened rather than what was requested.

## Components

Mirrors the memory subsystem's pure/bridge split exactly:

- `src/usage/usage_types.h` — pure structs: `UsagePolicy` (configured limits, 0 = disabled), `UsageState` (persisted counters/timestamps, versioned), `UsageDecision` (allowed/denied + reason + remaining quotas, mirrors `MemoryDecision`)
- `src/usage/usage_accounting.{h,cpp}` — pure logic: given a policy, state, and an explicitly-passed `now` (unix seconds), decides admission and computes remaining quota. Zero I/O, zero OS calls — directly unit-tested (`tests/usage/test_usage_accounting.cpp`) without real time passing.
- `src/usage/usage_state_store.{h,cpp}` — the only file touching the filesystem. Loads/saves a small versioned local text file, atomically (write-temp-then-`rename()`, which is atomic on POSIX/Linux/Android/Termux).
- `src/usage/usage_manager.{h,cpp}` — coordinates the store and accounting behind the interface `Runtime` actually calls; owns the injectable clock (`ClockFn`) that makes deterministic testing possible.

## Persistence format

A small, versioned, line-based text file — deliberately not JSON or a database, to avoid a new third-party dependency for a handful of integers:

```
SYJ_EDGEMIND_USAGE_STATE_V1
version=1
period_start_unix=<int64>
messages_used_this_period=<int64>
tokens_used_this_period=<int64>
session_start_unix=<int64>
```

Written atomically: to `<path>.tmp`, flushed, then `rename()`d over `<path>`. On POSIX this means a process interrupted mid-write leaves the **previous valid** state file intact, never a half-written one.

## Fail-closed on corruption — distinct from legitimate exhaustion

`UsageStateStore::load()` returns one of three distinct outcomes:

- `Ok` — parsed and validated successfully
- `NotFound` — no state file exists yet (fresh install; **not** a corruption event)
- `Corrupted` — the file exists but failed to parse or failed validation (wrong magic line, missing field, unrecognized version, negative counter, out-of-range timestamp, non-numeric value)

`Corrupted` state is **never** treated as "unlimited access." `UsageManager::check_admission()` returns `UsageDecision{allowed=false, outcome=StateCorrupted, ...}` with a diagnostic that explicitly says the state could not be read safely — deliberately different wording from an exhaustion denial ("daily message limit reached"), so a user (or a script parsing the diagnostic) can tell the two apart. `session_start()` and `record_generation()` also refuse (return `false`) against corrupted state, rather than silently overwriting it — recovering from corruption currently requires manually deleting the state file; there is no automated recovery/reset command yet (see Known limitations).

## What counts as usage

- **Messages**: one increment per `Runtime::generate()` call that actually reached the inference engine (i.e., wasn't itself blocked by the quota pre-check).
- **Tokens**: counted as the number of `on_token` callback invocations during that generation — this is exactly one call per token `InferenceEngine::generate()` actually produced, not an estimate.
- **Session time**: not accumulated as a stored duration. `UsageState::session_start_unix` is set once by `session_start()` (called once per `Runtime::load()`), and remaining/exhausted session time is always computed as `now - session_start_unix` at check time. This avoids any drift from a missed accounting call.
- Usage is recorded even when generation errors partway through, since tokens that were genuinely produced genuinely used resources.

## Reset periods

`UsagePolicy::reset_period_seconds` (default 86400 = 24h) controls how often `messages_used_this_period`/`tokens_used_this_period` roll over. Rollover is evaluated lazily — at the next `check_admission()`/`record_generation()` call after the period has elapsed — not via a background timer. A `period_start_unix` of `0` (fresh state) always triggers an initial rollover to establish the first period. A clock that appears to have moved backwards relative to the stored `period_start_unix` is treated as needing a rollover rather than computing a negative elapsed time (a deliberate choice: this can grant a fresh period slightly early on a clock rollback, which is a far better failure mode for a local single-user tool than getting permanently stuck).

## Overflow / corruption safety

- Counters are validated on load against `UsageState::SYJ_EDGEMIND_MAX_SANE_COUNTER` (1e15) and timestamps against `SYJ_EDGEMIND_MAX_SANE_TIMESTAMP` (year 2100) — both generic sanity ceilings independent of any specific policy.
- `UsageManager::record_generation()` saturates rather than wraps if a counter would exceed the sane ceiling — consistent with `MemoryEstimate::total_bytes()`'s saturating-add convention elsewhere in this codebase. A saturated (very large) counter reads as "exhausted" to any sane policy, which is the safe failure direction.
- Negative token counts are refused outright (`record_generation()` returns `false`).

## CLI

```
--time-limit-minutes <n>   Session time limit in minutes (default: unlimited)
--message-limit <n>        Messages allowed per reset period (default: unlimited)
--token-limit <n>          Generated tokens allowed per reset period (default: unlimited)
--reset-period-hours <n>   How often message/token limits reset (default: 24)
--usage-state-path <p>     Local file for persisted usage state (default: .syj_edgemind_usage_state)
```

Interactive command: `/usage` — shows current usage, remaining quota (for whichever dimensions are configured), and when the current period resets. A single command rather than three (`/usage`/`/limits`/`/quota`) was a deliberate choice — the report already covers limits, remaining, and reset time in one place, and three near-duplicate commands would be redundant.

A quota denial during an already-loaded interactive session (as opposed to at load time) prints the usage diagnostic and **continues** the session rather than exiting — `/usage`, `/info`, `/memory`, `/reset` all remain usable. This is different from a genuine `EngineError`, which still ends the interactive loop (per the existing Phase 1 "do not continue with an invalid runtime" rule) — a quota denial means "not right now," not "this runtime is broken."

## C API

`SYJ_EDGEMIND_ERROR_QUOTA_EXCEEDED` follows the same special-case pattern as `SYJ_EDGEMIND_ERROR_MEMORY_BUDGET_EXCEEDED`: `syj_edgemind_create()` keeps the runtime handle alive (does not return `NULL`) specifically so the caller can retrieve the diagnostic via `syj_edgemind_get_usage_report()` before calling `syj_edgemind_destroy()`. `syj_edgemind_config` gained `session_time_limit_seconds`, `daily_message_limit`, `daily_token_limit`, `reset_period_seconds`, `usage_state_path` (all optional/zero-defaulted).

## Known limitations

- No automated recovery from corrupted state — the current mitigation is "delete the state file manually." A `--reset-usage-state` flag or equivalent is a reasonable follow-up, not yet implemented.
- Session time tracking is per-process-lifetime via `session_start()`, called once per `Runtime::load()` — it does not currently distinguish multiple concurrent processes sharing the same `usage_state_path`, and there is no file locking around reads/writes (a race between two processes writing at nearly the same moment could lose one write, though the atomic rename means neither process can observe a torn/partial file).
- `reset_period_seconds` is a single global window shared by both the message and token limits; independent windows per limit type are not supported.
- This has been syntax-checked against the real, verified llama.cpp-adjacent code paths (`Runtime`, C API, CLI) but **not build-verified or run** in any environment with real llama.cpp/a real GGUF model — see `docs/development.md`.
