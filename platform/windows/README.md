# Windows Platform Layer

**Status: Implemented — build scripts and CPU-mode selection exist and were statically audited; real Windows/MSVC build execution has NOT been performed. See docs/supported-platforms.md for the exact validation status.**

This directory documents Windows-specific build notes, packaging conventions, and troubleshooting for `scripts/build-windows.ps1` / `build-windows.bat`. It does not contain, and must never contain, inference logic — Windows wraps the single shared C API (`src/api/edge_mind_api.h`), exactly like every other platform layer. No Windows-specific model-loading, memory, usage, or tokenizer code exists or is planned.

## Building

```powershell
.\scripts\build-windows.ps1
```

or, from `cmd.exe`:

```
scripts\build-windows.bat
```

Both produce the exact same build — the `.bat` file is a thin delegator to the PowerShell script, not a second implementation.

## CPU modes

By default, the build targets baseline x86-64 only (**Portable** mode) — no AVX/AVX2 instructions are emitted, and it should run on any x86-64 Windows machine. This is deliberate: the pinned ggml/llama.cpp build performs **compile-time** instruction-set selection, not runtime CPU-feature dispatch, so a binary built with AVX/AVX2 enabled will attempt those instructions unconditionally on whatever CPU it runs on — there is no fallback and no detection. See `docs/supported-platforms.md` for the full explanation and the three-mode breakdown (Portable / AVX / AVX2).

```powershell
.\scripts\build-windows.ps1                # Portable (default)
.\scripts\build-windows.ps1 -EnableAVX     # requires an AVX-capable CPU
.\scripts\build-windows.ps1 -EnableAVX2    # requires an AVX2-capable CPU
.\scripts\build-windows.ps1 -Clean         # force a fresh reconfigure
.\scripts\build-windows.ps1 -Generator "Visual Studio 17 2022"   # pin a specific VS generator
```

AVX/AVX2 are always explicit opt-ins — the script never enables them based on the machine building it, and `GGML_NATIVE` is never enabled on Windows for the same reason (it would implicitly target the *build* machine's CPU, which may not match wherever the binary actually runs).

## Generator and architecture

This project's documentation commits to **MSVC** as the Windows toolchain, but never to a specific Visual Studio release — so the script does not hardcode one. By default it lets CMake auto-detect a generator from whatever's installed (`-Generator` is unset); pass `-Generator "Visual Studio 17 2022"` (or any other installed version) for a fully deterministic, repeatable configure on a specific machine.

**Architecture is always pinned to `-A x64`**, since x64 *is* the one explicitly documented target (`docs/supported-platforms.md`) — this applies whenever no `-Generator` is given, or the given generator name starts with `"Visual Studio"` (the only family that accepts CMake's `-A` flag). If you explicitly request a non-Visual-Studio generator (e.g. Ninja), `-A` is omitted — passing it would make CMake's configure step fail outright — and you're responsible for ensuring x64 via your build environment/shell instead.

## Persistence on Windows

`ModelRegistry` and `UsageStateStore` both persist to disk by writing to a temp file and then replacing the real path. POSIX's `rename()` guarantees an atomic replace even when the destination already exists; Windows' `std::rename`/`_wrename` does not share that guarantee (it fails outright instead). Both call sites use `MoveFileExA` with `MOVEFILE_REPLACE_EXISTING` on Windows to address this — a replacement operation designed to preserve the same atomic-write intent, though it is not being claimed as an unconditional filesystem-level atomicity guarantee equivalent to POSIX `rename()` in every Windows storage configuration (behavior can vary by filesystem/volume, e.g. NTFS vs. network shares). What it does preserve on both platforms: the original file is never left partially written, and on failure the original is left untouched. See the `#ifdef _WIN32` blocks in `src/model/model_registry.cpp` and `src/usage/usage_state_store.cpp`. Nothing else about either subsystem's format, validation, or public API changed for this.

## Known limitations

- No installer, MSI, or code-signing — packaging-script-level only, per Phase 5's scope.
- No real Windows/MSVC build or runtime execution has been performed yet — everything above is implemented and statically reviewed, not hardware-confirmed. Report back once you've run it.
