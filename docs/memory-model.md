# Memory Model

**Status: Phase 0 — design notes only. No memory estimator/budget code exists yet (Phase 2).**

## Target

The primary target is a device with roughly 4 GB of total system RAM, CPU-only, no dedicated GPU. The runtime must never assume the entire 4 GB is available to it — the OS, the runtime itself, the application layer, the model, the KV cache, context, and temporary buffers all compete for the same pool.

Illustrative budget (not a guarantee — will be made configurable and re-validated with real measurements in Phase 8):

```
Total RAM target:       4096 MB
Runtime safety budget:  <= ~2800–3200 MB
Application overhead:   reserved
Model + KV cache:       controlled
```

## Planned responsibilities of the memory engine (Phase 2)

1. Detect available system memory where practical.
2. Accept an explicit memory budget from configuration.
3. Estimate model loading requirements before loading.
4. Estimate KV-cache requirements for the configured context size.
5. Reject configurations that exceed the safe budget, with a clear diagnostic — never proceed silently.
6. Where appropriate, recommend (not silently apply) a reduced context size.
7. Report human-readable diagnostics, e.g.:

```
Model requires approximately: 1820 MB
KV cache estimate:             420 MB
Runtime overhead estimate:     180 MB
Safety reserve:                300 MB

Estimated total:              2720 MB
Memory budget:                3000 MB

STATUS: SAFE
```

or, on failure:

```
STATUS: UNSAFE
The selected model/context configuration exceeds the configured
4 GB-device memory budget.

Recommended action:
Reduce context from 4096 to 2048.
```

## Memory mapping

Model loading uses llama.cpp's real mmap-based loading facilities (not a custom/fake mmap layer) once Phase 1 lands. Platform differences (Windows vs iOS sandboxing) will be documented here as they're implemented.

## Context management

Planned conservative profiles:

| Profile | Context |
|---|---|
| ULTRA_LOW | 512 |
| LOW | 1024 |
| BALANCED | 2048 |
| ADVANCED | 4096 |

The 4 GB profile defaults to a conservative context. When context approaches its limit, the runtime will stop generation, truncate old history, or start a new context — unbounded context growth is not allowed.
