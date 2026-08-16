cmake_minimum_required(VERSION 3.20)

project(SYJEdgeMind
    VERSION 0.1.0
    DESCRIPTION "Private, offline AI runtime for low-memory devices"
    LANGUAGES C CXX
)

# ---------------------------------------------------------------------------
# Phase 2 status: Native llama.cpp Runtime (Phase 1) + Memory Safety Engine
# (Phase 2).
#
# llama.cpp is fetched at the exact pinned tag recorded in docs/architecture.md
# (b10375). This repository does NOT vendor llama.cpp's source in git — it is
# fetched reproducibly at configure time via FetchContent, pinned to that tag,
# never to `main`, landing in the build tree's own dependency area rather
# than inside the repository working tree. See third_party/README.md.
# ---------------------------------------------------------------------------

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

option(SYJ_EDGEMIND_BUILD_TESTS "Build test suite" ON)
option(SYJ_EDGEMIND_BUILD_CLI "Build the syj-edgemind CLI executable" ON)
option(SYJ_EDGEMIND_ENABLE_WARNINGS "Enable strict warnings (-Wall -Wextra -Wpedantic / MSVC equivalent)" ON)
option(SYJ_EDGEMIND_USE_SYSTEM_LLAMA "Use an already-configured llama.cpp target instead of FetchContent (advanced/offline builds)" OFF)

# Pinned per docs/architecture.md — "llama.cpp version pinning". Defined
# unconditionally (not just inside the FetchContent branch below) so it's
# available for logging/diagnostics regardless of
# SYJ_EDGEMIND_USE_SYSTEM_LLAMA. Do NOT change this to a branch (e.g.
# "master"). Bumping the pin is a deliberate, reviewed change to both this
# file and docs/architecture.md.
set(SYJ_EDGEMIND_LLAMA_CPP_TAG "b10375")

add_library(syj_edgemind_warnings INTERFACE)
if(SYJ_EDGEMIND_ENABLE_WARNINGS)
    if(MSVC)
        target_compile_options(syj_edgemind_warnings INTERFACE /W4)
    else()
        target_compile_options(syj_edgemind_warnings INTERFACE -Wall -Wextra -Wpedantic)
    endif()
endif()

# ---------------------------------------------------------------------------
# llama.cpp dependency (pinned)
# ---------------------------------------------------------------------------
if(NOT SYJ_EDGEMIND_USE_SYSTEM_LLAMA)
    include(FetchContent)

    # llama.cpp's own CMake build produces many optional example/tool targets
    # and a server; SYJ EdgeMind only needs the `llama` and `ggml` libraries.
    set(LLAMA_BUILD_COMMON   OFF CACHE BOOL "" FORCE)
    set(LLAMA_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(LLAMA_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
    set(LLAMA_BUILD_TOOLS    OFF CACHE BOOL "" FORCE)
    set(LLAMA_BUILD_SERVER   OFF CACHE BOOL "" FORCE)
    set(LLAMA_CURL           OFF CACHE BOOL "" FORCE)

    FetchContent_Declare(
        llama_cpp
        GIT_REPOSITORY https://github.com/ggml-org/llama.cpp.git
        GIT_TAG        ${SYJ_EDGEMIND_LLAMA_CPP_TAG}
        GIT_SHALLOW    TRUE
        # Deliberately NOT setting SOURCE_DIR to third_party/llama.cpp:
        # doing so would vendor a full llama.cpp checkout into the repo
        # working tree (even though .gitignore excludes it from commits,
        # it would still sit inside the project directory, be picked up
        # by tools that scan the tree, and blur the "fetched dependency"
        # vs. "our code" boundary). Leaving SOURCE_DIR unset lets
        # FetchContent use its default location under build/_deps/, which
        # is exactly where a CMake-fetched dependency belongs. third_party/
        # remains reserved for a future SYJ_EDGEMIND_USE_SYSTEM_LLAMA-style
        # manually-vendored build if that's ever needed (see third_party/README.md).
    )
    FetchContent_MakeAvailable(llama_cpp)
endif()

# ---------------------------------------------------------------------------
# SYJ EdgeMind core runtime library
# ---------------------------------------------------------------------------
add_library(syj-edgemind-core STATIC
    src/core/config.cpp
    src/core/runtime.cpp
    src/tokenizer/tokenizer.cpp
    src/context/context_manager.cpp
    src/memory/memory_estimator.cpp
    src/memory/memory_budget.cpp
    src/memory/memory_observer.cpp
    src/usage/usage_accounting.cpp
    src/usage/usage_state_store.cpp
    src/usage/usage_manager.cpp
    src/model/model_types.cpp
    src/model/gguf_reader.cpp
    src/model/model_hash.cpp
    src/model/model_metadata.cpp
    src/model/model_verifier.cpp
    src/model/model_registry.cpp
    src/inference/inference_engine.cpp
    src/inference/sampler.cpp
    src/api/edge_mind_api.cpp
)

target_include_directories(syj-edgemind-core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src>
)

target_link_libraries(syj-edgemind-core
    PUBLIC
        llama
    PRIVATE
        syj_edgemind_warnings
)

# Phase 3's src/model/model_verifier.cpp and model_registry.cpp use
# std::filesystem. On GCC < 9, libstdc++'s filesystem implementation was a
# separate library that must be linked explicitly; GCC 9+ and all modern
# Clang (including current Android NDK toolchains, which is what
# Android/Termux builds use) bundle it into libstdc++/libc++ directly, so
# this only applies to an old, otherwise-unsupported compiler — included
# for defensiveness, not because it's expected to trigger on any of this
# project's actual supported-platforms.md targets.
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS "9.0")
    target_link_libraries(syj-edgemind-core PUBLIC stdc++fs)
endif()

target_compile_features(syj-edgemind-core PUBLIC cxx_std_17)

# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------
if(SYJ_EDGEMIND_BUILD_CLI)
    add_executable(syj-edgemind
        src/cli/main.cpp
    )
    target_link_libraries(syj-edgemind
        PRIVATE
            syj-edgemind-core
            syj_edgemind_warnings
    )
endif()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(SYJ_EDGEMIND_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

message(STATUS "SYJ EdgeMind: Phase 2 configured (llama.cpp tag ${SYJ_EDGEMIND_LLAMA_CPP_TAG})")
