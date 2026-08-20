# Supported Platforms

**Status: v0.3.0.** This tracks what is actually supported/verified as work lands; it is not a promise of future support beyond what's listed here.

## Android / Termux ARM64

The only platform with a confirmed real-hardware run so far, reported by the project maintainer (not verified in the sandbox that implemented Phase 2/v0.3.0 — see `docs/development.md`):

- Verified for Phase 1 (tag `v0.1.1`): clean CMake build, 3/3 tests, real inference against `SmolLM2-135M-Instruct-Q4_K_M.gguf`. Toolchain used: Clang, aarch64.
- **Not yet verified for Phase 2 (memory-budget admission) or v0.3.0 (usage/quota manager)** on real Android/Termux hardware — both are implemented and pass their own pure/filesystem tests in a plain Linux sandbox, but neither has been confirmed end-to-end on the platform this project actually targets.
- No CMake toolchain file or Termux-specific build script exists yet; the standard `cmake -S . -B build && cmake --build build` workflow is what's been used.

## Windows (Phase 5: Implemented — statically audited; real hardware execution NOT performed)

- Target: x64, Windows 10+/11, CPU-only
- Toolchain: CMake + MSVC (Visual Studio build tools, command-line build preferred over requiring the VS GUI)
- Build via `scripts/build-windows.ps1` (authoritative) or `scripts/build-windows.bat` (thin delegator to the same script — not a second implementation)
- **CPU modes** — compile-time instruction-set selection, not runtime dispatch (the pinned ggml/llama.cpp build has no CPU-feature runtime detection; verified against ggml's actual CMake configuration, not assumed):

  | Mode | CMake flags | Required CPU | Runtime safety |
  |---|---|---|---|
  | **Portable** (default) | `GGML_NATIVE=OFF`, `GGML_AVX=OFF`, `GGML_AVX2=OFF` | none beyond baseline x86-64 | safe on any x86-64 Windows CPU |
  | **AVX** (`-EnableAVX`) | `GGML_AVX=ON`, `GGML_AVX2=OFF`, `GGML_NATIVE=OFF` | AVX-capable CPU | **not guaranteed** — no fallback if the running CPU lacks AVX; the binary will attempt those instructions unconditionally |
  | **AVX2** (`-EnableAVX2`) | `GGML_AVX2=ON`, `GGML_AVX=ON`, `GGML_NATIVE=OFF` | AVX2-capable CPU | same caveat as AVX, narrower hardware set |

  `GGML_AVX=ON` is passed explicitly by the build script whenever AVX2 mode is selected — this does not assume or rely on ggml's own CMake implicitly deriving one from the other (that internal dependency was not conclusively confirmed against upstream source); passing both explicitly is correct and deterministic either way, since every AVX2-capable CPU does support AVX as a hardware fact.

- **Generator and architecture**: the script does not hardcode a specific Visual Studio version — only "MSVC" is documented here, never a VS release number, so pinning one would invent an undocumented constraint. It always pins `-A x64` explicitly (the one architecture actually documented as this project's Windows target), applied whenever no `-Generator` override is given or the given generator is Visual-Studio-family; a `-Generator` parameter is available for a fully deterministic, repeatable configure on a specific machine. See `platform/windows/README.md` for the exact flag behavior.

  `GGML_NATIVE` is never enabled by the Windows build scripts (it would implicitly target the *build* machine's CPU, not necessarily wherever the binary is deployed). AVX/AVX2 are always explicit, user-selected opt-ins — never inferred from the build machine.
- **Persistence portability**: `ModelRegistry::save()` and `UsageStateStore::save()` both write to a temp file and then replace the real path. POSIX `rename()` guarantees atomic replace even when the destination exists; Windows' `std::rename`/`_wrename` does not (it fails instead) — this was a previously-documented, unresolved gap, now addressed via a `MoveFileExA`/`MOVEFILE_REPLACE_EXISTING`-based replacement operation designed to preserve the same atomic-write intent (not claimed as an unconditional filesystem-level atomicity guarantee in every Windows storage configuration — see `platform/windows/README.md` for the precise distinction). The POSIX path is completely unchanged.
- **Validation status**: implemented and statically reviewed (source-level, plus manual review of both scripts — no PowerShell/cmd.exe interpreter is available in the sandbox that built this, so neither script has been executed or parser-validated). **No real Windows/MSVC build or runtime execution has been performed.** This is explicitly not claimed as hardware-confirmed.

## iOS (planned: Phase 6–7)

- Target: ARM64
- Toolchain: Swift + Objective-C++ bridge over the shared C API + llama.cpp
- Respects iOS sandboxing (app-scoped storage only), handles memory pressure and app lifecycle events, no background-execution assumptions
- Not started

## Not currently planned

GPU acceleration (CUDA/ROCm/Metal/DirectML) is explicitly out of scope for the default path — CPU-only must work standalone. Optional hardware acceleration may be considered later, but is not part of the current roadmap.

## Platform-specific limitations worth knowing about

- `MemoryObserver::observe_system_memory()` (Phase 2) reads `/proc/meminfo` and is Linux-only — this covers Android/Termux (which is Linux-kernel-based) but not Windows or iOS, and in any case is not yet wired into the memory-admission decision at all (see `docs/memory-model.md`).
- The v0.3.0 usage-state file and the Phase 3 model registry both use `rename()` for atomic writes on POSIX (Linux/Android/Termux, unchanged); Windows uses a `MoveFileExA`/`MOVEFILE_REPLACE_EXISTING`-based replacement designed to preserve the same intent (not an unconditional atomicity guarantee — see the Windows section above) as of Phase 5 — statically reviewed, not yet hardware-tested.
