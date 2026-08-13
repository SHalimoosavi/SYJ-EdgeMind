# Supported Platforms

**Status: Phase 0 — nothing builds or runs yet.** This tracks what is actually supported as phases land; it is not a promise of future support beyond what's listed here.

## Windows (planned: Phase 5)

- Target: x64, Windows 10+/11, CPU-only
- Toolchain: CMake + MSVC (Visual Studio build tools, command-line build preferred over requiring the VS GUI)
- CPU feature detection (AVX/AVX2) at build/runtime — the build must never emit instructions the target CPU can't execute

## iOS (planned: Phase 6–7)

- Target: ARM64
- Toolchain: Swift + Objective-C++ bridge over the shared C API + llama.cpp
- Respects iOS sandboxing (app-scoped storage only), handles memory pressure and app lifecycle events, no background-execution assumptions

## Not currently planned

GPU acceleration (CUDA/ROCm/Metal/DirectML) is explicitly out of scope for the default path — CPU-only must work standalone. Optional hardware acceleration may be considered later, but is not part of the current roadmap.
