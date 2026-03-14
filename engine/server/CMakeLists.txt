# M15.4 — server_app : binaire serveur headless (pas de Vulkan, pas de GLFW)
cmake_minimum_required(VERSION 3.20)
set(CMAKE_CXX_STANDARD 20)

if(NOT WIN32)
  message(STATUS "M15.4: skipping server_app on non-Windows host")
  return()
endif()

add_executable(server_app
  main_server.cpp
  ServerApp.cpp
  ServerProtocol.cpp
  CharacterPersistence.cpp
  QuestRuntime.cpp
  SpawnerRuntime.cpp
  EventRuntime.cpp
  UdpTransport.cpp
  TickScheduler.cpp
  SpatialPartition.cpp
  ZoneTransitions.cpp
  ${CMAKE_SOURCE_DIR}/engine/core/Config.cpp
  ${CMAKE_SOURCE_DIR}/engine/core/Log.cpp
  ${CMAKE_SOURCE_DIR}/engine/platform/FileSystem.cpp
)

target_include_directories(server_app PRIVATE
  ${CMAKE_SOURCE_DIR}
)

set_target_properties(server_app PROPERTIES
  OUTPUT_NAME "lcdlln_server"
  RUNTIME_OUTPUT_DIRECTORY         "${CMAKE_BINARY_DIR}/pkg/server"
  RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/pkg/server"
  RUNTIME_OUTPUT_DIRECTORY_DEBUG   "${CMAKE_BINARY_DIR}/pkg/server"
)

if(MSVC)
  target_compile_options(server_app PRIVATE /W4 /permissive- /Zc:preprocessor)
else()
  target_compile_options(server_app PRIVATE -Wall -Wextra -Wpedantic)
endif()

target_link_libraries(server_app PRIVATE ws2_32)

# Copie config.json à côté du binaire serveur si présent
if(EXISTS "${CMAKE_SOURCE_DIR}/config.json")
  add_custom_command(TARGET server_app POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/config.json"
            "$<TARGET_FILE_DIR:server_app>/config.json"
    COMMENT "Copie de config.json pour server_app..."
  )
endif()
