# Model Registry & Verification (Phase 3)

**Status: Implemented in this codebase; real Termux/Android hardware and `cmake`/`ctest` validation is PENDING — see "Validation status" at the bottom of this document.** Do not read anything in this file as claiming cross-platform hardware confirmation it does not have.

This document describes the model registry and verification system introduced to satisfy Phase 3's acceptance criterion: *"A user can safely acquire/import a GGUF model and verify it before inference."*

## Why local import, not a downloader

`ROADMAP.md` says "acquire/import." This implementation is **import-only** — there is no model downloader, no catalogue, no remote endpoint of any kind. Per the phase's own stated fallback ("local import is the safest first implementation if no trustworthy registry/download source already exists"): no such source exists anywhere in this codebase or its dependencies, so a downloader would mean either inventing a URL/catalogue (explicitly forbidden) or leaving it as a stub presented as production-ready (also forbidden). Local import — pointing SYJ EdgeMind at a GGUF file already on disk — is therefore the only implementation that satisfies the acceptance criterion without violating those constraints. A future acquisition layer (e.g. a curated, explicitly-configured mirror list) is a legitimate future phase, not something this phase should improvise.

## Pipeline

```
PATH
  |
Filesystem checks        (exists, regular file, readable, non-empty — ModelVerifier)
  |
GGUF structural validation   (GgufReader — header + metadata KV section, zero llama.cpp dependency)
  |
Model identity                (Sha256 of the full file — ModelVerifier)
  |
Checksum comparison           (only if an expected checksum was configured — ModelVerifier)
  |
Registry recording            (ModelRegistry::import_model — only for outcomes with a computed identity)
  |
Usage/quota admission         (existing v0.3.0 pipeline, evaluated BEFORE this stage — see below)
  |
Memory admission               (existing Phase 2 pipeline, untouched)
  |
Model loading / Inference      (existing, untouched)
```

Full ordering inside `Runtime::load()`: **config validation → quota admission → model verification → memory admission (inside `InferenceEngine::load()`) → model loading.** A denied quota check never reaches model verification. A failed verification never reaches memory admission, model loading, or `llama_model_load_from_file()` — this is the actual enforcement point for "never load an unverified model into inference." Verification runs *before* quota's session-time accounting would matter, but *after* the quota gate itself, matching the requested "usage admission → model verification → memory safety admission" ordering exactly.

## Components

Mirrors the memory/usage subsystems' pure/bridge split:

- `src/model/model_types.h/.cpp` — pure structs and enums: `GgufValidationStatus`, `ModelMetadata`, `ModelIdentity`, `VerificationStatus`/`VerificationResult`, `RegistryEntry`, `GgufLimits` (the sanity ceilings applied while parsing untrusted files).
- `src/model/gguf_reader.h/.cpp` — parses a GGUF file's **header and metadata key-value section only** (never the tensor-info array or tensor data). **Deliberately independent of llama.cpp** — implements the public GGUF binary format directly against the spec at <https://ggml-org-ggml.mintlify.app/formats/gguf> (fetched and verified 2026-08-16, not assumed from memory). This means a malformed/adversarial file is rejected before llama.cpp's own parser ever sees it (defense in depth), and this reader is unit-testable without linking llama.cpp at all.
- `src/model/model_hash.h/.cpp` — a self-contained SHA-256 (FIPS 180-4) implementation, streaming (fixed-size chunks, never loading a whole multi-gigabyte model into memory at once). See "Model identity" below for why this was written rather than adding a crypto dependency.
- `src/model/model_metadata.h/.cpp` — maps a GGUF `general.file_type` value to llama.cpp's human-readable quantization label (e.g. `Q4_K_M`). Presentation-only; never used in any verification/admission decision (see the file's header comment for the drift risk this deliberately isolates).
- `src/model/model_verifier.h/.cpp` — composes the above: filesystem checks → `GgufReader::validate()` → `compute_model_identity()` → optional checksum comparison. The single entry point every caller (`Runtime`, CLI, C API) uses to decide whether a file is safe to proceed.
- `src/model/model_registry.h/.cpp` — the only filesystem-touching file besides the two above that read model files themselves. Persists a local, versioned, percent-encoded line-based record of imported models (see "Persistence" below).

## Model identity

**Real cryptographic hash, not a filename or ad-hoc checksum**: SHA-256 of the full file content, lowercase hex, deterministic for identical bytes regardless of path/name/mtime.

