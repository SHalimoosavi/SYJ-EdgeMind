# Development

**Status: Phase 0 — the project does not build yet.** This page will gain real, tested instructions starting in Phase 1.

## Planned toolchain

- CMake >= 3.20
- A C++17-capable compiler (MSVC on Windows; clang/gcc elsewhere for development)
- Git

## Planned workflow

```
git clone https://github.com/SHalimoosavi/SYJ-EdgeMind.git
cd SYJ-EdgeMind
cmake -B build
cmake --build build
```

This will not produce a working binary until Phase 1 lands (the CMake foundation currently has no inference targets enabled — see comments in `CMakeLists.txt`).

## Directory guide

See [architecture.md](architecture.md) for what each directory under `src/`, `platform/`, `tests/`, and `scripts/` is for.

## Testing

Test categories (unit / integration / memory / inference / failure) are described in [ROADMAP.md](../ROADMAP.md) Phase 9 and will be built incrementally alongside the features they cover, not all at once at the end.
