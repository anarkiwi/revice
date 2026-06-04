# C64 screenscrape core (pure). The VICE adapters (vice/c64screen.c gathers
# VIC-II state; vice/mon_screen.c registers + renders) are compiled only inside
# a VICE tree.

add_library(revice_screen STATIC src/screen_core.c)
target_include_directories(revice_screen PUBLIC include)
revice_target_warnings(revice_screen)

add_executable(test_screen tests/test_screen.c)
target_link_libraries(test_screen PRIVATE revice_screen)
target_include_directories(test_screen PRIVATE ${CMAKE_SOURCE_DIR}/tests)
revice_target_warnings(test_screen)
add_test(NAME screen COMMAND test_screen)
