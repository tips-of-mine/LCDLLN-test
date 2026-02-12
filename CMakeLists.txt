cmake_minimum_required(VERSION 3.21)
project(LCDLLN_test LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(engine_app
    engine/app/main.cpp
    engine/core/Log.cpp
    engine/core/Time.cpp
    engine/core/Config.cpp
)

target_include_directories(engine_app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

if(MSVC)
    target_compile_options(engine_app PRIVATE /W4 /permissive-)
else()
    target_compile_options(engine_app PRIVATE -Wall -Wextra -Wpedantic)
endif()
