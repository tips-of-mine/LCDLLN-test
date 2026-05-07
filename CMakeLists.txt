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

# Windows + vcpkg (dynamic OpenSSL): runtime DLLs must sit next to the .exe or be on PATH.
if(WIN32)
	set(LCDLLN_OPENSSL_DLL_DIR "")
	# Prefer querying the OpenSSL imported targets for the actual DLL locations.
	# This avoids relying on "EXISTS(...)" during configure time, since vcpkg may generate the DLLs only during build.
	set(LCDLLN_OPENSSL_DLL_CRYPTO_PATH "")
	set(LCDLLN_OPENSSL_DLL_SSL_PATH "")
	if(TARGET OpenSSL::Crypto)
		# Check .dll directly, or derive bin/ from .lib (vcpkg layout: lib/libcrypto.lib → bin/libcrypto-3-x64.dll)
		foreach(_prop IN ITEMS IMPORTED_LOCATION_RELEASE IMPORTED_LOCATION IMPORTED_IMPLIB_RELEASE IMPORTED_IMPLIB)
			get_target_property(_loc OpenSSL::Crypto ${_prop})
			if(_loc)
				if(_loc MATCHES "\\.dll$")
					set(LCDLLN_OPENSSL_DLL_CRYPTO_PATH "${_loc}")
					break()
				elseif(_loc MATCHES "\\.lib$")
					get_filename_component(_lcdlln_lib_dir "${_loc}" DIRECTORY)
					get_filename_component(_lcdlln_pkg_root "${_lcdlln_lib_dir}" DIRECTORY)
					if(EXISTS "${_lcdlln_pkg_root}/bin/libcrypto-3-x64.dll")
						set(LCDLLN_OPENSSL_DLL_CRYPTO_PATH "${_lcdlln_pkg_root}/bin/libcrypto-3-x64.dll")
						break()
					endif()
				endif()
			endif()
		endforeach()
	endif()
	if(TARGET OpenSSL::SSL)
		# Same: accept .dll, or derive from .lib
		foreach(_prop IN ITEMS IMPORTED_LOCATION_RELEASE IMPORTED_LOCATION IMPORTED_IMPLIB_RELEASE IMPORTED_IMPLIB)
			get_target_property(_loc OpenSSL::SSL ${_prop})
			if(_loc)
				if(_loc MATCHES "\\.dll$")
					set(LCDLLN_OPENSSL_DLL_SSL_PATH "${_loc}")
					break()
				elseif(_loc MATCHES "\\.lib$")
					get_filename_component(_lcdlln_lib_dir "${_loc}" DIRECTORY)
					get_filename_component(_lcdlln_pkg_root "${_lcdlln_lib_dir}" DIRECTORY)
					if(EXISTS "${_lcdlln_pkg_root}/bin/libssl-3-x64.dll")
						set(LCDLLN_OPENSSL_DLL_SSL_PATH "${_lcdlln_pkg_root}/bin/libssl-3-x64.dll")
						break()
					endif()
				endif()
			endif()
		endforeach()
	endif()
	# Derive folder from whichever path was resolved.
	if(NOT LCDLLN_OPENSSL_DLL_CRYPTO_PATH STREQUAL "")
		get_filename_component(LCDLLN_OPENSSL_DLL_DIR "${LCDLLN_OPENSSL_DLL_CRYPTO_PATH}" DIRECTORY)
	elseif(NOT LCDLLN_OPENSSL_DLL_SSL_PATH STREQUAL "")
		get_filename_component(LCDLLN_OPENSSL_DLL_DIR "${LCDLLN_OPENSSL_DLL_SSL_PATH}" DIRECTORY)
	endif()
	if(DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
		set(_lcdlln_ossl_bin "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin")
		if(EXISTS "${_lcdlln_ossl_bin}/libcrypto-3-x64.dll")
			set(LCDLLN_OPENSSL_DLL_DIR "${_lcdlln_ossl_bin}")
		endif()
	endif()
	if(LCDLLN_OPENSSL_DLL_DIR STREQUAL "" AND EXISTS "${CMAKE_BINARY_DIR}/vcpkg_installed/x64-windows/bin/libcrypto-3-x64.dll")
		set(LCDLLN_OPENSSL_DLL_DIR "${CMAKE_BINARY_DIR}/vcpkg_installed/x64-windows/bin")
	endif()
	if(LCDLLN_OPENSSL_DLL_DIR STREQUAL "" AND EXISTS "${CMAKE_BINARY_DIR}/vcpkg_installed/x64-windows-release/bin/libcrypto-3-x64.dll")
		set(LCDLLN_OPENSSL_DLL_DIR "${CMAKE_BINARY_DIR}/vcpkg_installed/x64-windows-release/bin")
	endif()
	if(LCDLLN_OPENSSL_DLL_DIR STREQUAL "" AND DEFINED OPENSSL_ROOT_DIR AND EXISTS "${OPENSSL_ROOT_DIR}/bin/libcrypto-3-x64.dll")
		set(LCDLLN_OPENSSL_DLL_DIR "${OPENSSL_ROOT_DIR}/bin")
	endif()
	if(LCDLLN_OPENSSL_DLL_DIR STREQUAL "" AND DEFINED ENV{VCPKG_ROOT})
		foreach(_lcdlln_ossl_triplet IN ITEMS "x64-windows-release" "x64-windows")
			set(_lcdlln_ossl_classic "$ENV{VCPKG_ROOT}/installed/${_lcdlln_ossl_triplet}/bin")
			if(EXISTS "${_lcdlln_ossl_classic}/libcrypto-3-x64.dll")
				set(LCDLLN_OPENSSL_DLL_DIR "${_lcdlln_ossl_classic}")
				break()
			endif()
		endforeach()
	endif()
	if(NOT LCDLLN_OPENSSL_DLL_DIR STREQUAL "")
		message(STATUS "OpenSSL runtime DLLs will be copied from: ${LCDLLN_OPENSSL_DLL_DIR}")
	else()
		message(STATUS "OpenSSL DLL folder not detected — ensure libssl/libcrypto are on PATH or copy them next to the exe")
	endif()
endif()

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

# Dear ImGui (branche docking) — World Editor Windows : overlay Vulkan (dynamic rendering), pas d’OpenGL.
if(WIN32)
  FetchContent_Declare(imgui_lcdlln_fc
    GIT_REPOSITORY https://github.com/ocornut/imgui
    GIT_TAG        v1.91.9-docking
  )
  FetchContent_GetProperties(imgui_lcdlln_fc)
  if(NOT imgui_lcdlln_fc_POPULATED)
    FetchContent_Populate(imgui_lcdlln_fc)
  endif()
  set(IMGUI_DIR "${imgui_lcdlln_fc_SOURCE_DIR}")
  message(STATUS "LCDLLN: Dear ImGui (world editor / Vulkan): ${IMGUI_DIR}")
  add_library(imgui_lcdlln STATIC
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_vulkan.cpp
    ${IMGUI_DIR}/backends/imgui_impl_win32.cpp
  )
  target_include_directories(imgui_lcdlln PUBLIC
    "${IMGUI_DIR}"
    "${IMGUI_DIR}/backends"
  )
  target_link_libraries(imgui_lcdlln PUBLIC Vulkan::Vulkan)
  target_link_libraries(imgui_lcdlln PRIVATE user32)
  target_compile_definitions(imgui_lcdlln PRIVATE VK_USE_PLATFORM_WIN32_KHR)
  target_compile_features(imgui_lcdlln PUBLIC cxx_std_11)
  if(MSVC)
    target_compile_options(imgui_lcdlln PRIVATE /W0)
  endif()
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
  engine/core/LogConfig.cpp
  engine/core/Time.cpp
  engine/audio/AudioEngine.cpp
  engine/audio/MaMenuMusic.cpp
  engine/client/CombatHud.cpp
  engine/client/LocalizationService.cpp
  engine/client/auth/AuthUiPresenterCore.cpp
  engine/client/auth/AuthUiPresenterSettings.cpp
  engine/client/auth/AuthUiPresenterNative.cpp
  engine/client/auth/screens/AuthScreenLanguageSelect.cpp
  engine/client/auth/screens/AuthScreenLogin.cpp
  engine/client/auth/screens/AuthScreenRegister.cpp
  engine/client/auth/screens/AuthScreenForgotPassword.cpp
  engine/client/auth/screens/AuthScreenVerifyEmail.cpp
  engine/client/auth/screens/AuthScreenOptions.cpp
  engine/client/auth/screens/AuthScreenError.cpp
  engine/client/auth/screens/AuthScreenShardPick.cpp
  engine/client/auth/screens/AuthScreenTerms.cpp
  engine/client/auth/screens/AuthScreenCharacterCreate.cpp
  engine/client/auth/screens/AuthScreenCharacterSelect.cpp
  engine/client/AoEPreviewSystem.cpp
  engine/client/BuffBarPresenter.cpp
  engine/client/AuraFXSystem.cpp
  engine/client/FXManager.cpp
  engine/client/InventoryUi.cpp
  engine/client/TradeWindowUi.cpp
  engine/client/HarvestCastBar.cpp
  engine/client/CraftingUi.cpp
  engine/client/CharacterCreationUi.cpp
  engine/client/SettingsMenuUi.cpp
  engine/client/HudLayoutEditor.cpp
  engine/client/AdvancedCombatUi.cpp
  engine/client/AuctionUi.cpp
  engine/client/ShopUi.cpp
  engine/client/GameplayUdpClient.cpp
  engine/client/ProfilerHud.cpp
  engine/client/QuestUi.cpp
  engine/client/ThemeManager.cpp
  engine/client/UIModel.cpp
  engine/client/ChatUi.cpp
  engine/client/ChatWorldVisualPresenter.cpp
  engine/client/FriendsUi.cpp
  engine/client/PartyHud.cpp
  engine/client/ZonePreloadHook.cpp
  engine/net/ChatSystem.cpp
  engine/net/ChatEmotes.cpp
  engine/editor/EditorMode.cpp
  engine/editor/WorldEditorImGui.cpp
  engine/editor/WorldMapIo.cpp
  engine/editor/TreeSpeciesCatalog.cpp
  engine/editor/WorldEditorSession.cpp
  engine/editor/TexturePreviewCache.cpp
  engine/editor/TextureLibraryPanel.cpp
  # M100.1 — World editor "couche au-dessus" (engine::editor::world).
  # Cohabite avec WorldEditorImGui ; activé par --editor-world ou
  # editor.world.enabled. Pas référencé par engine/server/.
  engine/editor/world/WorldEditorShell.cpp
  engine/editor/world/CommandStack.cpp
  engine/editor/world/TerrainDocument.cpp
  engine/editor/world/TerrainBrush.cpp
  engine/editor/world/TerrainRaycast.cpp
  engine/editor/world/TerrainSculptCommand.cpp
  engine/editor/world/TerrainSculptTool.cpp
  engine/editor/world/TerrainStampCommand.cpp
  engine/editor/world/TerrainStampTool.cpp
  engine/editor/world/StampLibrary.cpp
  engine/editor/world/ProceduralStampGenerators.cpp
  engine/editor/world/SplatPaintCommand.cpp
  engine/editor/world/SplatPaintTool.cpp
  engine/editor/world/SplatRules.cpp
  engine/editor/world/EditorCameraController.cpp
  engine/editor/world/panels/ScenePanel.cpp
  engine/editor/world/panels/InspectorPanel.cpp
  engine/editor/world/panels/AssetBrowserPanel.cpp
  engine/editor/world/panels/OutlinerPanel.cpp
  engine/editor/world/panels/ConsolePanel.cpp
  engine/editor/world/panels/ToolPropertiesPanel.cpp
  engine/editor/world/panels/HistoryPanel.cpp
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
  engine/render/AuthGlyphPass.cpp
  engine/render/FontAtlasTtf.cpp
  engine/render/AuthLogoPass.cpp
  engine/render/auth/AuthUiRendererCore.cpp
  engine/render/auth/AuthImGuiCommon.cpp
  engine/render/auth/screens/AuthImGuiLanguageSelect.cpp
  engine/render/auth/screens/AuthImGuiLogin.cpp
  engine/render/auth/screens/AuthImGuiRegister.cpp
  engine/render/auth/screens/AuthImGuiError.cpp
  engine/render/auth/screens/AuthImGuiVerifyEmail.cpp
  engine/render/auth/screens/AuthImGuiForgotPassword.cpp
  engine/render/auth/screens/AuthImGuiOptions.cpp
  engine/render/auth/screens/AuthImGuiShardPick.cpp
  engine/render/auth/screens/AuthImGuiTerms.cpp
  engine/render/auth/screens/AuthImGuiCharacterCreate.cpp
  engine/render/auth/screens/AuthImGuiCharacterSelect.cpp
  engine/render/LnTheme.h
  engine/render/AuthImGuiRenderer.cpp
  engine/render/ChatImGuiRenderer.cpp
  engine/render/EditorHubImGuiRenderer.cpp
  engine/render/DecalSystem.cpp
  engine/render/DecalPass.cpp
  engine/render/ParticleSystem.cpp
  engine/render/ParticleBillboardPass.cpp
  engine/render/WaterRenderer.cpp
  engine/render/UnderwaterPass.cpp
  engine/render/DayNightCycle.cpp
  engine/render/WeatherSystem.cpp
  engine/render/DynamicLightSystem.cpp
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
  engine/render/TerrainChunkPipeline.cpp
  engine/render/terrain_chunk/ChunkRuntime.cpp
  engine/render/terrain_chunk/TerrainMeshGpuCache.cpp
  engine/render/GpuDrivenCullingPass.cpp
  engine/render/HiZPyramidPass.cpp
  engine/render/LightingPass.cpp
  engine/render/TonemapPass.cpp
  engine/render/BloomPass.cpp
  engine/render/AutoExposure.cpp
  engine/render/TaaPass.cpp
  engine/render/DeferredPipeline.cpp
  engine/math/Frustum.cpp
  engine/gameplay/CharacterController.cpp
  engine/gameplay/ThirdPersonCamera.cpp
  engine/gameplay/SkillSystem.cpp
  engine/gameplay/ClientPrediction.cpp
  engine/gameplay/StatusEffect.cpp
  engine/world/WorldModel.cpp
  engine/world/OutputVersion.cpp
  engine/world/ProbeData.cpp
  engine/world/PakReader.cpp
  engine/world/ChunkPackageLayout.cpp
  engine/world/StreamingScheduler.cpp
  engine/world/StreamCache.cpp
  engine/world/ChunkBudgetStats.cpp
  engine/world/terrain/LayerPalette.cpp
  engine/world/terrain/SplatMap.cpp
  engine/world/terrain/TerrainChunk.cpp
  engine/world/terrain/TerrainChunkLoader.cpp
  engine/world/terrain/TerrainLodChain.cpp
  engine/world/terrain/TerrainLodWorker.cpp
  engine/world/terrain/TerrainMeshBuilder.cpp
  engine/render/vk/DeferredDestroyQueue.cpp
  engine/render/GpuUploadQueue.cpp
  engine/render/vk/StagingAllocator.cpp
  engine/world/LodConfig.cpp
  engine/world/HlodRuntime.cpp
  engine/render/terrain/HeightmapLoader.cpp
  engine/render/terrain/TerrainMesh.cpp
  engine/render/terrain/TerrainSplatting.cpp
  engine/render/terrain/SplatSampling.cpp
  engine/render/terrain/TerrainHoleMask.cpp
  engine/render/terrain/TerrainGrassDetail.cpp
  engine/render/terrain/TerrainCliffMesh.cpp
  engine/render/terrain/TerrainRenderer.cpp
  engine/render/terrain/TerrainEditingTools.cpp
  engine/network/ByteWriter.cpp
  engine/network/ByteReader.cpp
  engine/network/PacketBuilder.cpp
  engine/network/PacketView.cpp
  engine/network/RequestResponseDispatcher.cpp
  engine/network/ErrorPacket.cpp
  engine/network/AuthRegisterPayloads.cpp
  engine/network/CharacterPayloads.cpp
  engine/network/ChatPayloads.cpp
  engine/network/ShardPayloads.cpp
  engine/network/TermsPayloads.cpp
)
# Shared gameplay wire codec (Encode*/Decode*); linked into client — must not be server-only.
target_sources(engine_core PRIVATE engine/server/ServerProtocol.cpp)
target_include_directories(engine_core PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_SOURCE_DIR}/external/stb
  ${CMAKE_SOURCE_DIR}/external/miniaudio
  ${VMA_INCLUDE_DIRS}
)
target_link_libraries(engine_core PUBLIC
  Vulkan::Vulkan
  glfw
  engine_auth
  spdlog::spdlog
)
if(WIN32)
  target_link_libraries(engine_core PRIVATE imgui_lcdlln)
