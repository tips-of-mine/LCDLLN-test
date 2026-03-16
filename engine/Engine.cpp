#include "engine/Engine.h"

#include "engine/core/Log.h"
#include "engine/editor/EditorMode.h"
#include "engine/core/memory/Memory.h"
#include "engine/platform/FileSystem.h"
#include "engine/render/DeferredPipeline.h"
#include "engine/render/ShaderCompiler.h"

#include <GLFW/glfw3.h>
#include <vulkan/vulkan.h>

#include <vk_mem_alloc.h>

#include <chrono>
#include <cmath>
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
	namespace
	{
		bool HasCliFlag(int argc, char** argv, std::string_view flag)
		{
			for (int i = 1; i < argc; ++i)
			{
				if (argv[i] && std::string_view(argv[i]) == flag)
				{
					return true;
				}
			}
			return false;
		}

		/// Return the first probe intensity parameter, or 1.0 when no probe exists.
		float GetGlobalProbeIntensity(const engine::world::ProbeSet& probeSet)
		{
			if (probeSet.probes.empty())
			{
				return 1.0f;
			}

			return probeSet.probes.front().params[0];
		}

		float GetTestMeshDistanceMeters(const engine::RenderState& rs,
			const std::vector<engine::world::ChunkDrawDecision>& chunkDrawDecisions)
		{
			if (!chunkDrawDecisions.empty())
				return chunkDrawDecisions[0].distanceMeters;

			const float dx = rs.camera.position.x;
			const float dy = rs.camera.position.y;
			const float dz = rs.camera.position.z;
			return std::sqrt(dx * dx + dy * dy + dz * dz);
		}

		engine::render::GpuDrawItem BuildGpuDrawItem(const engine::render::MeshAsset& mesh,
			uint32_t meshId,
			const float* modelMatrix,
			uint32_t lodLevel)
		{
			engine::render::GpuDrawItem item{};
			item.meshId = meshId;
			item.materialId = 0;
			item.lodLevel = lodLevel;
			std::memcpy(item.modelMatrix, modelMatrix, sizeof(item.modelMatrix));

			engine::math::Vec3 localMin(-0.5f, -0.5f, -0.5f);
			engine::math::Vec3 localMax(0.5f, 0.5f, 0.5f);
			if (mesh.hasLocalBounds)
			{
				localMin = mesh.localBoundsMin;
				localMax = mesh.localBoundsMax;
			}
			else
			{
				static bool s_loggedMissingBounds = false;
				if (!s_loggedMissingBounds)
				{
					if (engine::core::Log::IsActive()) LOG_WARN(Render, "[GpuDrivenCulling] Mesh local bounds missing, using unit bounds fallback");
					s_loggedMissingBounds = true;
				}
			}

			const engine::math::Vec3 localCenter = (localMin + localMax) * 0.5f;
			const engine::math::Vec3 localExtents = (localMax - localMin) * 0.5f;
			const float* m = modelMatrix;

			const float worldCenterX = m[0] * localCenter.x + m[4] * localCenter.y + m[8] * localCenter.z + m[12];
			const float worldCenterY = m[1] * localCenter.x + m[5] * localCenter.y + m[9] * localCenter.z + m[13];
			const float worldCenterZ = m[2] * localCenter.x + m[6] * localCenter.y + m[10] * localCenter.z + m[14];
			const float worldExtentX = std::fabs(m[0]) * localExtents.x + std::fabs(m[4]) * localExtents.y + std::fabs(m[8]) * localExtents.z;
			const float worldExtentY = std::fabs(m[1]) * localExtents.x + std::fabs(m[5]) * localExtents.y + std::fabs(m[9]) * localExtents.z;
			const float worldExtentZ = std::fabs(m[2]) * localExtents.x + std::fabs(m[6]) * localExtents.y + std::fabs(m[10]) * localExtents.z;

			item.boundsCenter[0] = worldCenterX;
			item.boundsCenter[1] = worldCenterY;
			item.boundsCenter[2] = worldCenterZ;
			item.boundsCenter[3] = 1.0f;
			item.boundsExtents[0] = worldExtentX;
			item.boundsExtents[1] = worldExtentY;
			item.boundsExtents[2] = worldExtentZ;
			item.boundsExtents[3] = 0.0f;
			item.indexCount = mesh.GetLodIndexCount(lodLevel);
			item.firstIndex = mesh.GetLodIndexOffset(lodLevel);
			item.vertexOffset = 0;
			item.firstInstance = 0;
			return item;
		}
	}

	void Engine::LoadZoneProbeAssets()
	{
		const std::string zoneMetaPath = m_cfg.GetString("world.zone_meta_path", "zone.meta");
		const std::string probesPath = m_cfg.GetString("world.probes_path", "probes.bin");
		const std::string atmospherePath = m_cfg.GetString("world.atmosphere_path", "atmosphere.json");
		std::string error;
		engine::world::OutputVersionHeader zoneHeader;

		if (!engine::world::LoadVersionedFileHeader(m_cfg, zoneMetaPath,
			engine::world::kZoneMetaMagic,
			engine::world::kZoneMetaVersion,
			zoneHeader,
			error))
		{
			if (engine::core::Log::IsActive()) LOG_WARN(Core, "[ZoneProbes] Runtime manifest fallback sky (path={}, reason={})", zoneMetaPath, error);
		}
		else if (engine::world::LoadProbeSet(m_cfg, probesPath, zoneHeader.contentHash, true, m_zoneProbes, error))
		{
			if (engine::core::Log::IsActive()) LOG_INFO(Core, "[ZoneProbes] Runtime probes ready (path={}, count={})", probesPath, m_zoneProbes.probes.size());
		}
		else
		{
			if (engine::core::Log::IsActive()) LOG_WARN(Core, "[ZoneProbes] Runtime probes fallback sky (path={}, reason={})", probesPath, error);
		}

		error.clear();
		if (engine::world::LoadAtmosphereSettings(m_cfg, atmospherePath, m_zoneAtmosphere, error))
		{
			if (engine::core::Log::IsActive()) LOG_INFO(Core, "[ZoneProbes] Runtime atmosphere ready (path={})", atmospherePath);
		}
		else
		{
			if (engine::core::Log::IsActive()) LOG_WARN(Core, "[ZoneProbes] Runtime atmosphere defaults active (path={}, reason={})", atmospherePath, error);
		}
	}

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
		std::fprintf(stderr, "[ENGINE] A1: before arg parse argc=%d\n", argc); std::fflush(stderr);
		for (int i = 1; i < argc; ++i)
		{
			if (!argv[i]) continue;
			const std::string_view arg(argv[i]);
			if (arg == "-log")     logToFile    = true;
			if (arg == "-console") logToConsole = true;
		}
		std::fprintf(stderr, "[ENGINE] A2: after arg parse logToFile=%d logToConsole=%d\n",	(int)logToFile, (int)logToConsole); std::fflush(stderr);

		engine::core::LogSettings logSettings;
		std::fprintf(stderr, "[ENGINE] A3: before GetString log.file\n"); std::fflush(stderr);
		logSettings.filePath    = logToFile
			? engine::core::Log::MakeTimestampedFilename("lcdlln.exe")
			: "";
			// : m_cfg.GetString("log.file", "engine.log");
		std::fprintf(stderr, "[ENGINE] A4: after GetString log.file='%s'\n", logSettings.filePath.c_str()); std::fflush(stderr);
		logSettings.console     = logToConsole;
		logSettings.flushAlways = true;
		logSettings.level       = engine::core::LogLevel::Info;
		std::fprintf(stderr, "[ENGINE] A5: before GetInt rotation/retention\n"); std::fflush(stderr);
		logSettings.rotation_size_mb = static_cast<size_t>(std::max(static_cast<int64_t>(0), m_cfg.GetInt("log.rotation_size_mb", 10)));
		logSettings.retention_days   = static_cast<int>(m_cfg.GetInt("log.retention_days", 7));
		std::fprintf(stderr, "[ENGINE] A6: after GetInt rotation=%zu retention=%d\n", logSettings.rotation_size_mb, logSettings.retention_days); std::fflush(stderr);

		std::fprintf(stderr, "[ENGINE] A7: before Log::Init\n"); std::fflush(stderr);
		engine::core::Log::Init(logSettings);
		std::fprintf(stderr, "[ENGINE] B: Log::Init OK\n"); std::fflush(stderr);

		if (!logSettings.filePath.empty() || logSettings.console)
		{
			if (engine::core::Log::IsActive()) LOG_INFO(Core, "[Boot] Log initialized (console={}, file={})", logToConsole ? "on" : "off", logSettings.filePath);
		}
		std::fprintf(stderr, "[ENGINE] B1: LOG_INFO OK\n"); std::fflush(stderr);

		// ------------------------------------------------------------------
		// Config + subsystems
		// ------------------------------------------------------------------
		m_vsync   = m_cfg.GetBool("render.vsync", true);
		m_fixedDt = m_cfg.GetDouble("time.fixed_dt", 0.0);
		m_editorEnabled = HasCliFlag(argc, argv, "--editor") || m_cfg.GetBool("editor.enabled", false);
		std::fprintf(stderr, "[ENGINE] C: config OK\n"); std::fflush(stderr);

		if (!logSettings.filePath.empty() || logSettings.console)
		{
			if (engine::core::Log::IsActive()) LOG_INFO(Core, "[Boot] Config loaded (vsync={}, fixed_dt={})", m_vsync ? "on" : "off", m_fixedDt);
		}
		std::fprintf(stderr, "[ENGINE] D: LOG_INFO config OK\n"); std::fflush(stderr);

		std::fprintf(stderr, "[ENGINE] E: avant editor mode check\n"); std::fflush(stderr);
		if (m_editorEnabled)
		{
			std::fprintf(stderr, "[ENGINE] E1: avant editor::EditorMod\n"); std::fflush(stderr);
			m_editorMode = std::make_unique<engine::editor::EditorMode>();
			std::fprintf(stderr, "[ENGINE] E2: après editor::EditorMod\n"); std::fflush(stderr);
			if (!m_editorMode->Init(m_cfg))
			{
				std::fprintf(stderr, "[ENGINE] E3: avant \n"); std::fflush(stderr);
				if (engine::core::Log::IsActive()) LOG_WARN(Core, "[Boot] EditorMode init failed; editor disabled");
				m_editorMode.reset();
				m_editorEnabled = false;
				std::fprintf(stderr, "[ENGINE] E4: après \n"); std::fflush(stderr);
			}
			else
			{
				const engine::render::Camera editorCamera = m_editorMode->BuildInitialCamera();
				m_renderStates[0].camera = editorCamera;
				m_renderStates[1].camera = editorCamera;
				if (engine::core::Log::IsActive()) LOG_INFO(Core, "[Boot] Editor mode enabled (--editor)");
			}
		}
		std::fprintf(stderr, "[ENGINE] F: apres editor mode block\n"); std::fflush(stderr);

		std::fprintf(stderr, "[ENGINE] G: chunkStats.Init\n"); std::fflush(stderr);
		m_chunkStats.Init(m_cfg);

		std::fprintf(stderr, "[ENGINE] H: lodConfig.Init\n"); std::fflush(stderr);
		m_lodConfig.Init(m_cfg);

		std::fprintf(stderr, "[ENGINE] I: hlodRuntime.Init\n"); std::fflush(stderr);
		m_hlodRuntime.Init(m_cfg);

		std::fprintf(stderr, "[ENGINE] J: LoadZoneProbeAssets\n"); std::fflush(stderr);
		LoadZoneProbeAssets();

		std::fprintf(stderr, "[ENGINE] K: streamCache.Init\n"); std::fflush(stderr);
		m_streamCache.Init(m_cfg);

		std::fprintf(stderr, "[ENGINE] L: streamingScheduler.SetStreamCache\n"); std::fflush(stderr);
		m_streamingScheduler.SetStreamCache(&m_streamCache);

		std::fprintf(stderr, "[ENGINE] M: gpuUploadQueue.Init\n"); std::fflush(stderr);
		m_gpuUploadQueue.Init(m_cfg);

		std::fprintf(stderr, "[ENGINE] N: subsystems OK\n"); std::fflush(stderr);
		if (engine::core::Log::IsActive()) LOG_INFO(Core, "[Boot] FrameArena init OK");

		// ------------------------------------------------------------------
		// Window
		// ------------------------------------------------------------------
		engine::platform::Window::CreateDesc desc{};
		desc.title  = "LCDLLN Engine";
		desc.width  = 1280;
		desc.height = 720;

		std::fprintf(stderr, "[ENGINE] O: avant Window::Create\n"); std::fflush(stderr);
		if (!m_window.Create(desc))
		{
			if (engine::core::Log::IsActive()) LOG_FATAL(Platform, "[Boot] Window::Create failed");
		}
		std::fprintf(stderr, "[ENGINE] P: Window::Create OK\n"); std::fflush(stderr);
		if (engine::core::Log::IsActive()) LOG_INFO(Core, "[Boot] Window::Create OK");

		m_window.SetOnResize([this](int w, int h) { OnResize(w, h); });
		m_window.SetOnClose([this]() { OnQuit(); });
		m_window.SetMessageHook([this](uint32_t msg, uint64_t wp, int64_t lp)
		{
			m_input.HandleMessage(msg, wp, lp);
		});
		m_window.GetClientSize(m_width, m_height);

		std::fprintf(stderr, "[ENGINE] Q: window setup OK w=%d h=%d\n", m_width, m_height); std::fflush(stderr);

		// -----------------------------------------------------------------
		// Vulkan init
		// -----------------------------------------------------------------
		std::fprintf(stderr, "[ENGINE] R: avant glfwInit\n"); std::fflush(stderr);
		if (glfwInit() != GLFW_TRUE)
		{
			if (engine::core::Log::IsActive()) LOG_WARN(Platform, "[Boot] glfwInit failed");
		}
		else
		{
			std::fprintf(stderr, "[ENGINE] R1: glfwInit OK\n"); std::fflush(stderr);
			if (engine::core::Log::IsActive()) LOG_INFO(Core, "[Boot] glfwInit OK");
			glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
			glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

			std::fprintf(stderr, "[ENGINE] R2: avant glfwCreateWindow\n"); std::fflush(stderr);
			m_glfwWindowForVk = glfwCreateWindow(1, 1, "VkSurface", nullptr, nullptr);
			std::fprintf(stderr, "[ENGINE] R3: glfwCreateWindow ptr=%p\n", (void*)m_glfwWindowForVk); std::fflush(stderr);
			if (!m_glfwWindowForVk)
			{
				if (engine::core::Log::IsActive()) LOG_WARN(Platform, "[Boot] glfwCreateWindow returned null");
			}
			else
			{
				LOG_INFO(Core, "[Boot] glfwCreateWindow OK");
			}

			std::fprintf(stderr, "[ENGINE] R4: avant VkInstance::Create\n"); std::fflush(stderr);
			if (m_glfwWindowForVk && m_vkInstance.Create())
			{
				std::fprintf(stderr, "[ENGINE] R5: VkInstance::Create OK\n"); std::fflush(stderr);
				if (engine::core::Log::IsActive()) LOG_INFO(Core, "[Boot] VkInstance::Create OK");

				std::fprintf(stderr, "[ENGINE] R6: avant CreateSurface\n"); std::fflush(stderr);
				if (!m_vkInstance.CreateSurface(m_glfwWindowForVk))
				{
					if (engine::core::Log::IsActive()) LOG_WARN(Platform, "[Boot] VkInstance::CreateSurface failed");
				}
				else
				{
					std::fprintf(stderr, "[ENGINE] R7: CreateSurface OK\n"); std::fflush(stderr);
					if (engine::core::Log::IsActive()) LOG_INFO(Core, "[Boot] VkInstance::CreateSurface OK");
					std::fprintf(stderr, "[ENGINE] R8: avant VkDeviceContext::Create\n"); std::fflush(stderr);
					if (!m_vkDeviceContext.Create(m_vkInstance.GetHandle(), m_vkInstance.GetSurface()))
					{
						if (engine::core::Log::IsActive()) LOG_WARN(Platform, "[Boot] VkDeviceContext::Create failed");
					}
					else
					{
						std::fprintf(stderr, "[ENGINE] R9: VkDeviceContext::Create OK\n"); std::fflush(stderr);
						VkPhysicalDeviceProperties physProps{};
						vkGetPhysicalDeviceProperties(m_vkDeviceContext.GetPhysicalDevice(), &physProps);
						if (engine::core::Log::IsActive()) LOG_INFO(Core, "[Boot] VkDeviceContext::Create OK (GPU: {})", physProps.deviceName);

						VkPresentModeKHR requestedMode = VK_PRESENT_MODE_FIFO_KHR;
						if (!m_vsync)
							requestedMode = VK_PRESENT_MODE_IMMEDIATE_KHR;
						else
						{
							const std::string pm = m_cfg.GetString("render.present_mode", "fifo");
							if (pm == "mailbox")
								requestedMode = VK_PRESENT_MODE_MAILBOX_KHR;
						}

						std::fprintf(stderr, "[ENGINE] R10: avant VkSwapchain::Create\n"); std::fflush(stderr);
						if (!m_vkSwapchain.Create(
							m_vkDeviceContext.GetPhysicalDevice(),
							m_vkDeviceContext.GetDevice(),
							m_vkInstance.GetSurface(),
							m_vkDeviceContext.GetGraphicsQueueFamilyIndex(),
							m_vkDeviceContext.GetPresentQueueFamilyIndex(),
							static_cast<uint32_t>(m_width), static_cast<uint32_t>(m_height),
							requestedMode))
						{
							if (engine::core::Log::IsActive()) LOG_WARN(Platform, "[Boot] VkSwapchain::Create failed");
						}
						else
						{
							std::fprintf(stderr, "[ENGINE] R11: VkSwapchain::Create OK\n"); std::fflush(stderr);
							VkExtent2D swapExtent = m_vkSwapchain.GetExtent();
							if (engine::core::Log::IsActive()) LOG_INFO(Core, "[Boot] VkSwapchain::Create OK (extent={}x{}, images={})",
								swapExtent.width, swapExtent.height, m_vkSwapchain.GetImageCount());

							std::fprintf(stderr, "[ENGINE] R12: avant CreateFrameResources\n"); std::fflush(stderr);
							if (!engine::render::CreateFrameResources(
								m_vkDeviceContext.GetDevice(),
								m_vkDeviceContext.GetGraphicsQueueFamilyIndex(),
								m_frameResources))
							{
								if (engine::core::Log::IsActive()) LOG_WARN(Platform, "[Boot] FrameSync::Init failed");
							}
							else
							{
								std::fprintf(stderr, "[ENGINE] R13: CreateFrameResources OK\n"); std::fflush(stderr);
								if (engine::core::Log::IsActive()) LOG_INFO(Core, "[Boot] FrameSync::Init OK");

								if (m_vkSwapchain.IsValid())
								{
									std::fprintf(stderr, "[ENGINE] R14: avant vmaCreateAllocator\n"); std::fflush(stderr);
									VmaVulkanFunctions vmaFuncs{};
									vmaFuncs.vkGetInstanceProcAddr               = vkGetInstanceProcAddr;
									vmaFuncs.vkGetDeviceProcAddr                 = vkGetDeviceProcAddr;
									vmaFuncs.vkGetPhysicalDeviceProperties       = vkGetPhysicalDeviceProperties;
									vmaFuncs.vkGetPhysicalDeviceMemoryProperties = vkGetPhysicalDeviceMemoryProperties;
									vmaFuncs.vkAllocateMemory                    = vkAllocateMemory;
									vmaFuncs.vkFreeMemory                        = vkFreeMemory;
									vmaFuncs.vkMapMemory                         = vkMapMemory;
									vmaFuncs.vkUnmapMemory                       = vkUnmapMemory;
									vmaFuncs.vkFlushMappedMemoryRanges           = vkFlushMappedMemoryRanges;
									vmaFuncs.vkInvalidateMappedMemoryRanges      = vkInvalidateMappedMemoryRanges;
									vmaFuncs.vkBindBufferMemory                  = vkBindBufferMemory;
									vmaFuncs.vkBindImageMemory                   = vkBindImageMemory;
									vmaFuncs.vkGetBufferMemoryRequirements       = vkGetBufferMemoryRequirements;
									vmaFuncs.vkGetImageMemoryRequirements        = vkGetImageMemoryRequirements;
									vmaFuncs.vkCreateBuffer                      = vkCreateBuffer;
									vmaFuncs.vkDestroyBuffer                     = vkDestroyBuffer;
									vmaFuncs.vkCreateImage                       = vkCreateImage;
									vmaFuncs.vkDestroyImage                      = vkDestroyImage;
									vmaFuncs.vkCmdCopyBuffer                     = vkCmdCopyBuffer;

									//vmaFuncs.vkGetBufferMemoryRequirements2KHR       = vkGetBufferMemoryRequirements2;
									//vmaFuncs.vkGetImageMemoryRequirements2KHR        = vkGetImageMemoryRequirements2;
									//vmaFuncs.vkBindBufferMemory2KHR                  = vkBindBufferMemory2;
									//vmaFuncs.vkBindImageMemory2KHR                   = vkBindImageMemory2;
									//vmaFuncs.vkGetPhysicalDeviceMemoryProperties2KHR = vkGetPhysicalDeviceMemoryProperties2;
									
									VmaAllocatorCreateInfo vmaInfo{};
									vmaInfo.physicalDevice   = m_vkDeviceContext.GetPhysicalDevice();
									vmaInfo.device           = m_vkDeviceContext.GetDevice();
									vmaInfo.instance         = m_vkInstance.GetHandle();
									vmaInfo.vulkanApiVersion = VK_API_VERSION_1_0;
									vmaInfo.pVulkanFunctions = &vmaFuncs;
									if (vmaCreateAllocator(&vmaInfo, reinterpret_cast<VmaAllocator*>(&m_vmaAllocator)) != VK_SUCCESS)
									{
										if (engine::core::Log::IsActive()) LOG_ERROR(Render, "VMA allocator creation failed");
										m_vmaAllocator = nullptr;
									}
									std::fprintf(stderr, "[ENGINE] R15: vmaCreateAllocator OK ptr=%p\n", m_vmaAllocator); std::fflush(stderr);

									// Vérification VmaAllocatorInfo
									VmaAllocatorInfo vmaAllocInfo{};
									vmaGetAllocatorInfo(reinterpret_cast<VmaAllocator>(m_vmaAllocator), &vmaAllocInfo);
									std::fprintf(stderr, "[ENGINE] R16: vmaGetAllocatorInfo device=%p physDev=%p instance=%p\n",
									    (void*)vmaAllocInfo.device, (void*)vmaAllocInfo.physicalDevice, (void*)vmaAllocInfo.instance); std::fflush(stderr);
									
									if (m_vmaAllocator)
									{
										// Test raw Vulkan — vérifie que le device est fonctionnel
										VkBufferCreateInfo testBuf{};
										testBuf.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
										testBuf.size  = 64;
										testBuf.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
										testBuf.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
										VkBuffer tmpBuf = VK_NULL_HANDLE;
										VkResult testResult = vkCreateBuffer(m_vkDeviceContext.GetDevice(), &testBuf, nullptr, &tmpBuf);
										std::fprintf(stderr, "[ENGINE] vkCreateBuffer test result=%d buf=%p\n", (int)testResult, (void*)tmpBuf); std::fflush(stderr);
										if (tmpBuf != VK_NULL_HANDLE) vkDestroyBuffer(m_vkDeviceContext.GetDevice(), tmpBuf, nullptr);

										// M10.4: réactivation du StagingAllocator avec budget streaming.
										const size_t stagingBudget = m_gpuUploadQueue.GetBudgetBytes();
										std::fprintf(stderr, "[ENGINE] R17: StagingAllocator::Init device=%p vma=%p budget=%zu\n",
											(void*)m_vkDeviceContext.GetDevice(), m_vmaAllocator, stagingBudget); std::fflush(stderr);
										if (!m_stagingAllocator.Init(m_vkDeviceContext.GetDevice(), m_vmaAllocator, stagingBudget))
										{
											if (engine::core::Log::IsActive()) WARN(Render, "[StagingAllocator] Init FAILED (pool_size_bytes={})", stagingBudget);
											std::fprintf(stderr, "[ENGINE] R18: StagingAllocator::Init FAILED (staging désactivé)\n"); std::fflush(stderr);
										}
										else
										{
											if (engine::core::Log::IsActive()) LOG_INFO(Render, "[StagingAllocator] Initialized. Pool size: {} bytes", stagingBudget);
											std::fprintf(stderr, "[ENGINE] R19: StagingAllocator::Init OK\n"); std::fflush(stderr);
										}

										std::fprintf(stderr, "[ENGINE] R20: avant make_unique DeferredPipeline\n"); std::fflush(stderr);
										m_pipeline = std::make_unique<engine::render::DeferredPipeline>();

										std::fprintf(stderr, "[ENGINE] R21: avant assetRegistry.Init\n"); std::fflush(stderr);
										m_assetRegistry.Init(m_vkDeviceContext.GetDevice(), m_vkDeviceContext.GetPhysicalDevice(), m_vmaAllocator, m_cfg);

										std::fprintf(stderr, "[ENGINE] R22: assetRegistry OK\n"); std::fflush(stderr);
										if (!m_profiler.Init(m_vkDeviceContext.GetDevice(), m_vkDeviceContext.GetPhysicalDevice(), 2u))
										{
											if (engine::core::Log::IsActive()) LOG_WARN(Core, "[Engine] Profiler init failed - profiling disabled");
										}
										if (!m_profilerHud.Init())
										{
											if (engine::core::Log::IsActive()) LOG_WARN(Core, "[Engine] ProfilerHud init failed - overlay disabled");
										    m_profiler.Shutdown(m_vkDeviceContext.GetDevice());
											std::fprintf(stderr, "[ENGINE] PROFILER FORCE DISABLED (STAB.9)\n");
											std::fflush(stderr);
										}
										if (!m_audioEngine.Init(m_cfg))
										{
											if (engine::core::Log::IsActive()) LOG_WARN(Core, "[Engine] AudioEngine init failed - audio disabled");
										}
										else
										{
											m_audioEngine.SetZone(0);
										}
										m_decalSystem.Init(m_cfg, m_assetRegistry);
										{
											engine::render::DecalComponent decal{};
											decal.center = { 0.0f, 0.05f, 0.0f };
											decal.halfExtents = { 2.0f, 1.0f, 2.0f };
											decal.albedoTexturePath = "textures/test.texr";
											decal.lifetimeSeconds = 30.0f;
											decal.fadeDurationSeconds = 5.0f;
											m_decalSystem.Spawn(decal);
										}

										{
											std::string lutPath = m_cfg.GetString("color_grading.lut_path", "");
											if (!lutPath.empty())
												m_colorGradingLutHandle = m_assetRegistry.LoadTexture(lutPath, true);
										}

										std::fprintf(stderr, "[ENGINE] R23: avant createImage SceneColor\n"); std::fflush(stderr);
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

										engine::render::ImageDesc decalOverlayDesc{};
										decalOverlayDesc.format = VK_FORMAT_R8G8B8A8_SRGB;
										decalOverlayDesc.usage  = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
										m_fgDecalOverlayId = m_frameGraph.createImage("DecalOverlay", decalOverlayDesc);

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
											bloomMipDesc.extentScalePower = i;
											std::snprintf(name, sizeof(name), "BloomDown_%u", i);
											m_fgBloomDownMipIds[i] = m_frameGraph.createImage(name, bloomMipDesc);
											std::snprintf(name, sizeof(name), "BloomUp_%u", i);
											m_fgBloomUpMipIds[i] = m_frameGraph.createImage(name, bloomMipDesc);
										}
										if (engine::core::Log::IsActive()) LOG_INFO(Render, "[Bloom] FrameGraph resources registered: %zu down + %zu up mips",
											m_fgBloomDownMipIds.size(),
											m_fgBloomUpMipIds.size());

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
										std::fprintf(stderr, "[ENGINE] R24: frame graph images OK\n"); std::fflush(stderr);

										{
											engine::render::ShaderCompiler sc;
											if (sc.LocateCompiler())
												if (engine::core::Log::IsActive()) LOG_INFO(Core, "[Boot] ShaderCompiler OK");
											else
												if (engine::core::Log::IsActive()) LOG_WARN(Render, "[Boot] ShaderCompiler glslangValidator not found");
										}
										std::fprintf(stderr, "[ENGINE] R25: ShaderCompiler check OK\n"); std::fflush(stderr);

										auto loadSpirv = [&](const char* spvPath) -> std::vector<uint32_t>
										{
											// 1) Essaye de charger directement le .spv depuis game/data
											std::vector<uint8_t> bytes = engine::platform::FileSystem::ReadAllBytesContent(m_cfg, spvPath);
											if (bytes.size() % 4 == 0 && !bytes.empty())
											{
												std::vector<uint32_t> out(bytes.size() / 4);
												std::memcpy(out.data(), bytes.data(), bytes.size());
												return out;
											}

											// 2) Pas de .spv valide → pas de fallback GLSL (pour éviter les crashes glslangValidator)
											// engine::render::ShaderCompiler compiler;
											// if (!compiler.LocateCompiler()) return {};
											// std::string base(spvPath);
											// if (base.size() > 4 && base.compare(base.size() - 4, 4, ".spv") == 0)
											// 	base.resize(base.size() - 4);
											// std::filesystem::path srcPath = engine::platform::FileSystem::ResolveContentPath(m_cfg, base);
											// engine::render::ShaderStage stage = engine::render::ShaderStage::Vertex;
											// if (base.size() >= 5 && base.compare(base.size() - 5, 5, ".comp") == 0)
											// 	stage = engine::render::ShaderStage::Compute;
											// else if (base.size() >= 5 && base.compare(base.size() - 5, 5, ".vert") == 0)
											// 	stage = engine::render::ShaderStage::Vertex;
											// else if (base.size() >= 5 && base.compare(base.size() - 5, 5, ".frag") == 0)
											// 	stage = engine::render::ShaderStage::Fragment;
											// auto c = compiler.CompileGlslToSpirv(srcPath, stage);
											// if (c.has_value() && !c->empty()) return std::move(*c);

											if (engine::core::Log::IsActive()) LOG_WARN(Render, "Shader SPIR-V not found or invalid: {}", spvPath);
											return {};
										};

										std::fprintf(stderr, "[ENGINE] R26: avant pipeline->Init\n"); std::fflush(stderr);
										std::fprintf(stderr, "[ENGINE] R27: appel Init...\n"); std::fflush(stderr);
										bool pipelineOk = m_pipeline->Init(
										    m_vkDeviceContext.GetDevice(),
										    m_vkDeviceContext.GetPhysicalDevice(),
										    m_vmaAllocator,
										    m_cfg,
										    static_cast<uint32_t>(m_cfg.GetInt("shadows.resolution", 1024)),
										    m_vkSwapchain.GetImageFormat(),
										    m_vkDeviceContext.GetGraphicsQueue(),
										    m_vkDeviceContext.GetGraphicsQueueFamilyIndex(),
										    loadSpirv
										);
										std::fprintf(stderr, "[ENGINE] R28: Init retourne %d\n", (int)pipelineOk); std::fflush(stderr);
										std::fprintf(stderr, "[ENGINE] R29: pipeline->Init OK\n"); std::fflush(stderr);
										if (engine::core::Log::IsActive()) LOG_INFO(Core, "[Boot] DeferredPipeline init OK");

										std::fprintf(stderr, "[ENGINE] R30: avant addPass Clear\n"); std::fflush(stderr);
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

										std::fprintf(stderr, "[ENGINE] R31: avant LoadMesh\n"); std::fflush(stderr);
										m_geometryMeshHandle = m_assetRegistry.LoadMesh("meshes/test.mesh");

										std::fprintf(stderr, "[ENGINE] R32: LoadMesh OK\n"); std::fflush(stderr);

										std::fprintf(stderr, "[ENGINE] R33: avant addPass GPU_Cull\n"); std::fflush(stderr);
										m_frameGraph.addPass("GPU_Cull",
											[](engine::render::PassBuilder&) {},
											[this](VkCommandBuffer cmd, engine::render::Registry&) {
												auto& cullingPass = m_pipeline->GetGpuDrivenCullingPass();
												if (!cullingPass.IsValid())
													return;

												const uint32_t readIdx = m_renderReadIndex.load(std::memory_order_acquire);
												const engine::RenderState& rs = m_renderStates[readIdx];
												engine::render::MeshAsset* mesh = rs.objectVisible ? m_geometryMeshHandle.Get() : nullptr;
												uint32_t drawItemCount = 0;
												engine::render::GpuDrawItem drawItem{};

												if (mesh && mesh->vertexBuffer != VK_NULL_HANDLE && mesh->indexBuffer != VK_NULL_HANDLE)
												{
													const float distCam = GetTestMeshDistanceMeters(rs, m_chunkDrawDecisions);
													const uint32_t lodLevel = static_cast<uint32_t>(m_lodConfig.GetLodLevel(distCam));
													drawItem = BuildGpuDrawItem(*mesh, m_geometryMeshHandle.Id(), rs.objectModelMatrix, lodLevel);
													if (drawItem.indexCount > 0)
														drawItemCount = 1;
												}

												if (!cullingPass.UploadDrawItems(m_vkDeviceContext.GetDevice(), m_currentFrame,
														drawItemCount > 0 ? &drawItem : nullptr, drawItemCount))
												{
													if (engine::core::Log::IsActive()) LOG_WARN(Render, "[GpuDrivenCulling] Draw-item upload failed");
													return;
												}

												const auto& hiZPass = m_pipeline->GetHiZPyramidPass();
												cullingPass.Record(
													m_vkDeviceContext.GetDevice(), cmd, rs.viewProjMatrix.m, m_currentFrame,
													hiZPass.GetImageView(m_currentFrame),
													hiZPass.GetExtent(),
													hiZPass.GetMipCount());
											});
										std::fprintf(stderr, "[ENGINE] R34: addPass GPU_Cull OK\n"); std::fflush(stderr);

										std::fprintf(stderr, "[ENGINE] R35: avant addPass Geometry\n"); std::fflush(stderr);
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
												engine::render::MeshAsset* mesh = rs.objectVisible ? m_geometryMeshHandle.Get() : nullptr;
												const engine::world::GlobalChunkCoord chunk = engine::world::WorldToGlobalChunkCoord(rs.camera.position.x, rs.camera.position.z);
												const engine::world::ChunkRing ring = m_world.GetRingForChunk(chunk);
												const uint32_t triCount = (mesh && mesh->indexCount > 0) ? (mesh->indexCount / 3) : 0;
												m_chunkStats.RecordDraw(chunk, ring, 1, triCount);
												// Provisoire pour le mesh de test unique a l'origine monde; un ticket dedie
												// remplacera ce calcul par une distance par instance/objet.
												const float distCam = GetTestMeshDistanceMeters(rs, m_chunkDrawDecisions);
												const int lodLevel = m_lodConfig.GetLodLevel(distCam);
												static int s_lastLoggedLod = -1;
												if (lodLevel != s_lastLoggedLod)
												{
													if (engine::core::Log::IsActive()) LOG_DEBUG(Render, "[LOD] Geometry test mesh lod={} dist_m={:.2f}", lodLevel, distCam);
													s_lastLoggedLod = lodLevel;
												}
												auto& cullingPass = m_pipeline->GetGpuDrivenCullingPass();
												auto& materialCache = m_pipeline->GetMaterialDescriptorCache();
												if (cullingPass.IsValid())
												{
													m_pipeline->GetGeometryPass().RecordIndirect(
														m_vkDeviceContext.GetDevice(), cmd, reg,
														m_vkSwapchain.GetExtent(),
														m_fgGBufferAId, m_fgGBufferBId, m_fgGBufferCId, m_fgGBufferVelocityId, m_fgDepthId,
														rs.prevViewProjMatrix.m, rs.viewProjMatrix.m, mesh,
														cullingPass.GetIndirectBuffer(m_currentFrame),
														cullingPass.GetDrawItemCount(m_currentFrame),
														materialCache.GetDescriptorSet(),
														rs.objectModelMatrix,
														materialCache.GetDefaultMaterialIndex());
												}
												else
												{
													m_pipeline->GetGeometryPass().Record(
														m_vkDeviceContext.GetDevice(), cmd, reg,
														m_vkSwapchain.GetExtent(),
														m_fgGBufferAId, m_fgGBufferBId, m_fgGBufferCId, m_fgGBufferVelocityId, m_fgDepthId,
														rs.prevViewProjMatrix.m, rs.viewProjMatrix.m, mesh,
														static_cast<uint32_t>(lodLevel),
														materialCache.GetDescriptorSet(),
														rs.objectModelMatrix,
														materialCache.GetDefaultMaterialIndex());
												}
											});
										std::fprintf(stderr, "[ENGINE] R36: addPass Geometry OK\n"); std::fflush(stderr);

										m_frameGraph.addPass("HiZ_Build",
											[this](engine::render::PassBuilder& b) {
												b.read(m_fgDepthId, engine::render::ImageUsage::SampledRead);
											},
											[this](VkCommandBuffer cmd, engine::render::Registry& reg) {
												auto& hiZPass = m_pipeline->GetHiZPyramidPass();
												if (!hiZPass.IsValid())
													return;

												hiZPass.Record(
													m_vkDeviceContext.GetDevice(), cmd,
													reg.getImage(m_fgDepthId), reg.getImageView(m_fgDepthId),
													m_vkSwapchain.GetExtent(), m_currentFrame);
											});

										std::fprintf(stderr, "[ENGINE] R37: avant addPass ShadowMap\n"); std::fflush(stderr);
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
													engine::render::MeshAsset* mesh = rs.objectVisible ? m_geometryMeshHandle.Get() : nullptr;
													const engine::world::GlobalChunkCoord chunk = engine::world::WorldToGlobalChunkCoord(rs.camera.position.x, rs.camera.position.z);
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
										std::fprintf(stderr, "[ENGINE] R38: addPass ShadowMap OK\n"); std::fflush(stderr);

										std::fprintf(stderr, "[ENGINE] R39: avant addPass SSAO_Generate\n"); std::fflush(stderr);
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
										std::fprintf(stderr, "[ENGINE] R40: addPass SSAO_Generate OK\n"); std::fflush(stderr);

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
										std::fprintf(stderr, "[ENGINE] R41: addPass SSAO_Blur OK\n"); std::fflush(stderr);

										if (m_pipeline->GetDecalPass().IsValid())
										{
											m_frameGraph.addPass("Decals",
												[this](engine::render::PassBuilder& b) {
													b.read(m_fgDepthId,           engine::render::ImageUsage::SampledRead);
													b.write(m_fgDecalOverlayId,   engine::render::ImageUsage::ColorWrite);
												},
												[this](VkCommandBuffer cmd, engine::render::Registry& reg) {
													const uint32_t readIdx = m_renderReadIndex.load(std::memory_order_acquire);
													const engine::RenderState& rs = m_renderStates[readIdx];
													const float* vp = rs.viewProjMatrix.m;
													float invViewProj[16]{};
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
														invViewProj[0]  = ( a11*b11-a21*b10+a31*b09)*inv;
														invViewProj[1]  = (-a10*b11+a20*b10-a30*b09)*inv;
														invViewProj[2]  = ( a13*b05-a23*b04+a33*b03)*inv;
														invViewProj[3]  = (-a12*b05+a22*b04-a32*b03)*inv;
														invViewProj[4]  = (-a01*b11+a21*b08-a31*b07)*inv;
														invViewProj[5]  = ( a00*b11-a20*b08+a30*b07)*inv;
														invViewProj[6]  = (-a03*b05+a23*b02-a33*b01)*inv;
														invViewProj[7]  = ( a02*b05-a22*b02+a32*b01)*inv;
														invViewProj[8]  = ( a01*b10-a11*b08+a31*b06)*inv;
														invViewProj[9]  = (-a00*b10+a10*b08-a30*b06)*inv;
														invViewProj[10] = ( a03*b04-a13*b02+a33*b00)*inv;
														invViewProj[11] = (-a02*b04+a12*b02-a32*b00)*inv;
														invViewProj[12] = (-a01*b09+a11*b07-a21*b06)*inv;
														invViewProj[13] = ( a00*b09-a10*b07+a20*b06)*inv;
														invViewProj[14] = (-a03*b03+a13*b01-a23*b00)*inv;
														invViewProj[15] = ( a02*b03-a12*b01+a22*b00)*inv;
													}
													m_decalSystem.BuildVisibleList(rs.camera, m_visibleDecals);
													const uint32_t frameIdx = m_currentFrame % 2;
													m_pipeline->GetDecalPass().Record(
														m_vkDeviceContext.GetDevice(), cmd, reg, m_vkSwapchain.GetExtent(),
														m_fgDepthId, m_fgDecalOverlayId, invViewProj, m_visibleDecals, frameIdx);
												});
											if (engine::core::Log::IsActive()) LOG_INFO(Render, "[Engine] Decal frame-graph pass registered");
										}
										else
										{
											m_frameGraph.addPass("DecalOverlay_Clear",
												[this](engine::render::PassBuilder& b) {
													b.write(m_fgDecalOverlayId, engine::render::ImageUsage::TransferDst);
												},
												[this](VkCommandBuffer cmd, engine::render::Registry& reg) {
													VkImage img = reg.getImage(m_fgDecalOverlayId);
													if (img == VK_NULL_HANDLE) return;
													VkClearColorValue clearColor = { { 0.0f, 0.0f, 0.0f, 0.0f } };
													VkImageSubresourceRange range = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
													vkCmdClearColorImage(cmd, img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clearColor, 1, &range);
												});
											if (engine::core::Log::IsActive()) LOG_WARN(Render, "[Engine] Decal pass disabled, overlay clear fallback registered");
										}

										std::fprintf(stderr, "[ENGINE] R42: avant addPass Lighting\n"); std::fflush(stderr);
										m_frameGraph.addPass("Lighting",
											[this](engine::render::PassBuilder& b) {
												b.read(m_fgGBufferAId,       engine::render::ImageUsage::SampledRead);
												b.read(m_fgGBufferBId,       engine::render::ImageUsage::SampledRead);
												b.read(m_fgGBufferCId,       engine::render::ImageUsage::SampledRead);
												b.read(m_fgDepthId,          engine::render::ImageUsage::SampledRead);
												b.read(m_fgSsaoBlurId,       engine::render::ImageUsage::SampledRead);
												b.read(m_fgDecalOverlayId,   engine::render::ImageUsage::SampledRead);
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
												lp.lightDir[0] = m_zoneAtmosphere.sunDirection[0]; lp.lightDir[1] = m_zoneAtmosphere.sunDirection[1]; lp.lightDir[2] = m_zoneAtmosphere.sunDirection[2]; lp.lightDir[3] = 0.0f;
												lp.lightColor[0] = m_zoneAtmosphere.sunColor[0]; lp.lightColor[1] = m_zoneAtmosphere.sunColor[1]; lp.lightColor[2] = m_zoneAtmosphere.sunColor[2]; lp.lightColor[3] = 0.0f;
												const float probeIntensity = GetGlobalProbeIntensity(m_zoneProbes);
												lp.ambientColor[0] = m_zoneAtmosphere.ambientColor[0] * probeIntensity;
												lp.ambientColor[1] = m_zoneAtmosphere.ambientColor[1] * probeIntensity;
												lp.ambientColor[2] = m_zoneAtmosphere.ambientColor[2] * probeIntensity;
												lp.ambientColor[3] = 0.0f;
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
													m_fgSceneColorHDRId, m_fgSsaoBlurId, m_fgDecalOverlayId,
													irrView, irrSamp, prefilterView, prefilterSamp, brdfView, brdfSamp,
													lp, frameIdx);
											});
										std::fprintf(stderr, "[ENGINE] R43: addPass Lighting OK\n"); std::fflush(stderr);

										std::fprintf(stderr, "[ENGINE] R44: avant addPass Bloom\n"); std::fflush(stderr);
										m_frameGraph.addPass("Bloom_Prefilter",
											[this](engine::render::PassBuilder& b) {
												b.read(m_fgSceneColorHDRId, engine::render::ImageUsage::SampledRead);
												b.write(m_fgBloomDownMipIds[0], engine::render::ImageUsage::ColorWrite);
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
													m_fgSceneColorHDRId, m_fgBloomDownMipIds[0], pp, frameIdx);
											});

										for (uint32_t i = 0; i < engine::render::kBloomMipCount - 1; ++i)
										{
											const engine::render::ResourceId idSrc = m_fgBloomDownMipIds[i];
											const engine::render::ResourceId idDst = m_fgBloomDownMipIds[i + 1];
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
											// src = niveau du dessous dans le pyramid upsample (ou down si premier)
											const engine::render::ResourceId idSrc = (i == engine::render::kBloomMipCount - 2)
												? m_fgBloomDownMipIds[i + 1]
												: m_fgBloomUpMipIds[i + 1];
											const engine::render::ResourceId idDst = m_fgBloomUpMipIds[i];
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
												b.read(m_fgBloomUpMipIds[0],         engine::render::ImageUsage::SampledRead);
												b.write(m_fgSceneColorHDRWithBloomId, engine::render::ImageUsage::ColorWrite);
											},
											[this](VkCommandBuffer cmd, engine::render::Registry& reg) {
												if (!m_pipeline->GetBloomCombinePass().IsValid()) return;
												engine::render::BloomCombinePass::CombineParams cp{};
												cp.intensity = static_cast<float>(m_cfg.GetDouble("bloom.intensity", 1.0));
												const uint32_t frameIdx = m_currentFrame % 2;
												m_pipeline->GetBloomCombinePass().Record(m_vkDeviceContext.GetDevice(), cmd, reg, m_vkSwapchain.GetExtent(), m_fgSceneColorHDRId, m_fgBloomUpMipIds[0], m_fgSceneColorHDRWithBloomId, cp, frameIdx);
											});
										std::fprintf(stderr, "[ENGINE] R45: addPass Bloom OK\n"); std::fflush(stderr);

										std::fprintf(stderr, "[ENGINE] R46: avant addPass AutoExposure+Tonemap\n"); std::fflush(stderr);
										m_frameGraph.addPass("AutoExposure_Luminance",
											[this](engine::render::PassBuilder& b) {
												b.read(m_fgSceneColorHDRWithBloomId, engine::render::ImageUsage::SampledRead);
											},
											[this](VkCommandBuffer cmd, engine::render::Registry& reg) {
												if (!m_pipeline->GetAutoExposure().IsValid()) return;
												const uint32_t frameIdx = m_currentFrame % 2u;
												m_pipeline->GetAutoExposure().Record(
													m_vkDeviceContext.GetDevice(), cmd, reg,
													m_fgSceneColorHDRWithBloomId, m_vkSwapchain.GetExtent(), frameIdx);
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
										std::fprintf(stderr, "[ENGINE] R47: addPass Tonemap OK\n"); std::fflush(stderr);

										std::fprintf(stderr, "[ENGINE] R48: avant addPass TAA\n"); std::fflush(stderr);
										m_frameGraph.addPass("TAA",
											[this](engine::render::PassBuilder& b) {
												// Lecture pour TAA
												b.read(m_fgSceneColorLDRId,   engine::render::ImageUsage::SampledRead);
												b.read(m_fgHistoryAId,        engine::render::ImageUsage::SampledRead);
												b.read(m_fgHistoryBId,        engine::render::ImageUsage::SampledRead);
												b.read(m_fgGBufferVelocityId, engine::render::ImageUsage::SampledRead);
												b.read(m_fgDepthId,           engine::render::ImageUsage::SampledRead);
												// Lecture supplémentaire pour init d'historique (copies image)
												b.read(m_fgSceneColorLDRId,   engine::render::ImageUsage::TransferSrc);
												// Historiques écrits uniquement par cette passe
												b.write(m_fgHistoryAId,       engine::render::ImageUsage::ColorWrite);
												b.write(m_fgHistoryBId,       engine::render::ImageUsage::ColorWrite);
											},
											[this](VkCommandBuffer cmd, engine::render::Registry& reg) {
												// Init / reset d'historique si nécessaire
												if (m_taaHistoryInvalid)
												{
													VkImage srcImg = reg.getImage(m_fgSceneColorLDRId);
													if (srcImg != VK_NULL_HANDLE)
													{
														VkExtent2D ext = m_vkSwapchain.GetExtent();
														VkImageCopy region{};
														region.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
														region.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
														region.extent        = { ext.width, ext.height, 1 };

														if (!m_taaHistoryEverFilled)
														{
															VkImage dstA = reg.getImage(m_fgHistoryAId);
															VkImage dstB = reg.getImage(m_fgHistoryBId);
															if (dstA != VK_NULL_HANDLE && dstB != VK_NULL_HANDLE)
															{
																vkCmdCopyImage(cmd, srcImg, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
																               dstA,   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
																               1, &region);
																vkCmdCopyImage(cmd, srcImg, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
																               dstB,   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
																               1, &region);
																m_taaHistoryEverFilled = true;
																if (engine::core::Log::IsActive()) LOG_INFO(Render, "[TAA] History initialized at frame 0");
																return;
															}
														}
														else
														{
															engine::render::ResourceId nextId = GetTaaHistoryNextId();
															VkImage dstNext = reg.getImage(nextId);
															if (dstNext != VK_NULL_HANDLE)
															{
																vkCmdCopyImage(cmd, srcImg, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
																               dstNext, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
																               1, &region);
															}
														}
													}
												}

												if (!m_pipeline->GetTaaPass().IsValid()) return;
												engine::render::TaaPass::TaaParams tp{};
												tp.alpha = 0.9f; tp._pad[0] = tp._pad[1] = tp._pad[2] = 0.0f;
												const uint32_t frameIdx = m_currentFrame % 2u;
												m_pipeline->GetTaaPass().Record(
													m_vkDeviceContext.GetDevice(), cmd, reg, m_vkSwapchain.GetExtent(),
													m_fgSceneColorLDRId,
													GetTaaHistoryPrevId(),
													m_fgGBufferVelocityId,
													m_fgDepthId,
													GetTaaHistoryNextId(),
													tp,
													frameIdx);
											});
										std::fprintf(stderr, "[ENGINE] R48: addPass TAA OK\n"); std::fflush(stderr);

										std::fprintf(stderr, "[ENGINE] R49: avant addPass CopyPresent\n"); std::fflush(stderr);
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
										std::fprintf(stderr, "[ENGINE] R50: addPass CopyPresent OK\n"); std::fflush(stderr);

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
					if (engine::core::Log::IsActive()) LOG_WARN(Platform, "[Boot] VkInstance::Create failed");
				else
					if (engine::core::Log::IsActive()) LOG_WARN(Platform, "[Boot] Vulkan instance or GLFW window for surface failed");
			}
		}

		// FS smoke test
		std::fprintf(stderr, "[ENGINE] S: avant FS smoke\n"); std::fflush(stderr);
		{
			const auto cfgText = engine::platform::FileSystem::ReadAllText("config.json");
			if (engine::core::Log::IsActive()) LOG_INFO(Platform, "FS ReadAllText('config.json'): {} bytes", cfgText.size());
			const auto contentCfgText = engine::platform::FileSystem::ReadAllTextContent(m_cfg, "config.json");
			if (engine::core::Log::IsActive()) LOG_INFO(Platform, "FS ReadAllTextContent(paths.content/'config.json'): {} bytes", contentCfgText.size());
		}
		std::fprintf(stderr, "[ENGINE] T: FS smoke OK\n"); std::fflush(stderr);

		if (engine::core::Log::IsActive()) LOG_INFO(Core, "Engine init: vsync={} (present mode from swapchain)", m_vsync ? "on" : "off");
		if (engine::core::Log::IsActive()) LOG_INFO(Core, "[Boot] Engine boot COMPLETE");
		std::fprintf(stderr, "[ENGINE] U: constructeur COMPLETE\n"); std::fflush(stderr);
	}

	Engine::~Engine() = default;

	int Engine::Run()
	{
		std::fprintf(stderr, "[RUN] debut Run\n"); std::fflush(stderr);
		if (engine::core::Log::IsActive()) LOG_DEBUG(Core, "[Engine] Entering render loop");

		auto lastFpsLog  = std::chrono::steady_clock::now();
		auto lastPresent = lastFpsLog;

		while (!m_quitRequested && !m_window.ShouldClose())
		{
			std::fprintf(stderr, "[RUN] BeginFrame\n"); std::fflush(stderr);
			BeginFrame();
			std::fprintf(stderr, "[RUN] Update\n"); std::fflush(stderr);
			Update();
			std::fprintf(stderr, "[RUN] SwapRenderState\n"); std::fflush(stderr);
			SwapRenderState();
			std::fprintf(stderr, "[RUN] Render\n"); std::fflush(stderr);
			//std::fprintf(stderr, "[RENDER] frameGraph extent=%ux%u framesInFlight=%u\n", m_swapchainExtent.width, m_swapchainExtent.height, m_framesInFlight); std::fflush(stderr);
			
			Render();

			std::fprintf(stderr, "[RUN] EndFrame\n"); std::fflush(stderr);
			EndFrame();
			std::fprintf(stderr, "[RUN] frame done\n"); std::fflush(stderr);

			const auto now = std::chrono::steady_clock::now();
			if (now - lastFpsLog >= std::chrono::seconds(1))
			{
				if (engine::core::Log::IsActive()) LOG_INFO(Core, "fps={:.1f} dt_ms={:.3f} frame={}", m_time.FPS(), m_time.DeltaSeconds() * 1000.0, m_time.FrameIndex());
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

		std::fprintf(stderr, "[RUN] sortie loop\n"); std::fflush(stderr);
		if (engine::core::Log::IsActive()) LOG_INFO(Core, "[Engine] Render loop exited cleanly");

		if (m_vkDeviceContext.IsValid())
		{
			vkDeviceWaitIdle(m_vkDeviceContext.GetDevice());
			std::fprintf(stderr, "[RUN] vkDeviceWaitIdle OK\n"); std::fflush(stderr);
			if (m_pipeline)
			{
				std::fprintf(stderr, "[RUN] avant pipeline->Destroy\n"); std::fflush(stderr);
				m_pipeline->Destroy(m_vkDeviceContext.GetDevice());
				std::fprintf(stderr, "[RUN] pipeline->Destroy OK\n"); std::fflush(stderr);
				m_pipeline.reset();
			}
			std::fprintf(stderr, "[RUN] avant profilerHud.Shutdown\n"); std::fflush(stderr);
			m_profilerHud.Shutdown();
			std::fprintf(stderr, "[RUN] avant profiler.Shutdown\n"); std::fflush(stderr);
			m_profiler.Shutdown(m_vkDeviceContext.GetDevice());
			std::fprintf(stderr, "[RUN] avant audioEngine.Shutdown\n"); std::fflush(stderr);
			m_audioEngine.Shutdown();
			std::fprintf(stderr, "[RUN] avant decalSystem.Shutdown\n"); std::fflush(stderr);
			m_decalSystem.Shutdown();
			std::fprintf(stderr, "[RUN] avant assetRegistry.Destroy\n"); std::fflush(stderr);
			m_assetRegistry.Destroy();
			std::fprintf(stderr, "[RUN] avant frameGraph.destroy\n"); std::fflush(stderr);
			m_frameGraph.destroy(m_vkDeviceContext.GetDevice(), m_vmaAllocator);
			std::fprintf(stderr, "[RUN] avant stagingAllocator.Destroy\n"); std::fflush(stderr);
			m_stagingAllocator.Destroy(m_vkDeviceContext.GetDevice());
			std::fprintf(stderr, "[RUN] avant DestroyFrameResources\n"); std::fflush(stderr);
			engine::render::DestroyFrameResources(m_vkDeviceContext.GetDevice(), m_frameResources);
			if (m_vmaAllocator)
			{
				std::fprintf(stderr, "[RUN] avant vmaDestroyAllocator\n"); std::fflush(stderr);
				vmaDestroyAllocator(reinterpret_cast<VmaAllocator>(m_vmaAllocator));
				m_vmaAllocator = nullptr;
			}
		}
		std::fprintf(stderr, "[RUN] avant vkSwapchain.Destroy\n"); std::fflush(stderr);
		m_vkSwapchain.Destroy();
		std::fprintf(stderr, "[RUN] avant vkDeviceContext.Destroy\n"); std::fflush(stderr);
		m_vkDeviceContext.Destroy();
		std::fprintf(stderr, "[RUN] avant vkInstance.Destroy\n"); std::fflush(stderr);
		m_vkInstance.Destroy();
		if (m_glfwWindowForVk)
		{
			std::fprintf(stderr, "[RUN] avant glfwDestroyWindow\n"); std::fflush(stderr);
			glfwDestroyWindow(m_glfwWindowForVk);
			m_glfwWindowForVk = nullptr;
		}
		std::fprintf(stderr, "[RUN] avant glfwTerminate\n"); std::fflush(stderr);
		glfwTerminate();
		std::fprintf(stderr, "[RUN] shutdown complete\n"); std::fflush(stderr);

		if (m_editorMode)
		{
			m_editorMode->Shutdown(m_window);
			m_editorMode.reset();
		}
		m_window.Destroy();
		if (engine::core::Log::IsActive()) LOG_INFO(Core, "[Engine] Shutdown complete");
		return 0;
	}

	void Engine::BeginFrame()
	{
		// PROFILE_FUNCTION();
		std::fprintf(stderr, "[BF] input.BeginFrame\n"); std::fflush(stderr);
		m_input.BeginFrame();
		std::fprintf(stderr, "[BF] window.PollEvents\n"); std::fflush(stderr);
		m_window.PollEvents();
		std::fprintf(stderr, "[BF] WasPressed\n"); std::fflush(stderr);

		if (!m_editorEnabled && m_input.WasPressed(engine::platform::Key::Escape))
			OnQuit();

		std::fprintf(stderr, "[BF] shaderHotReload.Poll\n"); std::fflush(stderr);
		m_shaderHotReload.Poll(m_cfg);
		std::fprintf(stderr, "[BF] shaderHotReload.ApplyPending\n"); std::fflush(stderr);
		m_shaderHotReload.ApplyPending(m_shaderCache);
		std::fprintf(stderr, "[BF] swapchainResizeCheck\n"); std::fflush(stderr);

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
					if (engine::core::Log::IsActive()) LOG_INFO(Platform, "Swapchain recreated {}x{}", m_width, m_height);
			}
		}

		std::fprintf(stderr, "[BF] time.BeginFrame\n"); std::fflush(stderr);
		m_time.BeginFrame();
		std::fprintf(stderr, "[BF] time.BeginFrame OK - profiler=%d\n", 
    		(int)m_profiler.IsInitialized()); std::fflush(stderr);  // ← AJOUTE CETTE LIGNE
		if (m_profiler.IsInitialized())
		{
			m_profiler.BeginFrame(m_currentFrame);
		}
		std::fprintf(stderr, "[BF] frameArena.BeginFrame\n"); std::fflush(stderr);
		m_frameArena.BeginFrame(m_time.FrameIndex());
		std::fprintf(stderr, "[BF] chunkStats.ResetPerFrame\n"); std::fflush(stderr);
		m_chunkStats.ResetPerFrame();
		std::fprintf(stderr, "[BF] done\n"); std::fflush(stderr);
	}

	void Engine::Update()
	{
		// PROFILE_FUNCTION();
		const uint32_t readIdx  = m_renderReadIndex.load(std::memory_order_acquire);
		const uint32_t writeIdx = 1u - (readIdx & 1u);
		const auto& readState   = m_renderStates[readIdx];
		auto& out               = m_renderStates[writeIdx];

		const double dt               = (m_fixedDt > 0.0) ? m_fixedDt : m_time.DeltaSeconds();
		const float  mouseSensitivity = static_cast<float>(m_cfg.GetDouble("camera.mouse_sensitivity", 0.002));

		out.camera = readState.camera;
		out.profilerDebugText = m_profilerHud.IsInitialized() ? m_profilerHud.GetState().debugText : std::string{};
		if (!m_editorEnabled)
		{
			m_fpsCameraController.Update(m_input, dt, mouseSensitivity, out.camera);
		}

		m_world.Update(out.camera.position);

		if (m_width > 0 && m_height > 0)
			out.camera.aspect = static_cast<float>(m_width) / static_cast<float>(m_height);

		engine::math::Vec3 listenerVelocity{};
		if (dt > 0.0)
		{
			const float invDt = static_cast<float>(1.0 / dt);
			listenerVelocity = engine::math::Vec3(
				(out.camera.position.x - readState.camera.position.x) * invDt,
				(out.camera.position.y - readState.camera.position.y) * invDt,
				(out.camera.position.z - readState.camera.position.z) * invDt);
		}
		m_audioEngine.SetListener(out.camera.position, listenerVelocity);
		m_audioEngine.Tick(static_cast<float>(dt));
		m_decalSystem.Tick(static_cast<float>(dt));

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

		if (m_editorMode)
		{
			m_editorMode->Update(m_input, m_window, out.camera, m_geometryMeshHandle.Get(), m_width, m_height, dt);
			std::memcpy(out.objectModelMatrix, m_editorMode->GetObjectModelMatrix(), sizeof(out.objectModelMatrix));
			out.objectVisible = m_editorMode->IsObjectVisible();
		}
		else
		{
			out.objectVisible = true;
		}

		out.frustum.ExtractFromMatrix(out.viewProjMatrix);

		{
			const float maxDrawDist = static_cast<float>(m_cfg.GetDouble("world.max_draw_distance_m", 0.0));
			std::span<const engine::world::ChunkRequest> pending = m_streamingScheduler.GetPrioritizedRequests();
			out.hlodDebugText = engine::world::BuildChunkDrawList(pending.data(), pending.size(), out.camera.position, out.frustum, m_hlodRuntime, maxDrawDist, m_chunkDrawDecisions);
			if ((m_currentFrame % 60) == 0 && !out.hlodDebugText.empty())
				if (engine::core::Log::IsActive()) LOG_DEBUG(World, "M09.5 {}", out.hlodDebugText);
			if ((m_currentFrame % 60) == 0 && !out.profilerDebugText.empty())
				if (engine::core::Log::IsActive()) LOG_DEBUG(Core, "M18.1 {}", out.profilerDebugText);
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
	    // PROFILE_FUNCTION();
	    std::fprintf(stderr, "[RENDER] debut\n"); std::fflush(stderr);
	    if (!m_vkDeviceContext.IsValid() || !m_vkSwapchain.IsValid() || m_frameResources[0].cmdPool == VK_NULL_HANDLE)
	    {
	        std::fprintf(stderr, "[RENDER] early return\n"); std::fflush(stderr);
	        return;
	    }
	
	    const uint32_t frameIndex          = m_currentFrame % 2;
	    engine::render::FrameResources& fr = m_frameResources[frameIndex];
	    ::VkDevice     device              = m_vkDeviceContext.GetDevice();
	    VkQueue        graphicsQueue       = m_vkDeviceContext.GetGraphicsQueue();
	    VkQueue        presentQueue        = m_vkDeviceContext.GetPresentQueue();
	    VkSwapchainKHR swapchain           = m_vkSwapchain.GetSwapchain();
	    VkExtent2D extent{ static_cast<uint32_t>(m_width), static_cast<uint32_t>(m_height) };
	
	    std::fprintf(stderr, "[RENDER] avant vkWaitForFences\n"); std::fflush(stderr);
	    vkWaitForFences(device, 1, &fr.fence, VK_TRUE, UINT64_MAX);
	    if (m_profiler.IsInitialized() && m_profiler.ResolveGpuFrame(device, frameIndex))
	    {
	        if (m_profilerHud.IsInitialized())
	        {
	            m_profilerHud.ApplySnapshot(m_profiler.GetLatestSnapshot());
	            m_renderStates[0].profilerDebugText = m_profilerHud.GetState().debugText;
	            m_renderStates[1].profilerDebugText = m_profilerHud.GetState().debugText;
	        }
	    }
	    std::fprintf(stderr, "[RENDER] Collect\n"); std::fflush(stderr);
	    m_deferredDestroyQueue.Collect(device, m_currentFrame > 0 ? m_currentFrame - 1 : 0);
	    std::fprintf(stderr, "[RENDER] stagingAllocator.BeginFrame\n"); std::fflush(stderr);
	    m_stagingAllocator.BeginFrame(frameIndex);
	    std::fprintf(stderr, "[RENDER] gpuUploadQueue\n"); std::fflush(stderr);
	    (void)m_gpuUploadQueue.PlanFrameUploads();
	
	    std::fprintf(stderr, "[RENDER] autoExposure\n"); std::fflush(stderr);
	    if (m_pipeline->GetAutoExposure().IsValid())
	    {
	        const float dt    = static_cast<float>(m_time.DeltaSeconds());
	        const float key   = static_cast<float>(m_cfg.GetDouble("exposure.key", 0.18));
	        const float speed = static_cast<float>(m_cfg.GetDouble("exposure.speed", 2.0));
	        m_pipeline->GetAutoExposure().Update(device, dt, key, speed, frameIndex);
	    }
	
	    std::fprintf(stderr, "[RENDER] avant vkAcquireNextImageKHR\n"); std::fflush(stderr);
	    uint32_t imageIndex = 0;
	    VkResult result = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, fr.imageAvailable, VK_NULL_HANDLE, &imageIndex);
	    std::fprintf(stderr, "[RENDER] vkAcquireNextImageKHR result=%d imageIndex=%u\n", (int)result, imageIndex); std::fflush(stderr);
	    if (result == VK_ERROR_OUT_OF_DATE_KHR) { m_swapchainResizeRequested = true; return; }
	    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) return;
	
	    std::fprintf(stderr, "[RENDER] avant vkResetCommandPool\n"); std::fflush(stderr);
	    vkResetCommandPool(device, fr.cmdPool, 0);
	
	    std::fprintf(stderr, "[RENDER] avant vkBeginCommandBuffer\n"); std::fflush(stderr);
	    VkCommandBufferBeginInfo beginInfo{};
	    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	    if (vkBeginCommandBuffer(fr.cmdBuffer, &beginInfo) != VK_SUCCESS) return;
	    if (m_profiler.IsInitialized())
	    {
	        m_profiler.BeginGpuFrame(fr.cmdBuffer, frameIndex);
	    }
	
	    std::fprintf(stderr, "[RENDER] avant frameGraph.execute\n"); std::fflush(stderr);
	    if (m_fgSceneColorHDRId != engine::render::kInvalidResourceId && m_fgBackbufferId != engine::render::kInvalidResourceId)
	    {
	        VkImage     backbufferImage = m_vkSwapchain.GetImage(imageIndex);
	        VkImageView backbufferView  = m_vkSwapchain.GetImageView(imageIndex);
	        m_fgRegistry.bindImage(m_fgBackbufferId, backbufferImage, backbufferView);
	        m_frameGraph.execute(m_vkDeviceContext.GetDevice(), m_vkDeviceContext.GetPhysicalDevice(), m_vmaAllocator, fr.cmdBuffer, m_fgRegistry, frameIndex, extent, 2u, m_vkDeviceContext.SupportsSynchronization2(), m_profiler.IsInitialized() ? &m_profiler : nullptr);
	    }
	    std::fprintf(stderr, "[RENDER] frameGraph.execute OK\n"); std::fflush(stderr);
	
	    std::fprintf(stderr, "[RENDER] avant vkEndCommandBuffer\n"); std::fflush(stderr);
	    if (vkEndCommandBuffer(fr.cmdBuffer) != VK_SUCCESS) return;
	
	    std::fprintf(stderr, "[RENDER] avant vkQueueSubmit frame=%u\n", m_currentFrame); std::fflush(stderr);
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
	    VkResult submitResult = vkQueueSubmit(graphicsQueue, 1, &submitInfo, fr.fence);
	    std::fprintf(stderr, "[RENDER] vkQueueSubmit r=%d\n", (int)submitResult); std::fflush(stderr);
	    if (submitResult != VK_SUCCESS) return;

	    std::fprintf(stderr, "[RENDER] avant vkQueuePresentKHR\n"); std::fflush(stderr);
	    VkPresentInfoKHR presentInfo{};
	    presentInfo.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	    presentInfo.waitSemaphoreCount = 1;
	    presentInfo.pWaitSemaphores    = signalSemaphores;
	    presentInfo.swapchainCount     = 1;
	    presentInfo.pSwapchains        = &swapchain;
	    presentInfo.pImageIndices      = &imageIndex;
	    result = vkQueuePresentKHR(presentQueue, &presentInfo);
	    std::fprintf(stderr, "[RENDER] vkQueuePresentKHR r=%d\n", (int)result); std::fflush(stderr);
	    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
	        m_swapchainResizeRequested = true;
	
	    m_currentFrame++;
	    std::fprintf(stderr, "[RENDER] done\n"); std::fflush(stderr);
	}

	void Engine::EndFrame()
	{
		// PROFILE_FUNCTION();
		if (m_profiler.IsInitialized())
		{
			m_profiler.EndFrame();
		}
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
