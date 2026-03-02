#pragma once

#include "engine/core/Config.h"
#include "engine/core/Time.h"
#include "engine/core/memory/FrameArena.h"
#include "engine/platform/Input.h"
#include "engine/platform/Window.h"
#include "engine/render/vk/VkDeviceContext.h"
#include "engine/render/vk/VkFrameSync.h"
#include "engine/render/vk/VkInstance.h"
#include "engine/render/vk/VkSwapchain.h"
#include "engine/render/ShaderCache.h"
#include "engine/render/ShaderCompiler.h"
#include "engine/render/ShaderHotReload.h"

struct GLFWwindow;

#include <array>
#include <atomic>
#include <cstdint>

namespace engine
{
	/// Minimal render state produced by Update and consumed by Render.
	struct RenderState
	{
		// Camera-like state (placeholder until math/render layers arrive).
		double posX = 0.0;
		double posY = 0.0;
		double posZ = 0.0;
		double yaw = 0.0;
		double pitch = 0.0;

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

		engine::core::Time m_time;
		engine::core::memory::FrameArena m_frameArena;

		std::array<RenderState, 2> m_renderStates{};
		std::atomic<uint32_t> m_renderReadIndex{ 0 };

		bool m_quitRequested = false;
		bool m_vsync = true;
		double m_fixedDt = 0.0;
		int m_width = 0;
		int m_height = 0;
	};
}

