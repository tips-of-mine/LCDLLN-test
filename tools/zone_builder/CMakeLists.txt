# M10.5 / M11.2 — Zone builder: writes chunk packages (chunk.meta + split pak/bin files).
cmake_minimum_required(VERSION 3.20)
set(CMAKE_CXX_STANDARD 20)

add_executable(zone_builder
  main.cpp
  ChunkPackageWriter.cpp
)
target_include_directories(zone_builder PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

if(MSVC)
  target_compile_options(zone_builder PRIVATE /W4 /permissive-)
else()
  target_compile_options(zone_builder PRIVATE -Wall -Wextra -Wpedantic)
endif()
