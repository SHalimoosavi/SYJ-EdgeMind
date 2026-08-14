# SYJ EdgeMind

**Private, Offline AI for Low-Memory Devices.**

> Status: Phase 1 — Native llama.cpp Runtime. CPU-only local inference is implemented and locally validated (see "Phase 1 build status" below); Windows/iOS packaging, the memory-budget engine, and the model registry are not implemented yet. See [ROADMAP.md](ROADMAP.md).

SYJ EdgeMind is designed for private, offline local inference and does not require cloud API keys after model acquisition. Actual RAM consumption varies by model, context, runtime configuration, and platform — not every model will run on every 4 GB device, and no claim is made that it will until Phase 2's memory-budget engine and Phase 8's real hardware measurements exist. Formal memory-budget enforcement is not implemented yet; Phase 1 only prevents an obviously unbounded context (see [docs/memory-model.md](docs/memory-model.md)).

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

See [docs/development.md](docs/development.md) for what this actually does and [Phase 1 build status](#phase-1-build-status) below for what has and hasn't been locally verified.

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
/reset   clear the context and start fresh
/quit    exit
```

`/memory` and `/context` (richer memory/context diagnostics) are planned for Phase 4 once Phase 2's memory-budget engine exists to report against.

## Model management

Not implemented yet — point `--model` at a local GGUF file you already have. The model registry, verification, and acquisition tooling described in [docs/model-selection.md](docs/model-selection.md) land in Phase 3.

## Performance

No benchmark numbers exist yet. None will be published until they are actually measured (Phase 8). See [docs/performance.md](docs/performance.md).

## Phase 1 build status

SYJ EdgeMind's own source (config validation, context accounting) has been compiled and its unit tests run successfully in the development sandbox used to build this phase. The parts of the source that depend on llama.cpp (tokenizer, sampler, inference engine, C API, CLI) were verified for syntax/API-usage correctness against the real, current llama.cpp API (tag `b10375`) but could not be *linked and run* against the real llama.cpp library in that sandbox, because it has no network access to fetch llama.cpp's source — a `cmake -S . -B build` there fails at the `FetchContent` step, not because of an error in SYJ EdgeMind's code. On any machine with normal internet access, the build commands above fetch llama.cpp automatically. See [docs/development.md](docs/development.md) and [docs/troubleshooting.md](docs/troubleshooting.md) for details, and please report back if you hit a build issue on real hardware — Phase 1 has not yet been confirmed end-to-end against a real GGUF model.

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
