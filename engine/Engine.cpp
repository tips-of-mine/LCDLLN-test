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
				else if (m_vkSwapchain.IsValid())
				{
					m_assetRegistry.Init(m_vkDeviceContext.GetDevice(), m_vkDeviceContext.GetPhysicalDevice(), m_cfg);
					engine::render::ImageDesc sceneColorDesc{};
					sceneColorDesc.format = m_vkSwapchain.GetImageFormat();
					sceneColorDesc.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
					sceneColorDesc.transient = true;
					m_fgSceneColorId = m_frameGraph.createImage("SceneColor", sceneColorDesc);
					m_fgBackbufferId = m_frameGraph.createExternalImage("Backbuffer");
					m_frameGraph.addPass("Clear",
						[this](engine::render::PassBuilder& b) { b.write(m_fgSceneColorId, engine::render::ImageUsage::TransferDst); },
						[this](VkCommandBuffer cmd, engine::render::Registry& reg) {
							VkImage img = reg.getImage(m_fgSceneColorId);
							if (img == VK_NULL_HANDLE) return;
							VkClearColorValue clearColor = { { 0.15f, 0.15f, 0.18f, 1.0f } };
							VkImageSubresourceRange range = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
							vkCmdClearColorImage(cmd, img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clearColor, 1, &range);
						});
					m_frameGraph.addPass("CopyPresent",
						[this](engine::render::PassBuilder& b) {
							b.read(m_fgSceneColorId, engine::render::ImageUsage::TransferSrc);
							b.write(m_fgBackbufferId, engine::render::ImageUsage::TransferDst);
						},
						[this](VkCommandBuffer cmd, engine::render::Registry& reg) {
							VkImage srcImg = reg.getImage(m_fgSceneColorId);
							VkImage dstImg = reg.getImage(m_fgBackbufferId);
							if (srcImg == VK_NULL_HANDLE || dstImg == VK_NULL_HANDLE) return;
							VkExtent2D ext = m_vkSwapchain.GetExtent();
							VkImageCopy region{};
							region.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
							region.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
							region.extent = { ext.width, ext.height, 1 };
							vkCmdCopyImage(cmd, srcImg, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dstImg, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
							VkImageMemoryBarrier barrier{};
							barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
							barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
							barrier.dstAccessMask = 0;
							barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
							barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
							barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
							barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
							barrier.image = dstImg;
							barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
							barrier.subresourceRange.baseMipLevel = 0;
							barrier.subresourceRange.levelCount = 1;
							barrier.subresourceRange.baseArrayLayer = 0;
							barrier.subresourceRange.layerCount = 1;
							vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
						});
					// Third pass (read-only) to validate 3+ passes compile and topological order (DoD M02.2).
					m_frameGraph.addPass("PostRead",
						[this](engine::render::PassBuilder& b) { b.read(m_fgSceneColorId, engine::render::ImageUsage::SampledRead); },
						[](VkCommandBuffer, engine::render::Registry&) {});
					// Asset system: load test mesh + texture (cache: second load returns same handle).
					engine::render::MeshHandle h1 = m_assetRegistry.LoadMesh("meshes/test.mesh");
					engine::render::MeshHandle h2 = m_assetRegistry.LoadMesh("meshes/test.mesh");
					engine::render::TextureHandle t1 = m_assetRegistry.LoadTexture("textures/test.texr", false);
					engine::render::TextureHandle t2 = m_assetRegistry.LoadTexture("textures/test.texr", false);
					if (h1.IsValid() && h2.IsValid() && h1.Id() == h2.Id()) { /* cache OK */ }
					if (t1.IsValid() && t2.IsValid() && t1.Id() == t2.Id()) { /* cache OK */ }
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

		// Destroy Vulkan frame resources, Frame Graph, swapchain, device context, then instance.
		if (m_vkDeviceContext.IsValid())
		{
			vkDeviceWaitIdle(m_vkDeviceContext.GetDevice());
			m_assetRegistry.Destroy();
			m_frameGraph.destroy(m_vkDeviceContext.GetDevice());
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
				m_frameGraph.destroy(m_vkDeviceContext.GetDevice());
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

		if (m_fgSceneColorId != engine::render::kInvalidResourceId && m_fgBackbufferId != engine::render::kInvalidResourceId)
		{
			VkImage backbufferImage = m_vkSwapchain.GetImage(imageIndex);
			VkImageView backbufferView = m_vkSwapchain.GetImageView(imageIndex);
			m_fgRegistry.bindImage(m_fgBackbufferId, backbufferImage, backbufferView);
			m_frameGraph.execute(
				m_vkDeviceContext.GetDevice(),
				m_vkDeviceContext.GetPhysicalDevice(),
				fr.cmdBuffer,
				m_fgRegistry,
				frameIndex,
				extent,
				2u);
		}

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

