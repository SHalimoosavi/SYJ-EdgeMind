# Phase 1 + Phase 2: minimal, meaningful tests only.
# No test framework dependency is introduced — each test is a small
# standalone executable that exits non-zero on failure, registered with
# CTest. This keeps the "minimal dependencies" rule from Phase 0 intact.
#
# tests/integration and tests/inference gain further content in later
# phases (Phase 3 model-verification tests, Phase 9 hardening).
# tests/memory holds Phase 2's memory-subsystem tests.

add_executable(test_config unit/test_config.cpp)
target_link_libraries(test_config PRIVATE syj-edgemind-core syj_edgemind_warnings)
add_test(NAME test_config COMMAND test_config)

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
