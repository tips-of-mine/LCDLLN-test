cmake_minimum_required(VERSION 3.20)

project(LCDLLN_Engine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(engine_core
  engine/core/Config.cpp
  engine/core/jobs/JobSystem.cpp
  engine/core/Log.cpp
  engine/core/Time.cpp
  engine/Engine.cpp
  engine/platform/FileSystem.cpp
  engine/platform/Input.cpp
  engine/platform/Window.cpp
)

target_include_directories(engine_core PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
)

if(MSVC)
  target_compile_options(engine_core PRIVATE /W4 /permissive-)
else()
  target_compile_options(engine_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(engine_app
  engine/app/main.cpp
)

target_link_libraries(engine_app PRIVATE engine_core)

if(WIN32)
  target_link_libraries(engine_app PRIVATE user32)
endif()

