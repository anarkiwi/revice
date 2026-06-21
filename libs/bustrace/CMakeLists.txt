# CPU bus-trace serializer (pure core). The VICE maincpu-bus hook + file I/O
# live in vice/soundbustrace.c, compiled only inside a VICE tree, never here.

add_library(revice_bustrace STATIC src/bustrace_core.c)
target_include_directories(revice_bustrace PUBLIC include)
revice_target_warnings(revice_bustrace)

add_executable(test_bustrace tests/test_bustrace.c)
target_link_libraries(test_bustrace PRIVATE revice_bustrace)
target_include_directories(test_bustrace PRIVATE ${CMAKE_SOURCE_DIR}/tests)
revice_target_warnings(test_bustrace)
add_test(NAME bustrace COMMAND test_bustrace)
