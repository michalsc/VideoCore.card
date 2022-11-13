cmake_minimum_required(VERSION 3.14.0)
project(VideoCore VERSION 1.0.0)
get_verstring(VERSTRING)

add_link_options(-ffreestanding -nostdlib -nostartfiles -Wl,-e,__start)
add_compile_options(-Os -m68040 -fomit-frame-pointer)
add_compile_definitions(PRIVATE VERSION_STRING="${VERSTRING}")

add_library(VideoCore INTERFACE)
target_include_directories(VideoCore INTERFACE src)

add_executable(VideoCore.card
    src/main.c
    src/mbox.c
    src/vc4.c
    src/vc6.c
    src/support.c
    src/debug.c
    src/end.c
)
target_link_libraries(VideoCore.card devicetree amiga)

install(PROGRAMS ${CMAKE_CURRENT_BINARY_DIR}/VideoCore.card DESTINATION ./VideoCore/)
