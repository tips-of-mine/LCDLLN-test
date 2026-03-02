#include "engine/Engine.h"

#include "engine/core/Log.h"
#include "engine/core/memory/Memory.h"
#include "engine/platform/FileSystem.h"

#include <chrono>
#include <thread>

namespace engine
{
	Engine::Engine(int argc, char** argv)
		: m_cfg(engine::core::Config::Load("config.json", argc, argv))
		, m_time(120)
		, m_frameArena(/*framesInFlight*/ 2, /*perFrameCapacityBytes*/ 1024 * 1024)
	{
		m_vsync = m_cfg.GetBool("render.vsync", true);
		m_fixedDt = m_cfg.GetDouble("time.fixed_dt", 0.0);

		engine::platform::Window::CreateDesc desc{};
		desc.title = "LCDLLN Engine";
		desc.width = 1280;
		desc.height = 720;

		if (!m_window.Create(desc))
		{
			LOG_FATAL(Platform, "Window::Create failed");
		}

		m_window.SetOnResize([this](int w, int h) { OnResize(w, h); });
		m_window.SetOnClose([this]() { OnQuit(); });
		m_window.SetMessageHook([this](uint32_t msg, uint64_t wp, int64_t lp)
		{
			m_input.HandleMessage(msg, wp, lp);
		});

		m_window.GetClientSize(m_width, m_height);

		// FS smoke: attempt reading config and a content-relative file (may be missing; API still exercised).
		{
			const auto cfgText = engine::platform::FileSystem::ReadAllText("config.json");
			LOG_INFO(Platform, "FS ReadAllText('config.json'): {} bytes", cfgText.size());

			const auto contentCfgText = engine::platform::FileSystem::ReadAllTextContent(m_cfg, "config.json");
			LOG_INFO(Platform, "FS ReadAllTextContent(paths.content/'config.json'): {} bytes", contentCfgText.size());
		}

		LOG_INFO(Core, "Engine init: vsync={}", m_vsync ? "on" : "off");
	}

	int Engine::Run()
	{
		auto lastFpsLog = std::chrono::steady_clock::now();
		auto lastPresent = lastFpsLog;

		while (!m_quitRequested && !m_window.ShouldClose())
		{
			BeginFrame();
			Update();
			SwapRenderState();
			Render();
			EndFrame();

			const auto now = std::chrono::steady_clock::now();
			if (now - lastFpsLog >= std::chrono::seconds(1))
			{
				LOG_INFO(Core, "fps={:.1f} dt_ms={:.3f} frame={}", m_time.FPS(), m_time.DeltaSeconds() * 1000.0, m_time.FrameIndex());
				lastFpsLog = now;
			}

			// Placeholder "present": frame pacing controlled by `render.vsync`.
			if (m_vsync)
			{
				constexpr auto target = std::chrono::microseconds(16666);
				const auto elapsed = now - lastPresent;
				if (elapsed < target)
				{
					std::this_thread::sleep_for(target - elapsed);
				}
				lastPresent = std::chrono::steady_clock::now();
			}
			else
			{
				lastPresent = now;
			}
		}

		m_window.Destroy();
		return 0;
	}

	void Engine::BeginFrame()
	{
		m_input.BeginFrame();
		m_window.PollEvents();

		if (m_input.WasPressed(engine::platform::Key::Escape))
		{
			OnQuit();
		}

		m_time.BeginFrame();
		m_frameArena.BeginFrame(m_time.FrameIndex());
	}

	void Engine::Update()
	{
		// Update writes to producer buffer while Render reads the consumer buffer.
		const uint32_t readIdx = m_renderReadIndex.load(std::memory_order_acquire);
		const uint32_t writeIdx = 1u - (readIdx & 1u);
		auto& out = m_renderStates[writeIdx];

		const double dt = (m_fixedDt > 0.0) ? m_fixedDt : m_time.DeltaSeconds();
		const double speed = 3.0;

		if (m_input.IsDown(engine::platform::Key::W)) out.posZ += speed * dt;
		if (m_input.IsDown(engine::platform::Key::S)) out.posZ -= speed * dt;
		if (m_input.IsDown(engine::platform::Key::A)) out.posX -= speed * dt;
		if (m_input.IsDown(engine::platform::Key::D)) out.posX += speed * dt;

		out.yaw += static_cast<double>(m_input.MouseDeltaX()) * 0.002;
		out.pitch += static_cast<double>(m_input.MouseDeltaY()) * 0.002;

		// Placeholder: build a minimal "draw list" count from temp allocations.
		for (int i = 0; i < 256; ++i)
		{
			(void)m_frameArena.alloc(64, alignof(std::max_align_t), engine::core::memory::MemTag::Temp);
		}
		out.drawItemCount = 256;
	}

	void Engine::Render()
	{
		const uint32_t idx = m_renderReadIndex.load(std::memory_order_acquire) & 1u;
		const auto& rs = m_renderStates[idx];

		// Placeholder "render": consume RenderState without mutating it.
		(void)rs.drawItemCount;
	}

	void Engine::EndFrame()
	{
		m_time.EndFrame();
	}

	void Engine::SwapRenderState()
	{
		const uint32_t readIdx = m_renderReadIndex.load(std::memory_order_relaxed);
		const uint32_t writeIdx = 1u - (readIdx & 1u);
		m_renderReadIndex.store(writeIdx, std::memory_order_release);
	}

	void Engine::OnResize(int w, int h)
	{
		m_width = w;
		m_height = h;
		LOG_INFO(Platform, "OnResize {}x{}", w, h);
	}

	void Engine::OnQuit()
	{
		if (!m_quitRequested)
		{
			m_quitRequested = true;
			LOG_INFO(Platform, "OnQuit");
		}
	}
}

