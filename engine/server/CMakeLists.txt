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
  target_link_libraries(server_app PRIVATE ws2_32 engine_auth spdlog::spdlog)
elseif(UNIX)
  find_package(OpenSSL REQUIRED)
  find_path(MYSQL_INCLUDE_DIR NAMES mysql.h mysql/mysql.h PATH_SUFFIXES mysql)
  find_library(MYSQL_LIBRARY NAMES mysqlclient)
  if(NOT MYSQL_INCLUDE_DIR OR NOT MYSQL_LIBRARY)
    message(FATAL_ERROR "MySQL client not found. Install libmysqlclient-dev (or mariadb-dev) and ensure mysql.h is available.")
  endif()
  add_executable(server_app
    main_server_linux.cpp
    MigrationRunner.cpp
    db/ConnectionPool.cpp
    db/DbHelpers.cpp
    NetServer.cpp
    ${CMAKE_SOURCE_DIR}/engine/core/Config.cpp
    ${CMAKE_SOURCE_DIR}/engine/core/Log.cpp
    ${CMAKE_SOURCE_DIR}/engine/platform/FileSystem.cpp
    ${CMAKE_SOURCE_DIR}/engine/network/PacketView.cpp
    ${CMAKE_SOURCE_DIR}/engine/network/ByteReader.cpp
    ${CMAKE_SOURCE_DIR}/engine/network/ByteWriter.cpp
    ${CMAKE_SOURCE_DIR}/engine/network/PacketBuilder.cpp
    ${CMAKE_SOURCE_DIR}/engine/network/ErrorPacket.cpp
    ${CMAKE_SOURCE_DIR}/engine/network/AuthRegisterPayloads.cpp
    ${CMAKE_SOURCE_DIR}/engine/network/ShardPayloads.cpp
    ${CMAKE_SOURCE_DIR}/engine/network/ShardTicketPayloads.cpp
    ${CMAKE_SOURCE_DIR}/engine/network/ServerListPayloads.cpp
    AccountValidation.cpp
    InMemoryAccountStore.cpp
    AuthRegisterHandler.cpp
    ConnectionSessionMap.cpp
    ${CMAKE_SOURCE_DIR}/engine/server/SessionManager.cpp
    ${CMAKE_SOURCE_DIR}/engine/server/RateLimitAndBan.cpp
    ${CMAKE_SOURCE_DIR}/engine/server/SecurityAuditLog.cpp
    ShardRegistry.cpp
    ShardRegisterHandler.cpp
    ShardTicketCrypto.cpp
    ShardTicketHandler.cpp
    ShardTicketValidator.cpp
    ServerListHandler.cpp
    HealthEndpoint.cpp
    PrometheusMetrics.cpp
  )
  target_include_directories(server_app PRIVATE ${MYSQL_INCLUDE_DIR} ${CMAKE_SOURCE_DIR})
  target_link_libraries(server_app PRIVATE pthread OpenSSL::SSL engine_auth spdlog::spdlog ${MYSQL_LIBRARY})

  # M21.6: DB layer smoke tests
  add_executable(db_layer_tests db/DbLayerTests.cpp db/ConnectionPool.cpp db/DbHelpers.cpp)
  target_include_directories(db_layer_tests PRIVATE ${CMAKE_SOURCE_DIR} ${MYSQL_INCLUDE_DIR})
  target_link_libraries(db_layer_tests PRIVATE engine_core ${MYSQL_LIBRARY} pthread)
  target_compile_options(db_layer_tests PRIVATE -Wall -Wextra -Wpedantic)
  add_test(NAME db_layer_tests COMMAND db_layer_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

  # M22.4: Shard ticket validation tests
  add_executable(shard_ticket_tests
    ShardTicketTests.cpp
    ShardTicketValidator.cpp
    ShardTicketCrypto.cpp
    ${CMAKE_SOURCE_DIR}/engine/network/ShardTicketPayloads.cpp
    ${CMAKE_SOURCE_DIR}/engine/network/ByteReader.cpp
    ${CMAKE_SOURCE_DIR}/engine/network/ByteWriter.cpp
    ${CMAKE_SOURCE_DIR}/engine/network/PacketBuilder.cpp
  )
  target_include_directories(shard_ticket_tests PRIVATE ${CMAKE_SOURCE_DIR})
  target_link_libraries(shard_ticket_tests PRIVATE engine_core OpenSSL::SSL)
  target_compile_options(shard_ticket_tests PRIVATE -Wall -Wextra -Wpedantic)
  add_test(NAME shard_ticket_tests COMMAND shard_ticket_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

  # M22.5: Server list and shard selection policy tests
  add_executable(server_list_policy_tests
    ServerListPolicyTests.cpp
    ShardRegistry.cpp
    ${CMAKE_SOURCE_DIR}/engine/network/ServerListPayloads.cpp
    ${CMAKE_SOURCE_DIR}/engine/network/ByteReader.cpp
    ${CMAKE_SOURCE_DIR}/engine/network/ByteWriter.cpp
    ${CMAKE_SOURCE_DIR}/engine/network/PacketBuilder.cpp
  )
  target_include_directories(server_list_policy_tests PRIVATE ${CMAKE_SOURCE_DIR})
  target_link_libraries(server_list_policy_tests PRIVATE engine_core)
  target_compile_options(server_list_policy_tests PRIVATE -Wall -Wextra -Wpedantic)
  add_test(NAME server_list_policy_tests COMMAND server_list_policy_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

  # M22.6: Shard server (ticket handshake only; no ShardToMasterClient on Linux)
  add_executable(shard_app
    main_shard_linux.cpp
    NetServer.cpp
    ShardTicketValidator.cpp
    ShardTicketHandshakeHandler.cpp
    ShardTicketCrypto.cpp
    HealthEndpoint.cpp
    PrometheusMetrics.cpp
    ${CMAKE_SOURCE_DIR}/engine/network/ShardTicketPayloads.cpp
    ${CMAKE_SOURCE_DIR}/engine/network/ByteReader.cpp
    ${CMAKE_SOURCE_DIR}/engine/network/ByteWriter.cpp
    ${CMAKE_SOURCE_DIR}/engine/network/PacketBuilder.cpp
  )
  target_include_directories(shard_app PRIVATE ${CMAKE_SOURCE_DIR})
  target_link_libraries(shard_app PRIVATE engine_core OpenSSL::SSL pthread)
  target_compile_options(shard_app PRIVATE -Wall -Wextra -Wpedantic)
  set_target_properties(shard_app PROPERTIES
    OUTPUT_NAME "lcdlln_shard"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/pkg/server"
  )
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
