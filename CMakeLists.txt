cmake_minimum_required(VERSION 3.20)

project(SYJEdgeMind
    VERSION 0.0.0
    DESCRIPTION "Private, offline AI runtime for low-memory devices"
    LANGUAGES C CXX
)

# ---------------------------------------------------------------------------
# Phase 0 status: this is a FOUNDATION only.
# llama.cpp is NOT yet wired in — that lands in Phase 1 (Native llama.cpp
# Runtime), once the pinned commit from docs/architecture.md is vendored
# under third_party/llama.cpp and verified to build against this CMake
# config. Until then, `syj-edgemind-core` intentionally has no sources that
# depend on llama.cpp's API, and there is no CLI target yet.
# ---------------------------------------------------------------------------

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

option(SYJ_EDGEMIND_BUILD_TESTS "Build test suite" ON)
option(SYJ_EDGEMIND_ENABLE_WARNINGS "Enable strict warnings (-Wall -Wextra -Wpedantic / MSVC equivalent)" ON)

add_library(syj_edgemind_warnings INTERFACE)
if(SYJ_EDGEMIND_ENABLE_WARNINGS)
    if(MSVC)
        target_compile_options(syj_edgemind_warnings INTERFACE /W4)
    else()
        target_compile_options(syj_edgemind_warnings INTERFACE -Wall -Wextra -Wpedantic)
    endif()
endif()

# ---------------------------------------------------------------------------
# Subdirectories are added incrementally, phase by phase. Uncommented only
# once real (non-placeholder) sources exist for that component, to honor the
# project rule against fake/stub implementations presented as working code.
# ---------------------------------------------------------------------------

# add_subdirectory(third_party/llama.cpp)   # Phase 1
# add_subdirectory(src/memory)              # Phase 2
# add_subdirectory(src/model)               # Phase 3
# add_subdirectory(src/context)             # Phase 1/2
# add_subdirectory(src/tokenizer)           # Phase 1
# add_subdirectory(src/inference)           # Phase 1
# add_subdirectory(src/api)                 # Phase 1
# add_subdirectory(src/cli)                 # Phase 1/4

# if(SYJ_EDGEMIND_BUILD_TESTS)
#     enable_testing()
#     add_subdirectory(tests)
# endif()

message(STATUS "SYJ EdgeMind: Phase 0 CMake foundation configured. No inference targets exist yet.")
