cmake_minimum_required(VERSION 3.20)
project(LCDLLN_Engine LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Force /MD (DLL CRT) pour toutes les configurations MSVC
if(MSVC)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")
endif()

# Modules CMake reutilisables (parite cmangos-tbc/cmake/).
# Charger AVANT add_subdirectory(src) pour que les helpers comme
# lcdlln_add_simple_test() soient disponibles dans les sous-CMakeLists.
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
include(LCDLLNHelpers)

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

add_library(engine_auth STATIC src/shared/auth/Argon2Hash.cpp)
target_include_directories(engine_auth PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(engine_auth PUBLIC argon2_ref OpenSSL::SSL)
if(MSVC)
  target_compile_options(engine_auth PRIVATE /W4 /permissive- /Zc:preprocessor)
endif()

# M101.1 — Lib pure du modele de graphe de routine (cluster M101 : editeur de
# routines nodal). AUCUNE dependance Vulkan/GLFW/reseau : reutilisable par
# engine_core (client + editeur) ET par server_app (shard, traduction PNJ M101.7).
add_library(routine_graph STATIC
  src/shared/routine/RoutineNodeSchema.cpp
  src/shared/routine/RoutineSerialization.cpp
  src/shared/routine/RoutineGraphValidation.cpp
  src/shared/routine/RoutineSegmentCodec.cpp
)
target_include_directories(routine_graph PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
if(MSVC)
  target_compile_options(routine_graph PRIVATE /W4 /permissive- /Zc:preprocessor)
else()
  target_compile_options(routine_graph PRIVATE -Wall -Wextra -Wpedantic)
endif()

# M101.2 — VM d'interpretation deterministe des graphes zone_event. Lib pure
# (depend de routine_graph + IRoutineHost ; aucune dependance Vulkan/GLFW).
add_library(routine_vm STATIC
  src/shared/routine/vm/ZoneEventVm.cpp
)
target_include_directories(routine_vm PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(routine_vm PUBLIC routine_graph)
if(MSVC)
  target_compile_options(routine_vm PRIVATE /W4 /permissive- /Zc:preprocessor)
else()
  target_compile_options(routine_vm PRIVATE -Wall -Wextra -Wpedantic)
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

set_source_files_properties(src/client/render/vk/VmaAllocator.cpp PROPERTIES
    COMPILE_DEFINITIONS "VMA_IMPLEMENTATION;VMA_STATIC_VULKAN_FUNCTIONS=0;VMA_DYNAMIC_VULKAN_FUNCTIONS=0"
)

add_library(engine_core
  src/client/render/vk/VmaAllocator.cpp
  src/client/render/vk/VkUtils.cpp
  src/shared/core/Clock.cpp
  src/shared/core/Config.cpp
  src/shared/core/ServerEndpoints.cpp
  src/shared/core/Profiler.cpp
  src/shared/core/jobs/JobSystem.cpp
  src/shared/core/Log.cpp
  src/shared/core/LogConfig.cpp
  src/shared/core/Time.cpp
  src/client/audio/AudioEngine.cpp
  src/client/audio/MaMenuMusic.cpp
  src/client/combat/CombatHud.cpp
  src/client/localization/LocalizationService.cpp
  src/client/auth/AuthUiPresenterCore.cpp
  src/client/auth/AuthUiPresenterSettings.cpp
  src/client/auth/AuthUiPresenterNative.cpp
  src/client/auth/screens/AuthScreenLanguageSelect.cpp
  src/client/auth/screens/AuthScreenLogin.cpp
  src/client/auth/screens/AuthScreenRegister.cpp
  src/client/auth/screens/AuthScreenForgotPassword.cpp
  src/client/auth/screens/AuthScreenVerifyEmail.cpp
  src/client/auth/screens/AuthScreenOptions.cpp
  src/client/auth/screens/AuthScreenError.cpp
  src/client/auth/screens/AuthScreenShardPick.cpp
  src/client/auth/screens/AuthScreenTerms.cpp
  src/client/auth/screens/AuthScreenCharacterCreate.cpp
  src/client/auth/screens/AuthScreenCharacterSelect.cpp
  src/client/combat/AoEPreviewSystem.cpp
  src/client/combat/BuffBarPresenter.cpp
  src/client/combat/AuraFXSystem.cpp
  src/client/fx/FXManager.cpp
  src/client/inventory/InventoryUi.cpp
  src/client/trade/TradeWindowUi.cpp
  src/client/crafting/HarvestCastBar.cpp
  src/client/crafting/CraftingUi.cpp
  src/client/character_creation/CharacterCreationUi.cpp
  src/client/character_creation/CharacterCustomizationSystem.cpp
  src/client/render/race/RacePreviewViewport.cpp
  src/client/settings/SettingsMenuUi.cpp
  src/client/hud/HudLayoutEditor.cpp
  src/client/combat/AdvancedCombatUi.cpp
  src/client/economy/AuctionUi.cpp
  src/client/economy/ShopUi.cpp
  src/client/net/GameplayUdpClient.cpp
  src/client/debug/ProfilerHud.cpp
  src/client/quest/QuestUi.cpp
  src/client/dialogue/DialogueTree.cpp
  src/client/dialogue/DialoguePresenter.cpp
  src/client/dialogue/DialogueConfigLoader.cpp
  src/client/dialogue/QuestConversationJournal.cpp
  src/client/ui_common/ThemeManager.cpp
  src/client/ui_common/UIModel.cpp
  src/client/chat/ChatUi.cpp
  src/client/chat/ChatWorldVisualPresenter.cpp
  src/client/mail/MailUi.cpp
  src/client/social/FriendsUi.cpp
  src/client/social/IgnoreListUi.cpp
  src/client/gmtickets/GmTicketUi.cpp
  src/client/reputation/ReputationUi.cpp
  src/client/arena/ArenaUi.cpp
  src/client/battleground/BattleGroundUi.cpp
  src/client/outdoorpvp/OutdoorPvpUi.cpp
  src/client/weather/WeatherUi.cpp
  src/client/events/GameEventUi.cpp
  src/client/guild/GuildUi.cpp
  src/client/auction/AuctionUi.cpp
  src/client/loot/LootRollUi.cpp
  src/client/lfg/LfgUi.cpp
  src/client/cinematics/CinematicUi.cpp
  src/client/skills/SkillBookUi.cpp
  src/client/social/PartyHud.cpp
  src/client/world/ZonePreloadHook.cpp
  src/shared/net/ChatSystem.cpp
  src/shared/net/ChatEmotes.cpp
  src/world_editor/ui/EditorMode.cpp
  src/world_editor/ui/WorldEditorImGui.cpp
  src/world_editor/ui/WorldMapIo.cpp
  src/world_editor/ui/TreeSpeciesCatalog.cpp
  src/world_editor/ui/WorldEditorSession.cpp
  src/world_editor/ui/TexturePreviewCache.cpp
  src/world_editor/ui/TextureLibraryPanel.cpp
  # M100.34 incrément 1 — Cible offscreen R8G8B8A8 dédiée au viewport
  # éditeur, exposée à ImGui pour affichage dans le ScenePanel.
  # L'image est créée + transitionnée SHADER_READ_ONLY mais reste noire
  # tant que la PR 2 ne branche pas la copie depuis SceneColor_LDR.
  src/world_editor/render/EditorViewportRenderTarget.cpp
  # M100.1 — World editor "couche au-dessus" (engine::editor::world).
  # Cohabite avec WorldEditorImGui ; activé par --editor-world ou
  # editor.world.enabled. Pas référencé par engine/server/.
  src/world_editor/core/WorldEditorShell.cpp
  src/world_editor/core/CommandStack.cpp
  # M101.4 / M101.5 — editeur de routines nodal (panel + commandes + palette/inspecteur).
  src/world_editor/routine/RoutineGraphCommands.cpp
  src/world_editor/routine/RoutineGraphPanel.cpp
  src/world_editor/routine/RoutineNodePalette.cpp
  src/world_editor/routine/RoutineNodeInspector.cpp
  # M100.16 — Hazard volumes (simulateur client + outil editeur ; format header-only).
  src/client/world/hazard/HazardSimulator.cpp
  src/world_editor/HazardTool.cpp
  # M100.17 — Easy Placement Tool (geometrie pure + outil ; format/commande header-only).
  src/world_editor/PlacementGeometry.cpp
  src/world_editor/PlacementTool.cpp
  # M100.32 — Interactive Props (simulateur d'animation pur + panneau editeur ;
  # types/format/relais header-only ; payloads reseau partages).
  src/client/world/interactive/InteractiveSimulator.cpp
  src/world_editor/InteractivePanel.cpp
  # M100.34 — Selection/Layers/Minimap/Export zone (geometrie + calques + export
  # via Save*Bin header-only ; panneau minimap ImGui guarde).
  src/world_editor/SelectionTool.cpp
  src/world_editor/LayersDocument.cpp
  src/world_editor/DeleteCommand.cpp
  src/world_editor/WorldEditorExporter.cpp
  src/world_editor/panels/MinimapPanel.cpp
  # M100.18 — Vegetation library + density painting (library JSON + Poisson-disk + outil).
  src/client/world/foliage/FoliageLibrary.cpp
  src/client/world/foliage/PoissonDiskSampler.cpp
  src/world_editor/FoliagePaintTool.cpp
  # M100.19 — Forest & Field procedural tools (generateurs purs + outils).
  src/world_editor/ForestFieldGen.cpp
  src/world_editor/ForestTool.cpp
  src/world_editor/FieldTool.cpp
  # M100.20 — Vegetation wind (WindSystem eval + zones + outil).
  src/client/world/wind/WindSystem.cpp
  src/world_editor/WindZoneTool.cpp
  # M100.21 — Player interaction (collecteur d'influences d'entites).
  src/client/world/foliage/EntityInfluenceCollector.cpp
  # M100.27 — Shade map + thermal (ComputeTemperature + builder).
  src/client/world/thermal/Thermal.cpp
  src/client/world/thermal/ShadeMapBuilder.cpp
  # M100.28 — Gameplay zones (requetes + outil).
  src/client/world/zones/ZoneQuery.cpp
  src/world_editor/ZoneTool.cpp
  # M100.29 — Spline tool & roads (sampler pur + outil).
  src/client/world/spline/SplineSampler.cpp
  src/world_editor/SplineTool.cpp
  # M100.30 — Bridges & modular walls (structures pures ; outils header-only).
  src/client/world/structures/Structures.cpp
  # M100.31 — Hamlet generator (generation pure + kit JSON ; outil header-only).
  src/client/world/structures/HamletKitLibrary.cpp
  src/world_editor/HamletGen.cpp
  # M100.33 — Footstep audio (resolution pure ; PlaytestMode header-only).
  src/client/audio/FootstepAudioSurfaceHook.cpp
  # Sous-projet 1, bloc B — modele de scene + selection partagee.
  src/world_editor/scene/EditorSelection.cpp
  src/world_editor/scene/EditorSceneModel.cpp
  src/world_editor/scene/EntityKey.cpp
  src/world_editor/scene/SelectionQuery.cpp
  src/world_editor/scene/DeleteEntitiesCommand.cpp
  # Sous-projet 1, bloc D — commande undoable d'edition de transform.
  src/world_editor/inspector/SetEntityTransformCommand.cpp
  src/world_editor/terrain/TerrainDocument.cpp
  src/world_editor/terrain/TerrainBrush.cpp
  src/world_editor/terrain/TerrainRaycast.cpp
  src/world_editor/terrain/TerrainSculptCommand.cpp
  src/world_editor/terrain/TerrainSculptTool.cpp
  src/world_editor/terrain/TerrainStampCommand.cpp
  src/world_editor/terrain/TerrainStampTool.cpp
  src/world_editor/terrain/StampLibrary.cpp
  src/world_editor/terrain/ProceduralStampGenerators.cpp
  # M100.35 — Toolbar à icônes + outils macros terrain (Mountain Range &
  # Valley Chain). Pas de Vulkan, pas de PNG d'icônes embarqués : la toolbar
  # rend des placeholders procéduraux (carré coloré + lettre) via
  # ImDrawList. Voir game/data/icons/editor/tools/README.md.
  src/world_editor/ui/EditorToolbar.cpp
  src/world_editor/ui/ToolbarIconAtlas.cpp
  src/world_editor/terrain/PolylineMacroCore.cpp
  src/world_editor/terrain/MacroPolylineCommandBase.cpp
  src/world_editor/terrain/MountainRangeCommand.cpp
  src/world_editor/terrain/MountainRangeTool.cpp
  src/world_editor/terrain/ValleyChainCommand.cpp
  src/world_editor/terrain/ValleyChainTool.cpp
  src/world_editor/splat/SplatPaintCommand.cpp
  src/world_editor/splat/SplatPaintTool.cpp
  src/world_editor/splat/SplatRules.cpp
  src/world_editor/camera/EditorCameraController.cpp
  src/world_editor/camera/CollisionPreviewCamera.cpp
  src/world_editor/water/WaterDocument.cpp
  src/world_editor/water/AddLakeCommand.cpp
  src/world_editor/water/AddRiverCommand.cpp
  src/world_editor/water/LakeTool.cpp
  src/world_editor/water/RiverTool.cpp
  # M100.36 — River Network Generator (watershed D8 + OceanSettings)
  src/world_editor/water/ConsolidatedHeightGrid.cpp
  # M100.46 — helper partagé qui assemble un grid 2x2 chunks depuis le
  # TerrainDocument (anciennement dupliqué dans CoastlineEditorTool,
  # ThermalWindErosionTool et OperationDispatcher).
  src/world_editor/water/HeightGridAssembly.cpp
  src/world_editor/water/D8FlowDirection.cpp
  src/world_editor/water/FlowAccumulation.cpp
  src/world_editor/water/PathSimplifyDouglasPeucker.cpp
  src/world_editor/water/WatershedSimulation.cpp
  src/world_editor/water/RiverNetworkCommand.cpp
  src/world_editor/water/RiverNetworkTool.cpp
  # M100.37 — Coastline & Sea Level Editor (marching squares + smoothing
  # + cliffs ; étend `OceanSettings` v3 + `LakeInstance::isOcean`).
  src/world_editor/water/CoastlineSegmentExtractor.cpp
  src/world_editor/water/CoastlineStats.cpp
  src/world_editor/water/CoastlineSmoothing.cpp
  src/world_editor/water/CoastlineCliffs.cpp
  src/world_editor/water/CoastlineCommand.cpp
  src/world_editor/water/CoastlineEditorTool.cpp
  # M100.38 — Hydraulic Erosion (particle-based, réutilise grid
  # consolidé + D8 + flow accumulation de M100.36).
  src/world_editor/terrain/erosion/BilinearGradientSample.cpp
  src/world_editor/terrain/erosion/DropletInitDistribution.cpp
  src/world_editor/terrain/erosion/DropletKernel.cpp
  src/world_editor/terrain/erosion/HydraulicSimulation.cpp
  src/world_editor/terrain/erosion/HydraulicErosionCommand.cpp
  src/world_editor/terrain/erosion/HydraulicErosionTool.cpp
  # M100.39 — Thermal & Wind Erosion (clôt Phase 2.5 terrain naturaliste).
  src/world_editor/terrain/erosion/ThermalSimulation.cpp
  src/world_editor/terrain/erosion/WindSimulation.cpp
  src/world_editor/terrain/erosion/ThermalWindErosionCommand.cpp
  src/world_editor/terrain/erosion/ThermalWindErosionTool.cpp
  # M100.40 — Mesh Insert Foundation + Cave Tool (démarre Phase 11
  # « Volumes 3D »). MVP éditeur-side : data model + IO binaire LCMI v1 +
  # CRUD document + outil Cave avec catalog JSON + camouflage splat.
  # Rendu glTF runtime + gizmo + auto-props reportés en follow-ups.
  src/world_editor/volumes/MeshInsertIo.cpp
  src/world_editor/volumes/MeshInsertDocument.cpp
  src/world_editor/volumes/caves/CaveCatalog.cpp
  src/world_editor/volumes/caves/CaveCamouflage.cpp
  src/world_editor/volumes/caves/PlaceCaveCommand.cpp
  src/world_editor/volumes/caves/CaveTool.cpp
  # M100.41 — Overhang Cliff Tool (Phase 11, réutilise MeshInsertSystem).
  # Pas de nouveau format binaire ; instances avec
  # `insertCategory = "overhang"` partagent `instances/mesh_inserts.bin`
  # avec les grottes de M100.40.
  src/world_editor/volumes/overhangs/OverhangCatalog.cpp
  src/world_editor/volumes/overhangs/PlaceOverhangCommand.cpp
  src/world_editor/volumes/overhangs/OverhangTool.cpp
  # M100.42 — Natural Arch Tool (Phase 11, réutilise MeshInsertSystem).
  # Pas de nouveau format binaire ; instances avec
  # `insertCategory = "arch"` partagent `instances/mesh_inserts.bin`.
  src/world_editor/volumes/arches/ArchCatalog.cpp
  src/world_editor/volumes/arches/PlaceArchCommand.cpp
  src/world_editor/volumes/arches/ArchTool.cpp
  # M100.43 — Dungeon Portal System (Phase 11). Premier ticket Phase 11
  # qui touche au serveur : opcodes 197/198 + migration 0063 +
  # nouveau format binaire LCDP v1 dans `instances/dungeon_portals.bin`
  # (distinct de LCMI car porte des metadata gameplay).
  src/world_editor/volumes/dungeons/DungeonCatalog.cpp
  src/world_editor/volumes/dungeons/DungeonPortalDocument.cpp
  src/world_editor/volumes/dungeons/DungeonPortalIo.cpp
  src/world_editor/volumes/dungeons/DungeonPortalTool.cpp
  src/world_editor/volumes/dungeons/PlaceDungeonPortalCommand.cpp
  # M100.44 — VMap Bridge & Phase 11 Validation (clôture Phase 11).
  # VMapBridge : export AABB proxy collision (LCVC v1) depuis les
  # documents de volumes. Phase11Validator : checks de cohérence
  # cross-fichiers avant export.
  src/world_editor/volumes/bridge/VMapBridge.cpp
  src/world_editor/volumes/bridge/Phase11Validator.cpp
  # M100.48 — Zone Validation Service (Phase 12). Registre de règles + service
  # ZoneValidator (lecture seule sur les données de zone) + règles MVP
  # (heightmap/splat/mesh inserts). Éditeur-only, pas de validation runtime.
  src/world_editor/validation/ValidationRuleRegistry.cpp
  src/world_editor/validation/ZoneValidator.cpp
  src/world_editor/validation/rules/HeightmapRules.cpp
  src/world_editor/validation/rules/SplatRules.cpp
  src/world_editor/validation/rules/MeshInsertRules.cpp
  # M100.45 — Phase 12 « Accessibilité » — infrastructure transverse
  # (Phase A) : registry de mode Simple/Advanced, registry de presets
  # d'outils (JSON), store de préférences utilisateur persistées.
  src/world_editor/modes/EditorModeRegistry.cpp
  src/world_editor/presets/ToolPresetIo.cpp
  src/world_editor/presets/ToolPresetRegistry.cpp
  src/world_editor/prefs/UserPrefsStore.cpp
  # M100.49 — Tutorial & Overlay guidance (coeur pur ; diagnostic differe car
  # depend de M100.48). OverlayGuidanceSystem = moteur de sequence ;
  # TutorialSystem = pilotage + flags ; TutorialIo = tutoriel "first_launch" code.
  src/world_editor/help/OverlayGuidanceSystem.cpp
  src/world_editor/tutorial/TutorialSystem.cpp
  src/world_editor/tutorial/TutorialIo.cpp
  # M100.49 partie 2 — Système de diagnostic « Pourquoi ça ne marche pas ? »
  # (registre + système + 10 règles workflow). S'appuie sur le ValidationContext
  # de M100.48. Cœur pur ; UI panel/one-click = passe séparée.
  src/world_editor/diagnostic/DiagnosticRuleRegistry.cpp
  src/world_editor/diagnostic/DiagnosticSystem.cpp
  src/world_editor/diagnostic/rules/WorkflowRules.cpp
  # M100.50 — Quick Start Wizard (cœur pur : FSM 5 étapes + résolution choix →
  # ZonePreset M100.46 + conditions/substitution + auto-gen déterministe).
  # UI (cartes, aperçu 3D) + exécution = passe séparée.
  src/world_editor/wizard/QuickStartWizard.cpp
  src/world_editor/wizard/ConditionEvaluator.cpp
  src/world_editor/wizard/AutoGenerators.cpp
  src/world_editor/wizard/WizardTemplateResolver.cpp
  # M100.46 — Zone Presets Library. Incrément 1 : socle data (format
  # JSON + parsing + registry + validation). Incrément 2a : couche de
  # paramètres typés (OperationParams) + customisation (relief/eau/
  # sécheresse). Le dispatcher + l'exécuteur + le reset viennent ensuite.
  src/world_editor/zone_presets/ZonePreset.cpp
  src/world_editor/zone_presets/ZonePresetIo.cpp
  src/world_editor/zone_presets/ZonePresetRegistry.cpp
  src/world_editor/zone_presets/OperationParams.cpp
  src/world_editor/zone_presets/CustomizationApplier.cpp
  # Incrément 2b : Reset des documents + dispatcher + exécuteur.
  # Incrément 2c : place_arch câblé. Incrément 2d : mountain_macro /
  # valley_macro / lake_polygon / river_manual câblés. Au total 8/14
  # types câblés ; les 4 restants (coastline, river_network,
  # hydraulic_erosion, thermal_wind_erosion) demandent d'extraire la
  # simulation des Tools UI (incrément 2e+).
  src/world_editor/zone_presets/WorldMapEditDocumentReset.cpp
  src/world_editor/zone_presets/OperationDispatcher.cpp
  src/world_editor/zone_presets/ZonePresetExecutor.cpp
  # Incrément 3 : dialog modal ImGui pour appliquer un preset à la
  # carte courante (entrée menu Fichier > "Appliquer un preset de
  # zone..."). Single-thread : bloque le main thread pendant Execute.
  src/world_editor/zone_presets/ZonePresetDialog.cpp
  # M100.47 incrément 1 — HelpContentStore : socle data des tooltips
  # riches (chargement editor/tooltips/*.json, lookup par id, hot-reload).
  src/world_editor/help/HelpContentStore.cpp
  # M100.47 incrément 3 — RichTooltipWidget : popup ImGui au survol d'un
  # widget. Consomme HelpContentStore. Mode Simple/Advanced piloté par
  # EditorModeRegistry. Hooké dans ToolPropertiesPanel (sliders macro
  # polyline en démo). HelpWindow F1 viendra en incrément suivant.
  src/world_editor/ui/RichTooltipWidget.cpp
  # M100.45 Phase B — migration des outils vers Simple/Advanced + presets.
  # ToolPresetApply : mapping pur preset -> struct d'outil (testable).
  # PresetDropdownWidget : widget ImGui A.6 réutilisable par les panels.
  src/world_editor/presets/ToolPresetApply.cpp
  src/world_editor/ui/PresetDropdownWidget.cpp
  src/world_editor/panels/ScenePanel.cpp
  src/world_editor/panels/InspectorPanel.cpp
  src/world_editor/panels/AssetBrowserPanel.cpp
  src/world_editor/panels/OutlinerPanel.cpp
  src/world_editor/panels/ConsolePanel.cpp
  src/world_editor/panels/ToolPropertiesPanel.cpp
  src/world_editor/panels/HistoryPanel.cpp
  src/world_editor/panels/SurfaceTablePanel.cpp
  src/world_editor/panels/CollisionEditorPanel.cpp
  src/client/app/Engine.cpp
  src/shared/platform/FileSystem.cpp
  src/shared/platform/FileWatcher.cpp
  src/client/render/vk/VkInstance.cpp
  src/client/render/vk/VkDeviceContext.cpp
  src/client/render/vk/VkSwapchain.cpp
  src/client/render/vk/VkFrameSync.cpp
  src/client/render/FrameGraph.cpp
  src/client/render/AssetRegistry.cpp
  src/client/render/Material.cpp
  src/client/render/ShaderCompiler.cpp
  src/client/render/ShaderCache.cpp
  src/client/render/ShaderHotReload.cpp
  src/client/render/Camera.cpp
  src/client/render/AuthGlyphPass.cpp
  src/client/render/FontAtlasTtf.cpp
  src/client/render/AuthLogoPass.cpp
  src/client/render/auth/AuthUiRendererCore.cpp
  src/client/render/auth/AuthImGuiCommon.cpp
  src/client/render/auth/screens/AuthImGuiLanguageSelect.cpp
  src/client/render/auth/screens/AuthImGuiLogin.cpp
  src/client/render/auth/screens/AuthImGuiRegister.cpp
  src/client/render/auth/screens/AuthImGuiError.cpp
  src/client/render/auth/screens/AuthImGuiVerifyEmail.cpp
  src/client/render/auth/screens/AuthImGuiForgotPassword.cpp
  src/client/render/auth/screens/AuthImGuiOptions.cpp
  src/client/render/auth/screens/AuthImGuiShardPick.cpp
  src/client/render/auth/screens/AuthImGuiTerms.cpp
  src/client/render/auth/screens/AuthImGuiCharacterCreate.cpp
  src/client/render/auth/screens/AuthImGuiCharacterSelect.cpp
  src/client/render/LnTheme.h
  src/client/render/AuthImGuiRenderer.cpp
  src/client/render/ChatImGuiRenderer.cpp
  src/client/render/DialogueImGuiRenderer.cpp
  src/client/render/MailImGuiRenderer.cpp
  src/client/render/GmTicketImGuiRenderer.cpp
  src/client/render/ReputationImGuiRenderer.cpp
  src/client/render/CharacterSheetImGuiRenderer.cpp
  src/client/render/ArenaImGuiRenderer.cpp
  src/client/render/BattleGroundImGuiRenderer.cpp
  src/client/render/OutdoorPvpImGuiRenderer.cpp
  src/client/render/WeatherImGuiRenderer.cpp
  src/client/render/GameEventImGuiRenderer.cpp
  src/client/render/GuildImGuiRenderer.cpp
  src/client/render/AuctionImGuiRenderer.cpp
  src/client/render/LootRollImGuiRenderer.cpp
  src/client/render/LfgImGuiRenderer.cpp
  src/client/render/CinematicImGuiRenderer.cpp
  src/client/render/SkillBookImGuiRenderer.cpp
  src/client/render/EditorHubImGuiRenderer.cpp
  src/client/render/DecalSystem.cpp
  src/client/render/DecalPass.cpp
  src/client/render/ParticleSystem.cpp
  src/client/render/ParticleBillboardPass.cpp
  src/client/render/UnderwaterPass.cpp
  src/client/render/WaterMeshGpu.cpp
  src/client/render/WaterPass.cpp
  src/client/render/DayNightCycle.cpp
  src/client/render/SkyPass.cpp
  src/client/render/WeatherSystem.cpp
  src/client/render/clouds/CloudWeatherMapper.cpp
  src/client/render/DynamicLightSystem.cpp
  src/client/render/CascadedShadowMaps.cpp
  src/client/render/BrdfLutPass.cpp
  src/client/render/SpecularPrefilterPass.cpp
  src/client/render/SkyCubeCapturePass.cpp
  src/client/render/IrradiancePass.cpp
  src/client/render/SsaoKernelNoise.cpp
  src/client/render/SsaoPass.cpp
  src/client/render/SsaoBlurPass.cpp
  src/client/render/ShadowMapPass.cpp
  src/client/render/PsoKey.cpp
  src/client/render/PipelineCache.cpp
  src/client/render/GeometryPass.cpp
  src/client/render/TerrainChunkPipeline.cpp
  src/client/render/terrain_chunk/ChunkRuntime.cpp
  src/client/render/terrain_chunk/DescriptorSetPool.cpp
  src/client/render/terrain_chunk/LayerArrayLoader.cpp
  src/client/render/terrain_chunk/SplatMapGpuCache.cpp
  src/client/render/terrain_chunk/TerrainChunkRenderer.cpp
  src/client/render/terrain_chunk/TerrainMeshGpuCache.cpp
  src/client/render/GpuDrivenCullingPass.cpp
  src/client/render/HiZPyramidPass.cpp
  src/client/render/LightingPass.cpp
  src/client/render/gi/DdgiVolume.cpp
  src/client/render/gi/DdgiUpdatePass.cpp
  src/client/render/gi/DdgiQuality.cpp
  src/client/render/VolumetricFogPass.cpp
  src/client/render/CloudPass.cpp
  src/client/render/DepthOfFieldPass.cpp
  src/client/render/ImpostorPass.cpp
  src/client/render/ImpostorAsset.cpp
  src/client/render/TonemapPass.cpp
  src/client/render/BloomPass.cpp
  src/client/render/AutoExposure.cpp
  src/client/render/TaaPass.cpp
  src/client/render/DeferredPipeline.cpp
  src/shared/math/Frustum.cpp
  src/shared/math/Quat.cpp
  src/client/render/skinned/Skeleton.cpp
  src/client/render/skinned/AnimationClip.cpp
  src/client/render/skinned/AnimationSampler.cpp
  src/client/render/skinned/AnimationCrossfade.cpp
  src/client/render/skinned/SkinnedMeshLoader.cpp
  src/client/render/skinned/SkinnedMesh.cpp
  src/client/render/skinned/SkinnedRenderer.cpp
  src/client/render/skinned/AvatarMaterialRouting.cpp
  src/client/render/static_mesh/StaticMeshLoader.cpp
  src/client/gameplay/CharacterController.cpp
  src/client/gameplay/TerrainCollider.cpp
  src/client/gameplay/CompositeWorldCollider.cpp
  src/client/gameplay/ThirdPersonCamera.cpp
  src/client/gameplay/SkillSystem.cpp
  src/client/gameplay/ClientPrediction.cpp
  src/client/gameplay/StatusEffect.cpp
  src/client/world/WorldModel.cpp
  src/client/world/OutputVersion.cpp
  src/client/world/ProbeData.cpp
  src/client/world/PakReader.cpp
  src/client/world/ChunkPackageLayout.cpp
  src/client/world/StreamingScheduler.cpp
  src/client/world/StreamCache.cpp
  src/client/world/ChunkBudgetStats.cpp
  src/client/world/surface/SurfaceType.cpp
  src/client/world/surface/SurfaceTable.cpp
  src/client/world/surface/SurfaceQueryService.cpp
  src/client/world/collision/CollisionProxy.cpp
  src/client/world/collision/AutoFitProxy.cpp
  src/client/world/collision/ProxyWireframe.cpp
  src/client/world/water/WaterSurfaces.cpp
  src/client/world/water/WaterMeshBuilder.cpp
  src/client/world/terrain/LayerPalette.cpp
  src/client/world/terrain/SplatMap.cpp
  src/client/world/terrain/TerrainChunk.cpp
  src/client/world/terrain/TerrainChunkLoader.cpp
  src/client/world/terrain/HeightmapHeightField.cpp
  src/client/world/terrain/ChunkHeightField.cpp
  src/client/world/terrain/TerrainLodChain.cpp
  src/client/world/terrain/TerrainLodWorker.cpp
  src/client/world/terrain/TerrainMeshBuilder.cpp
  src/client/render/vk/DeferredDestroyQueue.cpp
  src/client/render/GpuUploadQueue.cpp
  src/client/render/vk/StagingAllocator.cpp
  src/client/world/LodConfig.cpp
  src/client/world/HlodRuntime.cpp
  src/client/render/terrain/HeightmapLoader.cpp
  src/client/render/terrain/TerrainMesh.cpp
  src/client/render/terrain/TerrainSplatting.cpp
  src/client/render/terrain/SplatSampling.cpp
  src/client/render/terrain/TerrainHoleMask.cpp
  src/client/render/terrain/TerrainGrassDetail.cpp
  src/client/render/terrain/TerrainCliffMesh.cpp
  src/client/render/terrain/TerrainRenderer.cpp
  src/client/render/terrain/TerrainEditingTools.cpp
  src/shared/network/ByteWriter.cpp
  src/shared/network/ByteReader.cpp
  src/shared/network/PacketBuilder.cpp
  src/shared/network/PacketView.cpp
  src/shared/network/RequestResponseDispatcher.cpp
  src/shared/network/ErrorPacket.cpp
  src/shared/network/AuthRegisterPayloads.cpp
  src/shared/network/CharacterPayloads.cpp
  src/shared/network/ChatPayloads.cpp
  src/shared/network/DungeonPayloads.cpp   # M100.43 — opcodes 197/198, réservés (handler M100.44)
  src/shared/network/IgnoreListPayloads.cpp
  src/shared/network/MailPayloads.cpp
  src/shared/network/QuestPayloads.cpp
  src/shared/network/GmTicketPayloads.cpp
  src/shared/network/ReputationPayloads.cpp
  src/shared/network/ArenaPayloads.cpp
  src/shared/network/BattleGroundPayloads.cpp
  src/shared/network/OutdoorPvpPayloads.cpp
  src/shared/network/WeatherPayloads.cpp
  src/shared/network/GameEventPayloads.cpp
  src/shared/network/InteractivePayloads.cpp
  src/shared/network/GuildPayloads.cpp
  src/shared/network/AuctionPayloads.cpp
  src/shared/network/LootPayloads.cpp
  src/shared/network/LunarPayloads.cpp
  src/shared/network/WorldClockPayloads.cpp
  src/shared/network/AdminCommandPayloads.cpp
  src/shared/network/LfgPayloads.cpp
  src/shared/network/CinematicPayloads.cpp
  src/shared/network/SkillPayloads.cpp
  src/shared/network/TradePayloads.cpp
  src/shared/network/ShardPayloads.cpp
  src/shared/network/TermsPayloads.cpp
)
# Shared gameplay wire codec (Encode*/Decode*); linked into client — must not be server-only.
target_sources(engine_core PRIVATE src/shared/network/ServerProtocol.cpp)
target_include_directories(engine_core PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_SOURCE_DIR}/external/stb
  ${CMAKE_SOURCE_DIR}/external/cgltf
  ${CMAKE_SOURCE_DIR}/external/miniaudio
  ${VMA_INCLUDE_DIRS}
)
target_link_libraries(engine_core PUBLIC
  Vulkan::Vulkan
  glfw
  engine_auth
  spdlog::spdlog
  routine_graph
  routine_vm
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
    src/shared/platform/Input.cpp
    src/shared/platform/Window.cpp
    src/shared/network/NetClient.cpp
    src/shared/network/ShardToMasterClient.cpp
    src/shared/network/ServerListPayloads.cpp
    src/shared/network/ShardTicketPayloads.cpp
    src/shared/network/MasterShardClientFlow.cpp
  )
else()
  target_sources(engine_core PRIVATE
    src/shared/platform/Input_linux.cpp
    src/shared/platform/Window_linux.cpp
    src/shared/network/NetClient_linux.cpp
  )
endif()

add_executable(engine_app
  src/client/main.cpp
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
  src/world_editor/main.cpp
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

# Endpoints serveur (master TCP/HTTPS/HTTP tunnel, sonde, portail) — fichier
# éditable embarqué à côté de l'exe : config/server.ini. Auto-recréé au lancement
# s'il est absent (voir ServerEndpoints), mais on le livre dans l'artifact.
if(EXISTS "${CMAKE_SOURCE_DIR}/config/server.ini")
  add_custom_command(TARGET engine_app POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:engine_app>/config"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/config/server.ini"
            "$<TARGET_FILE_DIR:engine_app>/config/server.ini"
    COMMENT "Copie de config/server.ini..."
  )
  add_custom_command(TARGET world_editor_app POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:world_editor_app>/config"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/config/server.ini"
            "$<TARGET_FILE_DIR:world_editor_app>/config/server.ini"
    COMMENT "Copie de config/server.ini à côté de lcdlln_world_editor.exe..."
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
if(EXISTS "${CMAKE_SOURCE_DIR}/config/server.ini")
  install(FILES "${CMAKE_SOURCE_DIR}/config/server.ini" DESTINATION config)
endif()

# M19.2: Protocol v1 serialization unit tests
add_executable(protocol_v1_tests src/shared/network/ProtocolV1Tests.cpp)
target_link_libraries(protocol_v1_tests PRIVATE engine_core)
enable_testing()
add_test(NAME protocol_v1_tests COMMAND protocol_v1_tests)

# M101.1 — Tests de serialisation/round-trip/determinisme du modele de routine.
add_executable(routine_serialization_tests src/shared/routine/tests/RoutineSerializationTests.cpp)
target_link_libraries(routine_serialization_tests PRIVATE routine_graph)
add_test(NAME routine_serialization_tests COMMAND routine_serialization_tests)

# M101.2 — Tests de la VM zone_event (execution + determinisme).
add_executable(routine_vm_tests src/shared/routine/vm/tests/RoutineVmTests.cpp)
target_link_libraries(routine_vm_tests PRIVATE routine_vm)
add_test(NAME routine_vm_tests COMMAND routine_vm_tests)

# M101.6 — Tests de validation de graphe.
add_executable(routine_validation_tests src/shared/routine/tests/RoutineValidationTests.cpp)
target_link_libraries(routine_validation_tests PRIVATE routine_graph)
add_test(NAME routine_validation_tests COMMAND routine_validation_tests)

# M101.3 — Tests du codec routines.bin (round-trip en memoire).
add_executable(routine_segment_codec_tests src/shared/routine/tests/RoutineSegmentCodecTests.cpp)
target_link_libraries(routine_segment_codec_tests PRIVATE routine_graph)
add_test(NAME routine_segment_codec_tests COMMAND routine_segment_codec_tests)

# M101.7 (partiel) — Traduction npc_routine -> EventAIRow (cote shard).
add_executable(routine_to_eventai_tests
  src/shardd/ai/tests/RoutineToEventAITests.cpp
  src/shardd/ai/RoutineToEventAI.cpp)
target_link_libraries(routine_to_eventai_tests PRIVATE routine_graph)
add_test(NAME routine_to_eventai_tests COMMAND routine_to_eventai_tests)

# M101.4 / M101.5 — Tests des commandes d'edition de graphe (apply/undo).
add_executable(routine_graph_commands_tests src/world_editor/routine/tests/RoutineGraphCommandsTests.cpp)
target_link_libraries(routine_graph_commands_tests PRIVATE engine_core)
add_test(NAME routine_graph_commands_tests COMMAND routine_graph_commands_tests)

# M101.11 — Test de parsing du contenu d'aide des routines (format M100.47).
add_executable(routine_help_content_tests src/world_editor/routine/tests/RoutineHelpContentTests.cpp)
target_link_libraries(routine_help_content_tests PRIVATE engine_core)
add_test(NAME routine_help_content_tests COMMAND routine_help_content_tests)

# M100.48 — Tests Zone Validation Service (registre + regles MVP + ZoneValidator
# + incremental). Pure logique dans engine_core. (Ancrage distinct d'interactive_tests
# pour eviter un conflit CMake avec d'autres PR M100 en vol.)
add_executable(zone_validation_tests src/world_editor/tests/ZoneValidationTests.cpp)
target_link_libraries(zone_validation_tests PRIVATE engine_core)
add_test(NAME zone_validation_tests COMMAND zone_validation_tests)

# M100.16 — Tests hazards (round-trip binaire + simulateur). Format header-only,
# simulateur dans engine_core : on ne lie QUE engine_core (pas de doublon).
add_executable(hazard_tests src/client/world/hazard/tests/HazardTests.cpp)
target_link_libraries(hazard_tests PRIVATE engine_core)
add_test(NAME hazard_tests COMMAND hazard_tests)

# M100.32 — Tests Interactive Props (round-trip binaire + simulateur + reseau +
# relais serveur). Types/format/relais header-only ; simulateur + payloads dans
# engine_core : on ne lie QUE engine_core (pas de doublon).
add_executable(interactive_tests src/client/world/interactive/tests/InteractiveTests.cpp)
target_link_libraries(interactive_tests PRIVATE engine_core)
add_test(NAME interactive_tests COMMAND interactive_tests)

# M100.34 — Tests Selection/Layers/Minimap/DeleteCommand + export zone round-trip.
# Tout est dans engine_core (Save*Bin header-only) ; on lie zone_builder_lib
# comme le demande le ticket (writers offline partages, pas de doublon de format).
add_executable(export_zone_tests src/world_editor/tests/ExportZoneTests.cpp)
target_link_libraries(export_zone_tests PRIVATE engine_core zone_builder_lib)
add_test(NAME export_zone_tests COMMAND export_zone_tests)

# M100.17 — Tests placement (geometrie pure + format props.bin + commande).
add_executable(placement_tests src/world_editor/tests/PlacementTests.cpp)
target_link_libraries(placement_tests PRIVATE engine_core)
add_test(NAME placement_tests COMMAND placement_tests)

# M100.18 — Tests foliage (library JSON + Poisson-disk + regles + round-trip).
add_executable(foliage_tests src/client/world/foliage/tests/FoliageTests.cpp)
target_link_libraries(foliage_tests PRIVATE engine_core)
add_test(NAME foliage_tests COMMAND foliage_tests)

# M100.19 — Tests Forest & Field (generateurs purs : densite, regles, spacing).
add_executable(forest_field_tests src/world_editor/tests/ForestFieldTests.cpp)
target_link_libraries(forest_field_tests PRIVATE engine_core)
add_test(NAME forest_field_tests COMMAND forest_field_tests)

# M100.20 — Tests vent (sway proportionnel hauteur, override zone, round-trip).
add_executable(wind_tests src/client/world/wind/tests/WindTests.cpp)
target_link_libraries(wind_tests PRIVATE engine_core)
add_test(NAME wind_tests COMMAND wind_tests)

# Chantier C Phase 1 — Tests grille terrain 256 m (constante + helpers purs).
add_executable(terrain_chunk_grid_tests src/client/world/tests/TerrainChunkGridTests.cpp)
target_link_libraries(terrain_chunk_grid_tests PRIVATE engine_core)
add_test(NAME terrain_chunk_grid_tests COMMAND terrain_chunk_grid_tests)

# Chantier C Phase 2 — Tests des IHeightField (heightmap repli + chunk résident).
# CPU pur (StreamCache::Insert + LoadFromCache, aucune I/O Vulkan/disque).
# Pattern cross-platform IDENTIQUE à terrain_chunk_grid_tests (add_executable +
# link engine_core + add_test) : compile sur Windows ET Linux, exécuté par ctest.
# NE PAS utiliser lcdlln_add_simple_test ici : ce helper applique -Wall/-Wextra/
# -Wpedantic + pthread (GCC/Clang/Linux only) que MSVC rejette (D8021 /Wextra).
add_executable(height_field_tests src/client/world/terrain/tests/HeightFieldTests.cpp)
target_link_libraries(height_field_tests PRIVATE engine_core)
add_test(NAME height_field_tests COMMAND height_field_tests)

# M100.49 — Tests Tutorial & Overlay (moteur de sequence + systeme tutoriel +
# registre widgets). Diagnostic differe (depend de M100.48). Ancrage distinct
# pour eviter les conflits CMake avec d'autres PR M100 en vol.
add_executable(tutorial_diagnostic_tests src/world_editor/tests/TutorialDiagnosticTests.cpp)
target_link_libraries(tutorial_diagnostic_tests PRIVATE engine_core)
add_test(NAME tutorial_diagnostic_tests COMMAND tutorial_diagnostic_tests)

# M100.49 partie 2 — Tests du système de diagnostic (registre + 10 règles +
# tri par importance). Pure logique dans engine_core.
add_executable(diagnostic_tests src/world_editor/tests/DiagnosticTests.cpp)
target_link_libraries(diagnostic_tests PRIVATE engine_core)
add_test(NAME diagnostic_tests COMMAND diagnostic_tests)

# M100.21 — Tests collecteur d'influences (portee, troncature, flexion).
add_executable(entity_influence_tests src/client/world/foliage/tests/EntityInfluenceTests.cpp)
target_link_libraries(entity_influence_tests PRIVATE engine_core)
add_test(NAME entity_influence_tests COMMAND entity_influence_tests)

# Phase 0 chantier C — Test fonction pure ShouldDrawLegacyTerrain (header-only).
add_executable(terrain_render_selection_tests src/client/render/tests/TerrainRenderSelectionTests.cpp)
target_include_directories(terrain_render_selection_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME terrain_render_selection_tests COMMAND terrain_render_selection_tests)

# M100.27 — Tests thermal (shade round-trip/builder + ComputeTemperature + query).
add_executable(thermal_tests src/client/world/thermal/tests/ThermalTests.cpp)
target_link_libraries(thermal_tests PRIVATE engine_core)
add_test(NAME thermal_tests COMMAND thermal_tests)

# M100.28 — Tests zones (round-trip + transition meteo bord + champs quest).
add_executable(zones_tests src/client/world/zones/tests/ZonesTests.cpp)
target_link_libraries(zones_tests PRIVATE engine_core)
add_test(NAME zones_tests COMMAND zones_tests)

# M100.50 — Tests Quick Start Wizard (FSM + resolver 192 combinaisons + conditions
# + substitution + determinisme auto-gen). Pure logique dans engine_core.
add_executable(quick_start_wizard_tests src/world_editor/tests/QuickStartWizardTests.cpp)
target_link_libraries(quick_start_wizard_tests PRIVATE engine_core)
add_test(NAME quick_start_wizard_tests COMMAND quick_start_wizard_tests)

# M100.29 — Tests spline (Catmull-Rom, ground-fit, splat sum=255, round-trip).
# Nom prefixe m100_ : un target `spline_tests` existe deja (MoveSpline serveur).
add_executable(m100_spline_tests src/client/world/spline/tests/SplineTests.cpp)
target_link_libraries(m100_spline_tests PRIVATE engine_core)
add_test(NAME m100_spline_tests COMMAND m100_spline_tests)

# M100.30 — Tests structures (pont/mur, coins, surface pont, migration v1->v2).
add_executable(structures_tests src/client/world/structures/tests/StructuresTests.cpp)
target_link_libraries(structures_tests PRIVATE engine_core)
add_test(NAME structures_tests COMMAND structures_tests)

# M100.31 — Tests hameau (kit JSON, determinisme, min_spacing, snap route, 1 entree historique).
add_executable(hamlet_tests src/world_editor/tests/HamletTests.cpp)
target_link_libraries(hamlet_tests PRIVATE engine_core)
add_test(NAME hamlet_tests COMMAND hamlet_tests)

# M100.33 — Tests footstep audio + machine d'etat playtest.
add_executable(playtest_tests src/world_editor/tests/PlaytestTests.cpp)
target_link_libraries(playtest_tests PRIVATE engine_core)
add_test(NAME playtest_tests COMMAND playtest_tests)

# M45.6: structure DDGI (indexation grille + layout atlas) — tests CPU purs (pas de device Vulkan)
add_executable(ddgi_volume_tests src/client/render/gi/tests/DdgiVolumeTests.cpp)
target_link_libraries(ddgi_volume_tests PRIVATE engine_core)
add_test(NAME ddgi_volume_tests COMMAND ddgi_volume_tests)

# M45.8: niveaux de qualité / fallback DDGI (DdgiQuality) — tests CPU purs (pas de device Vulkan)
add_executable(ddgi_fallback_tests src/client/render/gi/tests/DdgiFallbackTests.cpp)
target_link_libraries(ddgi_fallback_tests PRIVATE engine_core)
add_test(NAME ddgi_fallback_tests COMMAND ddgi_fallback_tests)

# Phase 1: CHARACTER_LIST request/response payload round-trip tests
add_executable(character_list_payloads_tests src/shared/network/CharacterListPayloadsTests.cpp)
target_link_libraries(character_list_payloads_tests PRIVATE engine_core)
add_test(NAME character_list_payloads_tests COMMAND character_list_payloads_tests)

# Cycle jour/nuit — directions soleil/lune + invariants nuit (pure CPU).
add_executable(day_night_cycle_tests src/client/render/tests/DayNightCycleTests.cpp)
target_link_libraries(day_night_cycle_tests PRIVATE engine_core)
add_test(NAME day_night_cycle_tests COMMAND day_night_cycle_tests)

# Phase 3.6.5: CHARACTER_SAVE_POSITION request/response payload round-trip tests
add_executable(character_save_position_payloads_tests src/shared/network/CharacterSavePositionPayloadsTests.cpp)
target_link_libraries(character_save_position_payloads_tests PRIVATE engine_core)
add_test(NAME character_save_position_payloads_tests COMMAND character_save_position_payloads_tests)

# Système de personnages PR2 : CHARACTER_CREATE factionId + faction_str round-trip tests
add_executable(character_create_payloads_tests src/shared/network/CharacterCreatePayloadsTests.cpp)
target_link_libraries(character_create_payloads_tests PRIVATE engine_core)
add_test(NAME character_create_payloads_tests COMMAND character_create_payloads_tests)

# R1-B: PLAYER_STATS gameplay message encode/decode round-trip test
add_executable(player_stats_message_tests src/shared/network/PlayerStatsMessageTests.cpp)
target_link_libraries(player_stats_message_tests PRIVATE engine_core)
add_test(NAME player_stats_message_tests COMMAND player_stats_message_tests)

# Chat MVP: CHAT_SEND_REQUEST / CHAT_RELAY payload round-trip tests
add_executable(chat_payloads_tests src/shared/network/ChatPayloadsTests.cpp)
target_link_libraries(chat_payloads_tests PRIVATE engine_core)
add_test(NAME chat_payloads_tests COMMAND chat_payloads_tests)

# CMANGOS.18 (Phase 3.18 step 3): MAIL_*_REQUEST/RESPONSE payload round-trip tests
add_executable(mail_payloads_tests src/shared/network/MailPayloadsTests.cpp)
target_link_libraries(mail_payloads_tests PRIVATE engine_core)
add_test(NAME mail_payloads_tests COMMAND mail_payloads_tests)

# CMANGOS.23 (Phase 5.23 step 3+4): QUEST_*_REQUEST/RESPONSE payload round-trip tests
add_executable(quest_payloads_tests src/shared/network/QuestPayloadsTests.cpp)
target_link_libraries(quest_payloads_tests PRIVATE engine_core)
add_test(NAME quest_payloads_tests COMMAND quest_payloads_tests)

# CMANGOS.25 (Phase 3.25 step 3+4): IGNORE_*_REQUEST/RESPONSE payload round-trip tests
add_executable(ignore_list_payloads_tests src/shared/network/IgnoreListPayloadsTests.cpp)
target_link_libraries(ignore_list_payloads_tests PRIVATE engine_core)
add_test(NAME ignore_list_payloads_tests COMMAND ignore_list_payloads_tests)

# CMANGOS.32 (Phase 5.32 step 3+4): GMTICKET_*_REQUEST/RESPONSE payload round-trip tests
add_executable(gm_ticket_payloads_tests src/shared/network/GmTicketPayloadsTests.cpp)
target_link_libraries(gm_ticket_payloads_tests PRIVATE engine_core)
add_test(NAME gm_ticket_payloads_tests COMMAND gm_ticket_payloads_tests)

# CMANGOS.24 (Phase 3.24 step 3+4): REPUTATION_*_REQUEST/RESPONSE payload round-trip tests
add_executable(reputation_payloads_tests src/shared/network/ReputationPayloadsTests.cpp)
target_link_libraries(reputation_payloads_tests PRIVATE engine_core)
add_test(NAME reputation_payloads_tests COMMAND reputation_payloads_tests)

# CMANGOS.33 (Phase 5.33 step 3+4) — Lfg payloads round-trip tests (no DB / no network).
add_executable(lfg_payloads_tests src/shared/network/LfgPayloadsTests.cpp)
target_link_libraries(lfg_payloads_tests PRIVATE engine_core)
add_test(NAME lfg_payloads_tests COMMAND lfg_payloads_tests)

# CMANGOS.30 (Phase 5.30 step 3+4) — Cinematic payloads round-trip tests (no DB / no network).
add_executable(cinematic_payloads_tests src/shared/network/CinematicPayloadsTests.cpp)
target_link_libraries(cinematic_payloads_tests PRIVATE engine_core)
add_test(NAME cinematic_payloads_tests COMMAND cinematic_payloads_tests)

# CMANGOS.39 (Phase 4.39 step 3+4) — Skill payloads round-trip tests (no DB / no network).
add_executable(skill_payloads_tests src/shared/network/SkillPayloadsTests.cpp)
target_link_libraries(skill_payloads_tests PRIVATE engine_core)
add_test(NAME skill_payloads_tests COMMAND skill_payloads_tests)

# CMANGOS.21 (Phase 5.21 step 3+4) — Arena payloads round-trip tests (no DB / no network).
add_executable(arena_payloads_tests src/shared/network/ArenaPayloadsTests.cpp)
target_link_libraries(arena_payloads_tests PRIVATE engine_core)
add_test(NAME arena_payloads_tests COMMAND arena_payloads_tests)

# CMANGOS.10 (Phase 5 step 3+4) — BattleGround payloads round-trip tests (no DB / no network).
add_executable(battleground_payloads_tests src/shared/network/BattleGroundPayloadsTests.cpp)
target_link_libraries(battleground_payloads_tests PRIVATE engine_core)
add_test(NAME battleground_payloads_tests COMMAND battleground_payloads_tests)

# CMANGOS.36 (Phase 5.36 step 3+4) — OutdoorPvp payloads round-trip tests (no DB / no network).
add_executable(outdoorpvp_payloads_tests src/shared/network/OutdoorPvpPayloadsTests.cpp)
target_link_libraries(outdoorpvp_payloads_tests PRIVATE engine_core)
add_test(NAME outdoorpvp_payloads_tests COMMAND outdoorpvp_payloads_tests)

# CMANGOS.42 (Phase 4.42 step 3+4) — Weather payloads round-trip tests (no DB / no network).
add_executable(weather_payloads_tests src/shared/network/WeatherPayloadsTests.cpp)
target_link_libraries(weather_payloads_tests PRIVATE engine_core)
add_test(NAME weather_payloads_tests COMMAND weather_payloads_tests)

# CMANGOS.31 (Phase 5.31 step 3+4) — GameEvent payloads round-trip tests (no DB / no network).
add_executable(gameevent_payloads_tests src/shared/network/GameEventPayloadsTests.cpp)
target_link_libraries(gameevent_payloads_tests PRIVATE engine_core)
add_test(NAME gameevent_payloads_tests COMMAND gameevent_payloads_tests)

# CMANGOS.21 (Phase 5.21 step 3+4 Guilds) — Guild payloads round-trip tests (no DB / no network).
add_executable(guild_payloads_tests src/shared/network/GuildPayloadsTests.cpp)
target_link_libraries(guild_payloads_tests PRIVATE engine_core)
add_test(NAME guild_payloads_tests COMMAND guild_payloads_tests)

# CMANGOS.09 (Phase 5.09 step 3+4 AuctionHouse) — Auction payloads round-trip tests (no DB / no network).
add_executable(auction_payloads_tests src/shared/network/AuctionPayloadsTests.cpp)
target_link_libraries(auction_payloads_tests PRIVATE engine_core)
add_test(NAME auction_payloads_tests COMMAND auction_payloads_tests)

# CMANGOS.17 (Phase 3.17 step 3+4 Loot) — Loot payloads round-trip tests (no DB / no network).
add_executable(loot_payloads_tests src/shared/network/LootPayloadsTests.cpp)
target_link_libraries(loot_payloads_tests PRIVATE engine_core)
add_test(NAME loot_payloads_tests COMMAND loot_payloads_tests)

# Phase 5 Lunar — round-trip tests payloads (192-194).
add_executable(lunar_payloads_tests src/shared/network/LunarPayloadsTests.cpp)
target_link_libraries(lunar_payloads_tests PRIVATE engine_core)
add_test(NAME lunar_payloads_tests COMMAND lunar_payloads_tests)

# Phase 6 WorldClock — round-trip tests payloads (203-205).
add_executable(world_clock_payloads_tests src/shared/network/tests/WorldClockPayloadsTests.cpp)
target_link_libraries(world_clock_payloads_tests PRIVATE engine_core)
add_test(NAME world_clock_payloads_tests COMMAND world_clock_payloads_tests)

# AdminCommand RBAC — round-trip tests payloads (195/196).
add_executable(admin_command_payloads_tests src/shared/network/AdminCommandPayloadsTests.cpp)
target_link_libraries(admin_command_payloads_tests PRIVATE engine_core)
add_test(NAME admin_command_payloads_tests COMMAND admin_command_payloads_tests)

# Phase 5 Lunar — calendar deterministe tests (header-only).
add_executable(lunar_calendar_tests src/shardd/world/LunarCalendarTests.cpp)
target_link_libraries(lunar_calendar_tests PRIVATE engine_core)
add_test(NAME lunar_calendar_tests COMMAND lunar_calendar_tests)

# M38.1 + Phase 5 Lunar — DayNightCycle existant + extension lunaire.
add_executable(daynight_cycle_tests src/client/render/DayNightCycleTests.cpp)
target_link_libraries(daynight_cycle_tests PRIVATE engine_core)
add_test(NAME daynight_cycle_tests COMMAND daynight_cycle_tests)

# CMANGOS.27 (Phase 4.27 step 3+4): TRADE_*_REQUEST/RESPONSE/NOTIFICATION payload round-trip tests
add_executable(trade_payloads_tests src/shared/network/TradePayloadsTests.cpp)
target_link_libraries(trade_payloads_tests PRIVATE engine_core)
add_test(NAME trade_payloads_tests COMMAND trade_payloads_tests)

# M23.4: Log rotation smoke test (runtime logger)
add_executable(log_rotation_smoke_tests src/shared/core/LogRotationSmokeTest.cpp)
target_link_libraries(log_rotation_smoke_tests PRIVATE engine_core)
if(MSVC)
  target_compile_options(log_rotation_smoke_tests PRIVATE /W4 /permissive- /Zc:preprocessor)
endif()
add_test(NAME log_rotation_smoke_tests COMMAND log_rotation_smoke_tests)

# M20.2: Argon2 hashing wrapper unit tests (engine_auth first so linker resolves Log from engine_core)
add_executable(argon2_hash_tests src/shared/auth/Argon2HashTests.cpp)
target_link_libraries(argon2_hash_tests PRIVATE engine_auth engine_core)
add_test(NAME argon2_hash_tests COMMAND argon2_hash_tests)

# M20.3: Session manager state transitions unit tests
add_executable(session_manager_tests src/masterd/session/SessionManagerTests.cpp src/masterd/session/SessionManager.cpp)
target_include_directories(session_manager_tests PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(session_manager_tests PRIVATE engine_core)
if(MSVC)
  target_compile_options(session_manager_tests PRIVATE /W4 /permissive- /Zc:preprocessor)
endif()
add_test(NAME session_manager_tests COMMAND session_manager_tests)

# M20.4: Rate limiting, IP ban, audit log unit tests
add_executable(security_tests src/shared/security/SecurityTests.cpp src/shared/security/RateLimitAndBan.cpp src/shared/security/SecurityAuditLog.cpp)
target_include_directories(security_tests PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(security_tests PRIVATE engine_core)
if(MSVC)
  target_compile_options(security_tests PRIVATE /W4 /permissive- /Zc:preprocessor)
endif()
add_test(NAME security_tests COMMAND security_tests)

# M22.3: Shard down detection integration test (EvictStaleHeartbeats + callback)
add_executable(shard_down_detection_tests src/masterd/shards/ShardDownDetectionTests.cpp src/masterd/shards/ShardRegistry.cpp)
target_include_directories(shard_down_detection_tests PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(shard_down_detection_tests PRIVATE engine_core)
if(MSVC)
  target_compile_options(shard_down_detection_tests PRIVATE /W4 /permissive- /Zc:preprocessor)
endif()
add_test(NAME shard_down_detection_tests COMMAND shard_down_detection_tests)

# M25.2: NetServer bandwidth caps + backpressure unit tests
add_executable(netserver_bandwidth_tests src/shared/network/NetServerBandwidthThrottleTests.cpp)
target_include_directories(netserver_bandwidth_tests PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(netserver_bandwidth_tests PRIVATE engine_core)
if(MSVC)
  target_compile_options(netserver_bandwidth_tests PRIVATE /W4 /permissive- /Zc:preprocessor)
endif()
add_test(NAME netserver_bandwidth_tests COMMAND netserver_bandwidth_tests)

# M25.4: DDoS basic mitigation (connection throttle) unit tests
add_executable(ddos_connection_protector_tests src/shared/security/ConnectionDDoSProtectorTests.cpp)
target_include_directories(ddos_connection_protector_tests PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(ddos_connection_protector_tests PRIVATE engine_core)
if(MSVC)
  target_compile_options(ddos_connection_protector_tests PRIVATE /W4 /permissive- /Zc:preprocessor)
endif()
add_test(NAME ddos_connection_protector_tests COMMAND ddos_connection_protector_tests)

# STAB.14: Runtime localization service unit tests
add_executable(localization_service_tests src/client/localization/LocalizationServiceTests.cpp)
target_link_libraries(localization_service_tests PRIVATE engine_core)
if(MSVC)
  target_compile_options(localization_service_tests PRIVATE /W4 /permissive- /Zc:preprocessor)
endif()
add_test(NAME localization_service_tests COMMAND localization_service_tests)

# Tests unitaires CPU pour TexturePreviewCache (ResampleRgba8Box, GenerateProceduralAlbedoLayer).
# Pas de dependance Vulkan : tournables en CI sans GPU.
add_executable(texture_preview_cache_tests src/world_editor/tests/TexturePreviewCacheTests.cpp)
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
add_executable(world_editor_session_tests src/world_editor/tests/WorldEditorSessionTests.cpp)
target_include_directories(world_editor_session_tests PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(world_editor_session_tests PRIVATE engine_core)
if(MSVC)
  target_compile_options(world_editor_session_tests PRIVATE /W4 /permissive- /Zc:preprocessor)
endif()
add_test(NAME world_editor_session_tests COMMAND world_editor_session_tests)

# Sous-projet 1 (boucle d'edition d'une zone) — Tests CPU pour TerrainDocument :
# InitFlatZone (alloc NxN chunks plats) + round-trip disque des chunks
# (source de verite de la zone). Pur CPU + E/S (pas de Vulkan), non gate WIN32
# pour tourner sous ctest Linux comme world_editor_session_tests.
add_executable(terrain_document_roundtrip_tests src/world_editor/tests/TerrainDocumentRoundTripTests.cpp)
target_include_directories(terrain_document_roundtrip_tests PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(terrain_document_roundtrip_tests PRIVATE engine_core)
if(MSVC)
  target_compile_options(terrain_document_roundtrip_tests PRIVATE /W4 /permissive- /Zc:preprocessor)
endif()
add_test(NAME terrain_document_roundtrip_tests COMMAND terrain_document_roundtrip_tests)

# Sous-projet 1, bloc B — Tests CPU pour EditorSelection (notification au
# changement, idempotence Clear). Pur CPU, non gate WIN32 (ctest Linux).
add_executable(editor_selection_tests src/world_editor/tests/EditorSelectionTests.cpp)
target_include_directories(editor_selection_tests PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(editor_selection_tests PRIVATE engine_core)
if(MSVC)
  target_compile_options(editor_selection_tests PRIVATE /W4 /permissive- /Zc:preprocessor)
endif()
add_test(NAME editor_selection_tests COMMAND editor_selection_tests)

# Sous-projet 1, bloc B — Tests CPU pour EditorSceneModel (agregation Terrain +
# layout + mesh inserts + dungeon portals, libelles, Find). Pur CPU, non gate.
add_executable(editor_scene_model_tests src/world_editor/tests/EditorSceneModelTests.cpp)
target_include_directories(editor_scene_model_tests PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(editor_scene_model_tests PRIVATE engine_core)
if(MSVC)
  target_compile_options(editor_scene_model_tests PRIVATE /W4 /permissive- /Zc:preprocessor)
endif()
add_test(NAME editor_scene_model_tests COMMAND editor_scene_model_tests)

# Lot 0 — Tests CPU pour EntityKey (clé stable de calque). Pur, ctest Linux.
add_executable(entity_key_tests src/world_editor/tests/EntityKeyTests.cpp)
target_include_directories(entity_key_tests PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(entity_key_tests PRIVATE engine_core)
if(MSVC)
  target_compile_options(entity_key_tests PRIVATE /W4 /permissive- /Zc:preprocessor)
endif()
add_test(NAME entity_key_tests COMMAND entity_key_tests)

# Lot 0 — Tests CPU pour SelectionQuery (encode/décode, pick, rect/lasso). ctest Linux.
add_executable(selection_query_tests src/world_editor/tests/SelectionQueryTests.cpp)
target_include_directories(selection_query_tests PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(selection_query_tests PRIVATE engine_core)
if(MSVC)
  target_compile_options(selection_query_tests PRIVATE /W4 /permissive- /Zc:preprocessor)
endif()
add_test(NAME selection_query_tests COMMAND selection_query_tests)

# Lot 0 — Tests CPU pour DeleteEntitiesOps (remove/restore par index). ctest Linux.
add_executable(delete_entities_ops_tests src/world_editor/tests/DeleteEntitiesOpsTests.cpp)
target_include_directories(delete_entities_ops_tests PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(delete_entities_ops_tests PRIVATE engine_core)
if(MSVC)
  target_compile_options(delete_entities_ops_tests PRIVATE /W4 /permissive- /Zc:preprocessor)
endif()
add_test(NAME delete_entities_ops_tests COMMAND delete_entities_ops_tests)

# Lot 0 — Tests CPU pour DeleteEntitiesCommand (Execute/Undo/Redo). ctest Linux.
add_executable(delete_entities_command_tests src/world_editor/tests/DeleteEntitiesCommandTests.cpp)
target_include_directories(delete_entities_command_tests PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(delete_entities_command_tests PRIVATE engine_core)
if(MSVC)
  target_compile_options(delete_entities_command_tests PRIVATE /W4 /permissive- /Zc:preprocessor)
endif()
add_test(NAME delete_entities_command_tests COMMAND delete_entities_command_tests)

# Sous-projet 1, bloc D — Tests CPU pour SetEntityTransformCommand (Execute/Undo,
# TryMerge meme entite, coalescing via CommandStack). Pur CPU, non gate.
add_executable(set_entity_transform_command_tests src/world_editor/tests/SetEntityTransformCommandTests.cpp)
target_include_directories(set_entity_transform_command_tests PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(set_entity_transform_command_tests PRIVATE engine_core)
if(MSVC)
  target_compile_options(set_entity_transform_command_tests PRIVATE /W4 /permissive- /Zc:preprocessor)
endif()
add_test(NAME set_entity_transform_command_tests COMMAND set_entity_transform_command_tests)

# Sous-projet 1, bloc H — Tests CPU pour Config::SaveToFile (round-trip JSON
# plat des 4 types scalaires + echappement). Pur CPU + E/S, non gate WIN32.
add_executable(config_save_to_file_tests src/world_editor/tests/ConfigSaveToFileTests.cpp)
target_include_directories(config_save_to_file_tests PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(config_save_to_file_tests PRIVATE engine_core)
if(MSVC)
  target_compile_options(config_save_to_file_tests PRIVATE /W4 /permissive- /Zc:preprocessor)
endif()
add_test(NAME config_save_to_file_tests COMMAND config_save_to_file_tests)

# M100.1 — Tests unitaires CPU pour la coquille WorldEditorShell.
# Vérifient le cycle Init/Shutdown, la persistance du layout .ini, le
# dispatch des raccourcis F1..F12 et le flag dirty. Le test crée un
# contexte ImGui isolé en interne (pas de fenêtre Vulkan ni device).
# Windows-only : ImGui n'est compilé que sous WIN32 dans ce repo.
if(WIN32)
  add_executable(world_editor_shell_tests src/world_editor/tests/WorldEditorShellTests.cpp)
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
  add_executable(command_stack_tests src/world_editor/tests/CommandStackTests.cpp)
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
  add_executable(terrain_chunk_tests src/client/world/terrain/tests/TerrainChunkTests.cpp)
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
  add_executable(splat_map_tests src/client/world/terrain/tests/SplatMapTests.cpp)
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
  add_executable(chunk_runtime_tests src/client/render/terrain_chunk/tests/ChunkRuntimeTests.cpp)
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
  add_executable(terrain_mesh_gpu_cache_tests src/client/render/terrain_chunk/tests/TerrainMeshGpuCacheTests.cpp)
  target_include_directories(terrain_mesh_gpu_cache_tests PRIVATE ${CMAKE_SOURCE_DIR})
  target_link_libraries(terrain_mesh_gpu_cache_tests PRIVATE engine_core)
  if(MSVC)
    target_compile_options(terrain_mesh_gpu_cache_tests PRIVATE /W4 /permissive- /Zc:preprocessor)
  endif()
  add_test(NAME terrain_mesh_gpu_cache_tests COMMAND terrain_mesh_gpu_cache_tests)
endif()

# Terrain Chunk Runtime — Tests unitaires pour SplatMapGpuCache (mock
# allocator). Vérifie 2 VkImage par chunk (layers 0..3 + 4..7), cache
# hit, eviction.
if(WIN32)
  add_executable(splat_map_gpu_cache_tests src/client/render/terrain_chunk/tests/SplatMapGpuCacheTests.cpp)
  target_include_directories(splat_map_gpu_cache_tests PRIVATE ${CMAKE_SOURCE_DIR})
  target_link_libraries(splat_map_gpu_cache_tests PRIVATE engine_core)
  if(MSVC)
    target_compile_options(splat_map_gpu_cache_tests PRIVATE /W4 /permissive- /Zc:preprocessor)
  endif()
  add_test(NAME splat_map_gpu_cache_tests COMMAND splat_map_gpu_cache_tests)
endif()

# Terrain Chunk Runtime — Tests unitaires pour LayerArrayLoader path
# resolution (pure CPU, pas de Vulkan). Vérifie .texr first, fallback
# placeholders, contentRoot prefix, distinct paths par mapType.
if(WIN32)
  add_executable(layer_array_loader_tests src/client/render/terrain_chunk/tests/LayerArrayLoaderTests.cpp)
  target_include_directories(layer_array_loader_tests PRIVATE ${CMAKE_SOURCE_DIR})
  target_link_libraries(layer_array_loader_tests PRIVATE engine_core)
  if(MSVC)
    target_compile_options(layer_array_loader_tests PRIVATE /W4 /permissive- /Zc:preprocessor)
  endif()
  add_test(NAME layer_array_loader_tests COMMAND layer_array_loader_tests)
endif()

# M100.5 — Tests de parité éditeur ↔ client pour le format terrain.bin.
# Round-trip byte-exact (SaveTerrainBin → LoadTerrainBin → memcmp) +
# meshes deterministe (BuildLod0Mesh produit des vertex/index buffers
# identiques bit-à-bit pour le chunk source et le chunk re-lu).
if(WIN32)
  add_executable(terrain_parity_tests src/client/world/terrain/tests/TerrainParityTests.cpp)
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
  add_executable(terrain_lod_tests src/client/world/terrain/tests/TerrainLodTests.cpp)
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
  add_executable(editor_camera_controller_tests src/world_editor/tests/EditorCameraControllerTests.cpp)
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
  add_executable(terrain_sculpt_tests src/world_editor/tests/TerrainSculptTests.cpp)
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
  add_executable(terrain_stamp_tests src/world_editor/tests/TerrainStampTests.cpp)
  target_include_directories(terrain_stamp_tests PRIVATE ${CMAKE_SOURCE_DIR})
  target_link_libraries(terrain_stamp_tests PRIVATE engine_core)
  if(MSVC)
    target_compile_options(terrain_stamp_tests PRIVATE /W4 /permissive- /Zc:preprocessor)
  endif()
  add_test(NAME terrain_stamp_tests COMMAND terrain_stamp_tests)
endif()

# M100.35 — Tests unitaires CPU pour les outils macros terrain (Mountain
# Range & Valley Chain). Couvre la rasterisation sparse multi-chunks, la
# couture inter-chunks, le déterminisme du bruit, l'asymétrie, et la
# symétrie Execute/Undo de la commande. Pure CPU, gating WIN32 (pattern
# terrain_sculpt / terrain_stamp).
if(WIN32)
  add_executable(macro_terrain_tests src/world_editor/tests/MacroTerrainTests.cpp)
  target_include_directories(macro_terrain_tests PRIVATE ${CMAKE_SOURCE_DIR})
  target_link_libraries(macro_terrain_tests PRIVATE engine_core)
  if(MSVC)
    target_compile_options(macro_terrain_tests PRIVATE /W4 /permissive- /Zc:preprocessor)
  endif()
  add_test(NAME macro_terrain_tests COMMAND macro_terrain_tests)
endif()

# M100.35 — Tests unitaires CPU pour la toolbar à icônes : layout, hit-test
# pur, routage des clics vers `WorldEditorShell::SetActiveTool`, invariant
# de couverture viewport, fallback placeholder de l'atlas d'icônes.
if(WIN32)
  add_executable(editor_toolbar_tests src/world_editor/tests/EditorToolbarTests.cpp)
  target_include_directories(editor_toolbar_tests PRIVATE ${CMAKE_SOURCE_DIR})
  target_link_libraries(editor_toolbar_tests PRIVATE engine_core)
  if(MSVC)
    target_compile_options(editor_toolbar_tests PRIVATE /W4 /permissive- /Zc:preprocessor)
  endif()
  add_test(NAME editor_toolbar_tests COMMAND editor_toolbar_tests)
endif()

# M100.36 — Tests unitaires CPU pour le watershed D8 + flow accumulation +
# Douglas-Peucker + OceanSettings + RiverNetworkCommand + water.bin v1/v2
# round-trip. Pure CPU, gating WIN32.
if(WIN32)
  add_executable(river_network_tests src/world_editor/tests/RiverNetworkTests.cpp)
  target_include_directories(river_network_tests PRIVATE ${CMAKE_SOURCE_DIR})
  target_link_libraries(river_network_tests PRIVATE engine_core)
  if(MSVC)
    target_compile_options(river_network_tests PRIVATE /W4 /permissive- /Zc:preprocessor)
  endif()
  add_test(NAME river_network_tests COMMAND river_network_tests)
endif()

# M100.37 — Tests unitaires CPU pour la Coastline : marching squares,
# stats, smoothing band-filtered, cliffs slope-driven, water.bin v3
# round-trip (incl. LakeInstance::isOcean + OceanSettings étendu),
# CoastlineCommand Apply/Undo. Pure CPU, gating WIN32.
if(WIN32)
  add_executable(coastline_tests src/world_editor/tests/CoastlineTests.cpp)
  target_include_directories(coastline_tests PRIVATE ${CMAKE_SOURCE_DIR})
  target_link_libraries(coastline_tests PRIVATE engine_core)
  if(MSVC)
    target_compile_options(coastline_tests PRIVATE /W4 /permissive- /Zc:preprocessor)
  endif()
  add_test(NAME coastline_tests COMMAND coastline_tests)
endif()

# M100.38 — Tests unitaires CPU pour l'érosion hydraulique : déterminisme,
# bilinear distribute (conservation + parité couture), single-droplet,
# HydraulicErosionCommand Apply/Undo, max delta clamp. Pure CPU, gating WIN32.
if(WIN32)
  add_executable(hydraulic_erosion_tests src/world_editor/tests/HydraulicErosionTests.cpp)
  target_include_directories(hydraulic_erosion_tests PRIVATE ${CMAKE_SOURCE_DIR})
  target_link_libraries(hydraulic_erosion_tests PRIVATE engine_core)
  if(MSVC)
    target_compile_options(hydraulic_erosion_tests PRIVATE /W4 /permissive- /Zc:preprocessor)
  endif()
  add_test(NAME hydraulic_erosion_tests COMMAND hydraulic_erosion_tests)
endif()

# M100.39 — Tests unitaires CPU pour l'érosion thermique + éolienne
# (clôt Phase 2.5) : thermal flat / cone / mass conservation / convergence /
# preserveSteep, wind flat / déterminisme / 0 particules, Command
# Apply/Undo bit-equal. Pure CPU, gating WIN32.
if(WIN32)
  add_executable(thermal_wind_erosion_tests src/world_editor/tests/ThermalWindErosionTests.cpp)
  target_include_directories(thermal_wind_erosion_tests PRIVATE ${CMAKE_SOURCE_DIR})
  target_link_libraries(thermal_wind_erosion_tests PRIVATE engine_core)
  if(MSVC)
    target_compile_options(thermal_wind_erosion_tests PRIVATE /W4 /permissive- /Zc:preprocessor)
  endif()
  add_test(NAME thermal_wind_erosion_tests COMMAND thermal_wind_erosion_tests)
endif()

# M100.40 — Tests unitaires CPU pour la fondation Mesh Insert + Cave Tool
# (démarre Phase 11) : LCMI v1 binary round-trip, MeshInsertDocument CRUD,
# CaveCatalog JSON parsing, CaveCamouflage poids, PlaceCaveCommand
# Apply/Undo bit-equal. Pure CPU, gating WIN32.
if(WIN32)
  add_executable(mesh_insert_tests src/world_editor/tests/MeshInsertTests.cpp)
  target_include_directories(mesh_insert_tests PRIVATE ${CMAKE_SOURCE_DIR})
  target_link_libraries(mesh_insert_tests PRIVATE engine_core)
  if(MSVC)
    target_compile_options(mesh_insert_tests PRIVATE /W4 /permissive- /Zc:preprocessor)
  endif()
  add_test(NAME mesh_insert_tests COMMAND mesh_insert_tests)
endif()

# M100.41 — Tests unitaires CPU pour l'Overhang Cliff Tool (Phase 11).
# OverhangCatalog JSON parsing, PlaceOverhangCommand Apply/Undo, gating
# slope. Pure CPU, gating WIN32.
if(WIN32)
  add_executable(overhang_tool_tests src/world_editor/tests/OverhangToolTests.cpp)
  target_include_directories(overhang_tool_tests PRIVATE ${CMAKE_SOURCE_DIR})
  target_link_libraries(overhang_tool_tests PRIVATE engine_core)
  if(MSVC)
    target_compile_options(overhang_tool_tests PRIVATE /W4 /permissive- /Zc:preprocessor)
  endif()
  add_test(NAME overhang_tool_tests COMMAND overhang_tool_tests)
endif()

# M100.42 — Tests unitaires CPU pour le Natural Arch Tool (Phase 11).
# ArchCatalog JSON parsing, PlaceArchCommand Apply/Undo, ArchTool
# dérivation yaw/scale/position, garde-fou scale hors bornes.
if(WIN32)
  add_executable(arch_tool_tests src/world_editor/tests/ArchToolTests.cpp)
  target_include_directories(arch_tool_tests PRIVATE ${CMAKE_SOURCE_DIR})
  target_link_libraries(arch_tool_tests PRIVATE engine_core)
  if(MSVC)
    target_compile_options(arch_tool_tests PRIVATE /W4 /permissive- /Zc:preprocessor)
  endif()
  add_test(NAME arch_tool_tests COMMAND arch_tool_tests)
endif()

# M100.43 — Tests unitaires CPU pour le Dungeon Portal System (Phase 11).
# LCDP v1 round-trip, Document CRUD, Catalog JSON, Command Apply/Undo,
# Tool gating. Pure CPU, gating WIN32.
if(WIN32)
  add_executable(dungeon_portal_tests src/world_editor/tests/DungeonPortalTests.cpp)
  target_include_directories(dungeon_portal_tests PRIVATE ${CMAKE_SOURCE_DIR})
  target_link_libraries(dungeon_portal_tests PRIVATE engine_core)
  if(MSVC)
    target_compile_options(dungeon_portal_tests PRIVATE /W4 /permissive- /Zc:preprocessor)
  endif()
  add_test(NAME dungeon_portal_tests COMMAND dungeon_portal_tests)
endif()

# M100.43/.44 — Tests round-trip pour les payloads EnterDungeon{Request,
# Response} (opcodes 197/198). M100.43 réserve les opcodes ; M100.44
# câble le handler + ajoute BuildEnterDungeonResponsePacket.
if(WIN32)
  add_executable(dungeon_payloads_tests src/shared/network/DungeonPayloadsTests.cpp)
  target_include_directories(dungeon_payloads_tests PRIVATE ${CMAKE_SOURCE_DIR})
  target_link_libraries(dungeon_payloads_tests PRIVATE engine_core)
  if(MSVC)
    target_compile_options(dungeon_payloads_tests PRIVATE /W4 /permissive- /Zc:preprocessor)
  endif()
  add_test(NAME dungeon_payloads_tests COMMAND dungeon_payloads_tests)
endif()

# M100.44 — Tests unitaires CPU pour VMap Bridge & Phase 11 Validation
# (clôture Phase 11). TransformLocalAabbToWorld, VMapBridge::Build,
# LCVC v1 round-trip, Phase11Validator (guids dupliqués, gltf vide,
# template orphelin, difficulty range, triggerRadius). Pure CPU, WIN32.
if(WIN32)
  add_executable(phase11_bridge_tests src/world_editor/tests/Phase11BridgeTests.cpp)
  target_include_directories(phase11_bridge_tests PRIVATE ${CMAKE_SOURCE_DIR})
  target_link_libraries(phase11_bridge_tests PRIVATE engine_core)
  if(MSVC)
    target_compile_options(phase11_bridge_tests PRIVATE /W4 /permissive- /Zc:preprocessor)
  endif()
  add_test(NAME phase11_bridge_tests COMMAND phase11_bridge_tests)
endif()

# M100.45 — Tests unitaires CPU pour la Phase A « Accessibilité » :
# EditorModeRegistry (set + subscribers), ToolPresetIo (parsing JSON
# tolérant), ToolPresetRegistry (chargement répertoire + résilience
# fichier corrompu), UserPrefsStore (round-trip disque + sauvegarde
# atomique + premier lancement). Pure CPU, gating WIN32.
if(WIN32)
  add_executable(editor_modes_tests src/world_editor/tests/EditorModesTests.cpp)
  target_include_directories(editor_modes_tests PRIVATE ${CMAKE_SOURCE_DIR})
  target_link_libraries(editor_modes_tests PRIVATE engine_core)
  if(MSVC)
    target_compile_options(editor_modes_tests PRIVATE /W4 /permissive- /Zc:preprocessor)
  endif()
  add_test(NAME editor_modes_tests COMMAND editor_modes_tests)
endif()

# M100.45 Phase B — Tests unitaires CPU pour la migration des outils :
# application d'un preset aux paramètres internes (full / partiel /
# vide / clamp / round-trip JSON réel). Incrément 1 = hydraulic_erosion.
# Pure CPU, gating WIN32.
if(WIN32)
  add_executable(tool_migration_tests src/world_editor/tests/ToolMigrationTests.cpp)
  target_include_directories(tool_migration_tests PRIVATE ${CMAKE_SOURCE_DIR})
  target_link_libraries(tool_migration_tests PRIVATE engine_core)
  if(MSVC)
    target_compile_options(tool_migration_tests PRIVATE /W4 /permissive- /Zc:preprocessor)
  endif()
  add_test(NAME tool_migration_tests COMMAND tool_migration_tests)
endif()

# M100.46 — Tests unitaires CPU pour la Zone Presets Library (incrément 1) :
# parsing JSON, Validate (type/affectedBy inconnus, decoration non vide),
# helpers de type, registry (chargement répertoire + résilience). Pure
# CPU, gating WIN32.
if(WIN32)
  add_executable(zone_presets_tests src/world_editor/tests/ZonePresetsTests.cpp)
  target_include_directories(zone_presets_tests PRIVATE ${CMAKE_SOURCE_DIR})
  target_link_libraries(zone_presets_tests PRIVATE engine_core)
  if(MSVC)
    target_compile_options(zone_presets_tests PRIVATE /W4 /permissive- /Zc:preprocessor)
  endif()
  add_test(NAME zone_presets_tests COMMAND zone_presets_tests)
endif()

# M100.47 incrément 1 — Tests unitaires CPU pour le HelpContentStore :
# parsing JSON tooltips, loader content-path, robustesse aux fichiers
# corrompus / dossier absent / doublons. Pure CPU, gating WIN32.
if(WIN32)
  add_executable(help_content_tests src/world_editor/tests/HelpContentTests.cpp)
  target_include_directories(help_content_tests PRIVATE ${CMAKE_SOURCE_DIR})
  target_link_libraries(help_content_tests PRIVATE engine_core)
  if(MSVC)
    target_compile_options(help_content_tests PRIVATE /W4 /permissive- /Zc:preprocessor)
  endif()
  add_test(NAME help_content_tests COMMAND help_content_tests)
endif()

# M100.10 — Tests unitaires CPU pour la peinture splat (manual + auto-rules).
# Couvre l'invariant somme=255, le filtrage MatchesRules, la monotonie du
# falloff radial, la couture inter-chunks bord-à-bord, et la coalescence par
# stroke (1 entrée d'historique par geste). Pure CPU, gating WIN32.
if(WIN32)
  add_executable(splat_paint_tests src/world_editor/tests/SplatPaintTests.cpp)
  target_include_directories(splat_paint_tests PRIVATE ${CMAKE_SOURCE_DIR})
  target_link_libraries(splat_paint_tests PRIVATE engine_core)
  if(MSVC)
    target_compile_options(splat_paint_tests PRIVATE /W4 /permissive- /Zc:preprocessor)
  endif()
  add_test(NAME splat_paint_tests COMMAND splat_paint_tests)
endif()

# M100.11 — Tests SurfaceType enum + helpers (Phase 3b.1).
if(WIN32)
  add_executable(surface_type_tests src/client/world/surface/tests/SurfaceTypeTests.cpp)
  target_include_directories(surface_type_tests PRIVATE ${CMAKE_SOURCE_DIR})
  target_link_libraries(surface_type_tests PRIVATE engine_core)
  if(MSVC)
    target_compile_options(surface_type_tests PRIVATE /W4 /permissive- /Zc:preprocessor)
  endif()
  add_test(NAME surface_type_tests COMMAND surface_type_tests)
endif()

# M100.11 — Tests SurfaceTable JSON loader.
if(WIN32)
  add_executable(surface_table_tests src/client/world/surface/tests/SurfaceTableTests.cpp)
  target_include_directories(surface_table_tests PRIVATE ${CMAKE_SOURCE_DIR})
  target_link_libraries(surface_table_tests PRIVATE engine_core)
  if(MSVC)
    target_compile_options(surface_table_tests PRIVATE /W4 /permissive- /Zc:preprocessor)
  endif()
  add_test(NAME surface_table_tests COMMAND surface_table_tests)
endif()

# M100.11 — Tests LayerPalette migration string→SurfaceType enum.
if(WIN32)
  add_executable(layer_palette_surface_type_tests src/client/world/terrain/tests/LayerPaletteSurfaceTypeTests.cpp)
  target_include_directories(layer_palette_surface_type_tests PRIVATE ${CMAKE_SOURCE_DIR})
  target_link_libraries(layer_palette_surface_type_tests PRIVATE engine_core)
  if(MSVC)
    target_compile_options(layer_palette_surface_type_tests PRIVATE /W4 /permissive- /Zc:preprocessor)
  endif()
  add_test(NAME layer_palette_surface_type_tests COMMAND layer_palette_surface_type_tests)
endif()

# M100.11 — Tests SurfaceQueryService (mock cache via Insert).
if(WIN32)
  add_executable(surface_query_service_tests src/client/world/surface/tests/SurfaceQueryServiceTests.cpp)
  target_include_directories(surface_query_service_tests PRIVATE ${CMAKE_SOURCE_DIR})
  target_link_libraries(surface_query_service_tests PRIVATE engine_core)
  if(MSVC)
    target_compile_options(surface_query_service_tests PRIVATE /W4 /permissive- /Zc:preprocessor)
  endif()
  add_test(NAME surface_query_service_tests COMMAND surface_query_service_tests)
endif()

# M100.11 — Tests ClientPredictionSystem.SetSurfaceSpeedMultiplier.
if(WIN32)
  add_executable(client_prediction_surface_multiplier_tests
    src/client/gameplay/tests/ClientPredictionSurfaceMultiplierTests.cpp)
  target_include_directories(client_prediction_surface_multiplier_tests PRIVATE ${CMAKE_SOURCE_DIR})
  target_link_libraries(client_prediction_surface_multiplier_tests PRIVATE engine_core)
  if(MSVC)
    target_compile_options(client_prediction_surface_multiplier_tests PRIVATE /W4 /permissive- /Zc:preprocessor)
  endif()
  add_test(NAME client_prediction_surface_multiplier_tests COMMAND client_prediction_surface_multiplier_tests)
endif()

# M100.12 — Tests round-trip CollisionProxy (Phase 3b.2).
if(WIN32)
  add_executable(collision_proxy_roundtrip_tests src/client/world/collision/tests/CollisionProxyRoundtripTests.cpp)
  target_include_directories(collision_proxy_roundtrip_tests PRIVATE ${CMAKE_SOURCE_DIR})
  target_link_libraries(collision_proxy_roundtrip_tests PRIVATE engine_core)
  if(MSVC)
    target_compile_options(collision_proxy_roundtrip_tests PRIVATE /W4 /permissive- /Zc:preprocessor)
  endif()
  add_test(NAME collision_proxy_roundtrip_tests COMMAND collision_proxy_roundtrip_tests)
endif()

# M100.12 — Tests AutoFit dispatch heuristique.
if(WIN32)
  add_executable(auto_fit_proxy_tests src/client/world/collision/tests/AutoFitProxyTests.cpp)
  target_include_directories(auto_fit_proxy_tests PRIVATE ${CMAKE_SOURCE_DIR})
  target_link_libraries(auto_fit_proxy_tests PRIVATE engine_core)
  if(MSVC)
    target_compile_options(auto_fit_proxy_tests PRIVATE /W4 /permissive- /Zc:preprocessor)
  endif()
  add_test(NAME auto_fit_proxy_tests COMMAND auto_fit_proxy_tests)
endif()

# M100.12 — Tests ProxyWireframe edge generator.
if(WIN32)
  add_executable(proxy_wireframe_tests src/client/world/collision/tests/ProxyWireframeTests.cpp)
  target_include_directories(proxy_wireframe_tests PRIVATE ${CMAKE_SOURCE_DIR})
  target_link_libraries(proxy_wireframe_tests PRIVATE engine_core)
  if(MSVC)
    target_compile_options(proxy_wireframe_tests PRIVATE /W4 /permissive- /Zc:preprocessor)
  endif()
  add_test(NAME proxy_wireframe_tests COMMAND proxy_wireframe_tests)
endif()

# M100.13 — Tests WaterSurfaces serialization (Phase 4a).
if(WIN32)
  add_executable(water_surfaces_tests src/client/world/water/tests/WaterSurfacesTests.cpp)
  target_include_directories(water_surfaces_tests PRIVATE ${CMAKE_SOURCE_DIR})
  target_link_libraries(water_surfaces_tests PRIVATE engine_core)
  if(MSVC)
    target_compile_options(water_surfaces_tests PRIVATE /W4 /permissive- /Zc:preprocessor)
  endif()
  add_test(NAME water_surfaces_tests COMMAND water_surfaces_tests)
endif()

# M100.13 — Tests WaterMeshBuilder ear clipping + ribbon polyline.
if(WIN32)
  add_executable(water_mesh_builder_tests src/client/world/water/tests/WaterMeshBuilderTests.cpp)
  target_include_directories(water_mesh_builder_tests PRIVATE ${CMAKE_SOURCE_DIR})
  target_link_libraries(water_mesh_builder_tests PRIVATE engine_core)
  if(MSVC)
    target_compile_options(water_mesh_builder_tests PRIVATE /W4 /permissive- /Zc:preprocessor)
  endif()
  add_test(NAME water_mesh_builder_tests COMMAND water_mesh_builder_tests)
endif()

# M100.14 — Tests WaterMeshGpu CPU BuildDrawInfos.
if(WIN32)
  add_executable(water_mesh_gpu_tests src/client/render/tests/WaterMeshGpuTests.cpp)
  target_include_directories(water_mesh_gpu_tests PRIVATE ${CMAKE_SOURCE_DIR})
  target_link_libraries(water_mesh_gpu_tests PRIVATE engine_core)
  if(MSVC)
    target_compile_options(water_mesh_gpu_tests PRIVATE /W4 /permissive- /Zc:preprocessor)
  endif()
  add_test(NAME water_mesh_gpu_tests COMMAND water_mesh_gpu_tests)
endif()

# M100.14 — Tests WaterPassPushConstants layout (sizeof + offsetof).
if(WIN32)
  add_executable(water_pass_tests src/client/render/tests/WaterPassTests.cpp)
  target_include_directories(water_pass_tests PRIVATE ${CMAKE_SOURCE_DIR})
  target_link_libraries(water_pass_tests PRIVATE engine_core)
  if(MSVC)
    target_compile_options(water_pass_tests PRIVATE /W4 /permissive- /Zc:preprocessor)
  endif()
  add_test(NAME water_pass_tests COMMAND water_pass_tests)
endif()

# M22.6: Client flow app (AUTH → SERVER_LIST → TICKET → CONNECT SHARD). Windows only (NetClient).
if(WIN32)
  add_executable(client_flow_app src/shared/network/ClientFlowMain.cpp)
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
  if(EXISTS "${CMAKE_SOURCE_DIR}/config/server.ini")
    add_custom_command(TARGET client_flow_app POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:client_flow_app>/config"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              "${CMAKE_SOURCE_DIR}/config/server.ini"
              "$<TARGET_FILE_DIR:client_flow_app>/config/server.ini"
      COMMENT "Copie de config/server.ini (client_flow_app)..."
    )
  endif()
endif()

# M09.4: HLOD builder (offline tool, no Vulkan)
add_subdirectory(tools/hlod_builder)
# M45.4: Impostor builder (offline tool, no Vulkan) — pré-rend un atlas
# d'impostors octaédriques depuis un mesh glTF.
add_subdirectory(tools/impostor_builder)
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
add_subdirectory(src)

# M19.10: Network integration tests (Linux only: start server, TLS connect, scenarios)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  find_package(OpenSSL REQUIRED)
  add_executable(network_integration_tests src/shared/network/NetworkIntegrationTests.cpp)
  target_link_libraries(network_integration_tests PRIVATE engine_core OpenSSL::SSL)
  add_dependencies(network_integration_tests server_app)
  add_test(NAME network_integration_tests COMMAND network_integration_tests pkg/server/lcdlln_server ${CMAKE_BINARY_DIR} WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
endif()

# M25.1: Load tester (TLS multi-connection) - Windows only (reuses NetClient TLS)
add_subdirectory(tools/load_tester)

# M100.9: Mini-tool C++ standalone qui génère 8 PNG 4x4 unicolore dans
# game/data/terrain/placeholders/ via custom command POST_BUILD. Sert de fallback
# visuellement distinct pour le shader 8-layer terrain en attendant les vrais
# assets PBR. Pas de dépendance zlib externe (deflate stored block).
add_subdirectory(tools/gen_terrain_placeholders)