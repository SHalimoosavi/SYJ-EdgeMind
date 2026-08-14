# Troubleshooting

**Status: Phase 1.** Entries below reflect issues actually reproduced while implementing Phase 1, or are explicitly marked as not-yet-encountered guidance for errors the code is designed to produce. This page grows as real issues come in — it is not a hypothetical FAQ.

## Build

**`cmake -S . -B build` fails while fetching llama.cpp / hangs on network access**

SYJ EdgeMind's `CMakeLists.txt` uses `FetchContent` to pull the pinned llama.cpp tag (`b10375`) from `https://github.com/ggml-org/llama.cpp.git` at configure time. This requires outbound network (git/https) access. If you're behind a proxy or in a sandboxed/offline environment, this step will fail — this is expected and is exactly what happened in the sandbox used to build this phase (see `docs/development.md` → "What's verified so far"). Options:
- Ensure outbound HTTPS/git access to `github.com` is allowed.
- For offline builds, vendor llama.cpp yourself at `third_party/llama.cpp` (matching tag `b10375`) and reconfigure with `-DSYJ_EDGEMIND_USE_SYSTEM_LLAMA=ON` (you'll need to provide a `llama` CMake target yourself in that mode — this path is not yet documented step-by-step, since it hasn't been exercised).

**`cmake: not found`**

Install CMake >= 3.20 for your platform (e.g. `apt install cmake` on Debian/Ubuntu, `winget install Kitware.CMake` on Windows, `brew install cmake` on macOS).

## Runtime

**`ERROR: Model file does not exist: <path>`**

The `--model` path doesn't resolve to a readable file. Check the path is correct and relative to your current working directory, not the repository root.

**`ERROR: Failed to load GGUF model.`**

`llama_model_load_from_file` returned NULL. This means the file exists but isn't a GGUF file llama.cpp can parse (wrong format, truncated download, or a quantization/architecture llama.cpp at tag `b10375` doesn't support). Re-download the model or try a different one.

**`ERROR: Failed to create inference context.`**

The model loaded but `llama_init_from_model` failed — often an unreasonable `--context` value for the model/hardware. Try a smaller `--context` (e.g. `512`).

**`ERROR: Failed to tokenize prompt.`**

Either the prompt was empty, or `llama_tokenize` failed for the given text/vocab. Not expected in normal use; please report if you see it with an ordinary non-empty prompt.

## Not yet covered

- Insufficient memory during model load (Phase 2's memory-budget engine gives a clear pre-flight `STATUS: UNSAFE` diagnostic instead of an OS-level crash/OOM-kill; Phase 1 has no such pre-check, so an under-provisioned machine may currently be killed by the OS rather than getting a clean SYJ EdgeMind error)
- CPU feature-detection issues (AVX/AVX2/NEON) — inherited from llama.cpp's own build detection in Phase 1; SYJ EdgeMind doesn't add its own layer yet
- Windows-specific packaging issues (Phase 5)
- iOS-specific issues (Phase 6/7)
