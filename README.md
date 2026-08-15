# SYJ EdgeMind

**Private, Offline AI for Low-Memory Devices.**

> Status: v0.3.0 — Usage/Quota Manager (a parallel initiative alongside the phase-numbered roadmap — see [ROADMAP.md](ROADMAP.md)'s note). CPU-only local inference (v0.1.1, real-hardware validated by the maintainer), a memory-budget admission system (Phase 2), and a local usage/quota guard (v0.3.0) are implemented; see "Build status" below for exactly what's been verified where. Windows/iOS packaging and the model registry (Phase 3) are not implemented yet.

SYJ EdgeMind is designed for private, offline local inference and does not require cloud API keys after model acquisition. Actual RAM consumption varies by model, context, runtime configuration, and platform — not every model will run on every 4 GB device. As of Phase 2, SYJ EdgeMind estimates memory usage and refuses configurations that exceed your configured budget (see [docs/memory-model.md](docs/memory-model.md)), but that estimate is not yet checked against live available system RAM, and formal, comprehensive real-hardware validation of the memory-budget path itself is still pending (see "Build status" below).

## Why it exists

Most local-AI tooling assumes generous RAM, a GPU, or a heavyweight runtime (Python/Node/Electron). SYJ EdgeMind targets the opposite: CPU-only machines with roughly 4 GB of system RAM, where every megabyte allocated to the runtime is a megabyte not available to the model.

## Features (target — see ROADMAP for current status)

- CPU-only inference via [llama.cpp](https://github.com/ggerganov/llama.cpp) (pinned commit, see `docs/architecture.md`)
- Memory-mapped GGUF model loading
- A dedicated memory safety engine that estimates model + KV-cache + overhead before loading, and refuses unsafe configurations
- Configurable, conservative context profiles (512 / 1024 / 2048 / 4096)
- Streaming token generation
- One shared native C API and one inference core, used by a Windows CLI and (later) a minimal iOS app — no duplicated inference logic per platform
- Offline after model acquisition: no telemetry, no analytics, no hidden networking, no API keys
- Model verification via SHA-256 before load

## Architecture

See [docs/architecture.md](docs/architecture.md) for the full diagram and component breakdown. In short:

```
        SYJ EdgeMind
              |
     +--------+--------+
     |                 |
Windows CLI        iOS App/Core
     |                 |
     +--------+--------+
              |
        Native C API
              |
       SYJ Edge Runtime
              |
          llama.cpp
              |
    GGUF Quantized Model
              |
       CPU / RAM / mmap
```

## Supported hardware

See [docs/supported-platforms.md](docs/supported-platforms.md).

## Memory model

See [docs/memory-model.md](docs/memory-model.md) for how the memory safety budget is computed and enforced.

## Supported models

See [docs/model-selection.md](docs/model-selection.md) and [models/registry.json](models/registry.json).

## Installation

### Windows

Packaged installer scripts are not available yet — see ROADMAP (Phase 5). The CMake build below works on any platform with a C++17 compiler and CMake 3.20+, including Windows with MSVC, but there is no `.ps1`/`.bat` convenience script yet.

### iOS

Not yet available — see ROADMAP (Phase 6–7).

### Build from source (all platforms, Phase 1)

Requires CMake 3.20+, a C++17 compiler, and network access (to fetch the pinned llama.cpp source at configure time):

```
git clone https://github.com/SHalimoosavi/SYJ-EdgeMind.git
cd SYJ-EdgeMind
cmake -S . -B build
cmake --build build --config Release
```

See [docs/development.md](docs/development.md) for what this actually does and [Build status](#build-status) below for what has and hasn't been locally verified.

## Usage

```
./build/syj-edgemind --model /path/to/model.gguf --context 1024 --threads 4 --max-tokens 256
```

Omit a trailing prompt to start interactive mode instead of a single-shot response. See `--help` for all options.

## CLI commands

Interactive mode supports:

```
/help    show interactive commands
/info    show loaded model info (params, size, context, threads)
/memory  show the memory-budget diagnostic from the last load
/usage   show current usage, remaining quota, and reset time
/reset   clear the context and start fresh
/quit    exit
```

`/context` (richer context diagnostics) is planned for Phase 4.

## Model management

Not implemented yet — point `--model` at a local GGUF file you already have. The model registry, verification, and acquisition tooling described in [docs/model-selection.md](docs/model-selection.md) land in Phase 3.

## Performance

No benchmark numbers exist yet. None will be published until they are actually measured (Phase 8). See [docs/performance.md](docs/performance.md).

## Memory management

SYJ EdgeMind estimates model weight, KV-cache, and compute-buffer memory before committing to a context, and refuses to create one if the total would exceed your configured budget (default 3000 MB budget / 300 MB safety reserve — tune with `--memory-budget`/`--safety-reserve`). The estimation is fail-closed: if a model's hyperparameters can't be established safely (invalid, out of a sane range, or would overflow), the configuration is rejected outright rather than silently treated as zero-cost. See [docs/memory-model.md](docs/memory-model.md) for the full pipeline, what's an actual measurement vs. an estimate, the exact (inclusive) boundary rule, and current limitations (notably: live system-RAM availability isn't factored into the decision yet — only the configured budget is).

## Usage/quota management (v0.3.0)

An optional, entirely local and offline usage guard — not a licensing or subscription system, no cloud, no telemetry. Configure any combination of `--time-limit-minutes`, `--message-limit`, `--token-limit` (each defaults to unlimited); usage persists across restarts in a small versioned local file (`--usage-state-path`, default `.syj_edgemind_usage_state`) and resets on a configurable period (`--reset-period-hours`, default 24h). A corrupted state file fails closed — it is never treated as unlimited access, and is diagnostically distinct from a legitimately exhausted quota. See [docs/usage-model.md](docs/usage-model.md) for the full design, including exactly what counts as "usage" and current limitations.

## Build status

SYJ EdgeMind's own llama.cpp-independent source (config validation, context accounting, the Phase 2 memory estimator/budget-policy module, and the v0.3.0 usage/quota subsystem) has been compiled *and its unit/integration tests actually run* successfully in the development sandbox used to build these — 8/8 tests pass, including catching and fixing three real, distinct bugs during development (a budget-policy boundary-condition bug, a test that asserted a false overflow premise, and a usage-state "fresh save fails validation" bug — all detailed in [docs/development.md](docs/development.md)). The parts of the source that depend on llama.cpp (tokenizer, sampler, inference engine, memory observer, C API, CLI, and `Runtime`'s usage-manager integration) were verified for syntax/API-usage correctness against the real, current llama.cpp API (tag `b10375`) but could not be *linked and run* against the real llama.cpp library in that sandbox, because it has no network access to fetch llama.cpp's source — a `cmake -S . -B build` there fails at the `FetchContent` step, not because of an error in SYJ EdgeMind's code. On any machine with normal internet access, the build commands above fetch llama.cpp automatically.

Separately, the project maintainer has confirmed real-hardware validation of the Phase 1 runtime (Android/Termux, `SmolLM2-135M-Instruct-Q4_K_M.gguf`, tag `v0.1.1`) — a genuine end-to-end build/test/inference pass. **Neither the Phase 2 memory-admission code nor the v0.3.0 usage/quota code has yet been confirmed on that or any other real hardware** — this includes not yet having run a normal load, `/memory`, `/usage`, a deliberately-unsafe memory configuration, or a deliberately-exhausted quota, against a real linked build. See [docs/development.md](docs/development.md) and [docs/troubleshooting.md](docs/troubleshooting.md) for details, and please report back once you've run it — that's the one thing no sandbox in this conversation has been able to do.

## Troubleshooting

See [docs/troubleshooting.md](docs/troubleshooting.md).

## Security

See [SECURITY.md](SECURITY.md) and [docs/security.md](docs/security.md). No telemetry, no hidden networking, no cloud fallback, no secrets, no API keys.

## Privacy

Models are downloaded/imported separately by the user, who is responsible for complying with the applicable model license. Inference itself requires zero network access.

## Development

See [docs/development.md](docs/development.md) and [CONTRIBUTING.md](CONTRIBUTING.md).

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md).

## License

See [LICENSE](LICENSE). Third-party dependency licenses (llama.cpp, any bundled models) are tracked separately — see `docs/architecture.md` §Dependency Licensing. The project license will be finalized once that audit is complete.

## Roadmap

See [ROADMAP.md](ROADMAP.md).
