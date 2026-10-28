cmake_minimum_required(VERSION 3.22)
project(f117vr C)

# VR: the engine as libf117.so for the Meta Quest app (../../../F117A Stealth Fighter 2.0, StereoKit).
# The same sources as the desktop build, with src/vr/host_vr.c in place of host.c (no SDL) and main.c's
# main renamed f117_main, which the engine thread runs. Build: tools/build_android.ps1.

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(SRC ${CMAKE_CURRENT_SOURCE_DIR}/../src)

set(F117_MODULES platform video sound frontend shell sim render3d flight debrief programs enhanced)
file(GLOB F117_SOURCES CONFIGURE_DEPENDS ${SRC}/*.c)
list(REMOVE_ITEM F117_SOURCES ${SRC}/host.c)
foreach(m ${F117_MODULES})
    file(GLOB m_sources CONFIGURE_DEPENDS ${SRC}/${m}/*.c)
    list(APPEND F117_SOURCES ${m_sources})
endforeach()
list(APPEND F117_SOURCES ${SRC}/vr/host_vr.c ${SRC}/vr/vr_world.c ${SRC}/vr/vr_fdm.c ${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/nuked-opl3/opl3.c)

add_library(f117 SHARED ${F117_SOURCES})
target_include_directories(f117 PRIVATE ${SRC} ${SRC}/gen ${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/nuked-opl3)
target_compile_definitions(f117 PRIVATE F117_VR)
set_source_files_properties(${SRC}/main.c PROPERTIES COMPILE_DEFINITIONS main=f117_main)
# The memory model reads multi-byte values through byte pointers (src/mem.h); the original was built for
# x86, where char is signed: keep it signed on ARM.
target_compile_options(f117 PRIVATE -Wall -Wextra -fno-strict-aliasing -fsigned-char -fvisibility=hidden)
target_link_libraries(f117 PRIVATE log m)
