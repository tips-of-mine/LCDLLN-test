# M25.1 — Offline tool: Load tester (1000+ TLS connections)
cmake_minimum_required(VERSION 3.20)
set(CMAKE_CXX_STANDARD 20)

if(WIN32)
  add_executable(load_tester
    main.cpp
    LoadTester.cpp
  )

  target_include_directories(load_tester PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(load_tester PRIVATE engine_core)

  if(MSVC)
    target_compile_options(load_tester PRIVATE /W4 /permissive- /Zc:preprocessor)
  else()
    target_compile_options(load_tester PRIVATE -Wall -Wextra -Wpedantic)
  endif()

  set_target_properties(load_tester PROPERTIES
    OUTPUT_NAME "load_tester"
    RUNTIME_OUTPUT_DIRECTORY         "${CMAKE_BINARY_DIR}/pkg/tools"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/pkg/tools"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG   "${CMAKE_BINARY_DIR}/pkg/tools"
  )
endif()

