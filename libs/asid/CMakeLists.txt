# ASID-over-MIDI protocol codec (pure core). The ALSA transport + VICE
# sound_device_t registration live in vice/soundasid.c, compiled only inside a
# VICE tree, never here.

add_library(revice_asid STATIC src/asid_core.c)
target_include_directories(revice_asid PUBLIC include)
revice_target_warnings(revice_asid)

add_executable(test_asid tests/test_asid.c)
target_link_libraries(test_asid PRIVATE revice_asid)
target_include_directories(test_asid PRIVATE ${CMAKE_SOURCE_DIR}/tests)
revice_target_warnings(test_asid)
add_test(NAME asid COMMAND test_asid)
