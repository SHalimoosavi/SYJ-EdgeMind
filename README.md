# SYJ EdgeMind

**Private, Offline AI for Low-Memory Devices.**

> Status: Phase 0 — Architecture & Repository Bootstrap. Not yet functional. See [ROADMAP.md](ROADMAP.md).

SYJ EdgeMind is designed for private, offline local inference and does not require cloud API keys after model acquisition. Actual RAM consumption varies by model, context, runtime configuration, and platform — not every model will run on every 4 GB device.

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

Not yet available — see ROADMAP (Phase 5).

### iOS

Not yet available — see ROADMAP (Phase 6–7).

## Usage

Not yet available — the runtime does not build or run yet (Phase 1 delivers the first working CLI).

## CLI commands

Planned; documented in full once Phase 4 lands. Interactive commands will include `/help`, `/info`, `/memory`, `/context`, `/reset`, `/quit`.

## Model management

Planned; see [docs/model-selection.md](docs/model-selection.md).

## Performance

No benchmark numbers exist yet. None will be published until they are actually measured (Phase 8). See [docs/performance.md](docs/performance.md).

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
