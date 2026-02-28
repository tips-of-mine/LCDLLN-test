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
