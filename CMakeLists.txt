cmake_minimum_required(VERSION 3.24)
project(MMORPGEngine VERSION 0.1.0 LANGUAGES CXX)

# ---------------------------------------------------------------------------
# C++ standard
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD          20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS        OFF)

# ---------------------------------------------------------------------------
# Dependencies (via vcpkg toolchain or system)
# ---------------------------------------------------------------------------
find_package(nlohmann_json REQUIRED)
find_package(glfw3         REQUIRED)   # M00.4 — Window/Input platform layer
find_package(Vulkan        REQUIRED)   # M01.1 — Vulkan instance / surface
find_package(SQLite3       REQUIRED)   # M14.4 — Persistence
# nlohmann_json already found for engine_core; engine_quest uses it

# ---------------------------------------------------------------------------
# engine_core static library (M00.1: Log, Time, Config)
#                             (M00.2: Memory — SystemAllocator, LinearArena)
#                             (M00.3: Jobs — JobSystem)
#                             (M00.5: Engine — game loop)
#                             (M01.1: Vulkan instance / surface wrapper)
#                             (M01.2: Vulkan device + queues)
#                             (M01.3: Vulkan swapchain + render pass + framebuffers)
#                             (M01.4: Vulkan frame resources + render loop)
#                             (M01.5: Shader management + hot reload)
#                             (M02.1: Frame Graph API + registry)
#                             (M02.2: Frame Graph compilation + topo order)
#                             (M02.3: Frame Graph Vulkan barriers + layout tracking)
#                             (M02.4: Offscreen SceneColor + copy to swapchain)
#                             (M03.0: Camera + frustum culling MVP)
#                             (M03.1: Deferred GBuffer + geometry pass)
#                             (M03.2: Deferred lighting pass + tonemap)
#                             (M03.3: Materials BaseColor/Normal/ORM + loader)
# ---------------------------------------------------------------------------
add_library(engine_core STATIC
    engine/core/Log.cpp
    engine/core/Time.cpp
    engine/core/Config.cpp
    engine/core/Engine.cpp

    # M09.1 — World model: Zone 4km, Chunk 256m, rings
    engine/world/World.cpp
    # M13.2 — Spatial partition: cells 64m, interest set
    engine/world/CellGrid.cpp
    engine/world/InterestSet.cpp
    # M11.2 — Zone builder format reader (chunk.meta + instances.bin)
    engine/world/ZoneBuildFormat.cpp
    # M11.3 — Navmesh/portals binary format reader
    engine/world/NavMeshFormat.cpp
    # M11.4 — Probes (probes.bin) + zone atmosphere format reader
    engine/world/ProbesFormat.cpp
    # M11.5 — Versioned header validation (bin/pak/meta)
    engine/world/VersionedHeader.cpp
    # M09.2 — Budgets par chunk (triangles/drawcalls) + instrumentation
    engine/world/ChunkStats.cpp
    engine/world/HlodRuntime.cpp
    # M10.1 — Streaming scheduler: requests + priorities
    engine/streaming/StreamingScheduler.cpp
    # M10.5 — Chunk packaging: .pak format + chunk.meta
    engine/streaming/PakReader.cpp
    engine/streaming/ChunkMeta.cpp
    # M10.3 — Cache: CPU LRU + GPU deferred free
    engine/streaming/LruCache.cpp
    engine/render/vk/DeferredDestroyQueue.cpp
    # M10.4 — GPU upload budget/frame + staging
    engine/render/vk/VkUploadBudget.cpp
    # M09.3 — LOD rules + instancing (props/foliage)
    engine/world/Lod.cpp
    engine/world/InstanceBatch.cpp

    # M01.1 — Vulkan instance / surface
    engine/render/vk/VkInstance.cpp

    # M01.2 — Vulkan device + queues
    engine/render/vk/VkDeviceContext.cpp

    # M01.3 — Vulkan swapchain + render pass + framebuffers
    engine/render/vk/VkSwapchain.cpp

    # M01.4 — Vulkan frame resources + render loop
    engine/render/vk/VkFrameResources.cpp

    # M01.5 — Shader management + hot reload
    engine/render/ShaderCompiler.cpp
    engine/render/ShaderCache.cpp
    engine/render/ShaderWatcher.cpp

    # M02.1 — Frame Graph API + registry
    engine/render/FrameGraph.cpp

    # M02.4 — Offscreen SceneColor + copy to swapchain
    engine/render/vk/VkSceneColor.cpp
    engine/render/vk/VkTaaHistory.cpp
    engine/render/vk/VkTaaOutput.cpp
    engine/render/vk/VkTaaPipeline.cpp

    # M03.1 — Deferred GBuffer + geometry pass
    engine/render/vk/VkGBuffer.cpp
    engine/render/vk/VkGeometryPipeline.cpp

    # M04.1 — CSM: splits + matrices + stabilization
    engine/render/Csm.cpp

    # M04.2 — Shadow pass: depth-only render per cascade
    engine/render/vk/VkShadowMap.cpp
    engine/render/vk/VkShadowPipeline.cpp

    # M03.2 — Deferred lighting pass fullscreen (PBR GGX) + tonemap
    engine/render/vk/VkSceneColorHDR.cpp
    engine/render/vk/VkLightingPipeline.cpp

    # M08.1 — Bloom: prefilter + downsample pyramid
    engine/render/vk/VkBloomPyramid.cpp
    engine/render/vk/VkBloomPrefilterPipeline.cpp
    engine/render/vk/VkBloomDownsamplePipeline.cpp
    # M08.2 — Bloom: upsample + combine HDR
    engine/render/vk/VkBloomUpsamplePipeline.cpp
    engine/render/vk/VkBloomCombineTarget.cpp
    engine/render/vk/VkBloomCombinePipeline.cpp
    engine/render/vk/VkExposureReduce.cpp

    # M05.1 — IBL: BRDF LUT (compute) generation
    engine/render/vk/VkBrdfLut.cpp

    # M05.2 — IBL: irradiance cubemap convolution
    engine/render/vk/VkIrradianceCubemap.cpp

    # M05.3 — IBL: prefiltered specular cubemap (mips)
    engine/render/vk/VkPrefilteredEnvCubemap.cpp

    # M06.1 — SSAO: kernel + noise generation
    engine/render/vk/VkSsaoKernelNoise.cpp
    # M06.2 — SSAO: generate pass (depth+normal)
    engine/render/vk/VkSsaoRaw.cpp
    engine/render/vk/VkSsaoPipeline.cpp
    # M06.3 — SSAO: bilateral blur (2 passes)
    engine/render/vk/VkSsaoBlur.cpp
    engine/render/vk/VkSsaoBlurPipeline.cpp
    engine/render/vk/VkTonemapPipeline.cpp

    # M03.3 — Materials: BaseColor/Normal/ORM + loader
    engine/render/vk/VkMaterial.cpp
    engine/render/vk/VkTextureLoader.cpp

    # M03.0 — Camera + frustum culling MVP
    engine/render/Camera.cpp
    engine/render/Halton.cpp
    engine/math/Frustum.cpp
    # M12.1 — Editor: ray cast for selection
    engine/math/Ray.cpp
    # M12.1 — Editor: ImGui UI + panels + gizmo
    engine/editor/EditorUI.cpp
    # M12.3 — Volumes gameplay: JSON read/write for layout export
    engine/world/VolumeFormat.cpp
    # M12.4 — layout.json export (instances + volumes, stable sort)
    engine/world/LayoutExport.cpp

    # M16.2 — Game HUD: player/target bars, combat log
    engine/ui/GameHud.cpp
    # M16.5 — Theme manager: theme.json + style.qss
    engine/ui/ThemeManager.cpp

    # M17.2 — Particles MVP: CPU pool + billboard render pass
    engine/render/ParticleSystem.cpp
    engine/render/vk/VkParticlePass.cpp
    engine/render/vk/VkParticlePipeline.cpp

    # M00.2 — Memory subsystem
    engine/core/SystemAllocator.cpp
    engine/core/LinearArena.cpp
    engine/core/Memory.cpp

    # M00.3 — Job System
    engine/core/jobs/JobSystem.cpp
)

