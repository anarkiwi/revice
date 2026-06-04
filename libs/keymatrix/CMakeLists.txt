# Keyboard-matrix injection core (pure). The VICE adapter (vice/mon_keymatrix.c)
# fills km_host_t from VICE and is compiled only inside a VICE tree.

add_library(revice_keymatrix STATIC src/keymatrix_core.c)
target_include_directories(revice_keymatrix PUBLIC include)
revice_target_warnings(revice_keymatrix)

add_executable(test_keymatrix tests/test_keymatrix.c)
target_link_libraries(test_keymatrix PRIVATE revice_keymatrix)
target_include_directories(test_keymatrix PRIVATE ${CMAKE_SOURCE_DIR}/tests)
revice_target_warnings(test_keymatrix)
add_test(NAME keymatrix COMMAND test_keymatrix)
