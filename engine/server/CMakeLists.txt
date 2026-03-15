# M15.4 — server_app : binaire serveur headless (pas de Vulkan, pas de GLFW)
# WIN32: UDP server (UdpTransport). UNIX: TCP server (NetServer epoll, M19.4).
cmake_minimum_required(VERSION 3.20)
set(CMAKE_CXX_STANDARD 20)

if(WIN32)
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
    ${CMAKE_SOURCE_DIR}/engine/server/SessionManager.cpp
    ${CMAKE_SOURCE_DIR}/engine/server/RateLimitAndBan.cpp
    ${CMAKE_SOURCE_DIR}/engine/server/SecurityAuditLog.cpp
  )
  target_link_libraries(server_app PRIVATE ws2_32 engine_auth)
elseif(UNIX)
  find_package(OpenSSL REQUIRED)
  add_executable(server_app
    main_server_linux.cpp
    NetServer.cpp
    ${CMAKE_SOURCE_DIR}/engine/core/Config.cpp
    ${CMAKE_SOURCE_DIR}/engine/core/Log.cpp
    ${CMAKE_SOURCE_DIR}/engine/platform/FileSystem.cpp
    ${CMAKE_SOURCE_DIR}/engine/network/PacketView.cpp
    ${CMAKE_SOURCE_DIR}/engine/network/ByteReader.cpp
    ${CMAKE_SOURCE_DIR}/engine/network/ByteWriter.cpp
    ${CMAKE_SOURCE_DIR}/engine/network/PacketBuilder.cpp
    ${CMAKE_SOURCE_DIR}/engine/network/ErrorPacket.cpp
    AccountValidation.cpp
    ${CMAKE_SOURCE_DIR}/engine/server/SessionManager.cpp
    ${CMAKE_SOURCE_DIR}/engine/server/RateLimitAndBan.cpp
    ${CMAKE_SOURCE_DIR}/engine/server/SecurityAuditLog.cpp
  )
  target_link_libraries(server_app PRIVATE pthread OpenSSL::SSL engine_auth)
else()
  message(STATUS "M15.4: server_app not configured for this platform (WIN32/UNIX only)")
  return()
endif()

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

# Copie config.json à côté du binaire serveur si présent
if(EXISTS "${CMAKE_SOURCE_DIR}/config.json")
  add_custom_command(TARGET server_app POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/config.json"
            "$<TARGET_FILE_DIR:server_app>/config.json"
    COMMENT "Copie de config.json pour server_app..."
  )
endif()
