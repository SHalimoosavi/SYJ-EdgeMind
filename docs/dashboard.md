# Local Operations Dashboard — Phase 7

**Status:** Implemented in source; a full build/test run requires the pinned llama.cpp dependency and the pinned cpp-httplib dependency to be available to CMake. The implementation is intentionally not presented as hardware-validated from an environment that cannot fetch those dependencies.

## Purpose

The dashboard is an operational control plane for SYJ EdgeMind itself. It is not a generic file manager, shell, cloud console, or replacement for llama-server.

It exists to make the existing native runtime observable and operable on the low-memory device where SYJ EdgeMind actually runs:

- local runtime state
- registered/verified models
- live system-memory observation where the platform supports it
- memory admission diagnostics
- usage/quota diagnostics
- load / unload / reload / context reset
- streaming generation through the existing C API

The dashboard does **not** introduce a second inference path. The platform-facing dashboard executable includes the same `api/edge_mind_api.h` boundary as the CLI and never includes `llama.h` or SYJ EdgeMind C++ internals.

## Security boundary

- Binds to `127.0.0.1` only.
- Generates a 256-bit bearer token from OS secure entropy at startup.
  - POSIX/Android/Termux: `/dev/urandom`.
  - Windows: `BCryptGenRandom` with the system-preferred RNG.
- Fails closed if secure entropy cannot be obtained.
- API routes require `Authorization: Bearer <token>`.
- The root HTML shell contains no runtime data and is intentionally static; the operational API remains authenticated.
- Request bodies are bounded to 16 KiB.
- No arbitrary filesystem-read endpoint.
- No shell/command execution endpoint.
- No network proxy/download endpoint.
- No telemetry or cloud fallback.

The dashboard token is printed once at process startup. It is a local credential and must be treated as secret.

## Concurrency model

The current core runtime has no internal thread-safety primitives. Phase 7 therefore does not pretend that the runtime is concurrently safe.

A single dashboard operation mutex serializes:

- model load
- unload
- reload
- context reset
- generation
- runtime/model diagnostics that touch the runtime handle

Generation holds that mutex for the entire streaming inference operation. This prevents an unload/reload from invalidating the runtime while a C API generation callback is active.

The HTTP server itself may remain concurrent; the SYJ runtime boundary is serialized deliberately.

## API

| Endpoint | Method | Purpose |
|---|---|---|
| `/api/health` | GET | Dashboard/runtime health |
| `/api/version` | GET | Release + C ABI version |
| `/api/system` | GET | Live system-memory observation |
| `/api/models` | GET | Registered/verified model list |
| `/api/runtime` | GET | Loaded model and runtime state |
| `/api/memory` | GET | Last memory-admission diagnostic |
| `/api/usage` | GET | Usage/quota diagnostic |
| `/api/runtime/load` | POST | Load by `model_id` or explicit GGUF path |
| `/api/runtime/unload` | POST | Release model/context |
| `/api/runtime/reload` | POST | Reload the previous model configuration |
| `/api/runtime/reset` | POST | Clear context/KV state |
| `/api/generate` | POST | Stream generation as Server-Sent Events |

`/api/runtime/load` requires exactly one of `model_id` or `model_path`. Model IDs are preferred because they use the existing registry resolution and verification pipeline.

`/api/generate` accepts a bounded JSON object containing a `prompt` string and streams events such as:

```text
data: {"token":"..."}

data: {"done":true,"status":"OK"}
```

The generation operation remains synchronous at the core/C API boundary, while HTTP clients receive token fragments incrementally.

## Dependency

The dashboard uses `cpp-httplib` through CMake `FetchContent`, pinned to `v0.18.3`. It is not vendored into the repository and no Node/npm/bundler is required.

TLS is deliberately disabled because the server is loopback-only. The dashboard is not intended to be exposed to a LAN/WAN interface.

## Platform posture

The design is portable across Linux/Termux/Android and Windows because it uses:

- standard C++17
- the existing C API
- loopback HTTP
- OS-secure entropy primitives available on POSIX and Windows
- no desktop GUI framework
- no Node/npm toolchain

The code is **iOS-compatible in architecture** because it stays behind the same C API boundary, but this phase does not claim an iOS/Xcode build or an iOS HTTP-server integration has been executed.
