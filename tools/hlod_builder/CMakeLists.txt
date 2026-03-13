# M09.4 — HLOD builder: offline tool (no Vulkan, no engine).
cmake_minimum_required(VERSION 3.20)
set(CMAKE_CXX_STANDARD 20)

add_executable(hlod_builder
  main.cpp
  ManifestParser.cpp
  MeshLoader.cpp
  Clustering.cpp
  MeshMerge.cpp
  PakWriter.cpp
  ScanManifest.cpp
)
target_include_directories(hlod_builder PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

if(MSVC)
  target_compile_options(hlod_builder PRIVATE /W4 /permissive-)
else()
  target_compile_options(hlod_builder PRIVATE -Wall -Wextra -Wpedantic)
endif()