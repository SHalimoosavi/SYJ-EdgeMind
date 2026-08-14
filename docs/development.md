# Development

**Status: Phase 1 — Native llama.cpp Runtime is implemented.** The CLI builds and links against a real, pinned llama.cpp (tag `b10375`, see [architecture.md](architecture.md)) on any machine with normal network access. See "What's verified so far" below for exactly what has and hasn't been run in the sandbox used to build this phase.

## Toolchain

- CMake >= 3.20
- A C++17-capable compiler (MSVC on Windows; gcc/clang elsewhere)
- Git (used implicitly by CMake's `FetchContent` to pull llama.cpp)
- Network access at configure time, to fetch the pinned llama.cpp source

## Build

```
git clone https://github.com/SHalimoosavi/SYJ-EdgeMind.git
cd SYJ-EdgeMind
cmake -S . -B build
cmake --build build --config Release
```

The first `cmake -S . -B build` fetches llama.cpp (tag `b10375`) into `third_party/llama.cpp` via `FetchContent` and configures it as an in-tree dependency (`LLAMA_BUILD_EXAMPLES`/`TESTS`/`TOOLS`/`SERVER` are all disabled — SYJ EdgeMind only needs the `llama`/`ggml` libraries). This step requires network access; it is not repeated on subsequent builds unless `build/` is deleted.

## Run tests

```
ctest --test-dir build --output-on-failure
```

## Run inference

```
./build/syj-edgemind --model /path/to/model.gguf --context 1024 --threads 4 --max-tokens 64
```

or omit a trailing prompt for interactive mode. You need to supply your own local GGUF file — SYJ EdgeMind does not download or invent model URLs (Phase 3 adds a model registry/downloader; Phase 1 requires you to already have a `.gguf` file).

## Directory guide

See [architecture.md](architecture.md) for what each directory under `src/`, `platform/`, `tests/`, and `scripts/` is for. As of Phase 1:

- `src/core/` — configuration (`config.h/.cpp`) and the top-level RAII `Runtime` (`runtime.h/.cpp`)
- `src/tokenizer/` — thin wrapper over llama.cpp's `llama_tokenize`/`llama_token_to_piece`/`llama_vocab_is_eog`
- `src/context/` — `ContextManager`: bounded token accounting, no memory-budget reasoning yet (Phase 2)
- `src/inference/` — `InferenceEngine` (model/context lifecycle, streaming generation loop) and `Sampler` (llama.cpp sampler chain wrapper)
- `src/api/` — `edge_mind_api.h/.cpp`: the single public C API boundary; the CLI and future platform wrappers use only this header
- `src/cli/` — `syj-edgemind` executable, built only against `api/edge_mind_api.h`
- `src/memory/`, `src/model/` — still empty; Phase 2 and Phase 3 respectively

## What's verified so far (Phase 1, this sandbox)

The development sandbox used to implement this phase has no outbound network access (confirmed: `apt-get install cmake` and any `github.com`/`raw.githubusercontent.com` fetch from that sandbox's shell are blocked by its egress allowlist), so a full `cmake -S . -B build && cmake --build build` could not be executed there. What *was* actually done:

1. The real, current llama.cpp public API (tag `b10375`) was fetched and read via web tools (which have separate network access from that shell) and used as the basis for every `llama_*` call in this codebase — see `docs/architecture.md`'s "API surface verified against this tag" list.
2. `src/core/config.cpp` and `src/context/context_manager.cpp` (the two source files with no llama.cpp dependency) were compiled directly with `g++ -std=c++17 -Wall -Wextra -Wpedantic` and produced clean object files.
3. `tests/unit/test_config.cpp` and `tests/unit/test_context_manager.cpp` were compiled and *run* directly (bypassing CMake, since it wasn't installable), and both passed.
4. The remaining files (`tokenizer.cpp`, `sampler.cpp`, `inference_engine.cpp`, `edge_mind_api.cpp`, `edge_mind_api.h`, `main.cpp`, and every header) were syntax-checked with `g++ -fsyntax-only` against a hand-written stub `llama.h` reproducing the exact verified declarations from step 1 — this catches real typos and API-usage mistakes (wrong argument types/order/counts) but is **not** a substitute for linking against the real `libllama`, and does not prove runtime correctness against an actual GGUF model.
5. `edge_mind_api.h` was additionally confirmed to compile as plain C (`gcc -std=c11`), since it's meant to be usable from C, not just C++.

**Not yet done, honestly:** an actual `FetchContent`-driven build against real llama.cpp, and an actual generation run against a real GGUF model. Both require network access and/or a model file this sandbox doesn't have. If you build on a normal machine and hit an issue, please open an issue — Phase 1 should be treated as "implemented and locally reasoned through" rather than "proven end-to-end" until that happens.

## Testing

Test categories (unit / integration / memory / inference / failure) are described in [ROADMAP.md](../ROADMAP.md) Phase 9 and are built incrementally alongside the features they cover. Phase 1 adds: `test_config` (unit), `test_context_manager` (unit), `test_invalid_model_path` (integration — exercises the real `llama_backend_init`/failed-load path without needing a GGUF fixture).
