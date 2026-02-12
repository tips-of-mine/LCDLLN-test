cmake_minimum_required(VERSION 3.21)
project(LCDLLN_test LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(ENGINE_CORE_SOURCES
    engine/core/Log.cpp
    engine/core/Time.cpp
    engine/core/Config.cpp
    engine/core/memory/Memory.cpp
    engine/core/jobs/JobSystem.cpp
)

add_executable(engine_app
    engine/app/main.cpp
    ${ENGINE_CORE_SOURCES}
)

target_include_directories(engine_app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(job_system_tests
    engine/tests/JobSystemTests.cpp
    ${ENGINE_CORE_SOURCES}
)

target_include_directories(job_system_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

if(MSVC)
    target_compile_options(engine_app PRIVATE /W4 /permissive-)
    target_compile_options(job_system_tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(engine_app PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(job_system_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()
