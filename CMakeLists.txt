# CMakeLists.txt
cmake_minimum_required(VERSION 3.16)
project(hls_packager CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_options(/W4)
else()
    add_compile_options(-Wall -Wextra)
endif()

add_executable(hls_packager
    src/main.cpp
    src/ffmpeg_encoder.cpp
    src/hls_playlist_writer.cpp
    src/orchestrator.cpp
    src/process_runner.cpp
)

target_include_directories(hls_packager PRIVATE include)

find_package(Threads REQUIRED)
target_link_libraries(hls_packager PRIVATE Threads::Threads)