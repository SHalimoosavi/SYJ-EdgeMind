# Architecture

## Overview

```
        SYJ EdgeMind
              |
     +--------+--------+
     |                 |
Windows CLI        iOS App/Core
     |                 |
     +--------+--------+
              |
        Native C API        <- src/api/
              |
       SYJ Edge Runtime      <- src/core, src/memory, src/model,
              |                 src/inference, src/context, src/tokenizer
          llama.cpp           <- third_party/llama.cpp (pinned commit)
              |
    GGUF Quantized Model
              |
       CPU / RAM / mmap
```

There is exactly one inference core. Platform layers (`platform/windows`, `platform/ios`) are thin wrappers that call `src/api/edge_mind_api.h` and must never contain their own inference logic or duplicate anything in `src/inference/`.

## Components (Phase 0: interfaces/placeholders only, no implementation yet)

- `src/core/` — runtime lifecycle and configuration (`runtime.h/.cpp`, `config.h/.cpp`)
- `src/memory/` — memory budget + estimator (Phase 2)
- `src/model/` — model registry + manager, SHA-256 verification (Phase 3)
- `src/inference/` — llama.cpp integration, sampler (Phase 1)
- `src/context/` — context/token accounting and truncation strategy (Phase 1/2)
- `src/tokenizer/` — tokenizer wrapper (Phase 1)
- `src/api/` — the single stable C API every platform wrapper calls (Phase 1)
- `src/cli/` — Windows/desktop CLI entry point (Phase 1, hardened in Phase 4)
- `platform/windows/` — Windows-specific build notes and packaging (Phase 5)
- `platform/ios/` — Objective-C++/Swift bridge over the C API (Phase 6/7)

## Dependency strategy

Kept deliberately minimal:

- **llama.cpp** — the only inference dependency. Vendored under `third_party/llama.cpp` at a pinned commit (see below), not tracked against `main`.
- **C/C++ standard library** and **CMake** — build tooling.
- No Electron, React, Next.js, Python runtime, or Node runtime in the inference core. Python/Node may appear only as external tooling in `scripts/` (e.g. a model downloader) — never on the inference path.
- SwiftUI is permitted only in the optional iOS UI layer (Phase 7), which calls the same C API.

## llama.cpp version pinning

**Status (Phase 0): not yet pinned.** No llama.cpp commit has been vendored or verified against this repository yet — that happens at the start of Phase 1, when a specific commit is selected, recorded here, and the actual API surface it exposes is verified against what `src/inference/` and `src/tokenizer/` use. This file will be updated at that point with:

```
llama.cpp commit: <to be filled in during Phase 1>
Pinned on:         <date>
Reason for pin:    <API stability / feature availability / etc.>
```

The build must never silently track `main`. Any future bump is a deliberate, documented change, not an automatic update.

## Dependency licensing

Deferred to the Phase 10 release-candidate audit (see [ROADMAP.md](../ROADMAP.md)). llama.cpp is upstream MIT-licensed at the time of writing, which is why LICENSE provisionally uses MIT — but this repository does not claim full license compatibility until the Phase 10 audit is complete, and no model weights are redistributed under this license (see `models/README.md`).
