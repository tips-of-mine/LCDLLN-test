#pragma once

#include "engine/core/Config.h"
#include "engine/core/Time.h"
#include "engine/core/memory/FrameArena.h"
#include "engine/platform/Input.h"
#include "engine/platform/Window.h"
#include "engine/render/AssetRegistry.h"
#include "engine/render/FrameGraph.h"
#include "engine/render/vk/VkDeviceContext.h"
#include "engine/render/vk/VkFrameSync.h"
#include "engine/render/vk/VkInstance.h"
#include "engine/render/vk/VkSwapchain.h"
#include "engine/render/ShaderCache.h"
#include "engine/render/ShaderCompiler.h"
#include "engine/render/ShaderHotReload.h"
#include "engine/render/Camera.h"
#include "engine/render/GeometryPass.h"
#include "engine/math/Frustum.h"
#include "engine/math/Math.h"

struct GLFWwindow;

#include <array>
#include <atomic>
#include <cstdint>

namespace engine
{
	/// Minimal render state produced by Update and consumed by Render.
	struct RenderState
	{
		engine::render::Camera camera;
		engine::math::Mat4 viewMatrix;
		engine::math::Mat4 projMatrix;
		engine::math::Mat4 viewProjMatrix;
		engine::math::Frustum frustum;

		// Placeholder draw-list marker.
		uint32_t drawItemCount = 0;
	};

	/// Engine loop: BeginFrame/Update/Render/EndFrame with double-buffered RenderState.
	class Engine final
	{
	public:
		Engine(int argc, char** argv);
		Engine(const Engine&) = delete;
		Engine& operator=(const Engine&) = delete;

		/// Run the main loop until quit is requested.
		int Run();

		/// Registers a shader for hot-reload (path relative to paths.content, e.g. "shaders/foo.vert").
		void WatchShader(std::string_view relativePath, engine::render::ShaderStage stage, std::string_view defines = {});

		/// Returns the shader cache (e.g. to get SPIR-V by key).
		engine::render::ShaderCache& GetShaderCache() { return m_shaderCache; }
		const engine::render::ShaderCache& GetShaderCache() const { return m_shaderCache; }

	private:
		void BeginFrame();
		void Update();
		void Render();
		void EndFrame();

		void SwapRenderState();

		void OnResize(int w, int h);
		void OnQuit();

		engine::core::Config m_cfg;

		engine::platform::Window m_window;
		engine::platform::Input m_input;

		engine::render::VkInstance m_vkInstance;
		engine::render::VkDeviceContext m_vkDeviceContext;
		engine::render::VkSwapchain m_vkSwapchain;
		std::array<engine::render::FrameResources, 2> m_frameResources{};
		engine::render::ShaderCache m_shaderCache;
		engine::render::ShaderHotReload m_shaderHotReload;
		uint32_t m_currentFrame = 0;
		GLFWwindow* m_glfwWindowForVk = nullptr;
		bool m_swapchainResizeRequested = false;

		engine::render::FrameGraph m_frameGraph;
		engine::render::Registry m_fgRegistry;
		engine::render::ResourceId m_fgSceneColorId = engine::render::kInvalidResourceId;
		engine::render::ResourceId m_fgBackbufferId = engine::render::kInvalidResourceId;
		engine::render::ResourceId m_fgGBufferAId = engine::render::kInvalidResourceId;
		engine::render::ResourceId m_fgGBufferBId = engine::render::kInvalidResourceId;
		engine::render::ResourceId m_fgGBufferCId = engine::render::kInvalidResourceId;
		engine::render::ResourceId m_fgDepthId = engine::render::kInvalidResourceId;

		engine::render::GeometryPass m_geometryPass;
		engine::render::MeshHandle m_geometryMeshHandle;

		engine::render::AssetRegistry m_assetRegistry;

		engine::core::Time m_time;
		engine::core::memory::FrameArena m_frameArena;
		engine::render::FpsCameraController m_fpsCameraController;

		std::array<RenderState, 2> m_renderStates{};
		std::atomic<uint32_t> m_renderReadIndex{ 0 };

		bool m_quitRequested = false;
		bool m_vsync = true;
		double m_fixedDt = 0.0;
		int m_width = 0;
		int m_height = 0;
	};
}

