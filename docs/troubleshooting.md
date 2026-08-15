# Troubleshooting

**Status: v0.3.0.** Entries below reflect issues actually reproduced while implementing Phase 1/2/v0.3.0, or are explicitly marked as not-yet-encountered guidance for errors the code is designed to produce. This page grows as real issues come in — it is not a hypothetical FAQ.

## Build

**`cmake -S . -B build` fails while fetching llama.cpp / hangs on network access**

SYJ EdgeMind's `CMakeLists.txt` uses `FetchContent` to pull the pinned llama.cpp tag (`b10375`) from `https://github.com/ggml-org/llama.cpp.git` at configure time, landing in the build tree's own dependency area (`build/_deps/`), not inside the repository. This requires outbound network (git/https) access. If you're behind a proxy or in a sandboxed/offline environment, this step will fail — this is expected and is exactly what happened in every sandbox used to build these phases (see `docs/development.md` → "What's verified so far"). Options:
- Ensure outbound HTTPS/git access to `github.com` is allowed.
- For offline builds, vendor llama.cpp yourself at `third_party/llama.cpp` (matching tag `b10375`) and reconfigure with `-DSYJ_EDGEMIND_USE_SYSTEM_LLAMA=ON` (you'll need to provide a `llama` CMake target yourself in that mode — this path is not yet documented step-by-step, since it hasn't been exercised).

**`cmake: not found`**

Install CMake >= 3.20 for your platform (e.g. `apt install cmake` on Debian/Ubuntu, `winget install Kitware.CMake` on Windows, `brew install cmake` on macOS).

## Runtime — model loading

**`ERROR: Model file does not exist: <path>`**

The `--model` path doesn't resolve to a readable file. Check the path is correct and relative to your current working directory, not the repository root.

**`ERROR: Failed to load GGUF model.`**

`llama_model_load_from_file` returned NULL. This means the file exists but isn't a GGUF file llama.cpp can parse (wrong format, truncated download, or a quantization/architecture llama.cpp at tag `b10375` doesn't support). Re-download the model or try a different one.

**`ERROR: Failed to create inference context.`**

The model loaded but `llama_init_from_model` failed — often an unreasonable `--context` value for the model/hardware. Try a smaller `--context` (e.g. `512`).

**`ERROR: Failed to tokenize prompt.`**

Either the prompt was empty, or `llama_tokenize` failed for the given text/vocab. Not expected in normal use; please report if you see it with an ordinary non-empty prompt.

## Runtime — memory budget (Phase 2)

**`ERROR: Configuration exceeds the configured memory budget.` / `STATUS: UNSAFE`**

The estimated total (model weights + KV cache + compute buffer + runtime overhead) exceeds `--memory-budget` minus `--safety-reserve`. The printed diagnostic breaks down each component — see `docs/memory-model.md`. Options:
- Reduce `--context` (KV-cache size scales linearly with it).
- Increase `--memory-budget` if the device genuinely has more RAM available than the current budget assumes.
- Try a smaller/more aggressively quantized model.

**`STATUS: UNSAFE` with "Memory estimate could not be established safely..."**

This is a *different* failure from the one above — it means the model's own hyperparameters (layer count, embedding size, head counts) couldn't be read safely, either because they're invalid/out-of-range or a GGUF metadata field looked corrupt. This is deliberately fail-closed: SYJ EdgeMind refuses rather than guessing a number. Try a different model file; if you believe the model is genuinely fine, please report it (a legitimate model exceeding the current sane-range bounds — see `docs/memory-model.md`'s "Known limitations" — is possible but hasn't been seen yet).

## Runtime — usage/quota (v0.3.0)

**`ERROR: Usage quota check failed.` / `STATUS: DENIED` with "Reason: ... limit reached."**

One of `--time-limit-minutes`/`--message-limit`/`--token-limit` has been reached for the current reset period. Run `/usage` (interactive mode) to see exactly which dimension and how much time remains until the reset. This is not an error in the usual sense — SYJ EdgeMind refuses to generate further, but the loaded model/context stay intact; other commands (`/info`, `/memory`, `/reset`) remain usable, and a single-shot CLI invocation exits with code 1.

**`STATUS: DENIED` with "Reason: usage state could not be read safely..."**

The local usage-state file (default `.syj_edgemind_usage_state`, override with `--usage-state-path`) exists but failed validation — this is the fail-closed path, deliberately distinct from a legitimate quota denial (see `docs/usage-model.md`). SYJ EdgeMind will not overwrite it automatically. If you're confident the file is safe to discard (e.g. after a manual edit gone wrong, or an old/incompatible version's state file), delete it manually to start fresh; there is no automated repair command yet.

## Not yet covered

- Insufficient memory that somehow passes Phase 2's estimate but exceeds real available RAM anyway (live system-RAM isn't factored into the admission decision yet — see `docs/memory-model.md`'s "Known limitations"), so an under-provisioned machine could still be killed by the OS in this specific gap
- Concurrent processes racing on the same `--usage-state-path` (no file locking yet — see `docs/usage-model.md`'s "Known limitations")
- CPU feature-detection issues (AVX/AVX2/NEON) — inherited from llama.cpp's own build detection; SYJ EdgeMind doesn't add its own layer yet
- Windows-specific packaging issues (Phase 5)
- iOS-specific issues (Phase 6/7)
- Model Registry & Verification issues (Phase 3, not started)
