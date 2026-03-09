#include "engine/Engine.h"

#include "engine/core/Log.h"
#include "engine/core/memory/Memory.h"
#include "engine/platform/FileSystem.h"
#include "engine/render/DeferredPipeline.h"
#include "engine/render/ShaderCompiler.h"

#include <GLFW/glfw3.h>
#include <vulkan/vulkan.h>

#define VMA_STATIC_VULKAN_FUNCTIONS  0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 0
#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace engine
{
	Engine::Engine(int argc, char** argv)
		: m_cfg(engine::core::Config::Load("config.json", argc, argv))
		, m_time(120)
		, m_frameArena(/*framesInFlight*/ 2, /*perFrameCapacityBytes*/ 1024 * 1024)
	{
		std::fprintf(stderr, "[ENGINE] A: debut constructeur\n"); std::fflush(stderr);

		// ------------------------------------------------------------------
		// Logging
		// ------------------------------------------------------------------
		bool logToFile    = false;
		bool logToConsole = false;
		for (int i = 1; i < argc; ++i)
		{
			if (!argv[i]) continue;
			const std::string_view arg(argv[i]);
			if (arg == "-log")     logToFile    = true;
			if (arg == "-console") logToConsole = true;
		}

		engine::core::LogSettings logSettings;
		logSettings.filePath    = logToFile
			? engine::core::Log::MakeTimestampedFilename("lcdlln.exe")
			: "";
		logSettings.console     = logToConsole;
		logSettings.flushAlways = true;
		logSettings.level       = engine::core::LogLevel::Info;

		engine::core::Log::Init(logSettings);
		std::fprintf(stderr, "[ENGINE] B: Log::Init OK\n"); std::fflush(stderr);

		LOG_INFO(Core, "[Boot] Log initialized (console={}, file={})", logToConsole ? "on" : "off", logSettings.filePath);
		std::fprintf(stderr, "[ENGINE] C: LOG_INFO OK\n"); std::fflush(stderr);

		// ------------------------------------------------------------------
		// Config + subsystems
		// ------------------------------------------------------------------
		m_vsync   = m_cfg.GetBool("render.vsync", true);
		m_fixedDt = m_cfg.GetDouble("time.fixed_dt", 0.0);
		std::fprintf(stderr, "[ENGINE] D: config OK\n"); std::fflush(stderr);

		LOG_INFO(Core, "[Boot] Config loaded (vsync={}, fixed_dt={})", m_vsync ? "on" : "off", m_fixedDt);
		std::fprintf(stderr, "[ENGINE] E: chunkStats.Init\n"); std::fflush(stderr);

		m_chunkStats.Init(m_cfg);
		std::fprintf(stderr, "[ENGINE] F: lodConfig.Init\n"); std::fflush(stderr);

		m_lodConfig.Init(m_cfg);
		std::fprintf(stderr, "[ENGINE] G: hlodRuntime.Init\n"); std::fflush(stderr);

		m_hlodRuntime.Init(m_cfg);
		std::fprintf(stderr, "[ENGINE] H: streamCache.Init\n"); std::fflush(stderr);

		m_streamCache.Init(m_cfg);
		std::fprintf(stderr, "[ENGINE] I: streamingScheduler.SetStreamCache\n"); std::fflush(stderr);

		m_streamingScheduler.SetStreamCache(&m_streamCache);
		std::fprintf(stderr, "[ENGINE] J: gpuUploadQueue.Init\n"); std::fflush(stderr);

		m_gpuUploadQueue.Init(m_cfg);
		std::fprintf(stderr, "[ENGINE] K: subsystems OK\n"); std::fflush(stderr);

		LOG_INFO(Core, "[Boot] FrameArena init OK");

		// ------------------------------------------------------------------
		// Window
		// ------------------------------------------------------------------
		engine::platform::Window::CreateDesc desc{};
		desc.title  = "LCDLLN Engine";
		desc.width  = 1280;
		desc.height = 720;

		std::fprintf(stderr, "[ENGINE] L: avant Window::Create\n"); std::fflush(stderr);
		if (!m_window.Create(desc))
		{
			LOG_FATAL(Platform, "[Boot] Window::Create failed");
		}
		std::fprintf(stderr, "[ENGINE] M: Window::Create OK\n"); std::fflush(stderr);
		LOG_INFO(Core, "[Boot] Window::Create OK");

		m_window.SetOnResize([this](int w, int h) { OnResize(w, h); });
		m_window.SetOnClose([this]() { OnQuit(); });
		m_window.SetMessageHook([this](uint32_t msg, uint64_t wp, int64_t lp)
		{
			m_input.HandleMessage(msg, wp, lp);
		});
		m_window.GetClientSize(m_width, m_height);
		std::fprintf(stderr, "[ENGINE] N: window setup OK w=%d h=%d\n", m_width, m_height); std::fflush(stderr);

		// -----------------------------------------------------------------
		// Vulkan init
		// -----------------------------------------------------------------
		std::fprintf(stderr, "[ENGINE] O: avant glfwInit\n"); std::fflush(stderr);
		if (glfwInit() != GLFW_TRUE)
		{
			LOG_WARN(Platform, "[Boot] glfwInit failed");
		}
		else
		{
			std::fprintf(stderr, "[ENGINE] P: glfwInit OK\n"); std::fflush(stderr);
			LOG_INFO(Core, "[Boot] glfwInit OK");
			glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
			glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
			std::fprintf(stderr, "[ENGINE] Q: avant glfwCreateWindow\n"); std::fflush(stderr);
			m_glfwWindowForVk = glfwCreateWindow(1, 1, "VkSurface", nullptr, nullptr);
			std::fprintf(stderr, "[ENGINE] R: glfwCreateWindow ptr=%p\n", (void*)m_glfwWindowForVk); std::fflush(stderr);
			if (!m_glfwWindowForVk)
			{
				LOG_WARN(Platform, "[Boot] glfwCreateWindow returned null");
			}
			else
			{
				LOG_INFO(Core, "[Boot] glfwCreateWindow OK");
			}
			std::fprintf(stderr, "[ENGINE] S: avant VkInstance::Create\n"); std::fflush(stderr);
			if (m_glfwWindowForVk && m_vkInstance.Create())
			{
				std::fprintf(stderr, "[ENGINE] T: VkInstance::Create OK\n"); std::fflush(stderr);
				LOG_INFO(Core, "[Boot] VkInstance::Create OK");
				std::fprintf(stderr, "[ENGINE] U: avant CreateSurface\n"); std::fflush(stderr);
				if (!m_vkInstance.CreateSurface(m_glfwWindowForVk))
				{
					LOG_WARN(Platform, "[Boot] VkInstance::CreateSurface failed");
				}
				else
				{
					std::fprintf(stderr, "[ENGINE] V: CreateSurface OK\n"); std::fflush(stderr);
					LOG_INFO(Core, "[Boot] VkInstance::CreateSurface OK");
					std::fprintf(stderr, "[ENGINE] W: avant VkDeviceContext::Create\n"); std::fflush(stderr);
					if (!m_vkDeviceContext.Create(m_vkInstance.GetHandle(), m_vkInstance.GetSurface()))
					{
						LOG_WARN(Platform, "[Boot] VkDeviceContext::Create failed");
					}
					else
					{
						std::fprintf(stderr, "[ENGINE] X: VkDeviceContext::Create OK\n"); std::fflush(stderr);
						VkPhysicalDeviceProperties physProps{};
						vkGetPhysicalDeviceProperties(m_vkDeviceContext.GetPhysicalDevice(), &physProps);
						LOG_INFO(Core, "[Boot] VkDeviceContext::Create OK (GPU: {})", physProps.deviceName);

						VkPresentModeKHR requestedMode = VK_PRESENT_MODE_FIFO_KHR;
						if (!m_vsync)
							requestedMode = VK_PRESENT_MODE_IMMEDIATE_KHR;
						else
						{
							const std::string pm = m_cfg.GetString("render.present_mode", "fifo");
							if (pm == "mailbox")
								requestedMode = VK_PRESENT_MODE_MAILBOX_KHR;
						}

						std::fprintf(stderr, "[ENGINE] Y: avant VkSwapchain::Create\n"); std::fflush(stderr);
						if (!m_vkSwapchain.Create(
							m_vkDeviceContext.GetPhysicalDevice(),
							m_vkDeviceContext.GetDevice(),
							m_vkInstance.GetSurface(),
							m_vkDeviceContext.GetGraphicsQueueFamilyIndex(),
							m_vkDeviceContext.GetPresentQueueFamilyIndex(),
							static_cast<uint32_t>(m_width), static_cast<uint32_t>(m_height),
							requestedMode))
						{
							LOG_WARN(Platform, "[Boot] VkSwapchain::Create failed");
						}
						else
						{
							std::fprintf(stderr, "[ENGINE] Z: VkSwapchain::Create OK\n"); std::fflush(stderr);
							VkExtent2D swapExtent = m_vkSwapchain.GetExtent();
							LOG_INFO(Core, "[Boot] VkSwapchain::Create OK (extent={}x{}, images={})",
								swapExtent.width, swapExtent.height, m_vkSwapchain.GetImageCount());

							std::fprintf(stderr, "[ENGINE] AA: avant CreateFrameResources\n"); std::fflush(stderr);
							if (!engine::render::CreateFrameResources(
								m_vkDeviceContext.GetDevice(),
								m_vkDeviceContext.GetGraphicsQueueFamilyIndex(),
								m_frameResources))
							{
								LOG_WARN(Platform, "[Boot] FrameSync::Init failed");
							}
							else
							{
								std::fprintf(stderr, "[ENGINE] AB: CreateFrameResources OK\n"); std::fflush(stderr);
								LOG_INFO(Core, "[Boot] FrameSync::Init OK");

								if (m_vkSwapchain.IsValid())
								{
									std::fprintf(stderr, "[ENGINE] AC: avant vmaCreateAllocator\n"); std::fflush(stderr);
									VmaVulkanFunctions vmaFuncs{};
									vmaFuncs.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
									vmaFuncs.vkGetDeviceProcAddr   = vkGetDeviceProcAddr;
									
									VmaAllocatorCreateInfo vmaInfo{};
									vmaInfo.physicalDevice   = m_vkDeviceContext.GetPhysicalDevice();
									vmaInfo.device           = m_vkDeviceContext.GetDevice();
									vmaInfo.instance         = m_vkInstance.GetHandle();
									vmaInfo.vulkanApiVersion = VK_API_VERSION_1_2;
									vmaInfo.pVulkanFunctions = &vmaFuncs;
									if (vmaCreateAllocator(&vmaInfo, reinterpret_cast<VmaAllocator*>(&m_vmaAllocator)) != VK_SUCCESS)
									{
										LOG_ERROR(Render, "VMA allocator creation failed");
										m_vmaAllocator = nullptr;
									}
									std::fprintf(stderr, "[ENGINE] AD: vmaCreateAllocator OK ptr=%p\n", m_vmaAllocator); std::fflush(stderr);

									if (m_vmaAllocator)
									{
										std::fprintf(stderr, "[ENGINE] AE: avant StagingAllocator::Init\n"); std::fflush(stderr);
										if (!m_stagingAllocator.Init(m_vkDeviceContext.GetDevice(), m_vmaAllocator, m_gpuUploadQueue.GetBudgetBytes()))
											LOG_WARN(Render, "StagingAllocator init failed");
										std::fprintf(stderr, "[ENGINE] AF: avant make_unique DeferredPipeline\n"); std::fflush(stderr);

										m_pipeline = std::make_unique<engine::render::DeferredPipeline>();
										std::fprintf(stderr, "[ENGINE] AG: avant assetRegistry.Init\n"); std::fflush(stderr);

										m_assetRegistry.Init(m_vkDeviceContext.GetDevice(), m_vkDeviceContext.GetPhysicalDevice(), m_vmaAllocator, m_cfg);
										std::fprintf(stderr, "[ENGINE] AH: assetRegistry OK\n"); std::fflush(stderr);

										{
											std::string lutPath = m_cfg.GetString("color_grading.lut_path", "");
											if (!lutPath.empty())
												m_colorGradingLutHandle = m_assetRegistry.LoadTexture(lutPath, true);
										}

										std::fprintf(stderr, "[ENGINE] AI: avant createImage SceneColor\n"); std::fflush(stderr);
										engine::render::ImageDesc sceneColorDesc{};
										sceneColorDesc.format    = m_vkSwapchain.GetImageFormat();
										sceneColorDesc.usage     = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
										sceneColorDesc.transient = true;
										m_fgSceneColorId = m_frameGraph.createImage("SceneColor", sceneColorDesc);

										m_fgBackbufferId = m_frameGraph.createExternalImage("Backbuffer");

										engine::render::ImageDesc gbufADesc{};
										gbufADesc.format = VK_FORMAT_R8G8B8A8_SRGB;
										gbufADesc.usage  = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
										m_fgGBufferAId   = m_frameGraph.createImage("GBufferA", gbufADesc);

										engine::render::ImageDesc gbufBDesc{};
										gbufBDesc.format = VK_FORMAT_A2B10G10R10_UNORM_PACK32;
										gbufBDesc.usage  = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
										m_fgGBufferBId   = m_frameGraph.createImage("GBufferB", gbufBDesc);

										engine::render::ImageDesc gbufCDesc{};
										gbufCDesc.format = VK_FORMAT_R8G8B8A8_UNORM;
										gbufCDesc.usage  = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
										m_fgGBufferCId   = m_frameGraph.createImage("GBufferC", gbufCDesc);

										engine::render::ImageDesc gbufVelDesc{};
										gbufVelDesc.format = VK_FORMAT_R16G16_SFLOAT;
										gbufVelDesc.usage  = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
										m_fgGBufferVelocityId = m_frameGraph.createImage("GBufferVelocity", gbufVelDesc);

										engine::render::ImageDesc depthDesc{};
										depthDesc.format            = VK_FORMAT_D32_SFLOAT;
										depthDesc.usage             = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
										depthDesc.isDepthAttachment = true;
										m_fgDepthId = m_frameGraph.createImage("Depth", depthDesc);

										engine::render::ImageDesc sceneColorHDRDesc{};
										sceneColorHDRDesc.format = VK_FORMAT_R16G16B16A16_SFLOAT;
										sceneColorHDRDesc.usage  = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
										m_fgSceneColorHDRId = m_frameGraph.createImage("SceneColor_HDR", sceneColorHDRDesc);

										engine::render::ImageDesc sceneColorLDRDesc{};
										sceneColorLDRDesc.format = VK_FORMAT_R8G8B8A8_UNORM;
										sceneColorLDRDesc.usage  = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
										m_fgSceneColorLDRId = m_frameGraph.createImage("SceneColor_LDR", sceneColorLDRDesc);

										engine::render::ImageDesc ssaoRawDesc{};
										ssaoRawDesc.format = VK_FORMAT_R16_SFLOAT;
										ssaoRawDesc.usage  = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
										m_fgSsaoRawId = m_frameGraph.createImage("SSAO_Raw", ssaoRawDesc);

										engine::render::ImageDesc ssaoBlurDesc{};
										ssaoBlurDesc.format = VK_FORMAT_R16_SFLOAT;
										ssaoBlurDesc.usage  = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
										m_fgSsaoBlurTempId = m_frameGraph.createImage("SSAO_Blur_Temp", ssaoBlurDesc);
										m_fgSsaoBlurId     = m_frameGraph.createImage("SSAO_Blur", ssaoBlurDesc);

										engine::render::ImageDesc historyDesc{};
										historyDesc.format = VK_FORMAT_R8G8B8A8_UNORM;
										historyDesc.usage  = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT
										                   | VK_IMAGE_USAGE_SAMPLED_BIT
										                   | VK_IMAGE_USAGE_TRANSFER_DST_BIT
										                   | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
										m_fgHistoryAId = m_frameGraph.createImage("HistoryA", historyDesc);
										m_fgHistoryBId = m_frameGraph.createImage("HistoryB", historyDesc);

										engine::render::ImageDesc bloomMipDesc{};
										bloomMipDesc.format = VK_FORMAT_R16G16B16A16_SFLOAT;
										bloomMipDesc.usage  = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
										for (uint32_t i = 0; i < engine::render::kBloomMipCount; ++i)
										{
											char name[32];
											std::snprintf(name, sizeof(name), "BloomMip_%u", i);
											bloomMipDesc.extentScalePower = i;
											m_fgBloomMipIds[i] = m_frameGraph.createImage(name, bloomMipDesc);
										}

										engine::render::ImageDesc sceneColorHDRWithBloomDesc{};
										sceneColorHDRWithBloomDesc.format = VK_FORMAT_R16G16B16A16_SFLOAT;
										sceneColorHDRWithBloomDesc.usage  = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
										m_fgSceneColorHDRWithBloomId = m_frameGraph.createImage("SceneColor_HDR_WithBloom", sceneColorHDRWithBloomDesc);

										const uint32_t shadowRes = static_cast<uint32_t>(m_cfg.GetInt("shadows.resolution", 1024));
										engine::render::ImageDesc shadowDesc{};
										shadowDesc.format            = VK_FORMAT_D32_SFLOAT;
										shadowDesc.width             = shadowRes;
										shadowDesc.height            = shadowRes;
										shadowDesc.usage             = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
										shadowDesc.isDepthAttachment = true;
										for (uint32_t i = 0; i < engine::render::kCascadeCount; ++i)
										{
											char name[32];
											std::snprintf(name, sizeof(name), "ShadowMap_%u", i);
											m_fgShadowMapIds[i] = m_frameGraph.createImage(name, shadowDesc);
										}
										std::fprintf(stderr, "[ENGINE] AJ: frame graph images OK\n"); std::fflush(stderr);

										{
											engine::render::ShaderCompiler sc;
											if (sc.LocateCompiler())
												LOG_INFO(Core, "[Boot] ShaderCompiler OK");
											else
												LOG_WARN(Render, "[Boot] ShaderCompiler glslangValidator not found");
										}
										std::fprintf(stderr, "[ENGINE] AK: ShaderCompiler check OK\n"); std::fflush(stderr);

										auto loadSpirv = [&](const char* spvPath) -> std::vector<uint32_t>
										{
											std::vector<uint8_t> bytes = engine::platform::FileSystem::ReadAllBytesContent(m_cfg, spvPath);
											if (bytes.size() % 4 == 0 && !bytes.empty())
											{
												std::vector<uint32_t> out(bytes.size() / 4);
												std::memcpy(out.data(), bytes.data(), bytes.size());
												return out;
											}
											engine::render::ShaderCompiler compiler;
											if (!compiler.LocateCompiler()) return {};
											std::string base(spvPath);
											if (base.size() > 4 && base.compare(base.size() - 4, 4, ".spv") == 0)
												base.resize(base.size() - 4);
											std::filesystem::path srcPath = engine::platform::FileSystem::ResolveContentPath(m_cfg, base);
											engine::render::ShaderStage stage = engine::render::ShaderStage::Vertex;
											if (base.size() >= 5 && base.compare(base.size() - 5, 5, ".comp") == 0)
												stage = engine::render::ShaderStage::Compute;
											else if (base.size() >= 5 && base.compare(base.size() - 5, 5, ".vert") == 0)
												stage = engine::render::ShaderStage::Vertex;
											else if (base.size() >= 5 && base.compare(base.size() - 5, 5, ".frag") == 0)
												stage = engine::render::ShaderStage::Fragment;
											auto c = compiler.CompileGlslToSpirv(srcPath, stage);
											if (c.has_value() && !c->empty()) return std::move(*c);
											return {};
										};

										std::fprintf(stderr, "[ENGINE] AL: avant pipeline->Init\n"); std::fflush(stderr);
										m_pipeline->Init(
											m_vkDeviceContext.GetDevice(),
											m_vkDeviceContext.GetPhysicalDevice(),
											m_vmaAllocator,
											m_cfg,
											shadowRes,
											m_vkSwapchain.GetImageFormat(),
											m_vkDeviceContext.GetGraphicsQueue(),
											m_vkDeviceContext.GetGraphicsQueueFamilyIndex(),
											loadSpirv);
										std::fprintf(stderr, "[ENGINE] AM: pipeline->Init OK\n"); std::fflush(stderr);
										LOG_INFO(Core, "[Boot] DeferredPipeline init OK");

										std::fprintf(stderr, "[ENGINE] AN: avant addPass Clear\n"); std::fflush(stderr);
										m_frameGraph.addPass("Clear",
											[this](engine::render::PassBuilder& b) {
												b.write(m_fgSceneColorId, engine::render::ImageUsage::TransferDst);
											},
											[this](VkCommandBuffer cmd, engine::render::Registry& reg) {
												VkImage img = reg.getImage(m_fgSceneColorId);
												if (img == VK_NULL_HANDLE) return;
												VkClearColorValue clearColor = { { 0.15f, 0.15f, 0.18f, 1.0f } };
												VkImageSubresourceRange range = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
												vkCmdClearColorImage(cmd, img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clearColor, 1, &range);
											});

										std::fprintf(stderr, "[ENGINE] AO: avant LoadMesh\n"); std::fflush(stderr);
										m_geometryMeshHandle = m_assetRegistry.LoadMesh("meshes/test.mesh");
										std::fprintf(stderr, "[ENGINE] AP: LoadMesh OK\n"); std::fflush(stderr);

										std::fprintf(stderr, "[ENGINE] AQ: avant addPass Geometry\n"); std::fflush(stderr);
										m_frameGraph.addPass("Geometry",
											[this](engine::render::PassBuilder& b) {
												b.write(m_fgGBufferAId,        engine::render::ImageUsage::ColorWrite);
												b.write(m_fgGBufferBId,        engine::render::ImageUsage::ColorWrite);
												b.write(m_fgGBufferCId,        engine::render::ImageUsage::ColorWrite);
												b.write(m_fgGBufferVelocityId, engine::render::ImageUsage::ColorWrite);
												b.write(m_fgDepthId,           engine::render::ImageUsage::DepthWrite);
											},
											[this](VkCommandBuffer cmd, engine::render::Registry& reg) {
												const uint32_t readIdx = m_renderReadIndex.load(std::memory_order_acquire);
												const engine::RenderState& rs = m_renderStates[readIdx];
												engine::render::MeshAsset* mesh = m_geometryMeshHandle.Get();
												const engine::world::ChunkCoord chunk = engine::world::WorldToChunkCoord(rs.camera.position.x, rs.camera.position.z);
												const engine::world::ChunkRing ring = m_world.GetRingForChunk(chunk);
												const uint32_t triCount = (mesh && mesh->indexCount > 0) ? (mesh->indexCount / 3) : 0;
												m_chunkStats.RecordDraw(chunk, ring, 1, triCount);
												const int lodLevel = m_lodConfig.GetLodLevel(0.0f);
												m_pipeline->GetGeometryPass().Record(
													m_vkDeviceContext.GetDevice(), cmd, reg,
													m_vkSwapchain.GetExtent(),
													m_fgGBufferAId, m_fgGBufferBId, m_fgGBufferCId, m_fgGBufferVelocityId, m_fgDepthId,
													rs.prevViewProjMatrix.m, rs.viewProjMatrix.m, mesh,
													static_cast<uint32_t>(lodLevel));
											});
										std::fprintf(stderr, "[ENGINE] AR: addPass Geometry OK\n"); std::fflush(stderr);

										std::fprintf(stderr, "[ENGINE] AS: avant addPass ShadowMap\n"); std::fflush(stderr);
										for (uint32_t cascade = 0; cascade < engine::render::kCascadeCount; ++cascade)
										{
											const std::string passName = std::string("ShadowMap_") + std::to_string(cascade);
											m_frameGraph.addPass(passName,
												[this, cascade](engine::render::PassBuilder& b) {
													b.write(m_fgShadowMapIds[cascade], engine::render::ImageUsage::DepthWrite);
												},
												[this, cascade](VkCommandBuffer cmd, engine::render::Registry& reg) {
													if (!m_pipeline->GetShadowMapPass().IsValid()) return;
													const uint32_t readIdx = m_renderReadIndex.load(std::memory_order_acquire);
													const engine::RenderState& rs = m_renderStates[readIdx];
													engine::render::MeshAsset* mesh = m_geometryMeshHandle.Get();
													const engine::world::ChunkCoord chunk = engine::world::WorldToChunkCoord(rs.camera.position.x, rs.camera.position.z);
													const engine::world::ChunkRing ring = m_world.GetRingForChunk(chunk);
													const uint32_t triCount = (mesh && mesh->indexCount > 0) ? (mesh->indexCount / 3) : 0;
													m_chunkStats.RecordDraw(chunk, ring, 1, triCount);
													const float depthBiasConstant = static_cast<float>(m_cfg.GetDouble("shadows.depth_bias_constant", 1.25));
													const float depthBiasSlope    = static_cast<float>(m_cfg.GetDouble("shadows.depth_bias_slope", 1.75));
													const bool  cullFrontFaces    = m_cfg.GetBool("shadows.cull_front_faces", false);
													m_pipeline->GetShadowMapPass().Record(
														m_vkDeviceContext.GetDevice(), cmd, reg,
														m_fgShadowMapIds[cascade],
														rs.cascades.lightViewProj[cascade].m,
														mesh, depthBiasConstant, depthBiasSlope, cullFrontFaces);
												});
										}
										std::fprintf(stderr, "[ENGINE] AT: addPass ShadowMap OK\n"); std::fflush(stderr);

										std::fprintf(stderr, "[ENGINE] AU: avant addPass SSAO_Generate\n"); std::fflush(stderr);
										m_frameGraph.addPass("SSAO_Generate",
											[this](engine::render::PassBuilder& b) {
												b.read(m_fgDepthId,    engine::render::ImageUsage::SampledRead);
												b.read(m_fgGBufferBId, engine::render::ImageUsage::SampledRead);
												b.write(m_fgSsaoRawId, engine::render::ImageUsage::ColorWrite);
											},
											[this](VkCommandBuffer cmd, engine::render::Registry& reg) {
												if (!m_pipeline->GetSsaoPass().IsValid()) return;
												const uint32_t readIdx = m_renderReadIndex.load(std::memory_order_acquire);
												const engine::RenderState& rs = m_renderStates[readIdx];
												engine::render::SsaoPass::SsaoParams sp{};
												const float* proj = rs.projMatrix.m;
												const float a00=proj[0], a10=proj[1], a20=proj[2],  a30=proj[3];
												const float a01=proj[4], a11=proj[5], a21=proj[6],  a31=proj[7];
												const float a02=proj[8], a12=proj[9], a22=proj[10], a32=proj[11];
												const float a03=proj[12],a13=proj[13],a23=proj[14], a33=proj[15];
												const float b00=a00*a11-a10*a01, b01=a00*a21-a20*a01, b02=a00*a31-a30*a01;
												const float b03=a10*a21-a20*a11, b04=a10*a31-a30*a11, b05=a20*a31-a30*a21;
												const float b06=a02*a13-a12*a03, b07=a02*a23-a22*a03, b08=a02*a33-a32*a03;
												const float b09=a12*a23-a22*a13, b10=a12*a33-a32*a13, b11=a22*a33-a32*a23;
												const float det = b00*b11-b01*b10+b02*b09+b03*b08-b04*b07+b05*b06;
												if (det > 1e-7f || det < -1e-7f)
												{
													const float inv = 1.0f / det;
													sp.invProj[0]  = ( a11*b11-a21*b10+a31*b09)*inv;
													sp.invProj[1]  = (-a10*b11+a20*b10-a30*b09)*inv;
													sp.invProj[2]  = ( a13*b05-a23*b04+a33*b03)*inv;
													sp.invProj[3]  = (-a12*b05+a22*b04-a32*b03)*inv;
													sp.invProj[4]  = (-a01*b11+a21*b08-a31*b07)*inv;
													sp.invProj[5]  = ( a00*b11-a20*b08+a30*b07)*inv;
													sp.invProj[6]  = (-a03*b05+a23*b02-a33*b01)*inv;
													sp.invProj[7]  = ( a02*b05-a22*b02+a32*b01)*inv;
													sp.invProj[8]  = ( a01*b10-a11*b08+a31*b06)*inv;
													sp.invProj[9]  = (-a00*b10+a10*b08-a30*b06)*inv;
													sp.invProj[10] = ( a03*b04-a13*b02+a33*b00)*inv;
													sp.invProj[11] = (-a02*b04+a12*b02-a32*b00)*inv;
													sp.invProj[12] = (-a01*b09+a11*b07-a21*b06)*inv;
													sp.invProj[13] = ( a00*b09-a10*b07+a20*b06)*inv;
													sp.invProj[14] = (-a03*b03+a13*b01-a23*b00)*inv;
													sp.invProj[15] = ( a02*b03-a12*b01+a22*b00)*inv;
												}
												std::memcpy(sp.view, rs.viewMatrix.m, sizeof(sp.view));
												std::memcpy(sp.proj, rs.projMatrix.m, sizeof(sp.proj));
												const uint32_t frameIdx = m_currentFrame % 2;
												m_pipeline->GetSsaoPass().Record(
													m_vkDeviceContext.GetDevice(), cmd, reg,
													m_vkSwapchain.GetExtent(),
													m_fgDepthId, m_fgGBufferBId, m_fgSsaoRawId,
													m_pipeline->GetSsaoKernelNoise().GetKernelBuffer(),
													m_pipeline->GetSsaoKernelNoise().GetNoiseImageView(),
													m_pipeline->GetSsaoKernelNoise().GetNoiseSampler(),
													sp, frameIdx);
											});
										std::fprintf(stderr, "[ENGINE] AV: addPass SSAO_Generate OK\n"); std::fflush(stderr);

										m_frameGraph.addPass("SSAO_BlurH",
											[this](engine::render::PassBuilder& b) {
												b.read(m_fgSsaoRawId,       engine::render::ImageUsage::SampledRead);
												b.read(m_fgDepthId,         engine::render::ImageUsage::SampledRead);
												b.write(m_fgSsaoBlurTempId, engine::render::ImageUsage::ColorWrite);
											},
											[this](VkCommandBuffer cmd, engine::render::Registry& reg) {
												if (!m_pipeline->GetSsaoBlurPass().IsValid()) return;
												const uint32_t frameIdx = m_currentFrame % 2;
												m_pipeline->GetSsaoBlurPass().Record(
													m_vkDeviceContext.GetDevice(), cmd, reg, m_vkSwapchain.GetExtent(),
													m_fgSsaoRawId, m_fgDepthId, m_fgSsaoBlurTempId, true, frameIdx);
											});

										m_frameGraph.addPass("SSAO_BlurV",
											[this](engine::render::PassBuilder& b) {
												b.read(m_fgSsaoBlurTempId, engine::render::ImageUsage::SampledRead);
												b.read(m_fgDepthId,        engine::render::ImageUsage::SampledRead);
												b.write(m_fgSsaoBlurId,    engine::render::ImageUsage::ColorWrite);
											},
											[this](VkCommandBuffer cmd, engine::render::Registry& reg) {
												if (!m_pipeline->GetSsaoBlurPass().IsValid()) return;
												const uint32_t frameIdx = m_currentFrame % 2;
												m_pipeline->GetSsaoBlurPass().Record(
													m_vkDeviceContext.GetDevice(), cmd, reg, m_vkSwapchain.GetExtent(),
													m_fgSsaoBlurTempId, m_fgDepthId, m_fgSsaoBlurId, false, frameIdx);
											});
										std::fprintf(stderr, "[ENGINE] AW: addPass SSAO_Blur OK\n"); std::fflush(stderr);

										std::fprintf(stderr, "[ENGINE] AX: avant addPass Lighting\n"); std::fflush(stderr);
										m_frameGraph.addPass("Lighting",
											[this](engine::render::PassBuilder& b) {
												b.read(m_fgGBufferAId,       engine::render::ImageUsage::SampledRead);
												b.read(m_fgGBufferBId,       engine::render::ImageUsage::SampledRead);
												b.read(m_fgGBufferCId,       engine::render::ImageUsage::SampledRead);
												b.read(m_fgDepthId,          engine::render::ImageUsage::SampledRead);
												b.read(m_fgSsaoBlurId,       engine::render::ImageUsage::SampledRead);
												b.write(m_fgSceneColorHDRId, engine::render::ImageUsage::ColorWrite);
											},
											[this](VkCommandBuffer cmd, engine::render::Registry& reg) {
												if (!m_pipeline->GetLightingPass().IsValid()) return;
												const uint32_t readIdx = m_renderReadIndex.load(std::memory_order_acquire);
												const engine::RenderState& rs = m_renderStates[readIdx];
												engine::render::LightingPass::LightParams lp{};
												const float* vp = rs.viewProjMatrix.m;
												const float a00=vp[0], a10=vp[1], a20=vp[2],  a30=vp[3];
												const float a01=vp[4], a11=vp[5], a21=vp[6],  a31=vp[7];
												const float a02=vp[8], a12=vp[9], a22=vp[10], a32=vp[11];
												const float a03=vp[12],a13=vp[13],a23=vp[14], a33=vp[15];
												const float b00=a00*a11-a10*a01, b01=a00*a21-a20*a01, b02=a00*a31-a30*a01;
												const float b03=a10*a21-a20*a11, b04=a10*a31-a30*a11, b05=a20*a31-a30*a21;
												const float b06=a02*a13-a12*a03, b07=a02*a23-a22*a03, b08=a02*a33-a32*a03;
												const float b09=a12*a23-a22*a13, b10=a12*a33-a32*a13, b11=a22*a33-a32*a23;
												const float det = b00*b11-b01*b10+b02*b09+b03*b08-b04*b07+b05*b06;
												if (det > 1e-7f || det < -1e-7f)
												{
													const float inv = 1.0f / det;
													lp.invViewProj[0]  = ( a11*b11-a21*b10+a31*b09)*inv;
													lp.invViewProj[1]  = (-a10*b11+a20*b10-a30*b09)*inv;
													lp.invViewProj[2]  = ( a13*b05-a23*b04+a33*b03)*inv;
													lp.invViewProj[3]  = (-a12*b05+a22*b04-a32*b03)*inv;
													lp.invViewProj[4]  = (-a01*b11+a21*b08-a31*b07)*inv;
													lp.invViewProj[5]  = ( a00*b11-a20*b08+a30*b07)*inv;
													lp.invViewProj[6]  = (-a03*b05+a23*b02-a33*b01)*inv;
													lp.invViewProj[7]  = ( a02*b05-a22*b02+a32*b01)*inv;
													lp.invViewProj[8]  = ( a01*b10-a11*b08+a31*b06)*inv;
													lp.invViewProj[9]  = (-a00*b10+a10*b08-a30*b06)*inv;
													lp.invViewProj[10] = ( a03*b04-a13*b02+a33*b00)*inv;
													lp.invViewProj[11] = (-a02*b04+a12*b02-a32*b00)*inv;
													lp.invViewProj[12] = (-a01*b09+a11*b07-a21*b06)*inv;
													lp.invViewProj[13] = ( a00*b09-a10*b07+a20*b06)*inv;
													lp.invViewProj[14] = (-a03*b03+a13*b01-a23*b00)*inv;
													lp.invViewProj[15] = ( a02*b03-a12*b01+a22*b00)*inv;
												}
												lp.cameraPos[0] = rs.camera.position.x;
												lp.cameraPos[1] = rs.camera.position.y;
												lp.cameraPos[2] = rs.camera.position.z;
												lp.cameraPos[3] = 0.0f;
												lp.lightDir[0] = 0.5774f; lp.lightDir[1] = 0.5774f; lp.lightDir[2] = 0.5774f; lp.lightDir[3] = 0.0f;
												lp.lightColor[0] = 1.0f; lp.lightColor[1] = 0.95f; lp.lightColor[2] = 0.85f; lp.lightColor[3] = 0.0f;
												lp.ambientColor[0] = 0.03f; lp.ambientColor[1] = 0.03f; lp.ambientColor[2] = 0.05f; lp.ambientColor[3] = 0.0f;
												VkImageView irrView       = VK_NULL_HANDLE;
												VkSampler   irrSamp       = VK_NULL_HANDLE;
												VkImageView prefilterView = m_pipeline->GetSpecularPrefilterPass().IsValid() ? m_pipeline->GetSpecularPrefilterPass().GetImageView() : VK_NULL_HANDLE;
												VkSampler   prefilterSamp = m_pipeline->GetSpecularPrefilterPass().IsValid() ? m_pipeline->GetSpecularPrefilterPass().GetSampler()    : VK_NULL_HANDLE;
												VkImageView brdfView      = m_pipeline->GetBrdfLutPass().GetImageView();
												VkSampler   brdfSamp      = m_pipeline->GetBrdfLutPass().GetSampler();
												lp.useIBL = (irrView != VK_NULL_HANDLE && prefilterView != VK_NULL_HANDLE && brdfView != VK_NULL_HANDLE) ? 1.0f : 0.0f;
												const uint32_t frameIdx = m_currentFrame % 2;
												m_pipeline->GetLightingPass().Record(
													m_vkDeviceContext.GetDevice(), cmd, reg,
													m_vkSwapchain.GetExtent(),
													m_fgGBufferAId, m_fgGBufferBId, m_fgGBufferCId, m_fgDepthId,
													m_fgSceneColorHDRId, m_fgSsaoBlurId,
													irrView, irrSamp, prefilterView, prefilterSamp, brdfView, brdfSamp,
													lp, frameIdx);
											});
										std::fprintf(stderr, "[ENGINE] AY: addPass Lighting OK\n"); std::fflush(stderr);

										std::fprintf(stderr, "[ENGINE] AZ: avant addPass Bloom\n"); std::fflush(stderr);
										m_frameGraph.addPass("Bloom_Prefilter",
											[this](engine::render::PassBuilder& b) {
												b.read(m_fgSceneColorHDRId, engine::render::ImageUsage::SampledRead);
												b.write(m_fgBloomMipIds[0], engine::render::ImageUsage::ColorWrite);
											},
											[this](VkCommandBuffer cmd, engine::render::Registry& reg) {
												if (!m_pipeline->GetBloomPrefilterPass().IsValid()) return;
												engine::render::BloomPrefilterPass::PrefilterParams pp{};
												pp.threshold = static_cast<float>(m_cfg.GetDouble("bloom.threshold", 1.0));
												pp.knee      = static_cast<float>(m_cfg.GetDouble("bloom.knee", 0.5));
												const uint32_t frameIdx = m_currentFrame % 2;
												m_pipeline->GetBloomPrefilterPass().Record(
													m_vkDeviceContext.GetDevice(), cmd, reg,
													m_vkSwapchain.GetExtent(),
													m_fgSceneColorHDRId, m_fgBloomMipIds[0], pp, frameIdx);
											});

										for (uint32_t i = 0; i < engine::render::kBloomMipCount - 1; ++i)
										{
											const engine::render::ResourceId idSrc = m_fgBloomMipIds[i];
											const engine::render::ResourceId idDst = m_fgBloomMipIds[i + 1];
											char passName[32];
											std::snprintf(passName, sizeof(passName), "Bloom_Downsample_%u", i);
											m_frameGraph.addPass(passName,
												[this, idSrc, idDst](engine::render::PassBuilder& b) {
													b.read(idSrc, engine::render::ImageUsage::SampledRead);
													b.write(idDst, engine::render::ImageUsage::ColorWrite);
												},
												[this, i, idSrc, idDst](VkCommandBuffer cmd, engine::render::Registry& reg) {
													if (!m_pipeline->GetBloomDownsamplePass().IsValid()) return;
													VkExtent2D ext = m_vkSwapchain.GetExtent();
													VkExtent2D extentDst{ std::max(1u, ext.width >> (i+1)), std::max(1u, ext.height >> (i+1)) };
													const uint32_t frameIdx = m_currentFrame % 2;
													m_pipeline->GetBloomDownsamplePass().Record(m_vkDeviceContext.GetDevice(), cmd, reg, extentDst, idSrc, idDst, frameIdx);
												});
										}

										for (uint32_t ii = engine::render::kBloomMipCount - 1; ii-- > 0; )
										{
											const uint32_t i = ii;
											const engine::render::ResourceId idSrc = m_fgBloomMipIds[i + 1];
											const engine::render::ResourceId idDst = m_fgBloomMipIds[i];
											char passName[32];
											std::snprintf(passName, sizeof(passName), "Bloom_Upsample_%u", i);
											m_frameGraph.addPass(passName,
												[this, idSrc, idDst](engine::render::PassBuilder& b) {
													b.read(idSrc, engine::render::ImageUsage::SampledRead);
													b.write(idDst, engine::render::ImageUsage::ColorWrite);
												},
												[this, i, idSrc, idDst](VkCommandBuffer cmd, engine::render::Registry& reg) {
													if (!m_pipeline->GetBloomUpsamplePass().IsValid()) return;
													VkExtent2D ext = m_vkSwapchain.GetExtent();
													VkExtent2D extentDst{ std::max(1u, ext.width >> i), std::max(1u, ext.height >> i) };
													const uint32_t frameIdx = m_currentFrame % 2;
													m_pipeline->GetBloomUpsamplePass().Record(m_vkDeviceContext.GetDevice(), cmd, reg, extentDst, idSrc, idDst, frameIdx);
												});
										}

										m_frameGraph.addPass("Bloom_Combine",
											[this](engine::render::PassBuilder& b) {
												b.read(m_fgSceneColorHDRId,           engine::render::ImageUsage::SampledRead);
												b.read(m_fgBloomMipIds[0],            engine::render::ImageUsage::SampledRead);
												b.write(m_fgSceneColorHDRWithBloomId, engine::render::ImageUsage::ColorWrite);
											},
											[this](VkCommandBuffer cmd, engine::render::Registry& reg) {
												if (!m_pipeline->GetBloomCombinePass().IsValid()) return;
												engine::render::BloomCombinePass::CombineParams cp{};
												cp.intensity = static_cast<float>(m_cfg.GetDouble("bloom.intensity", 1.0));
												const uint32_t frameIdx = m_currentFrame % 2;
												m_pipeline->GetBloomCombinePass().Record(m_vkDeviceContext.GetDevice(), cmd, reg, m_vkSwapchain.GetExtent(), m_fgSceneColorHDRId, m_fgBloomMipIds[0], m_fgSceneColorHDRWithBloomId, cp, frameIdx);
											});
										std::fprintf(stderr, "[ENGINE] BA: addPass Bloom OK\n"); std::fflush(stderr);

										std::fprintf(stderr, "[ENGINE] BB: avant addPass AutoExposure+Tonemap\n"); std::fflush(stderr);
										m_frameGraph.addPass("AutoExposure_Luminance",
											[this](engine::render::PassBuilder& b) {
												b.read(m_fgSceneColorHDRWithBloomId, engine::render::ImageUsage::SampledRead);
											},
											[this](VkCommandBuffer cmd, engine::render::Registry& reg) {
												if (!m_pipeline->GetAutoExposure().IsValid()) return;
												m_pipeline->GetAutoExposure().Record(m_vkDeviceContext.GetDevice(), cmd, reg, m_fgSceneColorHDRWithBloomId, m_vkSwapchain.GetExtent());
											});

										m_frameGraph.addPass("Tonemap",
											[this](engine::render::PassBuilder& b) {
												b.read(m_fgSceneColorHDRWithBloomId, engine::render::ImageUsage::SampledRead);
												b.write(m_fgSceneColorLDRId,         engine::render::ImageUsage::ColorWrite);
											},
											[this](VkCommandBuffer cmd, engine::render::Registry& reg) {
												if (!m_pipeline->GetTonemapPass().IsValid()) return;
												engine::render::TonemapPass::TonemapParams tp{};
												tp.exposure = m_pipeline->GetAutoExposure().IsValid()
													? m_pipeline->GetAutoExposure().GetExposure()
													: static_cast<float>(m_cfg.GetDouble("tonemap.exposure", 1.0));
												bool lutEnabled = m_cfg.GetBool("color_grading.enable", false) && m_colorGradingLutHandle.IsValid();
												tp.strength = lutEnabled ? static_cast<float>(m_cfg.GetDouble("color_grading.strength", 1.0)) : 0.0f;
												VkImageView lutView = VK_NULL_HANDLE;
												if (lutEnabled) { engine::render::TextureAsset* lutTex = m_colorGradingLutHandle.Get(); if (lutTex && lutTex->view != VK_NULL_HANDLE) lutView = lutTex->view; }
												const uint32_t frameIdx = m_currentFrame % 2;
												m_pipeline->GetTonemapPass().Record(m_vkDeviceContext.GetDevice(), cmd, reg, m_vkSwapchain.GetExtent(), m_fgSceneColorHDRWithBloomId, m_fgSceneColorLDRId, tp, lutView, frameIdx);
											});
										std::fprintf(stderr, "[ENGINE] BC: addPass Tonemap OK\n"); std::fflush(stderr);

										std::fprintf(stderr, "[ENGINE] BD: avant addPass TAA\n"); std::fflush(stderr);
										m_frameGraph.addPass("TAA_InitHistory",
											[this](engine::render::PassBuilder& b) {
												b.read(m_fgSceneColorLDRId, engine::render::ImageUsage::TransferSrc);
												b.write(m_fgHistoryAId,     engine::render::ImageUsage::TransferDst);
												b.write(m_fgHistoryBId,     engine::render::ImageUsage::TransferDst);
											},
											[this](VkCommandBuffer cmd, engine::render::Registry& reg) {
												if (!m_taaHistoryInvalid) return;
												VkImage srcImg = reg.getImage(m_fgSceneColorLDRId);
												if (srcImg == VK_NULL_HANDLE) return;
												VkExtent2D ext = m_vkSwapchain.GetExtent();
												VkImageCopy region{};
												region.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
												region.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
												region.extent = { ext.width, ext.height, 1 };
												if (!m_taaHistoryEverFilled)
												{
													VkImage dstA = reg.getImage(m_fgHistoryAId);
													VkImage dstB = reg.getImage(m_fgHistoryBId);
													if (dstA != VK_NULL_HANDLE && dstB != VK_NULL_HANDLE)
													{
														vkCmdCopyImage(cmd, srcImg, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dstA, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
														vkCmdCopyImage(cmd, srcImg, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dstB, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
														m_taaHistoryEverFilled = true;
													}
												}
												else
												{
													engine::render::ResourceId nextId = GetTaaHistoryNextId();
													VkImage dstNext = reg.getImage(nextId);
													if (dstNext != VK_NULL_HANDLE)
														vkCmdCopyImage(cmd, srcImg, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dstNext, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
												}
											});

										m_frameGraph.addPass("TAA",
											[this](engine::render::PassBuilder& b) {
												b.read(m_fgSceneColorLDRId,   engine::render::ImageUsage::SampledRead);
												b.read(m_fgHistoryAId,        engine::render::ImageUsage::SampledRead);
												b.read(m_fgHistoryBId,        engine::render::ImageUsage::SampledRead);
												b.read(m_fgGBufferVelocityId, engine::render::ImageUsage::SampledRead);
												b.read(m_fgDepthId,           engine::render::ImageUsage::SampledRead);
												b.write(m_fgHistoryAId,       engine::render::ImageUsage::ColorWrite);
												b.write(m_fgHistoryBId,       engine::render::ImageUsage::ColorWrite);
											},
											[this](VkCommandBuffer cmd, engine::render::Registry& reg) {
												if (!m_pipeline->GetTaaPass().IsValid()) return;
												engine::render::TaaPass::TaaParams tp{};
												tp.alpha = 0.9f; tp._pad[0] = tp._pad[1] = tp._pad[2] = 0.0f;
												const uint32_t frameIdx = m_currentFrame % 2u;
												m_pipeline->GetTaaPass().Record(m_vkDeviceContext.GetDevice(), cmd, reg, m_vkSwapchain.GetExtent(), m_fgSceneColorLDRId, GetTaaHistoryPrevId(), m_fgGBufferVelocityId, m_fgDepthId, GetTaaHistoryNextId(), tp, frameIdx);
											});
										std::fprintf(stderr, "[ENGINE] BE: addPass TAA OK\n"); std::fflush(stderr);

										std::fprintf(stderr, "[ENGINE] BF: avant addPass CopyPresent\n"); std::fflush(stderr);
										m_frameGraph.addPass("CopyPresent",
											[this](engine::render::PassBuilder& b) {
												b.read(m_fgHistoryAId,      engine::render::ImageUsage::TransferSrc);
												b.read(m_fgHistoryBId,      engine::render::ImageUsage::TransferSrc);
												b.read(m_fgSceneColorLDRId, engine::render::ImageUsage::TransferSrc);
												b.write(m_fgBackbufferId,   engine::render::ImageUsage::TransferDst);
											},
											[this](VkCommandBuffer cmd, engine::render::Registry& reg) {
												engine::render::ResourceId srcId = m_pipeline->GetTaaPass().IsValid() ? GetTaaHistoryNextId() : m_fgSceneColorLDRId;
												VkImage srcImg = reg.getImage(srcId);
												VkImage dstImg = reg.getImage(m_fgBackbufferId);
												if (srcImg == VK_NULL_HANDLE || dstImg == VK_NULL_HANDLE) return;
												VkExtent2D ext = m_vkSwapchain.GetExtent();
												VkImageBlit region{};
												region.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
												region.srcOffsets[0] = { 0, 0, 0 };
												region.srcOffsets[1] = { static_cast<int32_t>(ext.width), static_cast<int32_t>(ext.height), 1 };
												region.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
												region.dstOffsets[0] = { 0, 0, 0 };
												region.dstOffsets[1] = { static_cast<int32_t>(ext.width), static_cast<int32_t>(ext.height), 1 };
												vkCmdBlitImage(cmd, srcImg, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dstImg, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region, VK_FILTER_LINEAR);
												VkImageMemoryBarrier barrier{};
												barrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
												barrier.srcAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT;
												barrier.dstAccessMask       = 0;
												barrier.oldLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
												barrier.newLayout           = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
												barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
												barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
												barrier.image               = dstImg;
												barrier.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
												vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
											});
										std::fprintf(stderr, "[ENGINE] BG: addPass CopyPresent OK\n"); std::fflush(stderr);

										m_frameGraph.addPass("PostRead",
											[this](engine::render::PassBuilder& b) {
												b.read(m_fgSceneColorId, engine::render::ImageUsage::SampledRead);
											},
											[](VkCommandBuffer, engine::render::Registry&) {});

										engine::render::MeshHandle    h2 = m_assetRegistry.LoadMesh("meshes/test.mesh");
										engine::render::TextureHandle t1 = m_assetRegistry.LoadTexture("textures/test.texr", false);
										engine::render::TextureHandle t2 = m_assetRegistry.LoadTexture("textures/test.texr", false);
										if (m_geometryMeshHandle.IsValid() && h2.IsValid() && m_geometryMeshHandle.Id() == h2.Id()) { /* cache OK */ }
										if (t1.IsValid() && t2.IsValid() && t1.Id() == t2.Id()) { /* cache OK */ }
										std::fprintf(stderr, "[ENGINE] BH: all passes registered OK\n"); std::fflush(stderr);
									}
								}
							}
						}
					}
				}
			}
			else
			{
				if (m_glfwWindowForVk)
					LOG_WARN(Platform, "[Boot] VkInstance::Create failed");
				else
					LOG_WARN(Platform, "[Boot] Vulkan instance or GLFW window for surface failed");
			}
		}

		// FS smoke test
		std::fprintf(stderr, "[ENGINE] BI: avant FS smoke\n"); std::fflush(stderr);
		{
			const auto cfgText = engine::platform::FileSystem::ReadAllText("config.json");
			LOG_INFO(Platform, "FS ReadAllText('config.json'): {} bytes", cfgText.size());
			const auto contentCfgText = engine::platform::FileSystem::ReadAllTextContent(m_cfg, "config.json");
			LOG_INFO(Platform, "FS ReadAllTextContent(paths.content/'config.json'): {} bytes", contentCfgText.size());
		}
		std::fprintf(stderr, "[ENGINE] BJ: FS smoke OK\n"); std::fflush(stderr);

		LOG_INFO(Core, "Engine init: vsync={} (present mode from swapchain)", m_vsync ? "on" : "off");
		LOG_INFO(Core, "[Boot] Engine boot COMPLETE");
		std::fprintf(stderr, "[ENGINE] BK: constructeur COMPLETE\n"); std::fflush(stderr);
	}

	Engine::~Engine() = default;

	int Engine::Run()
	{
		auto lastFpsLog  = std::chrono::steady_clock::now();
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

			if (!m_vsync)
			{
				constexpr auto safetyTarget = std::chrono::microseconds(5000);
				const auto elapsed = now - lastPresent;
				if (elapsed < safetyTarget)
					std::this_thread::sleep_for(safetyTarget - elapsed);
			}
			lastPresent = std::chrono::steady_clock::now();
		}

		if (m_vkDeviceContext.IsValid())
		{
			vkDeviceWaitIdle(m_vkDeviceContext.GetDevice());
			if (m_pipeline)
			{
				m_pipeline->Destroy(m_vkDeviceContext.GetDevice());
				m_pipeline.reset();
			}
			m_assetRegistry.Destroy();
			m_frameGraph.destroy(m_vkDeviceContext.GetDevice(), m_vmaAllocator);
			m_stagingAllocator.Destroy(m_vkDeviceContext.GetDevice());
			engine::render::DestroyFrameResources(m_vkDeviceContext.GetDevice(), m_frameResources);
			if (m_vmaAllocator)
			{
				vmaDestroyAllocator(reinterpret_cast<VmaAllocator>(m_vmaAllocator));
				m_vmaAllocator = nullptr;
			}
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
			OnQuit();

		m_shaderHotReload.Poll(m_cfg);
		m_shaderHotReload.ApplyPending(m_shaderCache);

		if (m_swapchainResizeRequested)
		{
			m_swapchainResizeRequested = false;
			if (m_vkDeviceContext.IsValid() && m_vkSwapchain.IsValid() && m_width > 0 && m_height > 0)
			{
				vkDeviceWaitIdle(m_vkDeviceContext.GetDevice());
				m_frameGraph.destroy(m_vkDeviceContext.GetDevice(), m_vmaAllocator);
				if (m_pipeline)
					m_pipeline->InvalidateFramebufferCaches(m_vkDeviceContext.GetDevice());
				if (m_vkSwapchain.Recreate(static_cast<uint32_t>(m_width), static_cast<uint32_t>(m_height)))
					LOG_INFO(Platform, "Swapchain recreated {}x{}", m_width, m_height);
			}
		}

		m_time.BeginFrame();
		m_frameArena.BeginFrame(m_time.FrameIndex());
		m_chunkStats.ResetPerFrame();
	}

	void Engine::Update()
	{
		const uint32_t readIdx  = m_renderReadIndex.load(std::memory_order_acquire);
		const uint32_t writeIdx = 1u - (readIdx & 1u);
		const auto& readState   = m_renderStates[readIdx];
		auto& out               = m_renderStates[writeIdx];

		const double dt               = (m_fixedDt > 0.0) ? m_fixedDt : m_time.DeltaSeconds();
		const float  mouseSensitivity = static_cast<float>(m_cfg.GetDouble("camera.mouse_sensitivity", 0.002));

		out.camera = readState.camera;
		m_fpsCameraController.Update(m_input, dt, mouseSensitivity, out.camera);

		m_world.Update(out.camera.position);

		if (m_width > 0 && m_height > 0)
			out.camera.aspect = static_cast<float>(m_width) / static_cast<float>(m_height);

		out.viewMatrix = out.camera.ComputeViewMatrix();
		{
			const engine::math::Vec3 forward(-out.viewMatrix.m[8], -out.viewMatrix.m[9], -out.viewMatrix.m[10]);
			m_streamingScheduler.PushRequests(m_world.GetPendingChunkRequests(), out.camera.position, forward);
			m_streamingScheduler.DropStaleFromAllQueues();
		}

		if (m_width > 0 && m_height > 0 && std::abs(out.camera.fovYDeg - readState.camera.fovYDeg) > 0.0001f)
			m_taaHistoryInvalid = true;

		out.projMatrix = out.camera.ComputeProjectionMatrix();

		const uint32_t taaSampleIndex = m_currentFrame % engine::render::kTaaHaltonN;
		float jitterX = 0.0f, jitterY = 0.0f;
		if (!m_taaHistoryInvalid && m_width > 0 && m_height > 0)
			engine::render::GetJitterNdc(taaSampleIndex, static_cast<uint32_t>(m_width), static_cast<uint32_t>(m_height), jitterX, jitterY);
		out.projMatrix.m[8] += jitterX;
		out.projMatrix.m[9] += jitterY;

		out.viewProjMatrix = out.projMatrix * out.viewMatrix;
		out.jitterCurrNdc[0] = jitterX;
		out.jitterCurrNdc[1] = jitterY;
		out.prevViewProjMatrix = m_taaHistoryInvalid ? out.viewProjMatrix : readState.viewProjMatrix;
		if (m_taaHistoryInvalid) m_taaHistoryInvalid = false;

		out.frustum.ExtractFromMatrix(out.viewProjMatrix);

		{
			const float maxDrawDist = static_cast<float>(m_cfg.GetDouble("world.max_draw_distance_m", 0.0));
			std::span<const engine::world::ChunkRequest> pending = m_streamingScheduler.GetPrioritizedRequests();
			out.hlodDebugText = engine::world::BuildChunkDrawList(pending.data(), pending.size(), out.camera.position, out.frustum, m_hlodRuntime, maxDrawDist, m_chunkDrawDecisions);
			if ((m_currentFrame % 60) == 0 && !out.hlodDebugText.empty())
				LOG_DEBUG(World, "M09.5 {}", out.hlodDebugText);
		}

		{
			const engine::math::Vec3 lightDirTowardLight(0.5774f, 0.5774f, 0.5774f);
			const float lambda = 0.7f;
			const uint32_t shadowMapResolution = static_cast<uint32_t>(m_cfg.GetInt("shadows.resolution", 1024));
			engine::render::ComputeCascades(out.camera, lightDirTowardLight, lambda, shadowMapResolution, out.cascades);
		}

		for (int i = 0; i < 256; ++i)
			(void)m_frameArena.alloc(64, alignof(std::max_align_t), engine::core::memory::MemTag::Temp);
		out.drawItemCount = 256;
	}

	void Engine::Render()
	{
		if (!m_vkDeviceContext.IsValid() || !m_vkSwapchain.IsValid() || m_frameResources[0].cmdPool == VK_NULL_HANDLE)
			return;

		const uint32_t frameIndex          = m_currentFrame % 2;
		engine::render::FrameResources& fr = m_frameResources[frameIndex];
		::VkDevice     device              = m_vkDeviceContext.GetDevice();
		VkQueue        graphicsQueue       = m_vkDeviceContext.GetGraphicsQueue();
		VkQueue        presentQueue        = m_vkDeviceContext.GetPresentQueue();
		VkSwapchainKHR swapchain           = m_vkSwapchain.GetSwapchain();
		VkExtent2D     extent              = m_vkSwapchain.GetExtent();

		vkWaitForFences(device, 1, &fr.fence, VK_TRUE, UINT64_MAX);
		m_deferredDestroyQueue.Collect(device, m_currentFrame > 0 ? m_currentFrame - 1 : 0);
		m_stagingAllocator.BeginFrame(frameIndex);
		(void)m_gpuUploadQueue.PlanFrameUploads();

		if (m_pipeline->GetAutoExposure().IsValid())
		{
			const float dt    = static_cast<float>(m_time.DeltaSeconds());
			const float key   = static_cast<float>(m_cfg.GetDouble("exposure.key", 0.18));
			const float speed = static_cast<float>(m_cfg.GetDouble("exposure.speed", 2.0));
			m_pipeline->GetAutoExposure().Update(device, dt, key, speed);
		}

		uint32_t imageIndex = 0;
		VkResult result = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, fr.imageAvailable, VK_NULL_HANDLE, &imageIndex);
		if (result == VK_ERROR_OUT_OF_DATE_KHR) { m_swapchainResizeRequested = true; return; }
		if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) return;

		vkResetCommandPool(device, fr.cmdPool, 0);

		VkCommandBufferBeginInfo beginInfo{};
		beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		if (vkBeginCommandBuffer(fr.cmdBuffer, &beginInfo) != VK_SUCCESS) return;

		if (m_fgSceneColorHDRId != engine::render::kInvalidResourceId && m_fgBackbufferId != engine::render::kInvalidResourceId)
		{
			VkImage     backbufferImage = m_vkSwapchain.GetImage(imageIndex);
			VkImageView backbufferView  = m_vkSwapchain.GetImageView(imageIndex);
			m_fgRegistry.bindImage(m_fgBackbufferId, backbufferImage, backbufferView);
			m_frameGraph.execute(m_vkDeviceContext.GetDevice(), m_vkDeviceContext.GetPhysicalDevice(), m_vmaAllocator, fr.cmdBuffer, m_fgRegistry, frameIndex, extent, 2u, m_vkDeviceContext.SupportsSynchronization2());
		}

		if (vkEndCommandBuffer(fr.cmdBuffer) != VK_SUCCESS) return;

		VkSemaphore          waitSemaphores[]   = { fr.imageAvailable };
		VkPipelineStageFlags waitStages[]       = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
		VkSemaphore          signalSemaphores[] = { fr.renderFinished };

		VkSubmitInfo submitInfo{};
		submitInfo.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submitInfo.waitSemaphoreCount   = 1;
		submitInfo.pWaitSemaphores      = waitSemaphores;
		submitInfo.pWaitDstStageMask    = waitStages;
		submitInfo.commandBufferCount   = 1;
		submitInfo.pCommandBuffers      = &fr.cmdBuffer;
		submitInfo.signalSemaphoreCount = 1;
		submitInfo.pSignalSemaphores    = signalSemaphores;

		vkResetFences(device, 1, &fr.fence);
		if (vkQueueSubmit(graphicsQueue, 1, &submitInfo, fr.fence) != VK_SUCCESS) return;

		VkPresentInfoKHR presentInfo{};
		presentInfo.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
		presentInfo.waitSemaphoreCount = 1;
		presentInfo.pWaitSemaphores    = signalSemaphores;
		presentInfo.swapchainCount     = 1;
		presentInfo.pSwapchains        = &swapchain;
		presentInfo.pImageIndices      = &imageIndex;

		result = vkQueuePresentKHR(presentQueue, &presentInfo);
		if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
			m_swapchainResizeRequested = true;

		m_currentFrame++;
	}

	void Engine::EndFrame()
	{
		if (m_currentFrame > 0 && (m_currentFrame % 60) == 0)
			m_chunkStats.LogStats();
	}

	void Engine::SwapRenderState()
	{
		const uint32_t readIdx  = m_renderReadIndex.load(std::memory_order_acquire);
		const uint32_t writeIdx = 1u - (readIdx & 1u);
		m_renderReadIndex.store(writeIdx, std::memory_order_release);
	}

	engine::render::ResourceId Engine::GetTaaHistoryPrevId() const
	{
		const uint32_t nextIdx = m_currentFrame % 2u;
		const uint32_t prevIdx = nextIdx ^ 1u;
		return prevIdx == 0u ? m_fgHistoryAId : m_fgHistoryBId;
	}

	engine::render::ResourceId Engine::GetTaaHistoryNextId() const
	{
		const uint32_t nextIdx = m_currentFrame % 2u;
		return nextIdx == 0u ? m_fgHistoryAId : m_fgHistoryBId;
	}

	void Engine::OnResize(int w, int h)
	{
		m_width  = w;
		m_height = h;
		m_taaHistoryInvalid        = true;
		m_swapchainResizeRequested = true;
	}

	void Engine::OnQuit()
	{
		m_quitRequested = true;
	}

	void Engine::WatchShader(std::string_view relativePath, engine::render::ShaderStage stage, std::string_view defines)
	{
		m_shaderHotReload.Watch(relativePath, stage, defines);
	}

} // namespace engine
