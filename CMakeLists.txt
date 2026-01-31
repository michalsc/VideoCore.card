cmake_minimum_required(VERSION 3.14.0)
project(VideoCore VERSION 1.3)

include(cmake/verstring.cmake)
get_verstring(VERSTRING)

if(NOT TARGET devicetree)
    add_subdirectory(devicetree.resource EXCLUDE_FROM_ALL)
endif()
if(NOT TARGET unicam)
    add_subdirectory(unicam.resource EXCLUDE_FROM_ALL)
endif()

add_executable(VideoCore.card
    src/main.c
    src/mbox.c
    src/vc4.c
    src/vc6.c
    src/support.c
    src/debug.c
    src/end.c
    src/unicam.c
    src/buddyalloc.c
)

target_include_directories(VideoCore.card PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_options(VideoCore.card PRIVATE -Os -m68040 -fomit-frame-pointer)
target_compile_definitions(VideoCore.card PRIVATE VERSION_STRING="${VERSTRING}")
target_link_options(VideoCore.card PRIVATE -ffreestanding -nostdlib -nostartfiles -Wl,-e,__start)
target_link_libraries(VideoCore.card devicetree amiga unicam)
target_compile_definitions(VideoCore.card PUBLIC CARD_NAME="VideoCore.card")

install(PROGRAMS ${CMAKE_CURRENT_BINARY_DIR}/VideoCore.card DESTINATION VideoCore/)
