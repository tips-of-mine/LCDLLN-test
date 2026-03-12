cmake_minimum_required(VERSION 3.20)
project(LCDLLN_Engine LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Force /MD (DLL CRT) pour toutes les configurations MSVC
if(MSVC)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")
endif()

find_package(Vulkan REQUIRED)
find_package(glfw3 CONFIG REQUIRED)

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

set_source_files_properties(engine/render/vk/VmaAllocator.cpp PROPERTIES
    COMPILE_DEFINITIONS "VMA_IMPLEMENTATION;VMA_STATIC_VULKAN_FUNCTIONS=0;VMA_DYNAMIC_VULKAN_FUNCTIONS=0"
)

add_library(engine_core
  engine/render/vk/VmaAllocator.cpp
  engine/render/vk/VkUtils.cpp
  engine/core/Config.cpp
  engine/core/jobs/JobSystem.cpp
  engine/core/Log.cpp
  engine/core/Time.cpp
  engine/Engine.cpp
  engine/platform/FileSystem.cpp
  engine/platform/FileWatcher.cpp
  engine/render/vk/VkInstance.cpp
  engine/render/vk/VkDeviceContext.cpp
  engine/render/vk/VkSwapchain.cpp
  engine/render/vk/VkFrameSync.cpp
  engine/render/FrameGraph.cpp
  engine/render/AssetRegistry.cpp
  engine/render/ShaderCompiler.cpp
  engine/render/ShaderCache.cpp
  engine/render/ShaderHotReload.cpp
  engine/render/Camera.cpp
  engine/render/CascadedShadowMaps.cpp
  engine/render/BrdfLutPass.cpp
  engine/render/SpecularPrefilterPass.cpp
  engine/render/SsaoKernelNoise.cpp
  engine/render/SsaoPass.cpp
  engine/render/SsaoBlurPass.cpp
  engine/render/ShadowMapPass.cpp
  engine/render/GeometryPass.cpp
  engine/render/LightingPass.cpp
  engine/render/TonemapPass.cpp
  engine/render/BloomPass.cpp
  engine/render/AutoExposure.cpp
  engine/render/TaaPass.cpp
  engine/render/DeferredPipeline.cpp
  engine/math/Frustum.cpp
  engine/world/WorldModel.cpp
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
)
target_include_directories(engine_core PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${VMA_INCLUDE_DIRS}
)
target_link_libraries(engine_core PUBLIC
  Vulkan::Vulkan
  glfw
)
if(MSVC)
  target_compile_options(engine_core PRIVATE /W4 /permissive- /Zc:preprocessor)
else()
  target_compile_options(engine_core PRIVATE -Wall -Wextra -Wpedantic)
endif()
if(WIN32)
  target_sources(engine_core PRIVATE
    engine/platform/Input.cpp
    engine/platform/Window.cpp
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
set_target_properties(engine_app PROPERTIES OUTPUT_NAME "lcdlln")
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

# M09.4: HLOD builder (offline tool, no Vulkan)
add_subdirectory(tools/hlod_builder)
# M10.5: Zone builder (chunk packages: chunk.meta + geo/tex/instances/nav/probes)
add_subdirectory(tools/zone_builder)
