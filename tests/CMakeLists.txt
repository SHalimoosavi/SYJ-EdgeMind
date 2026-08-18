# Phase 1 + Phase 2 + v0.3.0: minimal, meaningful tests only.
# No test framework dependency is introduced — each test is a small
# standalone executable that exits non-zero on failure, registered with
# CTest. This keeps the "minimal dependencies" rule from Phase 0 intact.
#
# tests/integration and tests/inference gain further content in later
# phases (Phase 3 model-verification tests, Phase 9 hardening).
# tests/memory holds Phase 2's memory-subsystem tests.
# tests/usage holds v0.3.0's usage/quota-subsystem tests.

add_executable(test_config unit/test_config.cpp)
target_link_libraries(test_config PRIVATE syj-edgemind-core syj_edgemind_warnings)
add_test(NAME test_config COMMAND test_config)

# Phase 4: model_selection.cpp only depends on ModelRegistry (llama.cpp-
# independent), same dependency shape as test_model_resolver — linked the
# same targeted way rather than pulling in the full syj-edgemind-core
# library (and therefore llama.cpp) for a test that doesn't need it.
add_executable(test_model_selection unit/test_model_selection.cpp
    ${CMAKE_SOURCE_DIR}/src/core/model_selection.cpp
    ${CMAKE_SOURCE_DIR}/src/model/model_registry.cpp
    ${CMAKE_SOURCE_DIR}/src/model/model_verifier.cpp
    ${CMAKE_SOURCE_DIR}/src/model/gguf_reader.cpp
    ${CMAKE_SOURCE_DIR}/src/model/model_hash.cpp
    ${CMAKE_SOURCE_DIR}/src/model/model_metadata.cpp
    ${CMAKE_SOURCE_DIR}/src/model/model_types.cpp
)
target_include_directories(test_model_selection PRIVATE ${CMAKE_SOURCE_DIR}/src model)
target_link_libraries(test_model_selection PRIVATE syj_edgemind_warnings)
add_test(NAME test_model_selection COMMAND test_model_selection)

add_executable(test_context_manager unit/test_context_manager.cpp)
target_link_libraries(test_context_manager PRIVATE syj-edgemind-core syj_edgemind_warnings)
add_test(NAME test_context_manager COMMAND test_context_manager)

add_executable(test_memory_estimator memory/test_memory_estimator.cpp)
target_link_libraries(test_memory_estimator PRIVATE syj-edgemind-core syj_edgemind_warnings)
add_test(NAME test_memory_estimator COMMAND test_memory_estimator)

add_executable(test_memory_budget memory/test_memory_budget.cpp)
target_link_libraries(test_memory_budget PRIVATE syj-edgemind-core syj_edgemind_warnings)
add_test(NAME test_memory_budget COMMAND test_memory_budget)

add_executable(test_invalid_model_path integration/test_invalid_model_path.cpp)
target_link_libraries(test_invalid_model_path PRIVATE syj-edgemind-core syj_edgemind_warnings)
add_test(NAME test_invalid_model_path COMMAND test_invalid_model_path)

add_executable(test_memory_admission memory/test_memory_admission.cpp)
target_link_libraries(test_memory_admission PRIVATE syj-edgemind-core syj_edgemind_warnings)
add_test(NAME test_memory_admission COMMAND test_memory_admission)

add_executable(test_usage_accounting usage/test_usage_accounting.cpp)
target_link_libraries(test_usage_accounting PRIVATE syj-edgemind-core syj_edgemind_warnings)
add_test(NAME test_usage_accounting COMMAND test_usage_accounting)

add_executable(test_usage_state_store usage/test_usage_state_store.cpp)
target_link_libraries(test_usage_state_store PRIVATE syj-edgemind-core syj_edgemind_warnings)
add_test(NAME test_usage_state_store COMMAND test_usage_state_store)

add_executable(test_usage_manager usage/test_usage_manager.cpp)
target_link_libraries(test_usage_manager PRIVATE syj-edgemind-core syj_edgemind_warnings)
add_test(NAME test_usage_manager COMMAND test_usage_manager)

