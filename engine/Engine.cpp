#include "engine/Engine.h"

#include "engine/core/Log.h"
#include "engine/core/memory/Memory.h"
#include "engine/platform/FileSystem.h"

#include <GLFW/glfw3.h>
#include <vulkan/vulkan.h>

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

		// Vulkan instance + surface (GLFW) for smoke test: init/shutdown.
		if (glfwInit() == GLFW_TRUE)
		{
			glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
			glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
			m_glfwWindowForVk = glfwCreateWindow(1, 1, "VkSurface", nullptr, nullptr);
			if (m_glfwWindowForVk && m_vkInstance.Create())
			{
				if (!m_vkInstance.CreateSurface(m_glfwWindowForVk))
				{
					LOG_WARN(Platform, "VkInstance::CreateSurface failed");
				}
				else if (!m_vkDeviceContext.Create(m_vkInstance.GetHandle(), m_vkInstance.GetSurface()))
				{
					LOG_WARN(Platform, "VkDeviceContext::Create failed");
				}
				else if (!m_vkSwapchain.Create(
					m_vkDeviceContext.GetPhysicalDevice(),
					m_vkDeviceContext.GetDevice(),
					m_vkInstance.GetSurface(),
					m_vkDeviceContext.GetGraphicsQueueFamilyIndex(),
					m_vkDeviceContext.GetPresentQueueFamilyIndex(),
					static_cast<uint32_t>(m_width), static_cast<uint32_t>(m_height)))
				{
					LOG_WARN(Platform, "VkSwapchain::Create failed");
				}
				else if (!engine::render::CreateFrameResources(
					m_vkDeviceContext.GetDevice(),
					m_vkDeviceContext.GetGraphicsQueueFamilyIndex(),
					m_frameResources))
				{
					LOG_WARN(Platform, "CreateFrameResources failed");
				}
			}
			else
			{
				LOG_WARN(Platform, "Vulkan instance or GLFW window for surface failed");
			}
		}
		else
		{
			LOG_WARN(Platform, "glfwInit failed");
		}

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

		// Destroy Vulkan frame resources, swapchain, device context, then instance.
		if (m_vkDeviceContext.IsValid())
		{
			vkDeviceWaitIdle(m_vkDeviceContext.GetDevice());
			engine::render::DestroyFrameResources(m_vkDeviceContext.GetDevice(), m_frameResources);
		}
		m_vkSwapchain.Destroy();
		m_vkDeviceContext.Destroy();
		m_vkInstance.Destroy();
		if (m_glfwWindowForVk)
		{
			glfwDestroyWindow(m_glfwWindowForVk);
			m_glfwWindowForVk = nullptr;
		}
		glfwTerminate();

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

		m_shaderHotReload.Poll(m_cfg);
		m_shaderHotReload.ApplyPending(m_shaderCache);

		if (m_swapchainResizeRequested)
		{
			m_swapchainResizeRequested = false;
			if (m_vkDeviceContext.IsValid() && m_vkSwapchain.IsValid() && m_width > 0 && m_height > 0)
			{
				vkDeviceWaitIdle(m_vkDeviceContext.GetDevice());
				if (m_vkSwapchain.Recreate(static_cast<uint32_t>(m_width), static_cast<uint32_t>(m_height)))
				{
					LOG_INFO(Platform, "Swapchain recreated {}x{}", m_width, m_height);
				}
			}
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
		if (!m_vkDeviceContext.IsValid() || !m_vkSwapchain.IsValid() || m_frameResources[0].cmdPool == VK_NULL_HANDLE)
		{
			return;
		}

		const uint32_t frameIndex = m_currentFrame % 2;
		engine::render::FrameResources& fr = m_frameResources[frameIndex];
		::VkDevice device = m_vkDeviceContext.GetDevice();
		VkQueue graphicsQueue = m_vkDeviceContext.GetGraphicsQueue();
		VkQueue presentQueue = m_vkDeviceContext.GetPresentQueue();
		VkSwapchainKHR swapchain = m_vkSwapchain.GetSwapchain();
		VkRenderPass renderPass = m_vkSwapchain.GetRenderPass();
		VkExtent2D extent = m_vkSwapchain.GetExtent();

		vkWaitForFences(device, 1, &fr.fence, VK_TRUE, UINT64_MAX);

		uint32_t imageIndex = 0;
		VkResult result = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, fr.imageAvailable, VK_NULL_HANDLE, &imageIndex);
		if (result == VK_ERROR_OUT_OF_DATE_KHR)
		{
			m_swapchainResizeRequested = true;
			return;
		}
		if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
		{
			return;
		}

		vkResetCommandPool(device, fr.cmdPool, 0);

		VkCommandBufferBeginInfo beginInfo{};
		beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		if (vkBeginCommandBuffer(fr.cmdBuffer, &beginInfo) != VK_SUCCESS)
		{
			return;
		}

		VkClearValue clearValue{};
		clearValue.color = { { 0.15f, 0.15f, 0.18f, 1.0f } };

		VkRenderPassBeginInfo rpBegin{};
		rpBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		rpBegin.renderPass = renderPass;
		rpBegin.framebuffer = m_vkSwapchain.GetFramebuffer(imageIndex);
		rpBegin.renderArea.offset = { 0, 0 };
		rpBegin.renderArea.extent = extent;
		rpBegin.clearValueCount = 1;
		rpBegin.pClearValues = &clearValue;

		vkCmdBeginRenderPass(fr.cmdBuffer, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);
		vkCmdEndRenderPass(fr.cmdBuffer);

		if (vkEndCommandBuffer(fr.cmdBuffer) != VK_SUCCESS)
		{
			return;
		}

		VkSemaphore waitSemaphores[] = { fr.imageAvailable };
		VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
		VkSemaphore signalSemaphores[] = { fr.renderFinished };

		VkSubmitInfo submitInfo{};
		submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submitInfo.waitSemaphoreCount = 1;
		submitInfo.pWaitSemaphores = waitSemaphores;
		submitInfo.pWaitDstStageMask = waitStages;
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &fr.cmdBuffer;
		submitInfo.signalSemaphoreCount = 1;
		submitInfo.pSignalSemaphores = signalSemaphores;

		vkResetFences(device, 1, &fr.fence);
		if (vkQueueSubmit(graphicsQueue, 1, &submitInfo, fr.fence) != VK_SUCCESS)
		{
			return;
		}

		VkPresentInfoKHR presentInfo{};
		presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
		presentInfo.waitSemaphoreCount = 1;
		presentInfo.pWaitSemaphores = signalSemaphores;
		presentInfo.swapchainCount = 1;
		presentInfo.pSwapchains = &swapchain;
		presentInfo.pImageIndices = &imageIndex;

		result = vkQueuePresentKHR(presentQueue, &presentInfo);
		if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
		{
			m_swapchainResizeRequested = true;
		}

		m_currentFrame++;
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

	void Engine::WatchShader(std::string_view relativePath, engine::render::ShaderStage stage, std::string_view defines)
	{
		m_shaderHotReload.Watch(relativePath, stage, defines);
	}

	void Engine::OnResize(int w, int h)
	{
		m_width = w;
		m_height = h;
		m_swapchainResizeRequested = true;
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

