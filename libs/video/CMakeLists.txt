# Native video-recording core (pure). The VICE adapter (vice/mon_video.c)
# fills video_host_t from VICE (the ZMBV screenshot recorder + warp controls)
# and is compiled only inside a VICE tree.

add_library(revice_video STATIC src/video_core.c)
target_include_directories(revice_video PUBLIC include)
revice_target_warnings(revice_video)

add_executable(test_video tests/test_video.c)
target_link_libraries(test_video PRIVATE revice_video)
target_include_directories(test_video PRIVATE ${CMAKE_SOURCE_DIR}/tests)
revice_target_warnings(test_video)
add_test(NAME video COMMAND test_video)
