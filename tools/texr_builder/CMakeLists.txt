# TEXR pack tool — offline .texr writer (v1 subset) + validate
cmake_minimum_required(VERSION 3.20)

add_executable(texr_builder
  main.cpp
  TexrPack.cpp
)
target_include_directories(texr_builder PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(texr_builder PRIVATE OpenSSL::Crypto lcdlln_texr)

set_target_properties(texr_builder PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY         "${CMAKE_BINARY_DIR}/pkg/texr_builder"
  RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/pkg/texr_builder"
  RUNTIME_OUTPUT_DIRECTORY_DEBUG   "${CMAKE_BINARY_DIR}/pkg/texr_builder"
)

if(MSVC)
  target_compile_options(texr_builder PRIVATE /W4 /permissive- /Zc:preprocessor)
else()
  target_compile_options(texr_builder PRIVATE -Wall -Wextra -Wpedantic)
endif()
