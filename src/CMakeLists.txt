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

# ── Embarquement anti-triche : JSON game/data → headers const (générés au build).
set(LCDLLN_GEN_DIR ${CMAKE_BINARY_DIR}/generated)
file(MAKE_DIRECTORY ${LCDLLN_GEN_DIR})

set(STATS_JSON    ${CMAKE_SOURCE_DIR}/game/data/gameplay/character_stats.json)
set(STATS_HDR     ${LCDLLN_GEN_DIR}/CharacterStatsData.h)
set(FACTIONS_JSON ${CMAKE_SOURCE_DIR}/game/data/races/factions.json)
set(FACTIONS_HDR  ${LCDLLN_GEN_DIR}/FactionsData.h)

add_custom_command(
  OUTPUT  ${STATS_HDR}
  COMMAND ${CMAKE_COMMAND} -DINPUT=${STATS_JSON} -DOUTPUT=${STATS_HDR} -DSYMBOL=kCharacterStatsJson -P ${CMAKE_SOURCE_DIR}/cmake/EmbedJson.cmake
  DEPENDS ${STATS_JSON} ${CMAKE_SOURCE_DIR}/cmake/EmbedJson.cmake
  COMMENT "Embedding character_stats.json -> CharacterStatsData.h")

add_custom_command(
  OUTPUT  ${FACTIONS_HDR}
  COMMAND ${CMAKE_COMMAND} -DINPUT=${FACTIONS_JSON} -DOUTPUT=${FACTIONS_HDR} -DSYMBOL=kFactionsJson -P ${CMAKE_SOURCE_DIR}/cmake/EmbedJson.cmake
  DEPENDS ${FACTIONS_JSON} ${CMAKE_SOURCE_DIR}/cmake/EmbedJson.cmake
  COMMENT "Embedding factions.json -> FactionsData.h")

add_custom_target(lcdlln_gen_character_data DEPENDS ${STATS_HDR} ${FACTIONS_HDR})

