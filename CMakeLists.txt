cmake_minimum_required(VERSION 3.22)
project(simpleFFplay C)

set(CMAKE_C_STANDARD 99)

# FFmpeg
set(ffmpeg_prefix "/usr/local/ffmpeg/4.4")
set(FFMPEG_LIBDIR "${ffmpeg_prefix}/lib")
set(FFMPEG_INCLUDE_DIRS "${ffmpeg_prefix}/include")

# SDL2
set(sdl2_prefix "/opt/homebrew/Cellar/sdl2/2.32.8")
set(SDL2_LIBDIR "${sdl2_prefix}/lib")
set(SDL2_INCLUDE_DIRS "${sdl2_prefix}/include")

include_directories(
        ${FFMPEG_INCLUDE_DIRS}
        ${SDL2_INCLUDE_DIRS}
)

link_directories(
        ${FFMPEG_LIBDIR}
        ${SDL2_LIBDIR}
)

set(SOURCE_FILES
        main.c
)

add_executable(simpleFFplay ${SOURCE_FILES})

# 使用完整路径链接，避免系统 FFmpeg 版本冲突
target_link_libraries(
        simpleFFplay
        ${FFMPEG_LIBDIR}/libavformat.dylib
        ${FFMPEG_LIBDIR}/libavcodec.dylib
        ${FFMPEG_LIBDIR}/libavutil.dylib
        ${FFMPEG_LIBDIR}/libswscale.dylib
        ${FFMPEG_LIBDIR}/libswresample.dylib
        ${SDL2_LIBDIR}/libSDL2.dylib
        m
)
