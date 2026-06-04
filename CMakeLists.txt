cmake_minimum_required(VERSION 3.13)

project(revice
        VERSION 0.1.0
        DESCRIPTION "Reusable, tested cores for the asid-vice custom VICE features"
        LANGUAGES C)

# revice ships pure-C "core" libraries that carry the logic of the asid-vice
# fork's custom features (ASID MIDI sound, keymatrix injection, screenscrape,
# DRIVE_ATTACH, silent-checkpoint). The cores have NO dependency on VICE: every
# point that touches the emulator is injected through a host-ops struct, so the
# cores build and unit-test standalone here. Thin VICE adapters live under each
# library's vice/ directory and are compiled only inside a VICE tree (see
# integration/vice/), never by this project.

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

option(REVICE_WERROR "Treat warnings as errors when building cores/tests" ON)

# Warning flags applied to every core + test target via revice_target_warnings().
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    set(REVICE_WARN_FLAGS -Wall -Wextra -Wshadow -Wstrict-prototypes)
    if(REVICE_WERROR)
        list(APPEND REVICE_WARN_FLAGS -Werror)
    endif()
endif()

function(revice_target_warnings tgt)
    if(REVICE_WARN_FLAGS)
        target_compile_options(${tgt} PRIVATE ${REVICE_WARN_FLAGS})
    endif()
endfunction()

include(CTest)
enable_testing()

add_subdirectory(libs/asid)
add_subdirectory(libs/keymatrix)
add_subdirectory(libs/screen)
add_subdirectory(libs/driveattach)
add_subdirectory(libs/checkpoint)
