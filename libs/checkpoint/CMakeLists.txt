# silent-checkpoint body decode (pure). The behavioral change to
# mon_breakpoint_check_checkpoint() + the `silent` struct field ship as a
# documented patch in integration/vice/patches/.

add_library(revice_checkpoint STATIC src/checkpoint_core.c)
target_include_directories(revice_checkpoint PUBLIC include)
revice_target_warnings(revice_checkpoint)

add_executable(test_checkpoint tests/test_checkpoint.c)
target_link_libraries(test_checkpoint PRIVATE revice_checkpoint)
target_include_directories(test_checkpoint PRIVATE ${CMAKE_SOURCE_DIR}/tests)
revice_target_warnings(test_checkpoint)
add_test(NAME checkpoint COMMAND test_checkpoint)
