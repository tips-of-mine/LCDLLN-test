cmake_minimum_required(VERSION 3.20)
project(engine LANGUAGES CXX VERSION 0.1)

set(CMAKE_CXX_STANDARD 20)

find_package(nlohmann_json CONFIG REQUIRED)
find_package(glfw3 CONFIG REQUIRED)

add_library(engine_core STATIC
  engine/core/Log.cpp
  engine/core/Time.cpp
  engine/core/Config.cpp
  engine/core/memory/Memory.cpp
  engine/core/jobs/JobSystem.cpp
  engine/core/Engine.cpp
)
target_include_directories(engine_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(engine_core PUBLIC nlohmann_json::nlohmann_json)

add_library(engine_platform STATIC
  engine/platform/Window.cpp
  engine/platform/Input.cpp
  engine/platform/FileSystem.cpp
)
target_include_directories(engine_platform PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(engine_platform PUBLIC glfw::glfw)

add_executable(engine_app main.cpp)
target_link_libraries(engine_app PRIVATE engine_core engine_platform)
