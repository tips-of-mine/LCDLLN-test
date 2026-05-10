# M15.4 — server_app : binaire serveur headless (pas de Vulkan, pas de GLFW)
# WIN32: UDP server (UdpTransport). UNIX: TCP server (NetServer epoll, M19.4).
#
# Fichier restauré post-réorg cmangos-style (Phase B). Tous les paths référencent
# désormais la nouvelle arborescence src/{shared,client,masterd,shardd,world_editor}/
# au lieu de l'ancienne engine/{core,net,network,server,platform}/.
cmake_minimum_required(VERSION 3.20)
set(CMAKE_CXX_STANDARD 20)

# Le helper lcdlln_add_simple_test() vit dans cmake/LCDLLNHelpers.cmake
# (parite avec la structure cmangos-tbc/cmake/). Il est charge depuis le
# CMakeLists.txt racine via :
#   list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
#   include(LCDLLNHelpers)
# Le helper est donc disponible ici sans redefinition.

if(WIN32)
  add_executable(server_app
    ${CMAKE_SOURCE_DIR}/src/shardd/main_win.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/server_bootstrap/ServerApp.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/gameplay/chat/ChatCommandParser.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/network/ServerProtocol.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/gameplay/social/FriendSystem.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/gameplay/social/PartySystem.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/gameplay/guild/GuildSystem.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/gameplay/guild/GuildTabard.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/gameplay/guild/GuildBank.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/net/ChatSystem.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/net/ChatEmotes.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/gameplay/character/CharacterPersistence.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/gameplay/economy/CurrencyConfig.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/gameplay/economy/VendorCatalog.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/gameplay/economy/PlayerWalletService.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/gameplay/trade/TradeSystem.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/gameplay/gathering/GatheringSystem.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/gameplay/crafting/CraftingSystem.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/gameplay/auction/AuctionHouse.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/gameplay/quest/QuestRuntime.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/gameplay/spawner/SpawnerRuntime.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/gameplay/event/EventRuntime.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/world/UdpTransport.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/world/TickScheduler.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/world/SpatialPartition.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/world/GridState.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/world/ZoneTransitions.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/world/LagCompensation.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/core/Config.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/core/Log.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/core/LogConfig.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/platform/FileSystem.cpp
    ${CMAKE_SOURCE_DIR}/src/masterd/session/SessionManager.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/security/RateLimitAndBan.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/security/SecurityAuditLog.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/security/UserRateLimiter.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/security/CaptchaVerifier.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/security/BotDetector.cpp
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
    ${CMAKE_SOURCE_DIR}/src/masterd/main_linux.cpp
    ${CMAKE_SOURCE_DIR}/src/masterd/migrations/MigrationRunner.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/db/ConnectionPool.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/db/DbHelpers.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/network/NetServer.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/world/LagCompensation.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/core/Config.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/core/Log.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/core/LogConfig.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/platform/FileSystem.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/net/ChatSystem.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/net/ChatEmotes.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/network/PacketView.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/network/ByteReader.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/network/ByteWriter.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/network/PacketBuilder.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/network/ErrorPacket.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/network/AuthRegisterPayloads.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/network/CharacterPayloads.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/network/ShardPayloads.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/network/ShardTicketPayloads.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/network/ServerListPayloads.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/network/TermsPayloads.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/gameplay/social/FriendSystem.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/gameplay/social/PartySystem.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/gameplay/guild/GuildSystem.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/gameplay/guild/GuildTabard.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/gameplay/guild/GuildBank.cpp
    ${CMAKE_SOURCE_DIR}/src/masterd/account/AccountValidation.cpp
    ${CMAKE_SOURCE_DIR}/src/masterd/account/InMemoryAccountStore.cpp
    ${CMAKE_SOURCE_DIR}/src/masterd/account/MysqlAccountStore.cpp
    ${CMAKE_SOURCE_DIR}/src/masterd/account/AccountRole.cpp
    ${CMAKE_SOURCE_DIR}/src/masterd/account/AccountRoleService.cpp
    ${CMAKE_SOURCE_DIR}/src/masterd/handlers/auth/AuthRegisterHandler.cpp
    ${CMAKE_SOURCE_DIR}/src/masterd/session/ConnectionSessionMap.cpp
    ${CMAKE_SOURCE_DIR}/src/masterd/email/SmtpMailer.cpp
    ${CMAKE_SOURCE_DIR}/src/masterd/email/LocalizedEmail.cpp
    ${CMAKE_SOURCE_DIR}/src/masterd/handlers/terms/TermsRepository.cpp
    ${CMAKE_SOURCE_DIR}/src/masterd/handlers/terms/TermsHandler.cpp
    ${CMAKE_SOURCE_DIR}/src/masterd/handlers/character/CharacterCreateHandler.cpp
    ${CMAKE_SOURCE_DIR}/src/masterd/handlers/character/CharacterListHandler.cpp
    ${CMAKE_SOURCE_DIR}/src/masterd/handlers/character/CharacterDeleteHandler.cpp
    ${CMAKE_SOURCE_DIR}/src/masterd/handlers/character/CharacterSavePositionHandler.cpp
    ${CMAKE_SOURCE_DIR}/src/masterd/handlers/character/CharacterEnterWorldHandler.cpp
    ${CMAKE_SOURCE_DIR}/src/masterd/session/SessionCharacterMap.cpp
    ${CMAKE_SOURCE_DIR}/src/masterd/handlers/chat/ChatRelayHandler.cpp
    ${CMAKE_SOURCE_DIR}/src/masterd/handlers/mail/MailHandler.cpp
    ${CMAKE_SOURCE_DIR}/src/masterd/handlers/quest/QuestHandler.cpp
    ${CMAKE_SOURCE_DIR}/src/masterd/handlers/social/IgnoreListHandler.cpp
    ${CMAKE_SOURCE_DIR}/src/masterd/handlers/gmtickets/GmTicketHandler.cpp
    ${CMAKE_SOURCE_DIR}/src/masterd/handlers/trade/TradeHandler.cpp
    ${CMAKE_SOURCE_DIR}/src/masterd/handlers/reputation/ReputationHandler.cpp
    ${CMAKE_SOURCE_DIR}/src/masterd/handlers/lfg/LfgHandler.cpp
    ${CMAKE_SOURCE_DIR}/src/masterd/handlers/cinematics/CinematicHandler.cpp
    ${CMAKE_SOURCE_DIR}/src/masterd/handlers/skills/SkillHandler.cpp
    ${CMAKE_SOURCE_DIR}/src/masterd/handlers/arena/ArenaHandler.cpp
    ${CMAKE_SOURCE_DIR}/src/masterd/handlers/battleground/BattleGroundHandler.cpp
    ${CMAKE_SOURCE_DIR}/src/masterd/trade/TradeSessionRegistry.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/network/IgnoreListPayloads.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/network/MailPayloads.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/network/QuestPayloads.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/network/GmTicketPayloads.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/network/TradePayloads.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/network/ReputationPayloads.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/network/LfgPayloads.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/network/CinematicPayloads.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/network/SkillPayloads.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/network/ArenaPayloads.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/network/BattleGroundPayloads.cpp
    ${CMAKE_SOURCE_DIR}/src/masterd/chat/ChatSanitizer.cpp
    ${CMAKE_SOURCE_DIR}/src/masterd/chat/ChatGate.cpp
    ${CMAKE_SOURCE_DIR}/src/masterd/chat/ChatCommandRouter.cpp
    ${CMAKE_SOURCE_DIR}/src/masterd/chat/ChatChannelRegistry.cpp
    ${CMAKE_SOURCE_DIR}/src/masterd/mail/MailManager.cpp
    ${CMAKE_SOURCE_DIR}/src/masterd/mail/MysqlMailStore.cpp
    ${CMAKE_SOURCE_DIR}/src/masterd/gmtickets/MysqlGmTicketStore.cpp
    ${CMAKE_SOURCE_DIR}/src/masterd/reputation/MysqlReputationStore.cpp
    ${CMAKE_SOURCE_DIR}/src/masterd/quests/MysqlQuestStateStore.cpp
    ${CMAKE_SOURCE_DIR}/src/masterd/social/MysqlIgnoreStore.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/network/ChatPayloads.cpp
    ${CMAKE_SOURCE_DIR}/src/masterd/handlers/password/PasswordResetStore.cpp
    ${CMAKE_SOURCE_DIR}/src/masterd/handlers/password/PasswordResetHandler.cpp
    ${CMAKE_SOURCE_DIR}/src/masterd/session/SessionManager.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/security/RateLimitAndBan.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/security/SecurityAuditLog.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/security/UserRateLimiter.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/security/CaptchaVerifier.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/security/BotDetector.cpp
    ${CMAKE_SOURCE_DIR}/src/masterd/shards/ShardRegistry.cpp
    ${CMAKE_SOURCE_DIR}/src/masterd/shards/ServerRegistry.cpp
    ${CMAKE_SOURCE_DIR}/src/masterd/handlers/shard/ShardRegisterHandler.cpp
    ${CMAKE_SOURCE_DIR}/src/masterd/handlers/shard/ShardTicketCrypto.cpp
    ${CMAKE_SOURCE_DIR}/src/masterd/handlers/shard/ShardTicketHandler.cpp
    ${CMAKE_SOURCE_DIR}/src/masterd/handlers/shard/ShardTicketValidator.cpp
    ${CMAKE_SOURCE_DIR}/src/masterd/handlers/shard/ServerListHandler.cpp
    ${CMAKE_SOURCE_DIR}/src/masterd/metrics/HealthEndpoint.cpp
    ${CMAKE_SOURCE_DIR}/src/masterd/metrics/PrometheusMetrics.cpp
  )
  target_include_directories(server_app PRIVATE ${MYSQL_INCLUDE_DIR} ${CMAKE_SOURCE_DIR})
  # M32.1: MySQL available on UNIX — FriendSystem compiles with full DB support.
  target_compile_definitions(server_app PRIVATE ENGINE_HAS_MYSQL=1)
  target_link_libraries(server_app PRIVATE pthread OpenSSL::SSL engine_auth spdlog::spdlog ${MYSQL_LIBRARY})

  # Phase 4 chat — SessionCharacterMap unit tests (pure in-memory, no DB).
  add_executable(session_character_map_tests
    ${CMAKE_SOURCE_DIR}/src/masterd/session/SessionCharacterMapTests.cpp
    ${CMAKE_SOURCE_DIR}/src/masterd/session/SessionCharacterMap.cpp
  )
  target_include_directories(session_character_map_tests PRIVATE ${CMAKE_SOURCE_DIR})
  target_link_libraries(session_character_map_tests PRIVATE pthread)
  target_compile_options(session_character_map_tests PRIVATE -Wall -Wextra -Wpedantic)
  add_test(NAME session_character_map_tests COMMAND session_character_map_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

  # M33.3: Anti-bot unit tests (UserRateLimiter, CaptchaVerifier bypass, BotDetector).
  add_executable(anti_bot_tests
    ${CMAKE_SOURCE_DIR}/src/shared/security/AntiBotTests.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/security/UserRateLimiter.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/security/CaptchaVerifier.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/security/BotDetector.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/core/Config.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/core/Log.cpp
  )
  target_include_directories(anti_bot_tests PRIVATE ${CMAKE_SOURCE_DIR})
  target_link_libraries(anti_bot_tests PRIVATE OpenSSL::SSL spdlog::spdlog pthread)
  target_compile_options(anti_bot_tests PRIVATE -Wall -Wextra -Wpedantic)
  add_test(NAME anti_bot_tests COMMAND anti_bot_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

  # M21.6: DB layer smoke tests
  add_executable(db_layer_tests
    ${CMAKE_SOURCE_DIR}/src/shared/db/DbLayerTests.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/db/ConnectionPool.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/db/DbHelpers.cpp
  )
  target_include_directories(db_layer_tests PRIVATE ${CMAKE_SOURCE_DIR} ${MYSQL_INCLUDE_DIR})
  target_link_libraries(db_layer_tests PRIVATE engine_core ${MYSQL_LIBRARY} pthread)
  target_compile_options(db_layer_tests PRIVATE -Wall -Wextra -Wpedantic)
  add_test(NAME db_layer_tests COMMAND db_layer_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

  # CMANGOS.06 (Phase 1c) : AccountRoleService tests (no DB required)
  add_executable(account_role_service_tests
    ${CMAKE_SOURCE_DIR}/src/masterd/account/AccountRoleServiceTests.cpp
    ${CMAKE_SOURCE_DIR}/src/masterd/account/AccountRoleService.cpp
    ${CMAKE_SOURCE_DIR}/src/masterd/account/AccountRole.cpp
    ${CMAKE_SOURCE_DIR}/src/masterd/account/InMemoryAccountStore.cpp
    ${CMAKE_SOURCE_DIR}/src/masterd/account/AccountValidation.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/security/SecurityAuditLog.cpp
  )
  target_include_directories(account_role_service_tests PRIVATE ${CMAKE_SOURCE_DIR})
  target_link_libraries(account_role_service_tests PRIVATE engine_core engine_auth OpenSSL::SSL spdlog::spdlog pthread)
  target_compile_options(account_role_service_tests PRIVATE -Wall -Wextra -Wpedantic)
  add_test(NAME account_role_service_tests COMMAND account_role_service_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

  # CMANGOS.13 (Phase 1a) : SQLStorage<T> tests
  add_executable(sql_storage_tests
    ${CMAKE_SOURCE_DIR}/src/shared/db/SQLStorageTests.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/db/ConnectionPool.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/db/DbHelpers.cpp
  )
  target_include_directories(sql_storage_tests PRIVATE ${CMAKE_SOURCE_DIR} ${MYSQL_INCLUDE_DIR})
  target_link_libraries(sql_storage_tests PRIVATE engine_core ${MYSQL_LIBRARY} pthread)
  target_compile_options(sql_storage_tests PRIVATE -Wall -Wextra -Wpedantic)
  add_test(NAME sql_storage_tests COMMAND sql_storage_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

  # CMANGOS.13 (Phase 1a) : SqlPreparedStatement + cache tests
  add_executable(sql_prepared_statement_tests
    ${CMAKE_SOURCE_DIR}/src/shared/db/SqlPreparedStatementTests.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/db/SqlPreparedStatement.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/db/ConnectionPool.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/db/DbHelpers.cpp
  )
  target_include_directories(sql_prepared_statement_tests PRIVATE ${CMAKE_SOURCE_DIR} ${MYSQL_INCLUDE_DIR})
  target_link_libraries(sql_prepared_statement_tests PRIVATE engine_core ${MYSQL_LIBRARY} pthread)
  target_compile_options(sql_prepared_statement_tests PRIVATE -Wall -Wextra -Wpedantic)
  add_test(NAME sql_prepared_statement_tests COMMAND sql_prepared_statement_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

  # CMANGOS.13 (Phase 1a) : SqlDelayThread async worker tests
  add_executable(sql_delay_thread_tests
    ${CMAKE_SOURCE_DIR}/src/shared/db/SqlDelayThreadTests.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/db/SqlDelayThread.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/db/ConnectionPool.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/db/DbHelpers.cpp
  )
  target_include_directories(sql_delay_thread_tests PRIVATE ${CMAKE_SOURCE_DIR} ${MYSQL_INCLUDE_DIR})
  target_link_libraries(sql_delay_thread_tests PRIVATE engine_core ${MYSQL_LIBRARY} pthread)
  target_compile_options(sql_delay_thread_tests PRIVATE -Wall -Wextra -Wpedantic)
  add_test(NAME sql_delay_thread_tests COMMAND sql_delay_thread_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

  # CMANGOS.16 (Phase 1b) : ConditionMgr tests
  add_executable(condition_mgr_tests
    ${CMAKE_SOURCE_DIR}/src/shardd/internals/globals/ConditionMgrTests.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/internals/globals/ConditionMgr.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/db/ConnectionPool.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/db/DbHelpers.cpp
  )
  target_include_directories(condition_mgr_tests PRIVATE ${CMAKE_SOURCE_DIR} ${MYSQL_INCLUDE_DIR})
  target_link_libraries(condition_mgr_tests PRIVATE engine_core ${MYSQL_LIBRARY} pthread)
  target_compile_options(condition_mgr_tests PRIVATE -Wall -Wextra -Wpedantic)
  add_test(NAME condition_mgr_tests COMMAND condition_mgr_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

  # CMANGOS.16 (Phase 1b) : ObjectAccessor tests (no DB required)
  add_executable(object_accessor_tests
    ${CMAKE_SOURCE_DIR}/src/shardd/internals/globals/ObjectAccessorTests.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/internals/globals/ObjectAccessor.cpp
  )
  target_include_directories(object_accessor_tests PRIVATE ${CMAKE_SOURCE_DIR})
  target_link_libraries(object_accessor_tests PRIVATE engine_core pthread)
  target_compile_options(object_accessor_tests PRIVATE -Wall -Wextra -Wpedantic)
  add_test(NAME object_accessor_tests COMMAND object_accessor_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

  # CMANGOS.16 (Phase 1b) : GraveyardManager tests
  add_executable(graveyard_manager_tests
    ${CMAKE_SOURCE_DIR}/src/shardd/internals/globals/GraveyardManagerTests.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/internals/globals/GraveyardManager.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/db/ConnectionPool.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/db/DbHelpers.cpp
  )
  target_include_directories(graveyard_manager_tests PRIVATE ${CMAKE_SOURCE_DIR} ${MYSQL_INCLUDE_DIR})
  target_link_libraries(graveyard_manager_tests PRIVATE engine_core ${MYSQL_LIBRARY} pthread)
  target_compile_options(graveyard_manager_tests PRIVATE -Wall -Wextra -Wpedantic)
  add_test(NAME graveyard_manager_tests COMMAND graveyard_manager_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

  # CMANGOS.16 (Phase 1b) : LocaleStrings tests
  add_executable(locale_strings_tests
    ${CMAKE_SOURCE_DIR}/src/shardd/internals/globals/LocaleStringsTests.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/internals/globals/LocaleStrings.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/db/ConnectionPool.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/db/DbHelpers.cpp
  )
  target_include_directories(locale_strings_tests PRIVATE ${CMAKE_SOURCE_DIR} ${MYSQL_INCLUDE_DIR})
  target_link_libraries(locale_strings_tests PRIVATE engine_core ${MYSQL_LIBRARY} pthread)
  target_compile_options(locale_strings_tests PRIVATE -Wall -Wextra -Wpedantic)
  add_test(NAME locale_strings_tests COMMAND locale_strings_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

  # CMANGOS.01 (Phase 2.01a) : ChatSanitizer pure unit tests (no DB)
  add_executable(chat_sanitizer_tests
    ${CMAKE_SOURCE_DIR}/src/masterd/chat/ChatSanitizerTests.cpp
    ${CMAKE_SOURCE_DIR}/src/masterd/chat/ChatSanitizer.cpp
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
    ${CMAKE_SOURCE_DIR}/src/masterd/chat/ChatGateTests.cpp
    ${CMAKE_SOURCE_DIR}/src/masterd/chat/ChatGate.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/db/ConnectionPool.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/db/DbHelpers.cpp
  )
  target_include_directories(chat_gate_tests PRIVATE ${CMAKE_SOURCE_DIR} ${MYSQL_INCLUDE_DIR})
  target_link_libraries(chat_gate_tests PRIVATE engine_core spdlog::spdlog ${MYSQL_LIBRARY} pthread)
  target_compile_options(chat_gate_tests PRIVATE -Wall -Wextra -Wpedantic)
  add_test(NAME chat_gate_tests COMMAND chat_gate_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

  # CMANGOS.02 (Phase 2.02a) : ObjectGuid pure tests (no DB)
  add_executable(object_guid_tests
    ${CMAKE_SOURCE_DIR}/src/shardd/internals/entities/ObjectGuidTests.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/internals/entities/ObjectGuid.cpp
  )
  target_include_directories(object_guid_tests PRIVATE ${CMAKE_SOURCE_DIR})
  target_link_libraries(object_guid_tests PRIVATE engine_core spdlog::spdlog pthread)
  target_compile_options(object_guid_tests PRIVATE -Wall -Wextra -Wpedantic)
  add_test(NAME object_guid_tests COMMAND object_guid_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

  # CMANGOS.18 (Phase 3.18a) : MailManager core (workflow Send/Take/COD/expire).
  lcdlln_add_simple_test(mail_manager_tests
    ${CMAKE_SOURCE_DIR}/src/masterd/mail/MailManagerTests.cpp
    ${CMAKE_SOURCE_DIR}/src/masterd/mail/MailManager.cpp)

  # CMANGOS.29 (Phase 5.29a) : AntiCheatGameplay (speed + teleport detection).
  lcdlln_add_simple_test(anticheat_gameplay_tests
    ${CMAKE_SOURCE_DIR}/src/shardd/anticheat/AntiCheatGameplayTests.cpp)
  # CMANGOS.32 (Phase 5.32a) : GmTicketSystem (header-only).
  lcdlln_add_simple_test(gm_ticket_system_tests
    ${CMAKE_SOURCE_DIR}/src/masterd/gmtickets/GmTicketSystemTests.cpp)

  # CMANGOS.23 (Phase 3.23a) : QuestStateTracker (header-only state machine).
  lcdlln_add_simple_test(quest_state_tests
    ${CMAKE_SOURCE_DIR}/src/masterd/quests/QuestStateTests.cpp)
  # CMANGOS.33 (Phase 5.33a) : LfgQueue header-only.
  lcdlln_add_simple_test(lfg_queue_tests
    ${CMAKE_SOURCE_DIR}/src/masterd/lfg/LfgQueueTests.cpp)

  # CMANGOS.28 (Phase 3.28a) : WorldStateRegistry (header-only).
  lcdlln_add_simple_test(world_state_registry_tests
    ${CMAKE_SOURCE_DIR}/src/masterd/world/WorldStateRegistryTests.cpp)

  # CMANGOS.20 (Phase 3.20a) : MotionGeneratorStack (header-only).
  lcdlln_add_simple_test(motion_generator_stack_tests
    ${CMAKE_SOURCE_DIR}/src/shardd/ai/MotionGeneratorStackTests.cpp)
  # CMANGOS.42 (Phase 4.42a) : WeatherManager (header-only).
  lcdlln_add_simple_test(weather_manager_tests
    ${CMAKE_SOURCE_DIR}/src/shardd/weather/WeatherManagerTests.cpp)
  # CMANGOS.31 (Phase 5.31a) : GameEventManager (header-only, recurring events).
  lcdlln_add_simple_test(game_event_manager_tests
    ${CMAKE_SOURCE_DIR}/src/masterd/events/GameEventManagerTests.cpp)

  # CMANGOS.30 (Phase 5.30a) : CinematicSequence (header-only, camera keyframe interp).
  lcdlln_add_simple_test(cinematic_sequence_tests
    ${CMAKE_SOURCE_DIR}/src/shardd/cinematics/CinematicSequenceTests.cpp)

  # CMANGOS.19 (Phase 3.19a) : InstanceManager (header-only).
  lcdlln_add_simple_test(instance_manager_tests
    ${CMAKE_SOURCE_DIR}/src/shardd/maps/InstanceManagerTests.cpp)

  # CMANGOS.36 (Phase 5.36a) : OutdoorPvPManager (header-only, zones contestees).
  lcdlln_add_simple_test(outdoor_pvp_manager_tests
    ${CMAKE_SOURCE_DIR}/src/shardd/outdoorpvp/OutdoorPvPManagerTests.cpp)

  # CMANGOS.22 (Phase 4.22a) : PoolManager weighted spawn (header-only).
  lcdlln_add_simple_test(pool_manager_tests
    ${CMAKE_SOURCE_DIR}/src/shardd/pools/PoolManagerTests.cpp)

  # CMANGOS.08 (Phase 5.08a) : ArenaTeam + ELO (header-only).
  lcdlln_add_simple_test(arena_team_tests
    ${CMAKE_SOURCE_DIR}/src/shardd/arena/ArenaTeamTests.cpp)

  # CMANGOS.38 (Phase 5.38a) : PlayerBotProfile (header-only).
  lcdlln_add_simple_test(player_bot_profile_tests
    ${CMAKE_SOURCE_DIR}/src/shardd/playerbot/PlayerBotProfileTests.cpp)

  # CMANGOS.41 (Phase 5.41a) : ServerUtil (header-only).
  lcdlln_add_simple_test(server_util_tests
    ${CMAKE_SOURCE_DIR}/src/shared/util/ServerUtilTests.cpp)

  # CMANGOS.40 (Phase 4.40a) : Formulas (header-only).
  lcdlln_add_simple_test(formulas_tests
    ${CMAKE_SOURCE_DIR}/src/shared/formulas/FormulasTests.cpp)

  # CMANGOS.34 (Phase 4.34a) : MetricRegistry (header-only).
  lcdlln_add_simple_test(metric_registry_tests
    ${CMAKE_SOURCE_DIR}/src/shared/metric/MetricRegistryTests.cpp)

  # CMANGOS.37 (Phase 4.37a) : CrashContext (header-only).
  lcdlln_add_simple_test(crash_context_tests
    ${CMAKE_SOURCE_DIR}/src/shared/platform/CrashContextTests.cpp)

  # CMANGOS.35 (Phase 4.35a) : Messager MPSC (header-only).
  lcdlln_add_simple_test(messager_tests
    ${CMAKE_SOURCE_DIR}/src/shared/messager/MessagerTests.cpp)

  # CMANGOS.10 (Phase 4.10a) : BattleGroundQueue (header-only).
  lcdlln_add_simple_test(battle_ground_queue_tests
    ${CMAKE_SOURCE_DIR}/src/shardd/battleground/BattleGroundQueueTests.cpp)

  # CMANGOS.27 (Phase 4.27a) : TradeSession FSM (header-only).
  lcdlln_add_simple_test(trade_session_tests
    ${CMAKE_SOURCE_DIR}/src/shardd/trade/TradeSessionTests.cpp)

  # CMANGOS.39 (Phase 4.39a) : SkillBook (header-only).
  lcdlln_add_simple_test(skill_book_tests
    ${CMAKE_SOURCE_DIR}/src/shardd/skills/SkillBookTests.cpp)

  # CMANGOS.12 (Phase 4.12a) : PacketLog ring buffer (header-only).
  lcdlln_add_simple_test(packet_log_tests
    ${CMAKE_SOURCE_DIR}/src/shared/packetlog/PacketLogTests.cpp)

  # CMANGOS.28 (Phase 3.28a) : WorldClock (header-only).
  lcdlln_add_simple_test(world_clock_tests
    ${CMAKE_SOURCE_DIR}/src/masterd/world/WorldClockTests.cpp)

  # CMANGOS.21 (Phase 3.21a) : GuildPermissionMatrix (header-only).
  lcdlln_add_simple_test(guild_permission_matrix_tests
    ${CMAKE_SOURCE_DIR}/src/shardd/guild/GuildPermissionMatrixTests.cpp)

  # CMANGOS.44 (Phase 5.44a) : AuctionHouseBot (header-only, low-pop AH filler).
  lcdlln_add_simple_test(auction_house_bot_tests
    ${CMAKE_SOURCE_DIR}/src/shardd/auction/AuctionHouseBotTests.cpp)

  # CMANGOS.25 (Phase 3.25a) : IgnoreListManager (header-only).
  lcdlln_add_simple_test(ignore_list_tests
    ${CMAKE_SOURCE_DIR}/src/masterd/social/IgnoreListTests.cpp)

  # CMANGOS.17 (Phase 3.17a) : LootTable + roll deterministe (seed-based).
  lcdlln_add_simple_test(loot_table_tests
    ${CMAKE_SOURCE_DIR}/src/shardd/loot/LootTableTests.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/loot/LootTable.cpp)

  # CMANGOS.11 (Phase 3.11a) : ThreatList AI target selector (header-only).
  lcdlln_add_simple_test(threat_list_tests
    ${CMAKE_SOURCE_DIR}/src/shardd/combat/ThreatListTests.cpp)
  # CMANGOS.24 (Phase 3.24a) : ReputationManager + spillover (header-only).
  lcdlln_add_simple_test(reputation_manager_tests
    ${CMAKE_SOURCE_DIR}/src/masterd/reputation/ReputationManagerTests.cpp)
  # CMANGOS.07 (Phase 3.07a) : EventAI data-driven evaluator.
  lcdlln_add_simple_test(event_ai_tests
    ${CMAKE_SOURCE_DIR}/src/shardd/ai/EventAITests.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/ai/EventAI.cpp)
  # CMANGOS.26 (Phase 3.26a) : SpellFamilyMask 128-bit + ProcEvent matching.
  lcdlln_add_simple_test(spell_family_mask_tests
    ${CMAKE_SOURCE_DIR}/src/shardd/spell/SpellFamilyMaskTests.cpp)
  # CMANGOS.14 (Phase 3.14a) : DBScripts VM step-by-step.
  lcdlln_add_simple_test(dbscript_tests
    ${CMAKE_SOURCE_DIR}/src/shardd/dbscripts/DBScriptTests.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/dbscripts/DBScript.cpp)

  # CMANGOS.05 (Phase 2.05a) : BIH<T> + AABB pure math tests (no DB)
  add_executable(bih_tests
    ${CMAKE_SOURCE_DIR}/src/shardd/internals/vmap/BIHTests.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/internals/vmap/AABB.cpp
  )
  target_include_directories(bih_tests PRIVATE ${CMAKE_SOURCE_DIR})
  target_link_libraries(bih_tests PRIVATE engine_core spdlog::spdlog pthread)
  target_compile_options(bih_tests PRIVATE -Wall -Wextra -Wpedantic)
  add_test(NAME bih_tests COMMAND bih_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

  # CMANGOS.03 (Phase 2.03a) : GridStateTracker pure tests (no DB)
  add_executable(grid_state_tests
    ${CMAKE_SOURCE_DIR}/src/shardd/world/GridStateTests.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/world/GridState.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/world/SpatialPartition.cpp
  )
  target_include_directories(grid_state_tests PRIVATE ${CMAKE_SOURCE_DIR})
  target_link_libraries(grid_state_tests PRIVATE engine_core spdlog::spdlog pthread)
  target_compile_options(grid_state_tests PRIVATE -Wall -Wextra -Wpedantic)
  add_test(NAME grid_state_tests COMMAND grid_state_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

  # CMANGOS.05 (Phase 2.05b) : VMapFormat encode/decode tests (no DB, no I/O)
  add_executable(vmap_format_tests
    ${CMAKE_SOURCE_DIR}/src/shardd/internals/vmap/VMapFormatTests.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/internals/vmap/VMapFormat.cpp
  )
  target_include_directories(vmap_format_tests PRIVATE ${CMAKE_SOURCE_DIR})
  target_link_libraries(vmap_format_tests PRIVATE engine_core spdlog::spdlog pthread)
  target_compile_options(vmap_format_tests PRIVATE -Wall -Wextra -Wpedantic)
  add_test(NAME vmap_format_tests COMMAND vmap_format_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

  # CMANGOS.04 (Phase 2.04a) : Spline<T> + MovementTypedefs (header-only).
  lcdlln_add_simple_test(spline_tests
    ${CMAKE_SOURCE_DIR}/src/shardd/internals/movement/SplineTests.cpp)

  # CMANGOS.05 (Phase 2.05c) : VMapManager LOS + GetHeight tests.
  lcdlln_add_simple_test(vmap_manager_tests
    ${CMAKE_SOURCE_DIR}/src/shardd/internals/vmap/VMapManagerTests.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/internals/vmap/VMapManager.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/internals/vmap/VMapFormat.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/internals/vmap/AABB.cpp)

  # CMANGOS.04 (Phase 2.04b) : MoveSpline runtime + MoveSplineInit builder.
  lcdlln_add_simple_test(move_spline_tests
    ${CMAKE_SOURCE_DIR}/src/shardd/internals/movement/MoveSplineTests.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/internals/movement/MoveSpline.cpp)

  # CMANGOS.05 (Phase 2.05d) : VMapStreamer + ManagedModel (refcount + delayed unload).
  lcdlln_add_simple_test(vmap_streamer_tests
    ${CMAKE_SOURCE_DIR}/src/shardd/internals/vmap/VMapStreamerTests.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/internals/vmap/VMapStreamer.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/internals/vmap/VMapManager.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/internals/vmap/VMapFormat.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/internals/vmap/AABB.cpp)

  # CMANGOS.04 (Phase 2.04c) : MoveSplinePacketBuilder pure encode/decode.
  lcdlln_add_simple_test(move_spline_packet_tests
    ${CMAKE_SOURCE_DIR}/src/shardd/internals/movement/MoveSplinePacketBuilderTests.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/internals/movement/MoveSplinePacketBuilder.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/internals/movement/MoveSpline.cpp)

  # CMANGOS.01 (Phase 2.01c) : ChatLocalPayload pure encode/decode.
  lcdlln_add_simple_test(chat_local_payload_tests
    ${CMAKE_SOURCE_DIR}/src/shardd/internals/chat/ChatLocalPayloadTests.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/internals/chat/ChatLocalPayload.cpp)

  # CMANGOS.01 (Phase 2.01b) : ChatCommandRouter pure tests
  add_executable(chat_command_router_tests
    ${CMAKE_SOURCE_DIR}/src/masterd/chat/ChatCommandRouterTests.cpp
    ${CMAKE_SOURCE_DIR}/src/masterd/chat/ChatCommandRouter.cpp
    ${CMAKE_SOURCE_DIR}/src/masterd/account/AccountRoleService.cpp
    ${CMAKE_SOURCE_DIR}/src/masterd/account/AccountRole.cpp
    ${CMAKE_SOURCE_DIR}/src/masterd/account/InMemoryAccountStore.cpp
    ${CMAKE_SOURCE_DIR}/src/masterd/account/AccountValidation.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/security/SecurityAuditLog.cpp
  )
  target_include_directories(chat_command_router_tests PRIVATE ${CMAKE_SOURCE_DIR})
  target_link_libraries(chat_command_router_tests PRIVATE engine_core engine_auth OpenSSL::SSL spdlog::spdlog pthread)
  target_compile_options(chat_command_router_tests PRIVATE -Wall -Wextra -Wpedantic)
  add_test(NAME chat_command_router_tests COMMAND chat_command_router_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

  # CMANGOS.01 (Phase 2.01b) : ChatChannelRegistry pure tests
  add_executable(chat_channel_registry_tests
    ${CMAKE_SOURCE_DIR}/src/masterd/chat/ChatChannelRegistryTests.cpp
    ${CMAKE_SOURCE_DIR}/src/masterd/chat/ChatChannelRegistry.cpp
  )
  target_include_directories(chat_channel_registry_tests PRIVATE ${CMAKE_SOURCE_DIR})
  target_link_libraries(chat_channel_registry_tests PRIVATE engine_core spdlog::spdlog pthread)
  target_compile_options(chat_channel_registry_tests PRIVATE -Wall -Wextra -Wpedantic)
  add_test(NAME chat_channel_registry_tests COMMAND chat_channel_registry_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

  # M22.4: Shard ticket validation tests
  add_executable(shard_ticket_tests
    ${CMAKE_SOURCE_DIR}/src/masterd/handlers/shard/ShardTicketTests.cpp
    ${CMAKE_SOURCE_DIR}/src/masterd/handlers/shard/ShardTicketValidator.cpp
    ${CMAKE_SOURCE_DIR}/src/masterd/handlers/shard/ShardTicketCrypto.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/network/ShardTicketPayloads.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/network/ByteReader.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/network/ByteWriter.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/network/PacketBuilder.cpp
  )
  target_include_directories(shard_ticket_tests PRIVATE ${CMAKE_SOURCE_DIR})
  target_link_libraries(shard_ticket_tests PRIVATE engine_core OpenSSL::SSL)
  target_compile_options(shard_ticket_tests PRIVATE -Wall -Wextra -Wpedantic)
  add_test(NAME shard_ticket_tests COMMAND shard_ticket_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

  # M22.5: Server list and shard selection policy tests
  add_executable(server_list_policy_tests
    ${CMAKE_SOURCE_DIR}/src/masterd/handlers/shard/ServerListPolicyTests.cpp
    ${CMAKE_SOURCE_DIR}/src/masterd/shards/ShardRegistry.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/network/ServerListPayloads.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/network/ByteReader.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/network/ByteWriter.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/network/PacketBuilder.cpp
  )
  target_include_directories(server_list_policy_tests PRIVATE ${CMAKE_SOURCE_DIR})
  target_link_libraries(server_list_policy_tests PRIVATE engine_core)
  target_compile_options(server_list_policy_tests PRIVATE -Wall -Wextra -Wpedantic)
  add_test(NAME server_list_policy_tests COMMAND server_list_policy_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

  # M22.6: Shard server (ticket handshake + enregistrement TLS/TCP vers le Master via ShardToMasterClient)
  add_executable(shard_app
    ${CMAKE_SOURCE_DIR}/src/shardd/main_linux.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/network/NetServer.cpp
    ${CMAKE_SOURCE_DIR}/src/masterd/handlers/shard/ShardTicketValidator.cpp
    ${CMAKE_SOURCE_DIR}/src/masterd/handlers/shard/ShardTicketHandshakeHandler.cpp
    ${CMAKE_SOURCE_DIR}/src/masterd/handlers/shard/ShardTicketCrypto.cpp
    ${CMAKE_SOURCE_DIR}/src/masterd/metrics/HealthEndpoint.cpp
    ${CMAKE_SOURCE_DIR}/src/masterd/metrics/PrometheusMetrics.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/network/ShardToMasterClient.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/network/ShardTicketPayloads.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/network/ByteReader.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/network/ByteWriter.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/network/PacketBuilder.cpp
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
