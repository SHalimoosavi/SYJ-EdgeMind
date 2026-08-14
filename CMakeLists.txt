cmake_minimum_required(VERSION 3.20)

project(SYJEdgeMind
    VERSION 0.1.0
    DESCRIPTION "Private, offline AI runtime for low-memory devices"
    LANGUAGES C CXX
)

# ============================================================================
# SYJ EdgeMind — Phase 1
# Native llama.cpp Runtime
#
# This phase establishes the first real native inference path.
#
# Design principles:
#   - CPU-first
#   - Offline inference
#   - GGUF models
#   - llama.cpp as the inference dependency
#   - Exact llama.cpp tag pin
#   - No tracking of upstream main/master
#   - No model weights in this repository
#   - No generated dependency sources in the Git working tree
#
# IMPORTANT:
# Phase 1 does NOT yet implement the complete 4 GB memory-budget system.
# Memory estimation/enforcement is a Phase 2 responsibility.
# ============================================================================

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING
        "Build type"
        FORCE
    )
endif()

set_property(
    CACHE CMAKE_BUILD_TYPE
    PROPERTY STRINGS
        Debug
        Release
        RelWithDebInfo
        MinSizeRel
)

# ============================================================================
# Build options
# ============================================================================

option(
    SYJ_EDGEMIND_BUILD_TESTS
    "Build SYJ EdgeMind test suite"
    ON
)

option(
    SYJ_EDGEMIND_BUILD_CLI
    "Build the syj-edgemind command-line interface"
    ON
)

option(
    SYJ_EDGEMIND_ENABLE_WARNINGS
    "Enable strict compiler warnings"
    ON
)

option(
    SYJ_EDGEMIND_USE_SYSTEM_LLAMA
    "Use an already configured llama.cpp target instead of FetchContent"
    OFF
)

# ============================================================================
# Compiler warnings
# ============================================================================

add_library(syj_edgemind_warnings INTERFACE)

if(SYJ_EDGEMIND_ENABLE_WARNINGS)

    if(MSVC)

        target_compile_options(
            syj_edgemind_warnings
            INTERFACE
                /W4
            )

    else()

        target_compile_options(
            syj_edgemind_warnings
            INTERFACE
                -Wall
                -Wextra
                -Wpedantic
            )

    endif()

endif()

# ============================================================================
# llama.cpp dependency
# ============================================================================
#
# The dependency is deliberately fetched into CMake's build/dependency area.
#
# It is NOT vendored into:
#
#     third_party/llama.cpp/
#
# and must NOT be committed to the SYJ EdgeMind repository.
#
# The exact tag is pinned for reproducibility.
# Never change this to a branch such as main/master.
#
# Any dependency upgrade must be a deliberate reviewed change accompanied by
# an update to docs/architecture.md.
# ============================================================================

set(
    SYJ_EDGEMIND_LLAMA_CPP_TAG
    "b10375"
    CACHE STRING
    "Pinned llama.cpp release tag"
)

if(NOT SYJ_EDGEMIND_USE_SYSTEM_LLAMA)

    include(FetchContent)

    # ------------------------------------------------------------------------
    # Disable unnecessary llama.cpp components.
    #
    # SYJ EdgeMind only requires the native llama inference library.
    # Examples, tools, server, tests and curl integration are not part of the
    # SYJ EdgeMind inference path.
    # ------------------------------------------------------------------------

    set(LLAMA_BUILD_COMMON
        OFF
        CACHE BOOL
        "Build llama.cpp common library"
        FORCE
    )

    set(LLAMA_BUILD_EXAMPLES
        OFF
        CACHE BOOL
        "Build llama.cpp examples"
        FORCE
    )

    set(LLAMA_BUILD_TESTS
        OFF
        CACHE BOOL
        "Build llama.cpp tests"
        FORCE
    )

    set(LLAMA_BUILD_TOOLS
        OFF
        CACHE BOOL
        "Build llama.cpp tools"
        FORCE
    )

    set(LLAMA_BUILD_SERVER
        OFF
        CACHE BOOL
        "Build llama.cpp server"
        FORCE
    )

    set(LLAMA_CURL
        OFF
        CACHE BOOL
        "Enable CURL support"
        FORCE
    )

    FetchContent_Declare(
        llama_cpp
        GIT_REPOSITORY
            https://github.com/ggml-org/llama.cpp.git
        GIT_TAG
            ${SYJ_EDGEMIND_LLAMA_CPP_TAG}
        GIT_SHALLOW
            TRUE
    )

    FetchContent_MakeAvailable(llama_cpp)

endif()

# ============================================================================
# Validate the expected llama.cpp target
# ============================================================================

if(NOT TARGET llama)

    message(
        FATAL_ERROR
        "SYJ EdgeMind requires the llama.cpp CMake target 'llama'. "
        "The configured llama.cpp dependency did not provide it."
    )

endif()

# ============================================================================
# SYJ EdgeMind core runtime
# ============================================================================
#
# There is exactly one inference core.
#
# Platform-specific implementations must call the public C API rather than
# implementing their own inference logic.
# ============================================================================

add_library(
    syj-edgemind-core
    STATIC

    src/core/config.cpp
    src/core/runtime.cpp

    src/tokenizer/tokenizer.cpp

    src/context/context_manager.cpp

    src/inference/inference_engine.cpp
    src/inference/sampler.cpp

    src/api/edge_mind_api.cpp
)

target_include_directories(
    syj-edgemind-core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
)

target_compile_features(
    syj-edgemind-core
    PUBLIC
        cxx_std_17
)

target_link_libraries(
    syj-edgemind-core
    PUBLIC
        llama

    PRIVATE
        syj_edgemind_warnings
)

# ============================================================================
# CLI
# ============================================================================

if(SYJ_EDGEMIND_BUILD_CLI)

    add_executable(
        syj-edgemind
        src/cli/main.cpp
    )

    target_compile_features(
        syj-edgemind
        PRIVATE
            cxx_std_17
    )

    target_link_libraries(
        syj-edgemind
        PRIVATE
            syj-edgemind-core
            syj_edgemind_warnings
    )

endif()

# ============================================================================
# Tests
# ============================================================================

if(SYJ_EDGEMIND_BUILD_TESTS)

    enable_testing()

    add_subdirectory(tests)

endif()

# ============================================================================
# Build summary
# ============================================================================

message(STATUS "")
message(STATUS "============================================================")
message(STATUS "SYJ EdgeMind")
message(STATUS "============================================================")
message(STATUS "Version:              ${PROJECT_VERSION}")
message(STATUS "Phase:                1 — Native llama.cpp Runtime")
message(STATUS "llama.cpp tag:        ${SYJ_EDGEMIND_LLAMA_CPP_TAG}")
message(STATUS "Build type:           ${CMAKE_BUILD_TYPE}")
message(STATUS "CLI:                  ${SYJ_EDGEMIND_BUILD_CLI}")
message(STATUS "Tests:                ${SYJ_EDGEMIND_BUILD_TESTS}")
message(STATUS "System llama.cpp:     ${SYJ_EDGEMIND_USE_SYSTEM_LLAMA}")
message(STATUS "Strict warnings:      ${SYJ_EDGEMIND_ENABLE_WARNINGS}")
message(STATUS "")
message(STATUS "Memory-budget enforcement: Phase 2")
message(STATUS "Model registry:            Phase 3")
message(STATUS "Windows packaging:         Phase 5")
message(STATUS "iOS bridge:                Phase 6/7")
message(STATUS "============================================================")
message(STATUS "")