**Why a self-contained implementation instead of a dependency**: the project has zero existing crypto dependency, and the CMake dependency strategy (FetchContent for llama.cpp only, "no unnecessary dependencies" as an explicit non-negotiable rule) weighs against adding OpenSSL/mbedTLS/libsodium for one hash function. SHA-256 is a small (~150 line), extremely well-specified (FIPS 180-4), standard algorithm — not an invented hash. The implementation was validated against real NIST/FIPS known-answer test vectors (`""`, `"abc"`, the standard two-block test string, and the one-million-`'a'` vector) and cross-checked against system `sha256sum` output on real files, including the exact chunked-`update()` pattern real file hashing uses. See `tests/model/test_model_hash.cpp`.

**Tradeoff accepted**: a hand-written crypto primitive carries more review burden than a linked, widely-audited library. This was a deliberate choice given the "no unnecessary dependencies" constraint and the algorithm's small, well-defined surface area — documented here rather than made silently, per the phase's explicit requirement.

## Verification

`VerificationStatus` distinguishes: `PathNotFound`, `PathNotRegularFile` (directories, symlinks-to-nowhere, special files), `PathUnreadable`, `FileEmpty`, `InvalidMagic`, `UnsupportedVersion`, `TruncatedHeader`, `MalformedMetadata`, `ChecksumMismatch`, `Verified`. Every value is a distinct enum, never a string classification — `is_verified(status)` is the single function every caller checks, so "what counts as safe to proceed" has exactly one definition in the codebase.

