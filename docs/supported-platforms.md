# Supported Platforms

**Status: v0.3.0.** This tracks what is actually supported/verified as work lands; it is not a promise of future support beyond what's listed here.

## Android / Termux ARM64

The only platform with a confirmed real-hardware run so far, reported by the project maintainer (not verified in the sandbox that implemented Phase 2/v0.3.0 — see `docs/development.md`):

- Verified for Phase 1 (tag `v0.1.1`): clean CMake build, 3/3 tests, real inference against `SmolLM2-135M-Instruct-Q4_K_M.gguf`. Toolchain used: Clang, aarch64.
- **Not yet verified for Phase 2 (memory-budget admission) or v0.3.0 (usage/quota manager)** on real Android/Termux hardware — both are implemented and pass their own pure/filesystem tests in a plain Linux sandbox, but neither has been confirmed end-to-end on the platform this project actually targets.
- No CMake toolchain file or Termux-specific build script exists yet; the standard `cmake -S . -B build && cmake --build build` workflow is what's been used.

## Windows (planned: Phase 5)

- Target: x64, Windows 10+/11, CPU-only
- Toolchain: CMake + MSVC (Visual Studio build tools, command-line build preferred over requiring the VS GUI)
- CPU feature detection (AVX/AVX2) at build/runtime — the build must never emit instructions the target CPU can't execute
- Not started; no build has ever been attempted on Windows in this project's history

## iOS (planned: Phase 6–7)

- Target: ARM64
- Toolchain: Swift + Objective-C++ bridge over the shared C API + llama.cpp
- Respects iOS sandboxing (app-scoped storage only), handles memory pressure and app lifecycle events, no background-execution assumptions
- Not started

## Not currently planned

GPU acceleration (CUDA/ROCm/Metal/DirectML) is explicitly out of scope for the default path — CPU-only must work standalone. Optional hardware acceleration may be considered later, but is not part of the current roadmap.

## Platform-specific limitations worth knowing about

- `MemoryObserver::observe_system_memory()` (Phase 2) reads `/proc/meminfo` and is Linux-only — this covers Android/Termux (which is Linux-kernel-based) but not Windows or iOS, and in any case is not yet wired into the memory-admission decision at all (see `docs/memory-model.md`).
- The v0.3.0 usage-state file uses `rename()` for atomic writes, which is a POSIX guarantee — covers Linux/Android/Termux; Windows semantics for `std::rename` onto an existing file differ (may fail rather than replace) and haven't been tested.