target_include_directories(engine_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}   # makes "engine/..." and "external/..." includes resolvable
)

target_link_libraries(engine_core
    PUBLIC
        nlohmann_json::nlohmann_json
        Vulkan::Vulkan
        imgui
        glfw
)

# Warnings (platform-appropriate)
if (MSVC)
    target_compile_options(engine_core PRIVATE /W4 /WX /utf-8)
else()
    target_compile_options(engine_core PRIVATE -Wall -Wextra -Werror)
endif()

# ---------------------------------------------------------------------------
# engine_platform static library  (M00.4: Window, Input, FileSystem)
# ---------------------------------------------------------------------------
add_library(engine_platform STATIC
    engine/platform/Window.cpp
    engine/platform/Input.cpp
    engine/platform/FileSystem.cpp
)

target_include_directories(engine_platform
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}   # "engine/platform/..." includes
)

target_link_libraries(engine_platform
    PUBLIC
        engine_core
        glfw
)

if (MSVC)
    target_compile_options(engine_platform PRIVATE /W4 /WX /utf-8)
else()
    target_compile_options(engine_platform PRIVATE -Wall -Wextra -Werror)
endif()

# ---------------------------------------------------------------------------
# engine_app — minimal executable (main.cpp)
# ---------------------------------------------------------------------------
add_executable(engine_app main.cpp)

