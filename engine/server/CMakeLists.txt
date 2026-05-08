# M15.4 — server_app : binaire serveur headless (pas de Vulkan, pas de GLFW)
# WIN32: UDP server (UdpTransport). UNIX: TCP server (NetServer epoll, M19.4).
cmake_minimum_required(VERSION 3.20)
set(CMAKE_CXX_STANDARD 20)

# Helper pour declarer un test simple sans deps externes (engine_core +
# spdlog + pthread suffisent).  Cible le pattern recurrent des tests
# CMANGOS Phase 2 (BIH, ObjectGuid, VMapFormat, ChatChannelRegistry,
# GridState, ChatSanitizer...) ou chaque target faisait ~9 lignes de
# boilerplate identique. Avec ce helper, declarer un test devient :
#
#   lcdlln_add_simple_test(my_thing_tests
#     foo/MyThingTests.cpp
#     foo/MyThing.cpp
#     foo/MyDep.cpp)
#
# Les tests qui ont besoin de MySQL, OpenSSL, ou d'autres libs gardent
# leur add_executable manuel (pas la majorite). Ce helper reduit la
# zone de conflit dans CMakeLists.txt entre PRs paralleles : chaque
# nouvelle test target = 1 appel sur 4-5 lignes au lieu de 9-10 lignes
# d'equivalent inline.
function(lcdlln_add_simple_test target_name)
  add_executable(${target_name} ${ARGN})
  target_include_directories(${target_name} PRIVATE ${CMAKE_SOURCE_DIR})
  target_link_libraries(${target_name} PRIVATE engine_core spdlog::spdlog pthread)
  target_compile_options(${target_name} PRIVATE -Wall -Wextra -Wpedantic)
  add_test(NAME ${target_name} COMMAND ${target_name} WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
endfunction()

if(WIN32)
  add_executable(server_app
    main_server.cpp
    ServerApp.cpp
    ChatCommandParser.cpp
    ServerProtocol.cpp
    FriendSystem.cpp
    PartySystem.cpp
    GuildSystem.cpp
    GuildTabard.cpp
    GuildBank.cpp
    ${CMAKE_SOURCE_DIR}/engine/net/ChatSystem.cpp
    ${CMAKE_SOURCE_DIR}/engine/net/ChatEmotes.cpp
    CharacterPersistence.cpp
    CurrencyConfig.cpp
    VendorCatalog.cpp
    PlayerWalletService.cpp
    TradeSystem.cpp
    GatheringSystem.cpp
    CraftingSystem.cpp
    AuctionHouse.cpp
    QuestRuntime.cpp
    SpawnerRuntime.cpp
    EventRuntime.cpp
    UdpTransport.cpp
    TickScheduler.cpp
    SpatialPartition.cpp
    GridState.cpp
    ZoneTransitions.cpp
    LagCompensation.cpp
    ${CMAKE_SOURCE_DIR}/engine/core/Config.cpp
    ${CMAKE_SOURCE_DIR}/engine/core/Log.cpp
    ${CMAKE_SOURCE_DIR}/engine/core/LogConfig.cpp
    ${CMAKE_SOURCE_DIR}/engine/platform/FileSystem.cpp
    ${CMAKE_SOURCE_DIR}/engine/server/SessionManager.cpp
    ${CMAKE_SOURCE_DIR}/engine/server/RateLimitAndBan.cpp
    ${CMAKE_SOURCE_DIR}/engine/server/SecurityAuditLog.cpp
    ${CMAKE_SOURCE_DIR}/engine/server/UserRateLimiter.cpp
    ${CMAKE_SOURCE_DIR}/engine/server/CaptchaVerifier.cpp
    ${CMAKE_SOURCE_DIR}/engine/server/BotDetector.cpp
  )
  # M32.1: no MySQL on WIN32 game shard — FriendSystem compiles in no-DB mode.
  # Do NOT define ENGINE_HAS_MYSQL so #if ENGINE_HAS_MYSQL evaluates to false.
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
    LagCompensation.cpp
    ${CMAKE_SOURCE_DIR}/engine/core/Config.cpp
    ${CMAKE_SOURCE_DIR}/engine/core/Log.cpp
    ${CMAKE_SOURCE_DIR}/engine/core/LogConfig.cpp
    ${CMAKE_SOURCE_DIR}/engine/platform/FileSystem.cpp
    ${CMAKE_SOURCE_DIR}/engine/net/ChatSystem.cpp
    ${CMAKE_SOURCE_DIR}/engine/net/ChatEmotes.cpp
    ${CMAKE_SOURCE_DIR}/engine/network/PacketView.cpp
    ${CMAKE_SOURCE_DIR}/engine/network/ByteReader.cpp
    ${CMAKE_SOURCE_DIR}/engine/network/ByteWriter.cpp
    ${CMAKE_SOURCE_DIR}/engine/network/PacketBuilder.cpp
    ${CMAKE_SOURCE_DIR}/engine/network/ErrorPacket.cpp
    ${CMAKE_SOURCE_DIR}/engine/network/AuthRegisterPayloads.cpp
    ${CMAKE_SOURCE_DIR}/engine/network/CharacterPayloads.cpp
    ${CMAKE_SOURCE_DIR}/engine/network/ShardPayloads.cpp
    ${CMAKE_SOURCE_DIR}/engine/network/ShardTicketPayloads.cpp
    ${CMAKE_SOURCE_DIR}/engine/network/ServerListPayloads.cpp
    ${CMAKE_SOURCE_DIR}/engine/network/TermsPayloads.cpp
    FriendSystem.cpp
    PartySystem.cpp
    GuildSystem.cpp
    GuildTabard.cpp
    GuildBank.cpp
    AccountValidation.cpp
    InMemoryAccountStore.cpp
    MysqlAccountStore.cpp
    AccountRole.cpp
    AccountRoleService.cpp
    AuthRegisterHandler.cpp
    ConnectionSessionMap.cpp
    SmtpMailer.cpp
    LocalizedEmail.cpp
    TermsRepository.cpp
    TermsHandler.cpp
    CharacterCreateHandler.cpp
    CharacterListHandler.cpp
    CharacterDeleteHandler.cpp
    CharacterSavePositionHandler.cpp
    CharacterEnterWorldHandler.cpp
    SessionCharacterMap.cpp
    ChatRelayHandler.cpp
    chat/ChatSanitizer.cpp
    chat/ChatGate.cpp
    chat/ChatCommandRouter.cpp
    chat/ChatChannelRegistry.cpp
    ${CMAKE_SOURCE_DIR}/engine/network/ChatPayloads.cpp
    PasswordResetStore.cpp
    PasswordResetHandler.cpp
    ${CMAKE_SOURCE_DIR}/engine/server/SessionManager.cpp
    ${CMAKE_SOURCE_DIR}/engine/server/RateLimitAndBan.cpp
    ${CMAKE_SOURCE_DIR}/engine/server/SecurityAuditLog.cpp
    ${CMAKE_SOURCE_DIR}/engine/server/UserRateLimiter.cpp
    ${CMAKE_SOURCE_DIR}/engine/server/CaptchaVerifier.cpp
    ${CMAKE_SOURCE_DIR}/engine/server/BotDetector.cpp
    ShardRegistry.cpp
    ServerRegistry.cpp
    ShardRegisterHandler.cpp
    ShardTicketCrypto.cpp
    ShardTicketHandler.cpp
    ShardTicketValidator.cpp
    ServerListHandler.cpp
    HealthEndpoint.cpp
    PrometheusMetrics.cpp
  )
  target_include_directories(server_app PRIVATE ${MYSQL_INCLUDE_DIR} ${CMAKE_SOURCE_DIR})
  # M32.1: MySQL available on UNIX — FriendSystem compiles with full DB support.
  target_compile_definitions(server_app PRIVATE ENGINE_HAS_MYSQL=1)
  target_link_libraries(server_app PRIVATE pthread OpenSSL::SSL engine_auth spdlog::spdlog ${MYSQL_LIBRARY})

  # Phase 4 chat — SessionCharacterMap unit tests (pure in-memory, no DB).
  add_executable(session_character_map_tests
    SessionCharacterMapTests.cpp
    SessionCharacterMap.cpp
  )
  target_include_directories(session_character_map_tests PRIVATE ${CMAKE_SOURCE_DIR})
  target_link_libraries(session_character_map_tests PRIVATE pthread)
  target_compile_options(session_character_map_tests PRIVATE -Wall -Wextra -Wpedantic)
  add_test(NAME session_character_map_tests COMMAND session_character_map_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

  # M33.3: Anti-bot unit tests (UserRateLimiter, CaptchaVerifier bypass, BotDetector).
  add_executable(anti_bot_tests
    AntiBotTests.cpp
    ${CMAKE_SOURCE_DIR}/engine/server/UserRateLimiter.cpp
    ${CMAKE_SOURCE_DIR}/engine/server/CaptchaVerifier.cpp
    ${CMAKE_SOURCE_DIR}/engine/server/BotDetector.cpp
    ${CMAKE_SOURCE_DIR}/engine/core/Config.cpp
    ${CMAKE_SOURCE_DIR}/engine/core/Log.cpp
  )
  target_include_directories(anti_bot_tests PRIVATE ${CMAKE_SOURCE_DIR})
  target_link_libraries(anti_bot_tests PRIVATE OpenSSL::SSL spdlog::spdlog pthread)
  target_compile_options(anti_bot_tests PRIVATE -Wall -Wextra -Wpedantic)
  add_test(NAME anti_bot_tests COMMAND anti_bot_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

  # M21.6: DB layer smoke tests
  add_executable(db_layer_tests db/DbLayerTests.cpp db/ConnectionPool.cpp db/DbHelpers.cpp)
  target_include_directories(db_layer_tests PRIVATE ${CMAKE_SOURCE_DIR} ${MYSQL_INCLUDE_DIR})
  target_link_libraries(db_layer_tests PRIVATE engine_core ${MYSQL_LIBRARY} pthread)
  target_compile_options(db_layer_tests PRIVATE -Wall -Wextra -Wpedantic)
  add_test(NAME db_layer_tests COMMAND db_layer_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

  # CMANGOS.06 (Phase 1c) : AccountRoleService tests (no DB required)
  add_executable(account_role_service_tests
    AccountRoleServiceTests.cpp
    AccountRoleService.cpp
    AccountRole.cpp
    InMemoryAccountStore.cpp
    AccountValidation.cpp
    ${CMAKE_SOURCE_DIR}/engine/server/SecurityAuditLog.cpp
  )
  target_include_directories(account_role_service_tests PRIVATE ${CMAKE_SOURCE_DIR})
  target_link_libraries(account_role_service_tests PRIVATE engine_core engine_auth OpenSSL::SSL spdlog::spdlog pthread)
  target_compile_options(account_role_service_tests PRIVATE -Wall -Wextra -Wpedantic)
  add_test(NAME account_role_service_tests COMMAND account_role_service_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

  # CMANGOS.13 (Phase 1a) : SQLStorage<T> tests
  add_executable(sql_storage_tests db/SQLStorageTests.cpp db/ConnectionPool.cpp db/DbHelpers.cpp)
  target_include_directories(sql_storage_tests PRIVATE ${CMAKE_SOURCE_DIR} ${MYSQL_INCLUDE_DIR})
  target_link_libraries(sql_storage_tests PRIVATE engine_core ${MYSQL_LIBRARY} pthread)
  target_compile_options(sql_storage_tests PRIVATE -Wall -Wextra -Wpedantic)
  add_test(NAME sql_storage_tests COMMAND sql_storage_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

  # CMANGOS.13 (Phase 1a) : SqlPreparedStatement + cache tests
  add_executable(sql_prepared_statement_tests
    db/SqlPreparedStatementTests.cpp
    db/SqlPreparedStatement.cpp
    db/ConnectionPool.cpp
    db/DbHelpers.cpp
  )
  target_include_directories(sql_prepared_statement_tests PRIVATE ${CMAKE_SOURCE_DIR} ${MYSQL_INCLUDE_DIR})
  target_link_libraries(sql_prepared_statement_tests PRIVATE engine_core ${MYSQL_LIBRARY} pthread)
  target_compile_options(sql_prepared_statement_tests PRIVATE -Wall -Wextra -Wpedantic)
  add_test(NAME sql_prepared_statement_tests COMMAND sql_prepared_statement_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

  # CMANGOS.13 (Phase 1a) : SqlDelayThread async worker tests
  add_executable(sql_delay_thread_tests
    db/SqlDelayThreadTests.cpp
    db/SqlDelayThread.cpp
    db/ConnectionPool.cpp
    db/DbHelpers.cpp
  )
  target_include_directories(sql_delay_thread_tests PRIVATE ${CMAKE_SOURCE_DIR} ${MYSQL_INCLUDE_DIR})
  target_link_libraries(sql_delay_thread_tests PRIVATE engine_core ${MYSQL_LIBRARY} pthread)
  target_compile_options(sql_delay_thread_tests PRIVATE -Wall -Wextra -Wpedantic)
  add_test(NAME sql_delay_thread_tests COMMAND sql_delay_thread_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

  # CMANGOS.16 (Phase 1b) : ConditionMgr tests
  add_executable(condition_mgr_tests
    shard/globals/ConditionMgrTests.cpp
    shard/globals/ConditionMgr.cpp
    db/ConnectionPool.cpp
    db/DbHelpers.cpp
  )
  target_include_directories(condition_mgr_tests PRIVATE ${CMAKE_SOURCE_DIR} ${MYSQL_INCLUDE_DIR})
  target_link_libraries(condition_mgr_tests PRIVATE engine_core ${MYSQL_LIBRARY} pthread)
  target_compile_options(condition_mgr_tests PRIVATE -Wall -Wextra -Wpedantic)
  add_test(NAME condition_mgr_tests COMMAND condition_mgr_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

  # CMANGOS.16 (Phase 1b) : ObjectAccessor tests (no DB required)
  add_executable(object_accessor_tests
    shard/globals/ObjectAccessorTests.cpp
    shard/globals/ObjectAccessor.cpp
  )
  target_include_directories(object_accessor_tests PRIVATE ${CMAKE_SOURCE_DIR})
  target_link_libraries(object_accessor_tests PRIVATE engine_core pthread)
  target_compile_options(object_accessor_tests PRIVATE -Wall -Wextra -Wpedantic)
  add_test(NAME object_accessor_tests COMMAND object_accessor_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

  # CMANGOS.16 (Phase 1b) : GraveyardManager tests
  add_executable(graveyard_manager_tests
    shard/globals/GraveyardManagerTests.cpp
    shard/globals/GraveyardManager.cpp
    db/ConnectionPool.cpp
    db/DbHelpers.cpp
  )
  target_include_directories(graveyard_manager_tests PRIVATE ${CMAKE_SOURCE_DIR} ${MYSQL_INCLUDE_DIR})
  target_link_libraries(graveyard_manager_tests PRIVATE engine_core ${MYSQL_LIBRARY} pthread)
  target_compile_options(graveyard_manager_tests PRIVATE -Wall -Wextra -Wpedantic)
  add_test(NAME graveyard_manager_tests COMMAND graveyard_manager_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

  # CMANGOS.16 (Phase 1b) : LocaleStrings tests
  add_executable(locale_strings_tests
    shard/globals/LocaleStringsTests.cpp
    shard/globals/LocaleStrings.cpp
    db/ConnectionPool.cpp
    db/DbHelpers.cpp
  )
  target_include_directories(locale_strings_tests PRIVATE ${CMAKE_SOURCE_DIR} ${MYSQL_INCLUDE_DIR})
  target_link_libraries(locale_strings_tests PRIVATE engine_core ${MYSQL_LIBRARY} pthread)
  target_compile_options(locale_strings_tests PRIVATE -Wall -Wextra -Wpedantic)
  add_test(NAME locale_strings_tests COMMAND locale_strings_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

  # CMANGOS.01 (Phase 2.01a) : ChatSanitizer pure unit tests (no DB)
  add_executable(chat_sanitizer_tests
    chat/ChatSanitizerTests.cpp
    chat/ChatSanitizer.cpp
  )
  target_include_directories(chat_sanitizer_tests PRIVATE ${CMAKE_SOURCE_DIR})
  target_link_libraries(chat_sanitizer_tests PRIVATE engine_core spdlog::spdlog pthread)
  target_compile_options(chat_sanitizer_tests PRIVATE -Wall -Wextra -Wpedantic)
  add_test(NAME chat_sanitizer_tests COMMAND chat_sanitizer_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

  # CMANGOS.01 (Phase 2.01a) : ChatGate tests (mock callbacks ; pas de
  # DB ni d'AccountStore réel — ChatGate.cpp embarque les helpers SQL et
  # AccountStore-banned mais les tests injectent directement des
  # std::function. On link MySQL/DbHelpers à cause des helpers compilés.
  add_executable(chat_gate_tests
    chat/ChatGateTests.cpp
    chat/ChatGate.cpp
    db/ConnectionPool.cpp
    db/DbHelpers.cpp
  )
  target_include_directories(chat_gate_tests PRIVATE ${CMAKE_SOURCE_DIR} ${MYSQL_INCLUDE_DIR})
  target_link_libraries(chat_gate_tests PRIVATE engine_core spdlog::spdlog ${MYSQL_LIBRARY} pthread)
  target_compile_options(chat_gate_tests PRIVATE -Wall -Wextra -Wpedantic)
  add_test(NAME chat_gate_tests COMMAND chat_gate_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

  # CMANGOS.02 (Phase 2.02a) : ObjectGuid pure tests (no DB)
  add_executable(object_guid_tests
    shard/entities/ObjectGuidTests.cpp
    shard/entities/ObjectGuid.cpp
  )
  target_include_directories(object_guid_tests PRIVATE ${CMAKE_SOURCE_DIR})
  target_link_libraries(object_guid_tests PRIVATE engine_core spdlog::spdlog pthread)
  target_compile_options(object_guid_tests PRIVATE -Wall -Wextra -Wpedantic)
  add_test(NAME object_guid_tests COMMAND object_guid_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

  # CMANGOS.05 (Phase 2.05a) : BIH<T> + AABB pure math tests (no DB)
  add_executable(bih_tests
    shard/vmap/BIHTests.cpp
    shard/vmap/AABB.cpp
  )
  target_include_directories(bih_tests PRIVATE ${CMAKE_SOURCE_DIR})
  target_link_libraries(bih_tests PRIVATE engine_core spdlog::spdlog pthread)
  target_compile_options(bih_tests PRIVATE -Wall -Wextra -Wpedantic)
  add_test(NAME bih_tests COMMAND bih_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

  # CMANGOS.03 (Phase 2.03a) : GridStateTracker pure tests (no DB)
  add_executable(grid_state_tests
    GridStateTests.cpp
    GridState.cpp
    SpatialPartition.cpp
  )
  target_include_directories(grid_state_tests PRIVATE ${CMAKE_SOURCE_DIR})
  target_link_libraries(grid_state_tests PRIVATE engine_core spdlog::spdlog pthread)
  target_compile_options(grid_state_tests PRIVATE -Wall -Wextra -Wpedantic)
  add_test(NAME grid_state_tests COMMAND grid_state_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

  # CMANGOS.05 (Phase 2.05b) : VMapFormat encode/decode tests (no DB, no I/O)
  add_executable(vmap_format_tests
    shard/vmap/VMapFormatTests.cpp
    shard/vmap/VMapFormat.cpp
  )
  target_include_directories(vmap_format_tests PRIVATE ${CMAKE_SOURCE_DIR})
  target_link_libraries(vmap_format_tests PRIVATE engine_core spdlog::spdlog pthread)
  target_compile_options(vmap_format_tests PRIVATE -Wall -Wextra -Wpedantic)
  add_test(NAME vmap_format_tests COMMAND vmap_format_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

  # CMANGOS.04 (Phase 2.04a) : Spline<T> + MovementTypedefs (header-only).
  lcdlln_add_simple_test(spline_tests
    shard/movement/SplineTests.cpp)

  # CMANGOS.05 (Phase 2.05c) : VMapManager LOS + GetHeight tests.
  lcdlln_add_simple_test(vmap_manager_tests
    shard/vmap/VMapManagerTests.cpp
    shard/vmap/VMapManager.cpp
    shard/vmap/VMapFormat.cpp
    shard/vmap/AABB.cpp)

  # CMANGOS.04 (Phase 2.04b) : MoveSpline runtime + MoveSplineInit builder.
  lcdlln_add_simple_test(move_spline_tests
    shard/movement/MoveSplineTests.cpp
    shard/movement/MoveSpline.cpp)

  # CMANGOS.01 (Phase 2.01b) : ChatCommandRouter pure tests
  add_executable(chat_command_router_tests
    chat/ChatCommandRouterTests.cpp
    chat/ChatCommandRouter.cpp
    AccountRoleService.cpp
    AccountRole.cpp
    InMemoryAccountStore.cpp
    AccountValidation.cpp
    ${CMAKE_SOURCE_DIR}/engine/server/SecurityAuditLog.cpp
  )
  target_include_directories(chat_command_router_tests PRIVATE ${CMAKE_SOURCE_DIR})
  target_link_libraries(chat_command_router_tests PRIVATE engine_core engine_auth OpenSSL::SSL spdlog::spdlog pthread)
  target_compile_options(chat_command_router_tests PRIVATE -Wall -Wextra -Wpedantic)
  add_test(NAME chat_command_router_tests COMMAND chat_command_router_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

  # CMANGOS.01 (Phase 2.01b) : ChatChannelRegistry pure tests
  add_executable(chat_channel_registry_tests
    chat/ChatChannelRegistryTests.cpp
    chat/ChatChannelRegistry.cpp
  )
  target_include_directories(chat_channel_registry_tests PRIVATE ${CMAKE_SOURCE_DIR})
  target_link_libraries(chat_channel_registry_tests PRIVATE engine_core spdlog::spdlog pthread)
  target_compile_options(chat_channel_registry_tests PRIVATE -Wall -Wextra -Wpedantic)
  add_test(NAME chat_channel_registry_tests COMMAND chat_channel_registry_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

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

  # M22.6: Shard server (ticket handshake + enregistrement TLS/TCP vers le Master via ShardToMasterClient)
  add_executable(shard_app
    main_shard_linux.cpp
    NetServer.cpp
    ShardTicketValidator.cpp
    ShardTicketHandshakeHandler.cpp
    ShardTicketCrypto.cpp
    HealthEndpoint.cpp
    PrometheusMetrics.cpp
    ${CMAKE_SOURCE_DIR}/engine/network/ShardToMasterClient.cpp
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

# Windows: engine_auth pulls OpenSSL DLLs — copy next to server exe (same as engine_app).
if(WIN32 AND DEFINED LCDLLN_OPENSSL_DLL_DIR AND NOT LCDLLN_OPENSSL_DLL_DIR STREQUAL "")
  add_custom_command(TARGET server_app POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${LCDLLN_OPENSSL_DLL_DIR}/libssl-3-x64.dll"
            "$<TARGET_FILE_DIR:server_app>/"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${LCDLLN_OPENSSL_DLL_DIR}/libcrypto-3-x64.dll"
            "$<TARGET_FILE_DIR:server_app>/"
    COMMENT "Copie des DLL OpenSSL à côté de lcdlln_server.exe..."
  )
endif()