if(WIN32)
  add_executable(server_app
    ${CMAKE_SOURCE_DIR}/src/shardd/main_win.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/server_bootstrap/ServerApp.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/world/AdmittedCharacterRegistry.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/world/ShardPresenceService.cpp
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
    # Character system PR1 — moteur de stats (serveur-only, anti-triche).
    ${CMAKE_SOURCE_DIR}/src/shardd/gameplay/character/CharacterStatsTables.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/gameplay/character/CharacterStatsEngine.cpp
    # Level-up runtime — helper de progression d'XP/niveau (pur, server-only).
    ${CMAKE_SOURCE_DIR}/src/shardd/gameplay/character/LevelProgression.cpp
    # Character system R1-A — résolveur PV enter-world (dérive depuis le moteur).
    ${CMAKE_SOURCE_DIR}/src/shardd/gameplay/character/SpawnStatsResolver.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/gameplay/economy/CurrencyConfig.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/gameplay/economy/VendorCatalog.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/gameplay/economy/PlayerWalletService.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/gameplay/trade/TradeSystem.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/gameplay/gathering/GatheringSystem.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/gameplay/crafting/CraftingSystem.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/gameplay/auction/AuctionHouse.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/gameplay/quest/QuestRuntime.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/gameplay/spawner/SpawnerRuntime.cpp
    # Combat SP1 — catalogue d'archétypes de créatures (stats data-driven).
    ${CMAKE_SOURCE_DIR}/src/shardd/gameplay/creature/CreatureArchetypeLibrary.cpp
    # Combat SP3 — kits de sorts par profil (data-driven).
    ${CMAKE_SOURCE_DIR}/src/shardd/gameplay/spell/SpellKitLibrary.cpp
    # SP-A — catalogue strict des compétences par-classe (serveur).
    ${CMAKE_SOURCE_DIR}/src/shardd/gameplay/spell/ClassSkillLibrary.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/gameplay/event/EventRuntime.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/world/UdpTransport.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/world/TickScheduler.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/world/SpatialPartition.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/world/GridState.cpp
    # Wave 18 : GridNotifier (foncteur Visitor + filter predicate).
    # GridVisitor.h est header-only template, pas de .cpp.
    ${CMAKE_SOURCE_DIR}/src/shardd/world/GridNotifier.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/world/ZoneTransitions.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/world/LagCompensation.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/core/Clock.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/core/Config.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/core/ServerEndpoints.cpp
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
  # Character system PR1 — accès aux headers JSON embarqués (anti-triche).
  add_dependencies(server_app lcdlln_gen_character_data)
  target_include_directories(server_app PRIVATE ${CMAKE_SOURCE_DIR} ${LCDLLN_GEN_DIR})
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
    ${CMAKE_SOURCE_DIR}/src/shared/db/SqlPreparedStatement.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/network/NetServer.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/world/LagCompensation.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/core/Clock.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/core/Config.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/core/ServerEndpoints.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/core/Log.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/core/LogConfig.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/platform/FileSystem.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/net/ChatSystem.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/net/ChatEmotes.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/network/PacketView.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/network/ByteReader.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/network/ByteWriter.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/network/PacketBuilder.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/network/PacketLog.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/network/ErrorPacket.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/network/AuthRegisterPayloads.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/network/CharacterPayloads.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/network/ShardPayloads.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/network/ShardTicketPayloads.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/network/ServerListPayloads.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/network/TermsPayloads.cpp
    # Audit F3 : tag HMAC-SHA256 préfixe authentifiant SHARD_REGISTER/HEARTBEAT
    # (utilisé par ShardRegisterHandler, compilé dans ce binaire master).
    ${CMAKE_SOURCE_DIR}/src/shared/network/ShardWireAuth.cpp
    # Audit F6 : refus au boot d'un secret HMAC partagé faible/par défaut.
    ${CMAKE_SOURCE_DIR}/src/shared/security/SharedSecretPolicy.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/gameplay/social/FriendSystem.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/world/ShardPresenceService.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/gameplay/social/PartySystem.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/gameplay/guild/GuildSystem.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/gameplay/guild/GuildTabard.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/gameplay/guild/GuildBank.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/account/AccountValidation.cpp
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
    ${CMAKE_SOURCE_DIR}/src/masterd/handlers/dungeon/EnterDungeonHandler.cpp
    ${CMAKE_SOURCE_DIR}/src/masterd/session/SessionCharacterMap.cpp
    ${CMAKE_SOURCE_DIR}/src/masterd/handlers/chat/ChatRelayHandler.cpp
    ${CMAKE_SOURCE_DIR}/src/masterd/handlers/mail/MailHandler.cpp
    ${CMAKE_SOURCE_DIR}/src/masterd/handlers/social/IgnoreListHandler.cpp
    ${CMAKE_SOURCE_DIR}/src/masterd/handlers/gmtickets/GmTicketHandler.cpp
    ${CMAKE_SOURCE_DIR}/src/masterd/handlers/trade/TradeHandler.cpp
    ${CMAKE_SOURCE_DIR}/src/masterd/handlers/reputation/ReputationHandler.cpp
    ${CMAKE_SOURCE_DIR}/src/masterd/handlers/lfg/LfgHandler.cpp
    ${CMAKE_SOURCE_DIR}/src/masterd/handlers/cinematics/CinematicHandler.cpp
    ${CMAKE_SOURCE_DIR}/src/masterd/handlers/skills/SkillHandler.cpp
    ${CMAKE_SOURCE_DIR}/src/masterd/handlers/arena/ArenaHandler.cpp
    ${CMAKE_SOURCE_DIR}/src/masterd/handlers/battleground/BattleGroundHandler.cpp
    ${CMAKE_SOURCE_DIR}/src/masterd/handlers/outdoorpvp/OutdoorPvpHandler.cpp
    ${CMAKE_SOURCE_DIR}/src/masterd/handlers/weather/WeatherHandler.cpp
    ${CMAKE_SOURCE_DIR}/src/masterd/handlers/events/GameEventHandler.cpp
    ${CMAKE_SOURCE_DIR}/src/masterd/handlers/interactive/InteractiveHandler.cpp
    ${CMAKE_SOURCE_DIR}/src/masterd/handlers/guild/GuildHandler.cpp
    ${CMAKE_SOURCE_DIR}/src/masterd/handlers/auction/AuctionHandler.cpp
    ${CMAKE_SOURCE_DIR}/src/masterd/handlers/loot/LootHandler.cpp
    ${CMAKE_SOURCE_DIR}/src/masterd/handlers/lunar/LunarHandler.cpp
    ${CMAKE_SOURCE_DIR}/src/masterd/handlers/worldclock/WorldClockHandler.cpp
    ${CMAKE_SOURCE_DIR}/src/masterd/handlers/admin/AdminCommandHandler.cpp
    ${CMAKE_SOURCE_DIR}/src/masterd/admin/SlashCommandRegistry.cpp
    ${CMAKE_SOURCE_DIR}/src/masterd/trade/TradeSessionRegistry.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/network/IgnoreListPayloads.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/network/DungeonPayloads.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/network/MailPayloads.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/network/GmTicketPayloads.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/network/TradePayloads.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/network/ReputationPayloads.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/network/LfgPayloads.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/network/CinematicPayloads.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/network/SkillPayloads.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/network/ArenaPayloads.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/network/BattleGroundPayloads.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/network/OutdoorPvpPayloads.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/network/WeatherPayloads.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/network/GameEventPayloads.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/network/InteractivePayloads.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/network/GuildPayloads.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/network/AuctionPayloads.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/network/LootPayloads.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/network/LunarPayloads.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/network/WorldClockPayloads.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/network/AdminCommandPayloads.cpp
    ${CMAKE_SOURCE_DIR}/src/masterd/chat/ChatSanitizer.cpp
    ${CMAKE_SOURCE_DIR}/src/masterd/chat/ChatGate.cpp
    ${CMAKE_SOURCE_DIR}/src/masterd/chat/ChatCommandRouter.cpp
    ${CMAKE_SOURCE_DIR}/src/masterd/chat/ChatChannelRegistry.cpp
    ${CMAKE_SOURCE_DIR}/src/masterd/mail/MailManager.cpp
    ${CMAKE_SOURCE_DIR}/src/masterd/mail/MysqlMailStore.cpp
    ${CMAKE_SOURCE_DIR}/src/masterd/gmtickets/MysqlGmTicketStore.cpp
    ${CMAKE_SOURCE_DIR}/src/masterd/reputation/MysqlReputationStore.cpp
    ${CMAKE_SOURCE_DIR}/src/masterd/social/MysqlIgnoreStore.cpp
    # Wave 5 Persistence phase 1 : Auction / Arena / GameEvents / Skills / OutdoorPvp.
    ${CMAKE_SOURCE_DIR}/src/masterd/auction/MysqlAuctionStore.cpp
    ${CMAKE_SOURCE_DIR}/src/masterd/arena/MysqlArenaStore.cpp
    ${CMAKE_SOURCE_DIR}/src/masterd/events/MysqlGameEventStore.cpp
    ${CMAKE_SOURCE_DIR}/src/masterd/skills/MysqlSkillStore.cpp
    ${CMAKE_SOURCE_DIR}/src/masterd/outdoorpvp/MysqlOutdoorPvpStore.cpp
    # Wave 5 Persistence phase 2 : Guild / BattleGround / Loot stores.
    ${CMAKE_SOURCE_DIR}/src/masterd/guild/MysqlGuildStore.cpp
    ${CMAKE_SOURCE_DIR}/src/masterd/battleground/MysqlBattleGroundStore.cpp
    ${CMAKE_SOURCE_DIR}/src/masterd/loot/MysqlLootStore.cpp
    # Wave 11 Persistence : Cinematic seen store (table cinematic_seen, 0058).
    ${CMAKE_SOURCE_DIR}/src/masterd/cinematics/InMemoryCinematicStore.cpp
    ${CMAKE_SOURCE_DIR}/src/masterd/cinematics/MysqlCinematicStore.cpp
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
    ${CMAKE_SOURCE_DIR}/src/masterd/shards/ShardPlayerPresenceCache.cpp
    ${CMAKE_SOURCE_DIR}/src/masterd/world/ZoneNameRegistry.cpp
    ${CMAKE_SOURCE_DIR}/src/masterd/shards/ServerRegistry.cpp
    ${CMAKE_SOURCE_DIR}/src/masterd/handlers/shard/ShardRegisterHandler.cpp
    ${CMAKE_SOURCE_DIR}/src/masterd/handlers/shard/ShardTicketCrypto.cpp
    ${CMAKE_SOURCE_DIR}/src/masterd/handlers/shard/ShardTicketHandler.cpp
    ${CMAKE_SOURCE_DIR}/src/masterd/handlers/shard/ShardTicketValidator.cpp
    ${CMAKE_SOURCE_DIR}/src/masterd/handlers/shard/ServerListHandler.cpp
    ${CMAKE_SOURCE_DIR}/src/masterd/metrics/HealthEndpoint.cpp
    ${CMAKE_SOURCE_DIR}/src/masterd/metrics/PrometheusMetrics.cpp
    # Wave 7 Entities foundation : Object base (header-only en pratique, .cpp
    # garde un translation unit nomme pour futures methodes out-of-line).
    ${CMAKE_SOURCE_DIR}/src/shardd/entities/Object.cpp
    # Wave 17 Entities suite : WorldObject (Object + position 3D + map/zone).
    ${CMAKE_SOURCE_DIR}/src/shardd/entities/WorldObject.cpp
    # Wave 17 Entities suite : Unit (WorldObject + HP/MP/level/faction).
    ${CMAKE_SOURCE_DIR}/src/shardd/entities/Unit.cpp
    # Wave 17 Entities suite : Player (Unit + accountId/characterId/name/xp).
    ${CMAKE_SOURCE_DIR}/src/shardd/entities/Player.cpp
    # Wave 17 Entities suite : Creature (Unit + templateEntry/spawnId).
    ${CMAKE_SOURCE_DIR}/src/shardd/entities/Creature.cpp
    # Character system PR1 — Player::ApplyDerivedStats référence le moteur de
    # stats ; ce server_app (master Linux) compile Player.cpp, il doit donc
    # aussi compiler le moteur sinon LINK FAILURE (ComputeStats/FromEmbedded).
    ${CMAKE_SOURCE_DIR}/src/shardd/gameplay/character/CharacterStatsTables.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/gameplay/character/CharacterStatsEngine.cpp
  )
  # Character system PR1 — accès aux headers JSON embarqués (anti-triche).
  add_dependencies(server_app lcdlln_gen_character_data)
  target_include_directories(server_app PRIVATE ${MYSQL_INCLUDE_DIR} ${CMAKE_SOURCE_DIR} ${LCDLLN_GEN_DIR})
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
    ${CMAKE_SOURCE_DIR}/src/shared/core/Clock.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/core/Config.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/core/ServerEndpoints.cpp
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
    ${CMAKE_SOURCE_DIR}/src/shared/db/SqlPreparedStatement.cpp
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
    ${CMAKE_SOURCE_DIR}/src/shared/account/AccountValidation.cpp
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
    ${CMAKE_SOURCE_DIR}/src/shared/db/SqlPreparedStatement.cpp
  )
  target_include_directories(sql_storage_tests PRIVATE ${CMAKE_SOURCE_DIR} ${MYSQL_INCLUDE_DIR})
  target_link_libraries(sql_storage_tests PRIVATE engine_core ${MYSQL_LIBRARY} pthread)
  target_compile_options(sql_storage_tests PRIVATE -Wall -Wextra -Wpedantic)
  add_test(NAME sql_storage_tests COMMAND sql_storage_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

  # CMANGOS.13 (Phase 1a) : SqlPreparedStatement + cache tests
  add_executable(sql_prepared_statement_tests
    ${CMAKE_SOURCE_DIR}/src/shared/db/SqlPreparedStatementTests.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/db/ConnectionPool.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/db/DbHelpers.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/db/SqlPreparedStatement.cpp
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
    ${CMAKE_SOURCE_DIR}/src/shared/db/SqlPreparedStatement.cpp
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
    ${CMAKE_SOURCE_DIR}/src/shared/db/SqlPreparedStatement.cpp
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
    ${CMAKE_SOURCE_DIR}/src/shared/db/SqlPreparedStatement.cpp
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
    ${CMAKE_SOURCE_DIR}/src/shared/db/SqlPreparedStatement.cpp
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
    ${CMAKE_SOURCE_DIR}/src/shared/db/SqlPreparedStatement.cpp
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

  # Wave 7 Entities foundation : ObjectGuid + UpdateMask + UpdateField + Object base.
  # Note : namespace engine::server::entities, distinct du ObjectGuid existant
  # dans engine::server::shard. Les deux cohabitent ; la migration progressive
  # vers la version "entities" sera faite par PRs futures.
  lcdlln_add_simple_test(entities_foundation_tests
    ${CMAKE_SOURCE_DIR}/src/shardd/entities/ObjectGuidTests.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/entities/Object.cpp)

  # Wave 17 Entities suite : WorldObject tests (position, map, zone, IsInWorld).
  lcdlln_add_simple_test(worldobject_tests
    ${CMAKE_SOURCE_DIR}/src/shardd/entities/WorldObjectTests.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/entities/WorldObject.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/entities/Object.cpp)

  # Wave 17 Entities suite : Unit tests (HP/MP/level/faction, clamp, IsAlive).
  lcdlln_add_simple_test(unit_tests
    ${CMAKE_SOURCE_DIR}/src/shardd/entities/UnitTests.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/entities/Unit.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/entities/WorldObject.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/entities/Object.cpp)

  # Wave 17 Entities suite : Player tests (accountId/characterId/name/xp + heritage Unit).
  # Character system PR1 — depuis Player::ApplyDerivedStats, PlayerTests référence
  # le moteur de stats : bloc explicite (pas lcdlln_add_simple_test) pour ajouter
  # les sources du moteur, la dépendance au codegen et le dossier des headers
  # générés (${LCDLLN_GEN_DIR}), que le helper ne sait pas exprimer.
  add_executable(player_tests
    ${CMAKE_SOURCE_DIR}/src/shardd/entities/PlayerTests.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/entities/Player.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/entities/Unit.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/entities/WorldObject.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/entities/Object.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/gameplay/character/CharacterStatsEngine.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/gameplay/character/CharacterStatsTables.cpp)
  add_dependencies(player_tests lcdlln_gen_character_data)
  target_include_directories(player_tests PRIVATE ${CMAKE_SOURCE_DIR} ${LCDLLN_GEN_DIR})
  target_link_libraries(player_tests PRIVATE engine_core spdlog::spdlog pthread)
  target_compile_options(player_tests PRIVATE -Wall -Wextra -Wpedantic)
  add_test(NAME player_tests COMMAND player_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

  # Wave 17 Entities suite : Creature tests (templateEntry/spawnId + heritage Unit).
  lcdlln_add_simple_test(creature_tests
    ${CMAKE_SOURCE_DIR}/src/shardd/entities/CreatureTests.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/entities/Creature.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/entities/Unit.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/entities/WorldObject.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/entities/Object.cpp)

  # Wave 17 Entities suite : UpdateMask delta tests cross-classe.
  # Verifie que les modifications sur Unit/Player/Creature ne marquent que
  # les champs effectivement changes (critique pour delta replication).
  # Character system PR1 — compile Player.cpp donc référence Player::ApplyDerivedStats
  # -> doit lier le moteur de stats (ComputeStats/FromEmbedded) sinon LINK FAILURE.
  # Bloc explicite (pas lcdlln_add_simple_test) pour ajouter les sources moteur +
  # la dépendance au codegen.
  add_executable(update_mask_delta_tests
    ${CMAKE_SOURCE_DIR}/src/shardd/entities/UpdateMaskDeltaTests.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/entities/Creature.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/entities/Player.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/entities/Unit.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/entities/WorldObject.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/entities/Object.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/gameplay/character/CharacterStatsEngine.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/gameplay/character/CharacterStatsTables.cpp)
  add_dependencies(update_mask_delta_tests lcdlln_gen_character_data)
  target_include_directories(update_mask_delta_tests PRIVATE ${CMAKE_SOURCE_DIR} ${LCDLLN_GEN_DIR})
  target_link_libraries(update_mask_delta_tests PRIVATE engine_core spdlog::spdlog pthread)
  target_compile_options(update_mask_delta_tests PRIVATE -Wall -Wextra -Wpedantic)
  add_test(NAME update_mask_delta_tests COMMAND update_mask_delta_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

  # TA.1 (replication des joueurs) : UdpTransport rendu cross-platform (Winsock +
  # BSD sockets POSIX). Le test loopback 127.0.0.1 valide le chemin POSIX a
  # l'execution sur le runner Linux (Init/Send/Receive/non-blocking/BoundPort).
  lcdlln_add_simple_test(udp_transport_tests
    ${CMAKE_SOURCE_DIR}/src/shardd/world/UdpTransportTests.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/world/UdpTransport.cpp)

  # TE.1 (replication des joueurs) : round-trip EncodeInput/DecodeInput (Y+yaw, TC.1)
  # + Hello. ServerProtocol.cpp est fourni par engine_core (lié par le helper).
  lcdlln_add_simple_test(server_protocol_tests
    ${CMAKE_SOURCE_DIR}/src/shared/network/ServerProtocolTests.cpp)

  # Combat SP1 : catalogue d'archétypes de créatures (parse + validation JSON).
  lcdlln_add_simple_test(creature_archetype_library_tests
    ${CMAKE_SOURCE_DIR}/src/shardd/gameplay/creature/CreatureArchetypeLibraryTests.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/gameplay/creature/CreatureArchetypeLibrary.cpp)

  # Combat SP2 : résolveur d'attaque pur (précision / critique, header-only).
  lcdlln_add_simple_test(attack_resolver_tests
    ${CMAKE_SOURCE_DIR}/src/shardd/gameplay/combat/AttackResolverTests.cpp)

  # Combat SP3 : kits de sorts par profil (parse + validation JSON).
  lcdlln_add_simple_test(spell_kit_library_tests
    ${CMAKE_SOURCE_DIR}/src/shardd/gameplay/spell/SpellKitLibraryTests.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/gameplay/spell/SpellKitLibrary.cpp)

  # SP-A : catalogue strict des compétences par-classe (serveur, parse + validation JSON).
  lcdlln_add_simple_test(class_skill_library_tests
    ${CMAKE_SOURCE_DIR}/src/shardd/gameplay/spell/ClassSkillLibraryTests.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/gameplay/spell/ClassSkillLibrary.cpp)

  # Grimoire — résolveur de layout des 10 slots de barre d'action (client, pur).
  # Note : ActionBarLayout.cpp et SpellKitCatalog.cpp sont déjà dans engine_core
  # (CMakeLists racine) → ne pas les re-lister ici (double définition de symboles).
  lcdlln_add_simple_test(action_bar_layout_tests
    ${CMAKE_SOURCE_DIR}/src/client/gameplay/ActionBarLayoutTests.cpp)

  # SP-A : catalogue client tolérant des compétences par-classe.
  # Note : ClassSkillCatalog.cpp est déjà dans engine_core (CMakeLists racine)
  # → ne pas le re-lister ici (double définition de symboles au link).
  lcdlln_add_simple_test(class_skill_catalog_tests
    ${CMAKE_SOURCE_DIR}/src/client/gameplay/ClassSkillCatalogTests.cpp)

  # TA.3 (replication des joueurs) : registre des personnages admis (gate de la
  # session UDP, lié au character_id authentifié du ticket). Pure logique testable.
  lcdlln_add_simple_test(admitted_character_registry_tests
    ${CMAKE_SOURCE_DIR}/src/shardd/world/AdmittedCharacterRegistryTests.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/world/AdmittedCharacterRegistry.cpp)

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

  # SP1 : QuestRuntime — cycle de vie piloté par le joueur (parse giver/turnIn, transitions).
  lcdlln_add_simple_test(quest_runtime_tests
    ${CMAKE_SOURCE_DIR}/src/shardd/gameplay/quest/QuestRuntimeTests.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/gameplay/quest/QuestRuntime.cpp)
  # SP4 (éditeur monde) : QuestEditIo — chargement authoring (quest_definitions + quest_texts fusionnés).
  # QuestEditIo.cpp est désormais dans engine_core (CMakeLists racine, requis
  # par QuestEditorPanel/WorldEditorShell) -> ne pas le re-lister ici (double
  # définition de symboles), même pattern que quest_text_catalog_tests ci-dessous.
  lcdlln_add_simple_test(quest_edit_io_tests
    ${CMAKE_SOURCE_DIR}/src/world_editor/quests/QuestEditIoTests.cpp)
  # SP2 (client) : QuestTextCatalog — titres/descriptions/étapes lisibles par locale.
  # QuestTextCatalog.cpp est desormais dans engine_core (CMakeLists racine, requis
  # par QuestImGuiRenderer/Engine.cpp) -> ne pas le re-lister ici (double définition
  # de symboles), meme pattern que ui_model_quest_giver_tests ci-dessous.
  lcdlln_add_simple_test(quest_text_catalog_tests
    ${CMAKE_SOURCE_DIR}/src/client/quest/QuestTextCatalogTests.cpp)
  # SP2 (client) : QuestGiverTable — association PNJ -> quêtes (donneur/receveur).
  # QuestGiverTable.cpp est desormais dans engine_core (meme raison que ci-dessus).
  lcdlln_add_simple_test(quest_giver_table_tests
    ${CMAKE_SOURCE_DIR}/src/client/quest/QuestGiverTableTests.cpp)
  # SP3 (client) : QuestPoiTable — positions minimap par cible de quête (targetId).
  # QuestPoiTable.cpp est desormais dans engine_core (meme raison que ci-dessus,
  # requis par le futur QuestUiPresenter) -> ne pas le re-lister ici (double
  # définition de symboles au link).
  lcdlln_add_simple_test(quest_poi_table_tests
    ${CMAKE_SOURCE_DIR}/src/client/quest/QuestPoiTableTests.cpp)
  # SP3 (client) : WorldToRadarUv — projection monde -> UV radar minimap
  # centré joueur (RebuildMinimap). Helper pur exposé dans QuestUi.h/.cpp
  # (déjà dans engine_core) -> le test ne relie que son propre .cpp.
  lcdlln_add_simple_test(quest_ui_radar_tests
    ${CMAKE_SOURCE_DIR}/src/client/quest/QuestUiRadarTests.cpp)
  # SP2 (client) : réception QuestGiverList (opcode 92) -> UIModel.giverList.
  # UIModel.cpp et ServerProtocol.cpp sont déjà dans engine_core (CMakeLists
  # racine) -> ne pas les re-lister ici (double définition de symboles).
  lcdlln_add_simple_test(ui_model_quest_giver_tests
    ${CMAKE_SOURCE_DIR}/src/client/ui_common/UIModelQuestGiverTests.cpp)
  # SP1 : compat des statuts de quête persistés (ancien enum 0/1/2 -> nouveau).
  lcdlln_add_simple_test(character_persistence_quest_compat_tests
    ${CMAKE_SOURCE_DIR}/src/shardd/gameplay/character/CharacterPersistenceQuestCompatTests.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/gameplay/character/CharacterPersistence.cpp)
  # CMANGOS.33 (Phase 5.33a) : LfgQueue header-only.
  lcdlln_add_simple_test(lfg_queue_tests
    ${CMAKE_SOURCE_DIR}/src/masterd/lfg/LfgQueueTests.cpp)

  # CMANGOS.28 (Phase 3.28a) : WorldStateRegistry (header-only).
  lcdlln_add_simple_test(world_state_registry_tests
    ${CMAKE_SOURCE_DIR}/src/masterd/world/WorldStateRegistryTests.cpp)

  # CMANGOS.20 (Phase 3.20a) : MotionGeneratorStack (header-only).
  lcdlln_add_simple_test(motion_generator_stack_tests
    ${CMAKE_SOURCE_DIR}/src/shardd/ai/MotionGeneratorStackTests.cpp)
  # Wave 24 (CMANGOS.20 step 2) SCAFFOLD : INavmeshProvider interface +
  # StubNavmeshProvider + WaypointMgr + PathFollowMotion state machine.
  # L'integration recast/detour vraie viendra dans une PR ulterieure
  # avec ajout vcpkg recastnavigation + RecastNavmeshProvider impl.
  lcdlln_add_simple_test(movement_tests
    ${CMAKE_SOURCE_DIR}/src/shardd/Movement/MovementTests.cpp)
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

  # Wave 21 (CMANGOS.19 step 2) : Map hierarchie -- base abstraite +
  # WorldMap / DungeonMap / BattlegroundMap. Header-only, pas de .cpp.
  lcdlln_add_simple_test(maps_subclasses_tests
    ${CMAKE_SOURCE_DIR}/src/shardd/maps/MapsSubclassesTests.cpp)

  # CMANGOS.36 (Phase 5.36a) : OutdoorPvPManager (header-only, zones contestees).
  lcdlln_add_simple_test(outdoor_pvp_manager_tests
    ${CMAKE_SOURCE_DIR}/src/shardd/outdoorpvp/OutdoorPvPManagerTests.cpp)

  # CMANGOS.22 (Phase 4.22a) : PoolManager weighted spawn (header-only).
  lcdlln_add_simple_test(pool_manager_tests
    ${CMAKE_SOURCE_DIR}/src/shardd/pools/PoolManagerTests.cpp)

  # Wave 20 (CMANGOS.22 step 2) : Pool nested + persistence.
  # PoolManager.h gagne le support nested + cycle detection (backward compat
  # avec les pools simples : si Pool.nested est vide, comportement identique
  # a la Phase 4.22a). PoolStatePersistence.h est header-only (interface +
  # InMemoryPoolStateStore pour tests).
  lcdlln_add_simple_test(pool_nested_tests
    ${CMAKE_SOURCE_DIR}/src/shardd/pools/PoolNestedTests.cpp)
  lcdlln_add_simple_test(pool_persistence_tests
    ${CMAKE_SOURCE_DIR}/src/shardd/pools/PoolPersistenceTests.cpp)

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

  # Character system PR1 — Config::LoadFromString : parsing JSON en mémoire (anti-triche).
  lcdlln_add_simple_test(config_loadfromstring_tests
    ${CMAKE_SOURCE_DIR}/src/shared/core/ConfigLoadFromStringTests.cpp)

  # Character system PR1 — CharacterStatsEngine : 11 stats déterministes depuis
  # les tables JSON embarquées. Bloc explicite (pas lcdlln_add_simple_test) :
  # le test a besoin du dossier des headers générés (${LCDLLN_GEN_DIR}) et de
  # la dépendance au codegen lcdlln_gen_character_data, que le helper ne sait
  # pas exprimer.
  add_executable(character_stats_engine_tests
    ${CMAKE_SOURCE_DIR}/src/shardd/gameplay/character/CharacterStatsEngineTests.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/gameplay/character/CharacterStatsEngine.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/gameplay/character/CharacterStatsTables.cpp)
  add_dependencies(character_stats_engine_tests lcdlln_gen_character_data)
  target_include_directories(character_stats_engine_tests PRIVATE ${CMAKE_SOURCE_DIR} ${LCDLLN_GEN_DIR})
  target_link_libraries(character_stats_engine_tests PRIVATE engine_core spdlog::spdlog pthread)
  target_compile_options(character_stats_engine_tests PRIVATE -Wall -Wextra -Wpedantic)
  add_test(NAME character_stats_engine_tests COMMAND character_stats_engine_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

  # Character system R1-A — SpawnStatsResolver : PV (max+courants) dérivés à
  # l'enter-world. Bloc explicite (comme character_stats_engine_tests) : besoin
  # du dossier des headers générés (${LCDLLN_GEN_DIR}) et de la dépendance au
  # codegen lcdlln_gen_character_data.
  add_executable(spawn_stats_resolver_tests
    ${CMAKE_SOURCE_DIR}/src/shardd/gameplay/character/SpawnStatsResolverTests.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/gameplay/character/SpawnStatsResolver.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/gameplay/character/CharacterStatsEngine.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/gameplay/character/CharacterStatsTables.cpp)
  add_dependencies(spawn_stats_resolver_tests lcdlln_gen_character_data)
  target_include_directories(spawn_stats_resolver_tests PRIVATE ${CMAKE_SOURCE_DIR} ${LCDLLN_GEN_DIR})
  target_link_libraries(spawn_stats_resolver_tests PRIVATE engine_core spdlog::spdlog pthread)
  target_compile_options(spawn_stats_resolver_tests PRIVATE -Wall -Wextra -Wpedantic)
  add_test(NAME spawn_stats_resolver_tests COMMAND spawn_stats_resolver_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

  # Level-up runtime — LevelProgression : helper de montée de niveau pur, ancré
  # sur la vraie courbe XpToNextLevel. Bloc explicite (comme
  # character_stats_engine_tests) : besoin du dossier des headers générés
  # (${LCDLLN_GEN_DIR}) et de la dépendance au codegen lcdlln_gen_character_data
  # (le test charge xpBase/xpFactor/levelMax depuis le JSON embarqué).
  add_executable(level_progression_tests
    ${CMAKE_SOURCE_DIR}/src/shardd/gameplay/character/LevelProgressionTests.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/gameplay/character/LevelProgression.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/gameplay/character/CharacterStatsEngine.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/gameplay/character/CharacterStatsTables.cpp)
  add_dependencies(level_progression_tests lcdlln_gen_character_data)
  target_include_directories(level_progression_tests PRIVATE ${CMAKE_SOURCE_DIR} ${LCDLLN_GEN_DIR})
  target_link_libraries(level_progression_tests PRIVATE engine_core spdlog::spdlog pthread)
  target_compile_options(level_progression_tests PRIVATE -Wall -Wextra -Wpedantic)
  add_test(NAME level_progression_tests COMMAND level_progression_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

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

  # Sous-chantier 1 (Task 1.1) : formule horloge monde deterministe partagee
  # (engine::world, header-only). Cible cross-platform (add_executable nu, pas
  # lcdlln_add_simple_test : ce helper linke pthread + flags GCC, non portables
  # Windows). engine_core expose ${CMAKE_SOURCE_DIR} en include PUBLIC, ce qui
  # resout les #include "src/...".
  add_executable(world_clock_tests
    ${CMAKE_SOURCE_DIR}/src/shared/world/tests/WorldClockTests.cpp)
  target_link_libraries(world_clock_tests PRIVATE engine_core)
  add_test(NAME world_clock_tests COMMAND world_clock_tests)

  # Theming runtime (sous-projet 1) : registre de palettes LnTheme (header-only).
  # engine_core fournit l'include path ${CMAKE_SOURCE_DIR} pour resoudre "src/...".
  add_executable(ln_theme_tests
    ${CMAKE_SOURCE_DIR}/src/client/render/tests/LnThemeTests.cpp)
  target_link_libraries(ln_theme_tests PRIVATE engine_core)
  add_test(NAME ln_theme_tests COMMAND ln_theme_tests)

  # Réticule de ciblage au sol (cercles + cône de vision 120°) : géométrie
  # analytique, paramétrage target_reticle.*, bake texture et cycle de fondu —
  # logique pure CPU (TargetReticleGeometry.cpp est fourni par engine_core,
  # aucune dépendance Vulkan dans le test).
  lcdlln_add_simple_test(target_reticle_tests
    ${CMAKE_SOURCE_DIR}/src/client/combat/TargetReticleTests.cpp)

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

  # Wave 22 (CMANGOS.15 + CMANGOS.17 step 2) : Groups master-side +
  # LootRule 4 impls (FFA, RoundRobin, MasterLooter, NeedBeforeGreed).
  # Header-only (Group + GroupManager + ILootRule + concrete impls).
  lcdlln_add_simple_test(group_manager_tests
    ${CMAKE_SOURCE_DIR}/src/masterd/Groups/GroupManagerTests.cpp)
  lcdlln_add_simple_test(loot_rule_tests
    ${CMAKE_SOURCE_DIR}/src/shardd/loot/LootRuleTests.cpp)

  # CMANGOS.11 (Phase 3.11a) : ThreatList AI target selector (header-only).
  lcdlln_add_simple_test(threat_list_tests
    ${CMAKE_SOURCE_DIR}/src/shardd/combat/ThreatListTests.cpp)
  # Wave 19 (CMANGOS.11 step 2) : HostileRefManager bidirectionnel (cibles +
  # attaquants) avec helpers EngageHostile / DisengageHostile / CleanupOnDeath.
  lcdlln_add_simple_test(hostile_ref_manager_tests
    ${CMAKE_SOURCE_DIR}/src/shardd/combat/HostileRefManagerTests.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/combat/HostileRefManager.cpp)
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
  # Wave 23 (CMANGOS.26 step 2) : SpellMgr + Spell state machine + Aura
  # tick/stack + ProcMgr event dispatch (header-only, etend SpellFamily).
  lcdlln_add_simple_test(spell_mgr_tests
    ${CMAKE_SOURCE_DIR}/src/shardd/spell/SpellMgrTests.cpp)
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

  # Wave 18 (CMANGOS.03 step 2) : GridVisitor pattern (template free function)
  # parcourt l'AoI 7x7 cells autour d'une position.
  add_executable(grid_visitor_tests
    ${CMAKE_SOURCE_DIR}/src/shardd/world/GridVisitorTests.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/world/SpatialPartition.cpp
  )
  target_include_directories(grid_visitor_tests PRIVATE ${CMAKE_SOURCE_DIR})
  target_link_libraries(grid_visitor_tests PRIVATE engine_core spdlog::spdlog pthread)
  target_compile_options(grid_visitor_tests PRIVATE -Wall -Wextra -Wpedantic)
  add_test(NAME grid_visitor_tests COMMAND grid_visitor_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

  # Wave 18 (CMANGOS.03 step 2) : GridNotifier foncteur Visitor avec filter
  # predicate (utilise pour broadcasts localises -- collecte recipients).
  add_executable(grid_notifier_tests
    ${CMAKE_SOURCE_DIR}/src/shardd/world/GridNotifierTests.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/world/GridNotifier.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/world/SpatialPartition.cpp
  )
  target_include_directories(grid_notifier_tests PRIVATE ${CMAKE_SOURCE_DIR})
  target_link_libraries(grid_notifier_tests PRIVATE engine_core spdlog::spdlog pthread)
  target_compile_options(grid_notifier_tests PRIVATE -Wall -Wextra -Wpedantic)
  add_test(NAME grid_notifier_tests COMMAND grid_notifier_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

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

  # Sous-projet A (skinning/animation) etape 2/17 : Quat + slerp pour le runtime
  # d'animation. Quat.cpp est compile dans engine_core (CMakeLists racine).
  lcdlln_add_simple_test(quat_tests
    ${CMAKE_SOURCE_DIR}/src/client/render/skinned/tests/QuatTests.cpp)

  # Sous-projet A (skinning/animation) etape 3/17 : Skeleton + Bone (plain-data,
  # bones en ordre topologique parent-avant-enfant).
  # Skeleton.cpp est compile dans engine_core (CMakeLists racine).
  lcdlln_add_simple_test(skeleton_tests
    ${CMAKE_SOURCE_DIR}/src/client/render/skinned/tests/SkeletonTests.cpp)

  # Sous-projet A (skinning/animation) etape 4/17 : AnimationClip + helpers
  # d'interpolation de keyframes (lerp Vec3, slerp Quat, clamp hors-plage).
  # AnimationClip.cpp est compile dans engine_core (CMakeLists racine).
  lcdlln_add_simple_test(animation_clip_tests
    ${CMAKE_SOURCE_DIR}/src/client/render/skinned/tests/AnimationClipTests.cpp)

  # Sous-projet A (skinning/animation) etape 5/17 : AnimationSampler::SamplePose.
  # Compose les keyframes (translation/rotation/scale) en matrices locales TRS,
  # avec fallback bindLocal pour les bones sans piste. Tasks 6 et 7 ajouteront
  # ComputeGlobalMatrices et ComputeFinalMatrices a la meme classe.
  # AnimationSampler.cpp est compile dans engine_core (CMakeLists racine).
  lcdlln_add_simple_test(animation_sampler_tests
    ${CMAKE_SOURCE_DIR}/src/client/render/skinned/tests/AnimationSamplerTests.cpp)

  # Sous-projet A (skinning/animation) etape 10/17 : SkinnedMeshLoader::LoadCpuOnlyForTests.
  # Parse un .glb via cgltf et produit SkinnedMeshCpuData (Skeleton + vertices + indices + clips).
  # Le test charge le vrai fixture game/data/models/avatars/y_bot/y_bot.glb et vérifie les
  # invariants (squelette topo-ordered, mesh non vide, clip mixamo trouvée, poids normalisés).
  # SkinnedMeshLoader.cpp est compile dans engine_core (CMakeLists racine).
  lcdlln_add_simple_test(skinned_mesh_loader_tests
    ${CMAKE_SOURCE_DIR}/src/client/render/skinned/tests/SkinnedMeshLoaderTests.cpp)

  # Routage de materiaux peau/habit de l'avatar (fonction pure, sans Vulkan).
  # AvatarMaterialRouting.cpp est compile dans engine_core (CMakeLists racine).
  lcdlln_add_simple_test(avatar_material_routing_tests
    ${CMAKE_SOURCE_DIR}/src/client/render/skinned/tests/AvatarMaterialRoutingTests.cpp)

  # Chargeur de mesh glTF statique (props, chantier B). StaticMeshLoader.cpp est
  # compile dans engine_core ; cgltf y est resolu via SkinnedMeshLoader.cpp (CGLTF_IMPLEMENTATION).
  lcdlln_add_simple_test(static_mesh_loader_tests
    ${CMAKE_SOURCE_DIR}/src/client/render/static_mesh/tests/StaticMeshLoaderTests.cpp)

  # Sous-projet B.1 (locomotion state machine) etape 1/14 : Mat4 helpers.
  # Identity / Translate / RotateY utilises par la state machine pour composer
  # T(pos) * R_y(yaw) par frame (model matrix de l'avatar).
  # Math.h est header-only, pas de .cpp a compiler.
  lcdlln_add_simple_test(mat4_helpers_tests
    ${CMAKE_SOURCE_DIR}/src/shared/math/tests/Mat4HelpersTests.cpp)

  # Sous-projet B.1 (locomotion state machine) etape 3/14 : AnimationCrossfade.
  # Tests du blend lineaire TRS entre 2 clips (lerp/slerp/lerp) sur 0.15s.
  lcdlln_add_simple_test(animation_crossfade_tests
    ${CMAKE_SOURCE_DIR}/src/client/render/skinned/tests/AnimationCrossfadeTests.cpp)

  # Sous-projet B.1 (locomotion state machine) etape 6/14 : TerrainCollider.
  # Tests du fallback "no terrain bound" (GroundHeightAt = 0, SweepCapsule
  # detection traversee Y=0) + edge cases (sweep ascendant, sweep horizontal).
  # Le comportement "avec terrain" est valide au smoke test visuel (Task 13)
  # car instancier un TerrainRenderer requiert un VkDevice.
  lcdlln_add_simple_test(terrain_collider_tests
    ${CMAKE_SOURCE_DIR}/src/client/gameplay/tests/TerrainColliderTests.cpp)

  # Saut realiste : verifie l'apex (~0.60 m) du CharacterController apres le
  # passage jumpSpeed 9.0 -> 4.9. Garde anti-regression (pas de saut ~2 m).
  lcdlln_add_simple_test(character_controller_jump_tests
    ${CMAKE_SOURCE_DIR}/src/client/gameplay/tests/CharacterControllerJumpTests.cpp)

  # Collision props : CompositeWorldCollider (terrain + cylindres verticaux).
  # Verifie sweep capsule-vs-cylindre (traversee bloquee, a-cote ignore, hors
  # bornes Y ignore, delegation QueryWater au terrain).
  lcdlln_add_simple_test(composite_world_collider_tests
    ${CMAKE_SOURCE_DIR}/src/client/gameplay/tests/CompositeWorldColliderTests.cpp)

  # Catalogue de collision des bâtiments : parse JSON (via Config), passable,
  # multi-boîtes, mesh absent -> nullptr, JSON invalide -> echec.
  lcdlln_add_simple_test(building_collision_catalog_tests
    ${CMAKE_SOURCE_DIR}/src/client/world/BuildingCollisionCatalog.cpp
    ${CMAKE_SOURCE_DIR}/src/client/world/tests/BuildingCollisionCatalogTests.cpp)

  # Anti-occlusion caméra : module math pur CameraOcclusionFade (fondu tramé
  # par occulteur + lissage temporel + purge). 9 cas : cœur, derrière joueur,
  # derrière caméra, latéral, transition, garde joueur, lissage, purge, id inconnu.
  lcdlln_add_simple_test(camera_occlusion_fade_tests
    ${CMAKE_SOURCE_DIR}/src/client/render/tests/CameraOcclusionFadeTests.cpp
    ${CMAKE_SOURCE_DIR}/src/client/render/CameraOcclusionFade.cpp)

  # Lot 5 (anti-vol step-up) : un mur (prop normal) n'est PAS escaladable
  # (step-up plafonne a maxStep) alors qu'un escalier (PropCylinder::stair) reste
  # gravissable (step-up jusqu'a maxClimb). Integre controller + collider a 60 Hz.
  lcdlln_add_simple_test(character_controller_stepup_tests
    ${CMAKE_SOURCE_DIR}/src/client/gameplay/tests/CharacterControllerStepUpTests.cpp)

  # Sous-projet C MVP : RaceDefinition.meshPath parsing.
  # Verifie que les 3 races MVP (humains, nains, orcs) exposent un meshPath
  # non vide depuis races.json, et que les races hors-MVP exposent une
  # string vide (fallback handle par Engine::GetRaceMesh a EnterWorld).
  # Note : CharacterCreationUi.cpp est deja linke via engine_core (cf.
  # CMakeLists racine ligne 241), donc ne pas le re-ajouter ici sous peine
  # d'erreur "multiple definition" au link.
  lcdlln_add_simple_test(race_definition_tests
    ${CMAKE_SOURCE_DIR}/src/client/character_creation/tests/RaceDefinitionTests.cpp)

  # CHAR-MODEL.25 : systeme de customisation de personnage.
  # Charge game/data/configuration/races/*.json (alignes sur races.json),
  # valide/genere des customisations, resout en assets concrets.
  # Note : CharacterCustomizationSystem.cpp est deja linke via engine_core
  # (CMakeLists racine), donc ne pas le re-ajouter ici (multiple definition).
  lcdlln_add_simple_test(character_customization_tests
    ${CMAKE_SOURCE_DIR}/src/client/character_creation/tests/CharacterCustomizationTests.cpp)

  # Système de personnages PR2 : factions (faction → classes + race) chargées
  # depuis game/data/races/factions.json. Valide 10 factions (9 sélectionnables
  # + empire_hynn masquée), classes distinctes, mapping faction→race.
  # Note : CharacterCreationUi.cpp est déjà linké via engine_core (CMakeLists
  # racine), donc ne pas le re-ajouter ici (multiple definition).
  lcdlln_add_simple_test(faction_definition_tests
    ${CMAKE_SOURCE_DIR}/src/client/character_creation/tests/FactionDefinitionTests.cpp)

  # Dialogue PNJ : modele d'arbre, presenter (navigation/choix/distance/auto-scroll),
  # journal de conversation. DialogueTree.cpp, DialoguePresenter.cpp,
  # QuestConversationJournal.cpp, DialogueConfigLoader.cpp sont deja linkes via
  # engine_core (CMakeLists racine), donc ne pas les re-ajouter ici (multiple definition).
  lcdlln_add_simple_test(dialogue_presenter_tests
    ${CMAKE_SOURCE_DIR}/src/client/dialogue/tests/DialoguePresenterTests.cpp)

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

  # Nuages volumetriques (client) : CloudParams (struct pure, interpolation) +
  # CloudWeatherMapper (WeatherState -> CloudParams). Tests header-only / pur
  # CPU ; CloudWeatherMapper.cpp est fourni par engine_core (lie par le helper).
  lcdlln_add_simple_test(cloud_params_tests
    ${CMAKE_SOURCE_DIR}/src/client/render/clouds/CloudParamsTests.cpp)
  lcdlln_add_simple_test(cloud_weather_mapper_tests
    ${CMAKE_SOURCE_DIR}/src/client/render/clouds/CloudWeatherMapperTests.cpp)
  # WeatherKindMap : conversion WeatherKind serveur (wire 156) -> WeatherState client.
  lcdlln_add_simple_test(weather_kind_map_tests
    ${CMAKE_SOURCE_DIR}/src/client/render/clouds/WeatherKindMapTests.cpp)

  # CMANGOS.01 (Phase 2.01b) : ChatCommandRouter pure tests
  add_executable(chat_command_router_tests
    ${CMAKE_SOURCE_DIR}/src/masterd/chat/ChatCommandRouterTests.cpp
    ${CMAKE_SOURCE_DIR}/src/masterd/chat/ChatCommandRouter.cpp
    ${CMAKE_SOURCE_DIR}/src/masterd/account/AccountRoleService.cpp
    ${CMAKE_SOURCE_DIR}/src/masterd/account/AccountRole.cpp
    ${CMAKE_SOURCE_DIR}/src/masterd/account/InMemoryAccountStore.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/account/AccountValidation.cpp
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
    ${CMAKE_SOURCE_DIR}/src/shared/network/PacketLog.cpp
  )
  target_include_directories(shard_ticket_tests PRIVATE ${CMAKE_SOURCE_DIR})
  target_link_libraries(shard_ticket_tests PRIVATE engine_core OpenSSL::SSL)
  target_compile_options(shard_ticket_tests PRIVATE -Wall -Wextra -Wpedantic)
  add_test(NAME shard_ticket_tests COMMAND shard_ticket_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

  # Audit F8 : verrou anti-brute-force sur PasswordResetHandler::HandleVerifyEmail
  # (code de vérification e-mail à 6 chiffres). Passe par HandlePacket() (point
  # d'entrée public) avec un NetServer jamais Init() — Send() y est un no-op sûr.
  add_executable(password_reset_handler_tests
    ${CMAKE_SOURCE_DIR}/src/masterd/handlers/password/PasswordResetHandlerTests.cpp
    ${CMAKE_SOURCE_DIR}/src/masterd/handlers/password/PasswordResetHandler.cpp
    ${CMAKE_SOURCE_DIR}/src/masterd/handlers/password/PasswordResetStore.cpp
    ${CMAKE_SOURCE_DIR}/src/masterd/account/InMemoryAccountStore.cpp
    ${CMAKE_SOURCE_DIR}/src/masterd/email/SmtpMailer.cpp
    ${CMAKE_SOURCE_DIR}/src/masterd/email/LocalizedEmail.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/security/RateLimitAndBan.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/security/SecurityAuditLog.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/network/NetServer.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/network/PacketLog.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/network/AuthRegisterPayloads.cpp
  )
  target_include_directories(password_reset_handler_tests PRIVATE ${CMAKE_SOURCE_DIR})
  target_link_libraries(password_reset_handler_tests PRIVATE engine_core engine_auth OpenSSL::SSL pthread)
  target_compile_options(password_reset_handler_tests PRIVATE -Wall -Wextra -Wpedantic)
  add_test(NAME password_reset_handler_tests COMMAND password_reset_handler_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

  # Présence enrichie (v9) : round-trip du payload SHARD_HEARTBEAT avec tableau joueurs.
  add_executable(shard_payloads_tests
    ${CMAKE_SOURCE_DIR}/src/shared/network/ShardPayloadsTests.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/network/ShardPayloads.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/network/ByteReader.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/network/ByteWriter.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/network/PacketBuilder.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/network/PacketLog.cpp
  )
  target_include_directories(shard_payloads_tests PRIVATE ${CMAKE_SOURCE_DIR})
  target_link_libraries(shard_payloads_tests PRIVATE engine_core)
  target_compile_options(shard_payloads_tests PRIVATE -Wall -Wextra -Wpedantic)
  add_test(NAME shard_payloads_tests COMMAND shard_payloads_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

  # Audit F3 : tag HMAC-SHA256 préfixe authentifiant SHARD_REGISTER/HEARTBEAT.
  add_executable(shard_wire_auth_tests
    ${CMAKE_SOURCE_DIR}/src/shared/network/ShardWireAuthTests.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/network/ShardWireAuth.cpp
  )
  target_include_directories(shard_wire_auth_tests PRIVATE ${CMAKE_SOURCE_DIR})
  target_link_libraries(shard_wire_auth_tests PRIVATE OpenSSL::SSL)
  target_compile_options(shard_wire_auth_tests PRIVATE -Wall -Wextra -Wpedantic)
  add_test(NAME shard_wire_auth_tests COMMAND shard_wire_auth_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

  # Audit F6 : refus au boot d'un secret HMAC partagé faible/par défaut (masterd + shardd).
  add_executable(shared_secret_policy_tests
    ${CMAKE_SOURCE_DIR}/src/shared/security/SharedSecretPolicyTests.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/security/SharedSecretPolicy.cpp
  )
  target_include_directories(shared_secret_policy_tests PRIVATE ${CMAKE_SOURCE_DIR})
  target_compile_options(shared_secret_policy_tests PRIVATE -Wall -Wextra -Wpedantic)
  add_test(NAME shared_secret_policy_tests COMMAND shared_secret_policy_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

  # Présence unifiée (Niveau 1) : ShardPresenceService — présence shard-locale.
  add_executable(shard_presence_service_tests
    ${CMAKE_SOURCE_DIR}/src/shardd/world/ShardPresenceServiceTests.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/world/ShardPresenceService.cpp
  )
  target_include_directories(shard_presence_service_tests PRIVATE ${CMAKE_SOURCE_DIR})
  target_link_libraries(shard_presence_service_tests PRIVATE pthread)
  target_compile_options(shard_presence_service_tests PRIVATE -Wall -Wextra -Wpedantic)
  add_test(NAME shard_presence_service_tests COMMAND shard_presence_service_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

  # Présence unifiée (Niveau 2) : ShardPlayerPresenceCache — agrégat master.
  add_executable(shard_player_presence_cache_tests
    ${CMAKE_SOURCE_DIR}/src/masterd/shards/ShardPlayerPresenceCacheTests.cpp
    ${CMAKE_SOURCE_DIR}/src/masterd/shards/ShardPlayerPresenceCache.cpp
  )
  target_include_directories(shard_player_presence_cache_tests PRIVATE ${CMAKE_SOURCE_DIR})
  target_link_libraries(shard_player_presence_cache_tests PRIVATE pthread)
  target_compile_options(shard_player_presence_cache_tests PRIVATE -Wall -Wextra -Wpedantic)
  add_test(NAME shard_player_presence_cache_tests COMMAND shard_player_presence_cache_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

  # M22.5: Server list and shard selection policy tests
  add_executable(server_list_policy_tests
    ${CMAKE_SOURCE_DIR}/src/masterd/handlers/shard/ServerListPolicyTests.cpp
    ${CMAKE_SOURCE_DIR}/src/masterd/shards/ShardRegistry.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/network/ServerListPayloads.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/network/ByteReader.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/network/ByteWriter.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/network/PacketBuilder.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/network/PacketLog.cpp
  )
  target_include_directories(server_list_policy_tests PRIVATE ${CMAKE_SOURCE_DIR})
  target_link_libraries(server_list_policy_tests PRIVATE engine_core)
  target_compile_options(server_list_policy_tests PRIVATE -Wall -Wextra -Wpedantic)
  add_test(NAME server_list_policy_tests COMMAND server_list_policy_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

  # Sécurité (audit F9) : AccountValidation — normalisation/validation e-mail, login, mot de passe.
  # Pure (pas de réseau, pas de DB) : dépend uniquement de NetErrorCode.h (header-only).
  add_executable(account_validation_tests
    ${CMAKE_SOURCE_DIR}/src/shared/account/AccountValidationTests.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/account/AccountValidation.cpp
  )
  target_include_directories(account_validation_tests PRIVATE ${CMAKE_SOURCE_DIR})
  target_compile_options(account_validation_tests PRIVATE -Wall -Wextra -Wpedantic)
  add_test(NAME account_validation_tests COMMAND account_validation_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

  # Wave 12: PacketLog ring buffer (debug RX/TX trace, opt-in) — pure unit tests.
  # Aucun lien sur engine_core ni OpenSSL : PacketLog est autonome (std + threads).
  # Module dans engine::server::netdebug (src/shared/network/PacketLog).
  # Wave 13 a supprime l'ancien stub header-only `engine::server::packetlog`
  # (src/shared/packetlog/) qui n'avait jamais ete utilise hors de son propre test.
  add_executable(packet_log_tests
    ${CMAKE_SOURCE_DIR}/src/shared/network/PacketLogTests.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/network/PacketLog.cpp
  )
  target_include_directories(packet_log_tests PRIVATE ${CMAKE_SOURCE_DIR})
  target_link_libraries(packet_log_tests PRIVATE pthread)
  target_compile_options(packet_log_tests PRIVATE -Wall -Wextra -Wpedantic)
  add_test(NAME packet_log_tests COMMAND packet_log_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

  # M22.6: Shard server (ticket handshake + enregistrement TLS/TCP vers le Master via ShardToMasterClient)
  # Wave 6: + EventAIRuntime + PoolManagerRuntime (tickes + seedes au boot).
  # Wave 8: + ThreatListRuntime + DBScriptRuntime (idem).
  # Wave 9: + AntiCheatGameplayRuntime + SpellFamilyRuntime + InstanceManagerRuntime.
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
    ${CMAKE_SOURCE_DIR}/src/shared/network/PacketLog.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/ai/EventAI.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/ai/EventAIRuntime.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/pools/PoolManagerRuntime.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/combat/ThreatListRuntime.cpp
    # Wave 19 : HostileRefManager bidirectionnel (cibles + attaquants).
    ${CMAKE_SOURCE_DIR}/src/shardd/combat/HostileRefManager.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/dbscripts/DBScript.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/dbscripts/DBScriptRuntime.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/anticheat/AntiCheatGameplayRuntime.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/spell/SpellFamilyRuntime.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/maps/InstanceManagerRuntime.cpp
    # Lot C (vague 5) : cablage boot des managers globals data-driven (SHARD-ONLY,
    # namespace engine::server::shard::globals). Charges au boot depuis les tables
    # de la migration 0042 (conditions/graveyards/locale_strings) si la DB est
    # configuree. NE PAS ajouter a server_app (master) : symboles shard uniquement.
    # ConnectionPool/DbHelpers/SqlPreparedStatement sont deja linkes plus bas (TA.4).
    ${CMAKE_SOURCE_DIR}/src/shardd/internals/globals/ConditionMgr.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/internals/globals/GraveyardManager.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/internals/globals/LocaleStrings.cpp
    # TA.2 (replication des joueurs) : replication AoI executable sous Linux.
    # On ajoute ServerApp + la grille AoI + le transport UDP (porte POSIX en TA.1)
    # + les systemes gameplay que ServerApp instancie. On N'AJOUTE PAS ce que
    # engine_core fournit deja (verifie) : ServerProtocol, ChatSystem, ChatEmotes,
    # Config, Log, LogConfig, FileSystem → NE PAS les rajouter (doublons de symboles,
    # shard_app linke engine_core). En revanche SecurityAuditLog n'est PAS dans
    # engine_core (uniquement la cible security_tests) et ServerApp l'utilise
    # (membre m_moderationAuditLog) → on l'ajoute ici.
    ${CMAKE_SOURCE_DIR}/src/shared/security/SecurityAuditLog.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/server_bootstrap/ServerApp.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/world/UdpTransport.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/world/AdmittedCharacterRegistry.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/world/ShardPresenceService.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/world/TickScheduler.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/world/SpatialPartition.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/world/GridState.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/world/GridNotifier.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/world/ZoneTransitions.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/gameplay/chat/ChatCommandParser.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/gameplay/social/FriendSystem.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/gameplay/social/PartySystem.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/gameplay/guild/GuildSystem.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/gameplay/guild/GuildTabard.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/gameplay/guild/GuildBank.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/gameplay/character/CharacterPersistence.cpp
    # Character system PR1 — moteur de stats (serveur-only, anti-triche).
    ${CMAKE_SOURCE_DIR}/src/shardd/gameplay/character/CharacterStatsTables.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/gameplay/character/CharacterStatsEngine.cpp
    # Level-up runtime — helper de progression d'XP/niveau (pur, server-only).
    ${CMAKE_SOURCE_DIR}/src/shardd/gameplay/character/LevelProgression.cpp
    # Character system R1-A — résolveur PV enter-world (appelé par HandleHello).
    ${CMAKE_SOURCE_DIR}/src/shardd/gameplay/character/SpawnStatsResolver.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/gameplay/economy/CurrencyConfig.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/gameplay/economy/VendorCatalog.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/gameplay/economy/PlayerWalletService.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/gameplay/trade/TradeSystem.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/gameplay/gathering/GatheringSystem.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/gameplay/crafting/CraftingSystem.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/gameplay/auction/AuctionHouse.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/gameplay/quest/QuestRuntime.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/gameplay/spawner/SpawnerRuntime.cpp
    # Combat SP1 — catalogue d'archétypes de créatures (stats data-driven).
    ${CMAKE_SOURCE_DIR}/src/shardd/gameplay/creature/CreatureArchetypeLibrary.cpp
    # Combat SP3 — kits de sorts par profil (data-driven).
    ${CMAKE_SOURCE_DIR}/src/shardd/gameplay/spell/SpellKitLibrary.cpp
    # SP-A — catalogue strict des compétences par-classe (serveur).
    ${CMAKE_SOURCE_DIR}/src/shardd/gameplay/spell/ClassSkillLibrary.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/world/LagCompensation.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/gameplay/event/EventRuntime.cpp
    # TA.4 : accès MySQL pour le pont position (lecture characters.spawn_x/y/z).
    ${CMAKE_SOURCE_DIR}/src/shared/db/ConnectionPool.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/db/DbHelpers.cpp
    ${CMAKE_SOURCE_DIR}/src/shared/db/SqlPreparedStatement.cpp
  )
  # TA.2 link en mode FICHIER (sans MySQL), strictement la config gameplay prouvee
  # linkable du bloc WIN32 `server_app`. CharacterPersistence est fichier-only
  # (aucun acces a la table characters) → activer ENGINE_HAS_MYSQL ici n'apporterait
  # pas le pont position et basculerait FriendSystem & co. en backend DB (friction
  # de link inutile a ce stade). Le branchement MySQL (T0.2) qui fait reellement le
  # pont position (spawn_x/y/z) est introduit en **TA.4**.
  # TA.4 : T0.2 = MySQL — le shard lit characters.spawn_x/y/z (même base que le master).
  # SHARD_POSITION_DB (et NON ENGINE_HAS_MYSQL) : n'active que le pont position dans
  # ServerApp, sans basculer les autres systèmes gameplay du shard en mode DB (ils restent
  # en mode fichier, config TA.2 prouvée linkable).
  target_include_directories(shard_app PRIVATE ${CMAKE_SOURCE_DIR} ${MYSQL_INCLUDE_DIR} ${LCDLLN_GEN_DIR})
  # Character system PR1 — accès aux headers JSON embarqués (anti-triche).
  add_dependencies(shard_app lcdlln_gen_character_data)
  target_compile_definitions(shard_app PRIVATE SHARD_POSITION_DB=1)
  target_link_libraries(shard_app PRIVATE engine_core OpenSSL::SSL pthread ${MYSQL_LIBRARY})
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
