# DRIVE_ATTACH body decoder (pure). The file_system_attach_disk/detach_disk
# calls stay in the monitor_binary.c wiring inside the VICE tree.

add_library(revice_driveattach STATIC src/driveattach_core.c)
target_include_directories(revice_driveattach PUBLIC include)
revice_target_warnings(revice_driveattach)

add_executable(test_driveattach tests/test_driveattach.c)
target_link_libraries(test_driveattach PRIVATE revice_driveattach)
target_include_directories(test_driveattach PRIVATE ${CMAKE_SOURCE_DIR}/tests)
revice_target_warnings(test_driveattach)
add_test(NAME driveattach COMMAND test_driveattach)