# Phase 3: model registry & verification. test_gguf_reader and
# test_model_hash link only their specific src/model/*.cpp translation
# units (not the whole syj-edgemind-core library) since GgufReader and
# Sha256/compute_model_identity are deliberately llama.cpp-independent —
# linking the full core library here would obscure that boundary and pull
# in an unnecessary llama dependency for a test that doesn't need it.
# test_model_verifier and test_model_registry need the same targeted set of
# translation units plus model_metadata.cpp (for the quantization-label
# lookup ModelRegistry::import_model uses).
add_executable(test_gguf_reader model/test_gguf_reader.cpp
    ${CMAKE_SOURCE_DIR}/src/model/gguf_reader.cpp
    ${CMAKE_SOURCE_DIR}/src/model/model_types.cpp
)
target_include_directories(test_gguf_reader PRIVATE ${CMAKE_SOURCE_DIR}/src model)
target_link_libraries(test_gguf_reader PRIVATE syj_edgemind_warnings)
add_test(NAME test_gguf_reader COMMAND test_gguf_reader)

add_executable(test_model_hash model/test_model_hash.cpp
    ${CMAKE_SOURCE_DIR}/src/model/model_hash.cpp
)
target_include_directories(test_model_hash PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_model_hash PRIVATE syj_edgemind_warnings)
add_test(NAME test_model_hash COMMAND test_model_hash)

add_executable(test_model_verifier model/test_model_verifier.cpp
    ${CMAKE_SOURCE_DIR}/src/model/model_verifier.cpp
    ${CMAKE_SOURCE_DIR}/src/model/gguf_reader.cpp
    ${CMAKE_SOURCE_DIR}/src/model/model_hash.cpp
    ${CMAKE_SOURCE_DIR}/src/model/model_metadata.cpp
    ${CMAKE_SOURCE_DIR}/src/model/model_types.cpp
)
target_include_directories(test_model_verifier PRIVATE ${CMAKE_SOURCE_DIR}/src model)
target_link_libraries(test_model_verifier PRIVATE syj_edgemind_warnings)
add_test(NAME test_model_verifier COMMAND test_model_verifier)

add_executable(test_model_registry model/test_model_registry.cpp
    ${CMAKE_SOURCE_DIR}/src/model/model_registry.cpp
    ${CMAKE_SOURCE_DIR}/src/model/model_verifier.cpp
    ${CMAKE_SOURCE_DIR}/src/model/gguf_reader.cpp
    ${CMAKE_SOURCE_DIR}/src/model/model_hash.cpp
    ${CMAKE_SOURCE_DIR}/src/model/model_metadata.cpp
    ${CMAKE_SOURCE_DIR}/src/model/model_types.cpp
)
target_include_directories(test_model_registry PRIVATE ${CMAKE_SOURCE_DIR}/src model)
target_link_libraries(test_model_registry PRIVATE syj_edgemind_warnings)
add_test(NAME test_model_registry COMMAND test_model_registry)

# v0.5.0: model_resolver is pure/llama.cpp-independent, but it does depend
# on ModelRegistry (a read-only registry lookup — see model_resolver.h), so
# its test needs the same registry+verifier translation units as
# test_model_registry above, plus test_gguf_fixture.h to construct real
# GGUF fixtures for the "resolve a real imported model_id" cases.
add_executable(test_model_resolver model/test_model_resolver.cpp
    ${CMAKE_SOURCE_DIR}/src/model/model_resolver.cpp
    ${CMAKE_SOURCE_DIR}/src/model/model_registry.cpp
    ${CMAKE_SOURCE_DIR}/src/model/model_verifier.cpp
    ${CMAKE_SOURCE_DIR}/src/model/gguf_reader.cpp
    ${CMAKE_SOURCE_DIR}/src/model/model_hash.cpp
    ${CMAKE_SOURCE_DIR}/src/model/model_metadata.cpp
    ${CMAKE_SOURCE_DIR}/src/model/model_types.cpp
)
target_include_directories(test_model_resolver PRIVATE ${CMAKE_SOURCE_DIR}/src model)
target_link_libraries(test_model_resolver PRIVATE syj_edgemind_warnings)
add_test(NAME test_model_resolver COMMAND test_model_resolver)

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS "9.0")
    # See the matching comment in the top-level CMakeLists.txt.
    target_link_libraries(test_gguf_reader PRIVATE stdc++fs)
    target_link_libraries(test_model_verifier PRIVATE stdc++fs)
    target_link_libraries(test_model_registry PRIVATE stdc++fs)
    target_link_libraries(test_model_resolver PRIVATE stdc++fs)
    target_link_libraries(test_model_selection PRIVATE stdc++fs)
endif()