target_link_libraries(engine_app
    PRIVATE engine_platform
            engine_core
)

if (MSVC)
    target_compile_options(engine_app PRIVATE /W4 /WX /utf-8)
else()
    target_compile_options(engine_app PRIVATE -Wall -Wextra -Werror)
endif()

# Copy config.json next to the executable so it is found at runtime.
add_custom_command(TARGET engine_app POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/config.json"
        "$<TARGET_FILE_DIR:engine_app>/config.json"
    COMMENT "Copying config.json to output directory"
)

# ---------------------------------------------------------------------------
# engine_ai — M14.2 Mob AI: state machine, threat table, leash/return
# ---------------------------------------------------------------------------
add_library(engine_ai STATIC
    engine/ai/ThreatTable.cpp
    engine/ai/MobAiUpdate.cpp
)
target_include_directories(engine_ai PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
if(MSVC)
    target_compile_options(engine_ai PRIVATE /W4 /WX /utf-8)
else()
    target_compile_options(engine_ai PRIVATE -Wall -Wextra -Werror)
endif()

# ---------------------------------------------------------------------------
# engine_spawner — M15.2 Spawner JSON
# ---------------------------------------------------------------------------
add_library(engine_spawner STATIC
    engine/spawner/SpawnerDef.cpp
)
target_include_directories(engine_spawner PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(engine_spawner PUBLIC nlohmann_json::nlohmann_json)
if(MSVC)
    target_compile_options(engine_spawner PRIVATE /W4 /WX /utf-8)
else()
    target_compile_options(engine_spawner PRIVATE -Wall -Wextra -Werror)
endif()

# ---------------------------------------------------------------------------
# engine_event — M15.3 Dynamic events JSON
# ---------------------------------------------------------------------------
add_library(engine_event STATIC
    engine/event/EventDef.cpp
)
target_include_directories(engine_event PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(engine_event PUBLIC nlohmann_json::nlohmann_json)
if(MSVC)
    target_compile_options(engine_event PRIVATE /W4 /WX /utf-8)
else()
    target_compile_options(engine_event PRIVATE -Wall -Wextra -Werror)
endif()

# ---------------------------------------------------------------------------
# engine_ui — M16.1 UIModel (stats, inv, quests, events)
# ---------------------------------------------------------------------------
add_library(engine_ui STATIC
    engine/ui/UIModel.cpp
)
target_include_directories(engine_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
if(MSVC)
    target_compile_options(engine_ui PRIVATE /W4 /WX /utf-8)
else()
    target_compile_options(engine_ui PRIVATE -Wall -Wextra -Werror)
endif()

# ---------------------------------------------------------------------------
# engine_quest — M15.1 Quest definitions JSON
# ---------------------------------------------------------------------------
add_library(engine_quest STATIC
    engine/quest/QuestDef.cpp
)
target_include_directories(engine_quest PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(engine_quest PUBLIC nlohmann_json::nlohmann_json)
if(MSVC)
    target_compile_options(engine_quest PRIVATE /W4 /WX /utf-8)
else()
    target_compile_options(engine_quest PRIVATE -Wall -Wextra -Werror)
endif()

# ---------------------------------------------------------------------------
# engine_persistence — M14.4 Character DB (SQLite)
# ---------------------------------------------------------------------------
add_library(engine_persistence STATIC
    engine/persistence/CharacterDb.cpp
)
target_include_directories(engine_persistence PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(engine_persistence PUBLIC SQLite::SQLite3)
if(MSVC)
    target_compile_options(engine_persistence PRIVATE /W4 /WX /utf-8)
else()
    target_compile_options(engine_persistence PRIVATE -Wall -Wextra -Werror)
endif()

# ---------------------------------------------------------------------------
# engine_loot — M14.3 LootTable data
# ---------------------------------------------------------------------------
add_library(engine_loot STATIC
    engine/loot/LootTable.cpp
)
target_include_directories(engine_loot PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
if(MSVC)
    target_compile_options(engine_loot PRIVATE /W4 /WX /utf-8)
else()
    target_compile_options(engine_loot PRIVATE -Wall -Wextra -Werror)
endif()

# ---------------------------------------------------------------------------
# engine_fx — M17.1 FX definitions + event->FX mapping (client-side only)
# ---------------------------------------------------------------------------
add_library(engine_fx STATIC
    engine/fx/FxDef.cpp
    engine/fx/FxManager.cpp
)
target_include_directories(engine_fx PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(engine_fx PUBLIC engine_core nlohmann_json::nlohmann_json)
if(MSVC)
    target_compile_options(engine_fx PRIVATE /W4 /WX /utf-8)
else()
    target_compile_options(engine_fx PRIVATE -Wall -Wextra -Werror)
endif()

# ---------------------------------------------------------------------------
# engine_network — M13.1 Server: UDP transport, protocol, server tick (no Vulkan/GLFW)
# ---------------------------------------------------------------------------
add_library(engine_network STATIC
    engine/network/UdpSocket.cpp
    engine/network/ServerCore.cpp
    engine/network/Replication.cpp
    engine/network/Combat.cpp
    engine/network/LootProtocol.cpp
    engine/network/QuestProtocol.cpp
    engine/network/EventProtocol.cpp
)
target_include_directories(engine_network PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
if(WIN32)
    target_link_libraries(engine_network PUBLIC ws2_32)
endif()
if(MSVC)
    target_compile_options(engine_network PRIVATE /W4 /WX /utf-8)
else()
    target_compile_options(engine_network PRIVATE -Wall -Wextra -Werror)
endif()

# ---------------------------------------------------------------------------
# server_app — M13.1 headless server: tick 20 Hz, accept, send empty snapshots
# ---------------------------------------------------------------------------
add_executable(server_app main_server.cpp)
target_link_libraries(server_app PRIVATE engine_network engine_core engine_ai engine_loot engine_persistence engine_quest engine_spawner engine_event)
if(MSVC)
    target_compile_options(server_app PRIVATE /W4 /WX /utf-8)
else()
    target_compile_options(server_app PRIVATE -Wall -Wextra -Werror)
endif()

# ---------------------------------------------------------------------------
# client_app — M13.1 test client: connect, receive snapshot + stats
# ---------------------------------------------------------------------------
add_executable(client_app main_client.cpp)
target_link_libraries(client_app PRIVATE engine_network engine_ui engine_fx)
if(WIN32)
    target_link_libraries(client_app PRIVATE ws2_32)
endif()
if(MSVC)
    target_compile_options(client_app PRIVATE /W4 /WX /utf-8)
else()
    target_compile_options(client_app PRIVATE -Wall -Wextra -Werror)
endif()

# ---------------------------------------------------------------------------
# hlod_builder — offline tool (M09.4: HLOD clustering + mesh merge → hlod.pak)
# ---------------------------------------------------------------------------
add_executable(hlod_builder tools/hlod_builder.cpp)
target_link_libraries(hlod_builder PRIVATE nlohmann_json::nlohmann_json)
target_include_directories(hlod_builder PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
if (MSVC)
    target_compile_options(hlod_builder PRIVATE /W4 /WX /utf-8)
else()
    target_compile_options(hlod_builder PRIVATE -Wall -Wextra -Werror)
endif()

# ---------------------------------------------------------------------------
# zone_builder — offline tool (M11.1: import glTF + layout.json → zone)
# Uses TinyGLTF (FetchContent) for glTF import as specified by ticket.
# ---------------------------------------------------------------------------
include(FetchContent)
FetchContent_Declare(
  tinygltf
  GIT_REPOSITORY https://github.com/syoyo/tinygltf.git
  GIT_TAG v2.6.3
)
set(TINYGLTF_BUILD_LOADER_EXAMPLE OFF CACHE BOOL "" FORCE)
set(TINYGLTF_HEADER_ONLY OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(tinygltf)
target_link_libraries(tinygltf PUBLIC nlohmann_json::nlohmann_json)

# M11.5: xxHash for content hash (builder only; no CMake in xxHash repo, add lib manually)
FetchContent_Declare(
  xxhash
  GIT_REPOSITORY https://github.com/Cyan4973/xxHash.git
  GIT_TAG v0.8.2
)
FetchContent_GetProperties(xxhash)
if(NOT xxhash_POPULATED)
  FetchContent_Populate(xxhash)
  add_library(xxhash STATIC ${xxhash_SOURCE_DIR}/xxhash.c)
  target_include_directories(xxhash PUBLIC ${xxhash_SOURCE_DIR})
endif()

add_executable(zone_builder tools/zone_builder.cpp)
target_link_libraries(zone_builder PRIVATE nlohmann_json::nlohmann_json tinygltf xxhash)
target_include_directories(zone_builder PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
if (MSVC)
    target_compile_options(zone_builder PRIVATE /W4 /WX /utf-8)
else()
    target_compile_options(zone_builder PRIVATE -Wall -Wextra -Werror)
endif()

# ---------------------------------------------------------------------------
# Dear ImGui — M12.1 editor UI (core + GLFW + Vulkan backends)
# ---------------------------------------------------------------------------
FetchContent_Declare(
  imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui.git
  GIT_TAG v1.92.6
)
FetchContent_Populate(imgui)
add_library(imgui STATIC
  ${imgui_SOURCE_DIR}/imgui.cpp
  ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp
  ${imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp
)
target_include_directories(imgui PUBLIC
  ${imgui_SOURCE_DIR}
  ${imgui_SOURCE_DIR}/backends
)
target_link_libraries(imgui PUBLIC glfw Vulkan::Vulkan)

# ---------------------------------------------------------------------------
# Recast/Detour — M11.3 navmesh bake (FetchContent, build Recast + Detour only)
# ---------------------------------------------------------------------------
set(RECASTNAVIGATION_DEMO OFF CACHE BOOL "" FORCE)
set(RECASTNAVIGATION_TESTS OFF CACHE BOOL "" FORCE)
set(RECASTNAVIGATION_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
  recastnavigation
  GIT_REPOSITORY https://github.com/recastnavigation/recastnavigation.git
  GIT_TAG v1.6.0
)
FetchContent_MakeAvailable(recastnavigation)

# ---------------------------------------------------------------------------
# navmesh_baker — offline tool (M11.3: Recast bake per chunk, navmesh.bin + portals.bin)
# ---------------------------------------------------------------------------
add_executable(navmesh_baker tools/navmesh_baker.cpp)
target_link_libraries(navmesh_baker PRIVATE Recast)
target_include_directories(navmesh_baker PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${recastnavigation_SOURCE_DIR}/Recast/Include)
if (MSVC)
    target_compile_options(navmesh_baker PRIVATE /W4 /utf-8)
else()
    target_compile_options(navmesh_baker PRIVATE -Wall -Wextra)
endif()
