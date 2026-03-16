cmake_minimum_required(VERSION 3.20)
project(LCDLLN_Engine LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Force /MD (DLL CRT) pour toutes les configurations MSVC
if(MSVC)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")
endif()

find_package(Vulkan REQUIRED)
find_package(glfw3 CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)

# Vulkan Memory Allocator (VMA): centralised GPU allocations to avoid maxMemoryAllocationCount limit.
# Prefer vendored copy (submodule or CI cache) so build works without network; else FetchContent.
set(VMA_INCLUDE_DIRS "")
if(EXISTS "${CMAKE_SOURCE_DIR}/external/VulkanMemoryAllocator/include/vk_mem_alloc.h")
  get_filename_component(VMA_VENDORED_DIR "${CMAKE_SOURCE_DIR}/external/VulkanMemoryAllocator/include" ABSOLUTE)
  set(VMA_INCLUDE_DIRS "${VMA_VENDORED_DIR}")
  message(STATUS "VMA: using vendored external/VulkanMemoryAllocator")
else()
  include(FetchContent)
  FetchContent_Declare(VMA
    GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator
    GIT_TAG        v3.0.1
  )
  FetchContent_Populate(VMA)
  get_filename_component(VMA_DIR_ABS "${vma_SOURCE_DIR}" ABSOLUTE)
  set(VMA_INCLUDE_DIRS "${VMA_DIR_ABS}/include" "${VMA_DIR_ABS}")
  message(STATUS "VMA: using FetchContent at ${VMA_DIR_ABS}")
endif()

# M20.2 — Argon2 (phc-winner-argon2 reference implementation)
include(FetchContent)
FetchContent_Declare(phc-winner-argon2
  GIT_REPOSITORY https://github.com/P-H-C/phc-winner-argon2
  GIT_TAG        20190702
)
FetchContent_Populate(phc-winner-argon2)
set(ARGON2_SRC_DIR "${phc-winner-argon2_SOURCE_DIR}/src")
add_library(argon2_ref STATIC
  ${ARGON2_SRC_DIR}/argon2.c
  ${ARGON2_SRC_DIR}/core.c
  ${ARGON2_SRC_DIR}/ref.c
  ${ARGON2_SRC_DIR}/encoding.c
  ${ARGON2_SRC_DIR}/thread.c
  ${ARGON2_SRC_DIR}/blake2/blake2b.c
)
target_include_directories(argon2_ref PUBLIC
  "${phc-winner-argon2_SOURCE_DIR}/include"
  "${phc-winner-argon2_SOURCE_DIR}/src"
)
set_target_properties(argon2_ref PROPERTIES C_STANDARD 99)
if(WIN32)
  target_compile_definitions(argon2_ref PRIVATE ARGON2_NO_THREADS)
endif()
message(STATUS "M20.2: Argon2 using ref implementation at ${phc-winner-argon2_SOURCE_DIR}")

# M23.4 — spdlog for rotating/daily logger (runtime + security_audit)
FetchContent_Declare(spdlog
  GIT_REPOSITORY https://github.com/gabime/spdlog
  GIT_TAG        v1.12.0
)
set(SPDLOG_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_TESTS  OFF CACHE BOOL "" FORCE)
set(_saved_BUILD_SHARED_LIBS ${BUILD_SHARED_LIBS})
set(BUILD_SHARED_LIBS OFF)
FetchContent_MakeAvailable(spdlog)
set(BUILD_SHARED_LIBS ${_saved_BUILD_SHARED_LIBS})
message(STATUS "M23.4: spdlog for logging rotation")

add_library(engine_auth STATIC engine/auth/Argon2Hash.cpp)
target_include_directories(engine_auth PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(engine_auth PUBLIC argon2_ref OpenSSL::SSL)
if(MSVC)
  target_compile_options(engine_auth PRIVATE /W4 /permissive- /Zc:preprocessor)
endif()

set_source_files_properties(engine/render/vk/VmaAllocator.cpp PROPERTIES
    COMPILE_DEFINITIONS "VMA_IMPLEMENTATION;VMA_STATIC_VULKAN_FUNCTIONS=0;VMA_DYNAMIC_VULKAN_FUNCTIONS=0"
)

add_library(engine_core
  engine/render/vk/VmaAllocator.cpp
  engine/render/vk/VkUtils.cpp
  engine/core/Config.cpp
  engine/core/Profiler.cpp
  engine/core/jobs/JobSystem.cpp
  engine/core/Log.cpp
  engine/core/Time.cpp
  engine/audio/AudioEngine.cpp
  engine/client/CombatHud.cpp
  engine/client/FXManager.cpp
  engine/client/InventoryUi.cpp
  engine/client/ProfilerHud.cpp
  engine/client/QuestUi.cpp
  engine/client/ThemeManager.cpp
  engine/client/UIModel.cpp
  engine/editor/EditorMode.cpp
  engine/Engine.cpp
  engine/platform/FileSystem.cpp
  engine/platform/FileWatcher.cpp
  engine/render/vk/VkInstance.cpp
  engine/render/vk/VkDeviceContext.cpp
  engine/render/vk/VkSwapchain.cpp
  engine/render/vk/VkFrameSync.cpp
  engine/render/FrameGraph.cpp
  engine/render/AssetRegistry.cpp
  engine/render/Material.cpp
  engine/render/ShaderCompiler.cpp
  engine/render/ShaderCache.cpp
  engine/render/ShaderHotReload.cpp
  engine/render/Camera.cpp
  engine/render/DecalSystem.cpp
  engine/render/DecalPass.cpp
  engine/render/ParticleSystem.cpp
  engine/render/ParticleBillboardPass.cpp
  engine/render/CascadedShadowMaps.cpp
  engine/render/BrdfLutPass.cpp
  engine/render/SpecularPrefilterPass.cpp
  engine/render/SsaoKernelNoise.cpp
  engine/render/SsaoPass.cpp
  engine/render/SsaoBlurPass.cpp
  engine/render/ShadowMapPass.cpp
  engine/render/PsoKey.cpp
  engine/render/PipelineCache.cpp
  engine/render/GeometryPass.cpp
  engine/render/GpuDrivenCullingPass.cpp
  engine/render/HiZPyramidPass.cpp
  engine/render/LightingPass.cpp
  engine/render/TonemapPass.cpp
  engine/render/BloomPass.cpp
  engine/render/AutoExposure.cpp
  engine/render/TaaPass.cpp
  engine/render/DeferredPipeline.cpp
  engine/math/Frustum.cpp
  engine/world/WorldModel.cpp
  engine/world/OutputVersion.cpp
  engine/world/ProbeData.cpp
  engine/world/PakReader.cpp
  engine/world/ChunkPackageLayout.cpp
  engine/world/StreamingScheduler.cpp
  engine/world/StreamCache.cpp
  engine/world/ChunkBudgetStats.cpp
  engine/render/vk/DeferredDestroyQueue.cpp
  engine/render/GpuUploadQueue.cpp
  engine/render/vk/StagingAllocator.cpp
  engine/world/LodConfig.cpp
  engine/world/HlodRuntime.cpp
  engine/network/ByteWriter.cpp
  engine/network/ByteReader.cpp
  engine/network/PacketBuilder.cpp
  engine/network/PacketView.cpp
  engine/network/RequestResponseDispatcher.cpp
  engine/network/ErrorPacket.cpp
  engine/network/AuthRegisterPayloads.cpp
  engine/network/ShardPayloads.cpp
)
target_include_directories(engine_core PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${VMA_INCLUDE_DIRS}
)
target_link_libraries(engine_core PUBLIC
  Vulkan::Vulkan
  glfw
  engine_auth
  spdlog::spdlog
)
if(WIN32)
  target_link_libraries(engine_core PRIVATE ws2_32 OpenSSL::SSL)
endif()
if(MSVC)
  target_compile_options(engine_core PRIVATE /W4 /permissive- /Zc:preprocessor)
else()
  target_compile_options(engine_core PRIVATE -Wall -Wextra -Wpedantic)
endif()
if(WIN32)
  target_sources(engine_core PRIVATE
    engine/platform/Input.cpp
    engine/platform/Window.cpp
    engine/network/NetClient.cpp
    engine/network/ShardToMasterClient.cpp
    engine/network/ServerListPayloads.cpp
    engine/network/ShardTicketPayloads.cpp
    engine/network/MasterShardClientFlow.cpp
  )
else()
  target_sources(engine_core PRIVATE
    engine/platform/Input_linux.cpp
    engine/platform/Window_linux.cpp
  )
endif()

add_executable(engine_app
  engine/app/main.cpp
)
set_target_properties(engine_app PROPERTIES
  OUTPUT_NAME "lcdlln"
  RUNTIME_OUTPUT_DIRECTORY         "${CMAKE_BINARY_DIR}/pkg/game"
  RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/pkg/game"
  RUNTIME_OUTPUT_DIRECTORY_DEBUG   "${CMAKE_BINARY_DIR}/pkg/game"
)
if(MSVC)
  target_compile_options(engine_app PRIVATE /Zc:preprocessor)
endif()
target_link_libraries(engine_app PRIVATE engine_core)
if(WIN32)
  target_link_libraries(engine_app PRIVATE user32)
endif()

# Copie les assets uniquement si le dossier existe dans le repo
if(EXISTS "${CMAKE_SOURCE_DIR}/assets")
  add_custom_command(TARGET engine_app POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
      "${CMAKE_SOURCE_DIR}/assets"
      "$<TARGET_FILE_DIR:engine_app>/assets"
    COMMENT "Copie des assets..."
  )
endif()

# Copie game/data (contenu) à côté de l'exécutable si présent
if(EXISTS "${CMAKE_SOURCE_DIR}/game/data")
  add_custom_command(TARGET engine_app POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/game/data"
            "$<TARGET_FILE_DIR:engine_app>/game/data"
    COMMENT "Copie du dossier game/data..."
  )
endif()

# Copie config.json à côté de l'exécutable si présent
if(EXISTS "${CMAKE_SOURCE_DIR}/config.json")
  add_custom_command(TARGET engine_app POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/config.json"
            "$<TARGET_FILE_DIR:engine_app>/config.json"
    COMMENT "Copie de config.json..."
  )
endif()

# M19.2: Protocol v1 serialization unit tests
add_executable(protocol_v1_tests engine/network/ProtocolV1Tests.cpp)
target_link_libraries(protocol_v1_tests PRIVATE engine_core)
enable_testing()
add_test(NAME protocol_v1_tests COMMAND protocol_v1_tests)

# M23.4: Log rotation smoke test (runtime logger)
add_executable(log_rotation_smoke_tests engine/core/LogRotationSmokeTest.cpp)
target_link_libraries(log_rotation_smoke_tests PRIVATE engine_core)
if(MSVC)
  target_compile_options(log_rotation_smoke_tests PRIVATE /W4 /permissive- /Zc:preprocessor)
endif()
add_test(NAME log_rotation_smoke_tests COMMAND log_rotation_smoke_tests)

# M20.2: Argon2 hashing wrapper unit tests (engine_auth first so linker resolves Log from engine_core)
add_executable(argon2_hash_tests engine/auth/Argon2HashTests.cpp)
target_link_libraries(argon2_hash_tests PRIVATE engine_auth engine_core)
add_test(NAME argon2_hash_tests COMMAND argon2_hash_tests)

# M20.3: Session manager state transitions unit tests
add_executable(session_manager_tests engine/server/SessionManagerTests.cpp engine/server/SessionManager.cpp)
target_include_directories(session_manager_tests PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(session_manager_tests PRIVATE engine_core)
if(MSVC)
  target_compile_options(session_manager_tests PRIVATE /W4 /permissive- /Zc:preprocessor)
endif()
add_test(NAME session_manager_tests COMMAND session_manager_tests)

# M20.4: Rate limiting, IP ban, audit log unit tests
add_executable(security_tests engine/server/SecurityTests.cpp engine/server/RateLimitAndBan.cpp engine/server/SecurityAuditLog.cpp)
target_include_directories(security_tests PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(security_tests PRIVATE engine_core)
if(MSVC)
  target_compile_options(security_tests PRIVATE /W4 /permissive- /Zc:preprocessor)
endif()
add_test(NAME security_tests COMMAND security_tests)

# M22.3: Shard down detection integration test (EvictStaleHeartbeats + callback)
add_executable(shard_down_detection_tests engine/server/ShardDownDetectionTests.cpp engine/server/ShardRegistry.cpp)
target_include_directories(shard_down_detection_tests PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(shard_down_detection_tests PRIVATE engine_core)
if(MSVC)
  target_compile_options(shard_down_detection_tests PRIVATE /W4 /permissive- /Zc:preprocessor)
endif()
add_test(NAME shard_down_detection_tests COMMAND shard_down_detection_tests)

# M22.6: Client flow app (AUTH → SERVER_LIST → TICKET → CONNECT SHARD). Windows only (NetClient).
if(WIN32)
  add_executable(client_flow_app engine/network/ClientFlowMain.cpp)
  target_link_libraries(client_flow_app PRIVATE engine_core)
  if(MSVC)
    target_compile_options(client_flow_app PRIVATE /W4 /permissive- /Zc:preprocessor)
  endif()
  set_target_properties(client_flow_app PROPERTIES
    OUTPUT_NAME "client_flow_app"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/pkg/client"
  )
endif()

# M09.4: HLOD builder (offline tool, no Vulkan)
add_subdirectory(tools/hlod_builder)
# M10.5: Zone builder (chunk packages: chunk.meta + geo/tex/instances/nav/probes)
add_subdirectory(tools/zone_builder)
# M21.2: Migration checksum tool (SHA-256 of db/migrations/*.sql)
add_subdirectory(tools/migration_checksum)
# M15.4: Server app (headless, no Vulkan)
add_subdirectory(engine/server)

# M19.10: Network integration tests (Linux only: start server, TLS connect, scenarios)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  find_package(OpenSSL REQUIRED)
  add_executable(network_integration_tests engine/network/NetworkIntegrationTests.cpp)
  target_link_libraries(network_integration_tests PRIVATE engine_core OpenSSL::SSL)
  add_dependencies(network_integration_tests server_app)
  add_test(NAME network_integration_tests COMMAND network_integration_tests pkg/server/lcdlln_server ${CMAKE_BINARY_DIR} WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
endif()