endif()
if(WIN32)
  target_link_libraries(engine_core PRIVATE ws2_32 OpenSSL::SSL shell32)
elseif(UNIX)
  target_link_libraries(engine_core PRIVATE pthread OpenSSL::SSL)
  if(NOT APPLE)
    target_link_libraries(engine_core PRIVATE dl)
  endif()
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
    engine/network/NetClient_linux.cpp
  )
endif()

add_executable(engine_app
  engine/app/main.cpp
)
if(WIN32)
  target_sources(engine_app PRIVATE "${CMAKE_SOURCE_DIR}/game/data/icons/lcdlln_client.rc")
endif()
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
  target_link_libraries(engine_app PRIVATE user32 shell32)
endif()

# World Editor : exécutable séparé, même cœur Vulkan que le jeu (flag interne --world-editor).
add_executable(world_editor_app
  engine/app/world_editor_main.cpp
)
if(WIN32)
  target_sources(world_editor_app PRIVATE "${CMAKE_SOURCE_DIR}/game/data/icons/lcdlln_world_editor.rc")
endif()
set_target_properties(world_editor_app PROPERTIES
  OUTPUT_NAME "lcdlln_world_editor"
  RUNTIME_OUTPUT_DIRECTORY         "${CMAKE_BINARY_DIR}/pkg/world_editor"
  RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/pkg/world_editor"
  RUNTIME_OUTPUT_DIRECTORY_DEBUG   "${CMAKE_BINARY_DIR}/pkg/world_editor"
)
target_include_directories(world_editor_app PRIVATE "${CMAKE_SOURCE_DIR}")
if(MSVC)
  target_compile_options(world_editor_app PRIVATE /Zc:preprocessor)
