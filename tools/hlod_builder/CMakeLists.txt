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
set_target_properties(hlod_builder PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY         "${CMAKE_BINARY_DIR}/pkg/hlod_builder"
  RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/pkg/hlod_builder"
  RUNTIME_OUTPUT_DIRECTORY_DEBUG   "${CMAKE_BINARY_DIR}/pkg/hlod_builder"
)

if(MSVC)
  target_compile_options(hlod_builder PRIVATE /W4 /permissive-)
else()
  target_compile_options(hlod_builder PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Copie build_hlod.bat à côté de l'exe (script d'utilisation)
if(EXISTS "${CMAKE_SOURCE_DIR}/build_hlod.bat")
  add_custom_command(TARGET hlod_builder POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/build_hlod.bat"
            "$<TARGET_FILE_DIR:hlod_builder>/build_hlod.bat"
    COMMENT "Copie de build_hlod.bat..."
  )
endif()