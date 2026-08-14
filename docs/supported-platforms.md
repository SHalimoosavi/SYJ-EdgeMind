# Supported Platforms

**Status: Phase 1 — native runtime implemented; platform-specific validation is still in progress.**

This document tracks what is **actually implemented and verified**, rather than
what is merely planned on the roadmap.

## Current Status

| Platform | Phase | Status |
|---|---:|---|
| Linux / development environment | 1 | Native runtime code implemented; direct source-level validation completed |
| Windows x64 | 5 | Planned |
| iOS | 6/7 | Planned |
| Android | — | Not currently a target |

## Phase 1 — Native Runtime

SYJ EdgeMind currently provides a platform-independent native C++ inference
core built around llama.cpp.

The core includes:

- Runtime lifecycle
- Runtime configuration
- GGUF model loading through llama.cpp
- Tokenization
- Context management
- Token sampling
- Native inference engine
- Stable C API
- CLI entry point
- Unit tests
- Invalid-model-path integration test

The llama.cpp dependency is pinned to:

```text
Tag:    b10375
Commit: ba360ef