endif()
target_link_libraries(world_editor_app PRIVATE engine_core)
if(WIN32)
  target_link_libraries(world_editor_app PRIVATE user32 shell32)
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
  add_custom_command(TARGET world_editor_app POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/game/data"
            "$<TARGET_FILE_DIR:world_editor_app>/game/data"
    COMMENT "Copie du dossier game/data à côté de lcdlln_world_editor.exe..."
  )
endif()

# Liens externes (défauts client : maître HTTPS / TCP / HTTP tunnel) — relatif à l’exe : external/external_links.json
if(EXISTS "${CMAKE_SOURCE_DIR}/external/external_links.json")
  add_custom_command(TARGET engine_app POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:engine_app>/external"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/external/external_links.json"
            "$<TARGET_FILE_DIR:engine_app>/external/external_links.json"
    COMMENT "Copie de external/external_links.json..."
  )
  add_custom_command(TARGET world_editor_app POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:world_editor_app>/external"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/external/external_links.json"
            "$<TARGET_FILE_DIR:world_editor_app>/external/external_links.json"
    COMMENT "Copie de external/external_links.json à côté de lcdlln_world_editor.exe..."
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
  add_custom_command(TARGET world_editor_app POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/config.json"
            "$<TARGET_FILE_DIR:world_editor_app>/config.json"
    COMMENT "Copie de config.json à côté de lcdlln_world_editor.exe..."
  )
endif()

if(WIN32 AND NOT LCDLLN_OPENSSL_DLL_DIR STREQUAL "")
  add_custom_command(TARGET engine_app POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${LCDLLN_OPENSSL_DLL_DIR}/libssl-3-x64.dll"
            "$<TARGET_FILE_DIR:engine_app>/"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${LCDLLN_OPENSSL_DLL_DIR}/libcrypto-3-x64.dll"
            "$<TARGET_FILE_DIR:engine_app>/"
    COMMENT "Copie des DLL OpenSSL (libssl / libcrypto) à côté de lcdlln.exe..."
  )
  add_custom_command(TARGET world_editor_app POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${LCDLLN_OPENSSL_DLL_DIR}/libssl-3-x64.dll"
            "$<TARGET_FILE_DIR:world_editor_app>/"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${LCDLLN_OPENSSL_DLL_DIR}/libcrypto-3-x64.dll"
            "$<TARGET_FILE_DIR:world_editor_app>/"
    COMMENT "Copie des DLL OpenSSL à côté de lcdlln_world_editor.exe..."
  )
endif()

# Paquet runtime : cmake --install build --prefix dist (exécutable + game/data + config)
install(TARGETS engine_app world_editor_app RUNTIME DESTINATION .)
if(EXISTS "${CMAKE_SOURCE_DIR}/game/data")
  install(DIRECTORY "${CMAKE_SOURCE_DIR}/game/data" DESTINATION .)
endif()
if(EXISTS "${CMAKE_SOURCE_DIR}/config.json")
  install(FILES "${CMAKE_SOURCE_DIR}/config.json" DESTINATION .)
endif()
if(EXISTS "${CMAKE_SOURCE_DIR}/external/external_links.json")
  install(FILES "${CMAKE_SOURCE_DIR}/external/external_links.json" DESTINATION external)
endif()

# M19.2: Protocol v1 serialization unit tests
add_executable(protocol_v1_tests engine/network/ProtocolV1Tests.cpp)
target_link_libraries(protocol_v1_tests PRIVATE engine_core)
enable_testing()
add_test(NAME protocol_v1_tests COMMAND protocol_v1_tests)

# Phase 1: CHARACTER_LIST request/response payload round-trip tests
add_executable(character_list_payloads_tests engine/network/CharacterListPayloadsTests.cpp)
target_link_libraries(character_list_payloads_tests PRIVATE engine_core)
add_test(NAME character_list_payloads_tests COMMAND character_list_payloads_tests)

# Phase 3.6.5: CHARACTER_SAVE_POSITION request/response payload round-trip tests
add_executable(character_save_position_payloads_tests engine/network/CharacterSavePositionPayloadsTests.cpp)
target_link_libraries(character_save_position_payloads_tests PRIVATE engine_core)
add_test(NAME character_save_position_payloads_tests COMMAND character_save_position_payloads_tests)

# Chat MVP: CHAT_SEND_REQUEST / CHAT_RELAY payload round-trip tests
add_executable(chat_payloads_tests engine/network/ChatPayloadsTests.cpp)
target_link_libraries(chat_payloads_tests PRIVATE engine_core)
add_test(NAME chat_payloads_tests COMMAND chat_payloads_tests)

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

# M25.2: NetServer bandwidth caps + backpressure unit tests
add_executable(netserver_bandwidth_tests engine/server/NetServerBandwidthThrottleTests.cpp)
target_include_directories(netserver_bandwidth_tests PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(netserver_bandwidth_tests PRIVATE engine_core)
if(MSVC)
  target_compile_options(netserver_bandwidth_tests PRIVATE /W4 /permissive- /Zc:preprocessor)
endif()
add_test(NAME netserver_bandwidth_tests COMMAND netserver_bandwidth_tests)

# M25.4: DDoS basic mitigation (connection throttle) unit tests
add_executable(ddos_connection_protector_tests engine/server/ConnectionDDoSProtectorTests.cpp)
target_include_directories(ddos_connection_protector_tests PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(ddos_connection_protector_tests PRIVATE engine_core)
if(MSVC)
  target_compile_options(ddos_connection_protector_tests PRIVATE /W4 /permissive- /Zc:preprocessor)
endif()
add_test(NAME ddos_connection_protector_tests COMMAND ddos_connection_protector_tests)

# STAB.14: Runtime localization service unit tests
add_executable(localization_service_tests engine/client/LocalizationServiceTests.cpp)
target_link_libraries(localization_service_tests PRIVATE engine_core)
if(MSVC)
  target_compile_options(localization_service_tests PRIVATE /W4 /permissive- /Zc:preprocessor)
endif()
add_test(NAME localization_service_tests COMMAND localization_service_tests)

# Tests unitaires CPU pour TexturePreviewCache (ResampleRgba8Box, GenerateProceduralAlbedoLayer).
# Pas de dependance Vulkan : tournables en CI sans GPU.
add_executable(texture_preview_cache_tests engine/editor/tests/TexturePreviewCacheTests.cpp)
target_include_directories(texture_preview_cache_tests PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(texture_preview_cache_tests PRIVATE engine_core)
if(MSVC)
  target_compile_options(texture_preview_cache_tests PRIVATE /W4 /permissive- /Zc:preprocessor)
endif()
add_test(NAME texture_preview_cache_tests COMMAND texture_preview_cache_tests)

# Tests unitaires CPU pour WorldEditorSession.
# Garde la propriete "splatLayerTextureRefs vide apres ActionNewMap" qui pilote
# le fallback orange du World Editor (terrain.frag push-constant noUserTextures).
# Pas de dependance Vulkan : ActionNewMap utilise uniquement la couche fichiers.
add_executable(world_editor_session_tests engine/editor/tests/WorldEditorSessionTests.cpp)
target_include_directories(world_editor_session_tests PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(world_editor_session_tests PRIVATE engine_core)
if(MSVC)
  target_compile_options(world_editor_session_tests PRIVATE /W4 /permissive- /Zc:preprocessor)
endif()
add_test(NAME world_editor_session_tests COMMAND world_editor_session_tests)

# M100.1 — Tests unitaires CPU pour la coquille WorldEditorShell.
# Vérifient le cycle Init/Shutdown, la persistance du layout .ini, le
# dispatch des raccourcis F1..F12 et le flag dirty. Le test crée un
# contexte ImGui isolé en interne (pas de fenêtre Vulkan ni device).
# Windows-only : ImGui n'est compilé que sous WIN32 dans ce repo.
if(WIN32)
  add_executable(world_editor_shell_tests engine/editor/world/tests/WorldEditorShellTests.cpp)
  target_include_directories(world_editor_shell_tests PRIVATE ${CMAKE_SOURCE_DIR})
  target_link_libraries(world_editor_shell_tests PRIVATE engine_core imgui_lcdlln)
  if(MSVC)
    target_compile_options(world_editor_shell_tests PRIVATE /W4 /permissive- /Zc:preprocessor)
  endif()
  add_test(NAME world_editor_shell_tests COMMAND world_editor_shell_tests)
endif()

# M100.2 — Tests unitaires CPU pour la pile undo/redo CommandStack.
# Pure CPU (pas d'ImGui ni Vulkan), mais on garde le gating WIN32 pour
# rester cohérent avec les autres tests éditeur monde et éviter de tirer
# engine_core sous Linux (where editor pieces aren't built anyway).
if(WIN32)
  add_executable(command_stack_tests engine/editor/world/tests/CommandStackTests.cpp)
  target_include_directories(command_stack_tests PRIVATE ${CMAKE_SOURCE_DIR})
  target_link_libraries(command_stack_tests PRIVATE engine_core)
  if(MSVC)
    target_compile_options(command_stack_tests PRIVATE /W4 /permissive- /Zc:preprocessor)
  endif()
  add_test(NAME command_stack_tests COMMAND command_stack_tests)
endif()

# M100.5 — Tests unitaires CPU pour TerrainChunk (heightmap par chunk).
# Pure CPU (pas d'ImGui ni Vulkan) : pose la struct, le format binaire `TRRN`,
# Save/LoadTerrainBin via OutputVersionHeader + xxhash64. Round-trip byte-exact
# vérifié dans TerrainParityTests (target séparé) ; ici on valide MakeFlat,
# SampleHeight bilinéaire/clamp, et la robustesse du Load (bad magic/version).
if(WIN32)
  add_executable(terrain_chunk_tests engine/world/terrain/tests/TerrainChunkTests.cpp)
  target_include_directories(terrain_chunk_tests PRIVATE ${CMAKE_SOURCE_DIR})
  target_link_libraries(terrain_chunk_tests PRIVATE engine_core)
  if(MSVC)
    target_compile_options(terrain_chunk_tests PRIVATE /W4 /permissive- /Zc:preprocessor)
  endif()
  add_test(NAME terrain_chunk_tests COMMAND terrain_chunk_tests)
endif()

# M100.9 — Tests unitaires CPU pour SplatMap (8-layer splat per chunk).
# Pure CPU (pas d'ImGui ni Vulkan) : pose la struct, le format binaire `SLAT`,
# Save/LoadSplatBin via OutputVersionHeader + xxhash64. Round-trip byte-exact
# vérifié dans le même fichier (Test_SaveLoad_Roundtrip + parity ajouté Task 15).
# Vérifie aussi l'invariant somme=255 par cellule (rejet au Save et au Load).
if(WIN32)
  add_executable(splat_map_tests engine/world/terrain/tests/SplatMapTests.cpp)
  target_include_directories(splat_map_tests PRIVATE ${CMAKE_SOURCE_DIR})
  target_link_libraries(splat_map_tests PRIVATE engine_core)
  if(MSVC)
    target_compile_options(splat_map_tests PRIVATE /W4 /permissive- /Zc:preprocessor)
  endif()
  add_test(NAME splat_map_tests COMMAND splat_map_tests)
endif()

# Terrain Chunk Runtime — Tests unitaires CPU pour ChunkRuntime
# (cache LRU + budget GPU + protection ring Active/Visible). Pas de Vulkan.
if(WIN32)
  add_executable(chunk_runtime_tests engine/render/terrain_chunk/tests/ChunkRuntimeTests.cpp)
  target_include_directories(chunk_runtime_tests PRIVATE ${CMAKE_SOURCE_DIR})
  target_link_libraries(chunk_runtime_tests PRIVATE engine_core)
  if(MSVC)
    target_compile_options(chunk_runtime_tests PRIVATE /W4 /permissive- /Zc:preprocessor)
  endif()
  add_test(NAME chunk_runtime_tests COMMAND chunk_runtime_tests)
endif()

# Terrain Chunk Runtime — Tests unitaires pour TerrainMeshGpuCache
# (mock allocator, pas de Vulkan). Vérifie cache hit/miss, eviction,
# tracking budget runtime.
if(WIN32)
  add_executable(terrain_mesh_gpu_cache_tests engine/render/terrain_chunk/tests/TerrainMeshGpuCacheTests.cpp)
  target_include_directories(terrain_mesh_gpu_cache_tests PRIVATE ${CMAKE_SOURCE_DIR})
  target_link_libraries(terrain_mesh_gpu_cache_tests PRIVATE engine_core)
  if(MSVC)
    target_compile_options(terrain_mesh_gpu_cache_tests PRIVATE /W4 /permissive- /Zc:preprocessor)
  endif()
  add_test(NAME terrain_mesh_gpu_cache_tests COMMAND terrain_mesh_gpu_cache_tests)
endif()

# M100.5 — Tests de parité éditeur ↔ client pour le format terrain.bin.
# Round-trip byte-exact (SaveTerrainBin → LoadTerrainBin → memcmp) +
# meshes deterministe (BuildLod0Mesh produit des vertex/index buffers
# identiques bit-à-bit pour le chunk source et le chunk re-lu).
if(WIN32)
  add_executable(terrain_parity_tests engine/world/terrain/tests/TerrainParityTests.cpp)
  target_include_directories(terrain_parity_tests PRIVATE ${CMAKE_SOURCE_DIR})
  target_link_libraries(terrain_parity_tests PRIVATE engine_core)
  if(MSVC)
    target_compile_options(terrain_parity_tests PRIVATE /W4 /permissive- /Zc:preprocessor)
  endif()
  add_test(NAME terrain_parity_tests COMMAND terrain_parity_tests)
endif()

# M100.8 — Tests unitaires CPU pour TerrainLodChain + TerrainLodWorker.
# Vérifie : box filter 2x2, roundtrip terrain_lods.bin, async non-bloquant
# pour le main thread, génération atomique pour les stale jobs.
if(WIN32)
  add_executable(terrain_lod_tests engine/world/terrain/tests/TerrainLodTests.cpp)
  target_include_directories(terrain_lod_tests PRIVATE ${CMAKE_SOURCE_DIR})
  target_link_libraries(terrain_lod_tests PRIVATE engine_core)
  if(MSVC)
    target_compile_options(terrain_lod_tests PRIVATE /W4 /permissive- /Zc:preprocessor)
  endif()
  add_test(NAME terrain_lod_tests COMMAND terrain_lod_tests)
endif()

# M100.4 — Tests unitaires CPU pour EditorCameraController.
# Pure CPU (pas d'ImGui ni Vulkan) : on vérifie le mapping des modes
# FPS/Orbital/TopDownOrtho vers la `engine::render::Camera`. Gating WIN32
# uniquement pour cohérence avec les autres tests éditeur monde
# (M100.1 / M100.2) ; le test lui-même est portable.
if(WIN32)
  add_executable(editor_camera_controller_tests engine/editor/world/tests/EditorCameraControllerTests.cpp)
  target_include_directories(editor_camera_controller_tests PRIVATE ${CMAKE_SOURCE_DIR})
  target_link_libraries(editor_camera_controller_tests PRIVATE engine_core)
  if(MSVC)
    target_compile_options(editor_camera_controller_tests PRIVATE /W4 /permissive- /Zc:preprocessor)
  endif()
  add_test(NAME editor_camera_controller_tests COMMAND editor_camera_controller_tests)
endif()

# M100.6 — Tests unitaires CPU pour la sculpture de terrain.
# Couvre le kernel de brosse (Raise/Lower/Smooth/Flatten/Noise), le merge des
# commandes par mergeKey de stroke, et un sanity-check de la couture
# inter-chunks. Pure CPU (pas d'ImGui ni Vulkan), gating WIN32 pour cohérence
# avec les autres tests éditeur monde.
if(WIN32)
  add_executable(terrain_sculpt_tests engine/editor/world/tests/TerrainSculptTests.cpp)
  target_include_directories(terrain_sculpt_tests PRIVATE ${CMAKE_SOURCE_DIR})
  target_link_libraries(terrain_sculpt_tests PRIVATE engine_core)
  if(MSVC)
    target_compile_options(terrain_sculpt_tests PRIVATE /W4 /permissive- /Zc:preprocessor)
  endif()
  add_test(NAME terrain_sculpt_tests COMMAND terrain_sculpt_tests)
endif()

# M100.7 — Tests unitaires CPU pour les stamps & generators procéduraux.
# Couvre les 3 archétypes (Mountain/Valley/Crater), la conversion uint16→float
# du loader StampLibrary, la TerrainStampCommand (Execute/Undo) et le mode
# Replace (overwrite vs Add). Pure CPU, gating WIN32 (pattern terrain_sculpt).
if(WIN32)
  add_executable(terrain_stamp_tests engine/editor/world/tests/TerrainStampTests.cpp)
  target_include_directories(terrain_stamp_tests PRIVATE ${CMAKE_SOURCE_DIR})
  target_link_libraries(terrain_stamp_tests PRIVATE engine_core)
  if(MSVC)
    target_compile_options(terrain_stamp_tests PRIVATE /W4 /permissive- /Zc:preprocessor)
  endif()
  add_test(NAME terrain_stamp_tests COMMAND terrain_stamp_tests)
endif()

# M100.10 — Tests unitaires CPU pour la peinture splat (manual + auto-rules).
# Couvre l'invariant somme=255, le filtrage MatchesRules, la monotonie du
# falloff radial, la couture inter-chunks bord-à-bord, et la coalescence par
# stroke (1 entrée d'historique par geste). Pure CPU, gating WIN32.
if(WIN32)
  add_executable(splat_paint_tests engine/editor/world/tests/SplatPaintTests.cpp)
  target_include_directories(splat_paint_tests PRIVATE ${CMAKE_SOURCE_DIR})
  target_link_libraries(splat_paint_tests PRIVATE engine_core)
  if(MSVC)
    target_compile_options(splat_paint_tests PRIVATE /W4 /permissive- /Zc:preprocessor)
  endif()
  add_test(NAME splat_paint_tests COMMAND splat_paint_tests)
endif()

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
  if(NOT LCDLLN_OPENSSL_DLL_DIR STREQUAL "")
    add_custom_command(TARGET client_flow_app POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              "${LCDLLN_OPENSSL_DLL_DIR}/libssl-3-x64.dll"
              "$<TARGET_FILE_DIR:client_flow_app>/"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              "${LCDLLN_OPENSSL_DLL_DIR}/libcrypto-3-x64.dll"
              "$<TARGET_FILE_DIR:client_flow_app>/"
      COMMENT "Copie des DLL OpenSSL à côté de client_flow_app.exe..."
    )
  endif()
  if(EXISTS "${CMAKE_SOURCE_DIR}/external/external_links.json")
    add_custom_command(TARGET client_flow_app POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:client_flow_app>/external"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              "${CMAKE_SOURCE_DIR}/external/external_links.json"
              "$<TARGET_FILE_DIR:client_flow_app>/external/external_links.json"
      COMMENT "Copie de external/external_links.json (client_flow_app)..."
    )
  endif()
endif()

# M09.4: HLOD builder (offline tool, no Vulkan)
add_subdirectory(tools/hlod_builder)
# M100.3: Zone builder library (statique, partagée par l'exécutable offline et
# le futur exporter éditeur — anti-duplication des writers binaires).
# Doit être déclarée AVANT add_subdirectory(tools/zone_builder) pour que la
# cible `zone_builder_lib` existe au moment où l'exe la référence.
add_subdirectory(tools/zone_builder/lib)
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

# M25.1: Load tester (TLS multi-connection) - Windows only (reuses NetClient TLS)
add_subdirectory(tools/load_tester)

# M100.9: Mini-tool C++ standalone qui génère 8 PNG 4x4 unicolore dans
# assets/terrain/placeholders/ via custom command POST_BUILD. Sert de fallback
# visuellement distinct pour le shader 8-layer terrain en attendant les vrais
# assets PBR. Pas de dépendance zlib externe (deflate stored block).
add_subdirectory(tools/gen_terrain_placeholders)