**GGUF validation security properties** (see `gguf_reader.cpp`'s inline comments for the exact mechanism of each):
- Every length/count field read from the file (string lengths, array counts, `metadata_kv_count`, `tensor_count`) is checked against both a sane ceiling (`GgufLimits`) *and* the file's actual remaining size *before* any read/allocation is attempted — an adversarial file claiming a 2⁶⁴-1-byte string is rejected in constant time, never attempted as an allocation.
- String *values* for uninteresting keys are never buffered — only their length is validated and the bytes are skipped (`seekg`), so memory use is bounded regardless of how many irrelevant metadata entries a file contains.
- GGUF version 1 (which used 32-bit count fields, unlike version 2/3's 64-bit fields) is explicitly reported as `UnsupportedVersion` rather than misparsed under version-2/3 assumptions.
- Nested arrays (array-of-array) are rejected as malformed — not part of the documented format, not produced by any real writer, and accepting them would require open-ended recursive bounds-checking for no practical benefit.

**Identity is only computed for structurally-valid files** — hashing a file already known to be malformed wastes I/O for no benefit, since nothing downstream needs the identity of a rejected file.

**Checksum comparison** is case-insensitive (a user may paste an uppercase-hex checksum) and only performed when `RuntimeConfig::expected_model_checksum_sha256` is non-empty; GGUF structural validation is mandatory either way.

## Model registry

**Data model** (`RegistryEntry`): `model_id` (the SHA-256 identity — primary key), `display_name`, `local_path`, `file_size_bytes`, `format` (currently always `"gguf"`), `architecture`, `quantization` (human label), `verification_status`, `verified_at_unix`, `expected_checksum_sha256`. Every field maps to a documented registry operation (CLI display, duplicate detection, or lookup) — nothing speculative.

**Persistence**: a local, versioned, plain-text file (default `.syj_edgemind_model_registry`, configurable via `RuntimeConfig::model_registry_path`). Format: a magic line (`SYJ_EDGEMIND_MODEL_REGISTRY_V1`) followed by blank-line-separated entry blocks, each a fixed set of `key=value` lines. String fields are percent-encoded (only `%`, `\n`, `\r`, `=` need escaping) so a path or name containing `=` round-trips exactly. Writes are atomic (`.tmp` file + `rename()`, same pattern as `UsageStateStore`). Loading distinguishes `NotFound` (fresh install — empty, not an error) from `Corrupted` (wrong magic line, missing required field, unrecognized field name, unparseable number, or an out-of-range `verification_status` value) — a corrupted registry is **never** silently treated as empty; that would quietly discard a user's import history rather than surfacing the problem, mirroring `UsageStateStore`'s `NotFound`/`Corrupted` split exactly.

**Identity and lookup**: `find_by_id(registry_path, model_id)` looks up a single entry by its SHA-256 identity.

**Duplicate behavior**: importing a file whose computed identity already exists in the registry does **not** create a second entry — it updates the existing entry's `local_path`/`display_name`/verification fields in place (the file may have been moved or renamed since the last import) and reports `was_new_entry = false`. A genuinely new identity gets a new entry and `was_new_entry = true`.

**What gets recorded**: only outcomes with a computed identity — `Verified` and `ChecksumMismatch` (both require a fully-hashed file). Every earlier rejection (`PathNotFound` through `MalformedMetadata`) has no deterministic identity to key an entry on, so the registry is left untouched for those — the caller still receives the full rejection detail via the returned `VerificationResult`, it simply isn't persisted.

## Runtime integration

`Runtime::load()`'s order is: **config validation → quota admission (`UsageManager`) → model verification (`ModelRegistry::import_model`, composing `ModelVerifier`) → memory admission (inside `InferenceEngine::load()`) → model loading.** A new `RuntimeError::ModelVerificationFailed` covers every non-`Verified` outcome (the granular `VerificationStatus` is available separately via `Runtime::verification_report()`, mirroring how `MemoryBudgetExceeded` is one `RuntimeError` value even though the memory-budget diagnostic itself is more detailed). Phase 2's memory-admission semantics and v0.3.0's usage/quota semantics are both completely untouched — verification is inserted as a new gate between two existing, unmodified stages, not a rewrite of either.

The C API gained `SYJ_EDGEMIND_ERROR_MODEL_VERIFICATION_FAILED`, `syj_edgemind_get_verification_report()`, and two new config fields (`expected_model_checksum_sha256`, `model_registry_path`). Following the existing `MEMORY_BUDGET_EXCEEDED`/`QUOTA_EXCEEDED` pattern, `syj_edgemind_create()` keeps the runtime handle alive specifically on this error so the diagnostic can still be retrieved before `syj_edgemind_destroy()`.

The CLI gained `--checksum <sha256>` and `--registry-path <path>` flags, a `/verify` interactive command (prints SYJ EdgeMind's own pre-load GGUF-level verification report, distinct from `/info`'s post-load llama.cpp-derived view), and now prints "Verifying and loading model: ..." during load (the C API's `syj_edgemind_create()` performs both stages synchronously in one call, so the message is deliberately phrased to not claim a separate visible verification step where none exists).

## Deliberately out of scope for this phase

- **A `/models` (list-all) or standalone `/import` (register without loading) CLI command.** The core acceptance criterion — verify before inference — is already satisfied by the automatic import-and-verify step inside every `--model` load; a separate registry-browsing/standalone-import UI is a real, legitimate feature but would need new C API surface (listing, formatting) not required to meet this phase's criterion. Deferred, not silently dropped.
- **Tensor-info-section parsing.** `GgufReader` deliberately stops after the metadata KV section — it never validates individual tensor shapes/offsets/types. This is consistent with "verification should be cheap, not a full model load" but means a file with a structurally valid header+metadata and a corrupted tensor section would still pass `GgufReader::validate()`; llama.cpp's own loader (downstream, after verification) is the actual backstop for tensor-level corruption.
- **`model_ftype_name()`'s table was checked against llama.cpp's `master` branch, not the exact pinned tag `b10375`** — the sandbox that produced this could not fetch that specific tag's header byte-for-byte. This is a presentation-only risk (a possibly-stale human-readable label), not a verification-correctness risk — see `model_metadata.h`'s header comment.

## Validation status

**What was validated for real, in this sandbox:**
- `GgufReader` against 12 real, byte-correct fixtures (valid GGUF v2 and v3, invalid magic, truncated header, truncated mid-metadata, empty file, two adversarial "absurd declared length" cases, unknown value-type code, missing-architecture-key, nonexistent path) — every case produced the correct status, no crashes, no unbounded allocation observed on the adversarial cases.
- `Sha256` against real NIST/FIPS 180-4 known-answer vectors, and `compute_model_identity()` against real files cross-checked byte-for-byte against system `sha256sum`.
- `ModelVerifier` and `ModelRegistry` each via a dedicated real test binary (`tests/model/test_model_verifier.cpp`, `tests/model/test_model_registry.cpp`) covering filesystem checks, checksum match/mismatch/case-insensitivity, corruption detection, import/dedup, and lookup.
- `src/core/config.cpp` compiled for real (no llama.cpp dependency). `src/core/runtime.cpp`, `src/api/edge_mind_api.cpp`, and `src/cli/main.cpp` were **syntax-checked** (`g++ -fsyntax-only`) — genuinely llama.cpp-independent checks for `runtime.cpp` and `main.cpp` (both only depend on forward-declared/opaque types), not stub-dependent.

**What was NOT validated (same limitation as every prior phase in this project):** this sandbox has no `.git` (never a real clone), no `cmake` binary (network-blocked package install, confirmed this session), and no real llama.cpp link. `cmake -S . -B build`, `ctest --test-dir build --output-on-failure`, `git diff --check`, and any Android/Termux/real-hardware run are all pending the maintainer's own environment — exactly the "hardware/runtime validation pending" status `ROADMAP.md` records for this phase, per the phase's own instruction not to mark it Done otherwise.
