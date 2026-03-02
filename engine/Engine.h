#pragma once

#include "engine/core/Config.h"
#include "engine/core/Time.h"
#include "engine/core/memory/FrameArena.h"
#include "engine/platform/Input.h"
#include "engine/platform/Window.h"

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

