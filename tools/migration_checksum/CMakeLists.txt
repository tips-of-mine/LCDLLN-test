# M21.2 — Outil offline : checksum SHA-256 des fichiers de migration SQL.
cmake_minimum_required(VERSION 3.20)
set(CMAKE_CXX_STANDARD 20)

find_package(OpenSSL REQUIRED)

add_executable(migration_checksum main.cpp)
target_include_directories(migration_checksum PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(migration_checksum PRIVATE OpenSSL::SSL)

set_target_properties(migration_checksum PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY         "${CMAKE_BINARY_DIR}/pkg/tools"
  RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/pkg/tools"
  RUNTIME_OUTPUT_DIRECTORY_DEBUG   "${CMAKE_BINARY_DIR}/pkg/tools"
)

if(MSVC)
  target_compile_options(migration_checksum PRIVATE /W4 /permissive- /Zc:preprocessor)
else()
  target_compile_options(migration_checksum PRIVATE -Wall -Wextra -Wpedantic)
endif()
