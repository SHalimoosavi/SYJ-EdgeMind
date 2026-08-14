# Phase 1: minimal, meaningful tests only (per Phase 1 spec §18).
# No test framework dependency is introduced — each test is a small
# standalone executable that exits non-zero on failure, registered with
# CTest. This keeps the "minimal dependencies" rule from Phase 0 intact.
#
# tests/integration and tests/memory/tests/inference gain real content in
# later phases (Phase 2 memory tests, Phase 3 model-verification tests,
# Phase 9 hardening) — they are not populated with placeholder tests here.

add_executable(test_config unit/test_config.cpp)
target_link_libraries(test_config PRIVATE syj-edgemind-core syj_edgemind_warnings)
add_test(NAME test_config COMMAND test_config)

add_executable(test_context_manager unit/test_context_manager.cpp)
target_link_libraries(test_context_manager PRIVATE syj-edgemind-core syj_edgemind_warnings)
add_test(NAME test_context_manager COMMAND test_context_manager)

add_executable(test_invalid_model_path integration/test_invalid_model_path.cpp)
target_link_libraries(test_invalid_model_path PRIVATE syj-edgemind-core syj_edgemind_warnings)
add_test(NAME test_invalid_model_path COMMAND test_invalid_model_path)
