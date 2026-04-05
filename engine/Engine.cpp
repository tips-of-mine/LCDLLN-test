#include "engine/Engine.h"

#include "engine/core/Log.h"
#include "engine/editor/EditorMode.h"
#include "engine/core/memory/Memory.h"
#include "engine/platform/FileSystem.h"
#include "engine/render/AuthUiRenderer.h"
#include "engine/render/DeferredPipeline.h"
#include "engine/render/ShaderCompiler.h"
#include "engine/server/ServerProtocol.h"

#include <GLFW/glfw3.h>
#include <vulkan/vulkan.h>

#include <vk_mem_alloc.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace engine
{
	namespace
	{
		void ApplyUserSettingsOverrides(engine::core::Config& cfg)
		{
			engine::core::Config persisted;
			if (!persisted.LoadFromFile("user_settings.json"))
			{
				LOG_INFO(Core, "[Boot] user_settings.json not found — using config defaults");
				return;
			}

			if (persisted.Has("render.vsync"))
				cfg.SetValue("render.vsync", persisted.GetBool("render.vsync", cfg.GetBool("render.vsync", true)));
			if (persisted.Has("render.fullscreen"))
				cfg.SetValue("render.fullscreen", persisted.GetBool("render.fullscreen", cfg.GetBool("render.fullscreen", true)));
			if (persisted.Has("client.locale"))
				cfg.SetValue("client.locale", persisted.GetString("client.locale", cfg.GetString("client.locale", "")));
			if (persisted.Has("audio.master_volume"))
				cfg.SetValue("audio.master_volume", persisted.GetDouble("audio.master_volume", cfg.GetDouble("audio.master_volume", 1.0)));
			if (persisted.Has("audio.music_volume"))
				cfg.SetValue("audio.music_volume", persisted.GetDouble("audio.music_volume", cfg.GetDouble("audio.music_volume", 1.0)));
			if (persisted.Has("audio.sfx_volume"))
				cfg.SetValue("audio.sfx_volume", persisted.GetDouble("audio.sfx_volume", cfg.GetDouble("audio.sfx_volume", 1.0)));
			if (persisted.Has("audio.ui_volume"))
				cfg.SetValue("audio.ui_volume", persisted.GetDouble("audio.ui_volume", cfg.GetDouble("audio.ui_volume", 1.0)));
			if (persisted.Has("camera.mouse_sensitivity"))
				cfg.SetValue("camera.mouse_sensitivity", persisted.GetDouble("camera.mouse_sensitivity", cfg.GetDouble("camera.mouse_sensitivity", 0.002)));
			if (persisted.Has("controls.invert_y"))
				cfg.SetValue("controls.invert_y", persisted.GetBool("controls.invert_y", cfg.GetBool("controls.invert_y", false)));
			if (persisted.Has("controls.movement_layout"))
				cfg.SetValue("controls.movement_layout", persisted.GetString("controls.movement_layout", cfg.GetString("controls.movement_layout", "wasd")));
			if (persisted.Has("client.gameplay_udp.enabled"))
				cfg.SetValue("client.gameplay_udp.enabled", persisted.GetBool("client.gameplay_udp.enabled", cfg.GetBool("client.gameplay_udp.enabled", false)));
			if (persisted.Has("client.allow_insecure_dev"))
				cfg.SetValue("client.allow_insecure_dev", persisted.GetBool("client.allow_insecure_dev", cfg.GetBool("client.allow_insecure_dev", true)));
			if (persisted.Has("client.auth_ui.timeout_ms"))
				cfg.SetValue("client.auth_ui.timeout_ms", persisted.GetInt("client.auth_ui.timeout_ms", cfg.GetInt("client.auth_ui.timeout_ms", 5000)));
			if (persisted.Has("render.auth_ui.background_blit.enabled"))
				cfg.SetValue("render.auth_ui.background_blit.enabled",
					persisted.GetBool("render.auth_ui.background_blit.enabled", cfg.GetBool("render.auth_ui.background_blit.enabled", true)));
			if (persisted.Has("render.auth_ui.background_path"))
				cfg.SetValue("render.auth_ui.background_path",
					persisted.GetString("render.auth_ui.background_path", cfg.GetString("render.auth_ui.background_path", "ui/login/background.png")));
			if (persisted.Has("render.auth_ui.background_blit.fit"))
				cfg.SetValue("render.auth_ui.background_blit.fit",
					persisted.GetString("render.auth_ui.background_blit.fit", cfg.GetString("render.auth_ui.background_blit.fit", "cover_height")));

			LOG_INFO(Core, "[Boot] user_settings.json overrides applied (fullscreen={}, vsync={}, locale={}, master={:.1f}, music={:.1f}, sfx={:.1f}, ui={:.1f}, sens={:.4f}, invert_y={}, layout={}, gameplay_udp={}, allow_insecure_dev={}, timeout_ms={}, auth_bg_blit={}, auth_bg_fit={})",
				cfg.GetBool("render.fullscreen", true),
				cfg.GetBool("render.vsync", true),
				cfg.GetString("client.locale", ""),
				cfg.GetDouble("audio.master_volume", 1.0),
				cfg.GetDouble("audio.music_volume", 1.0),
				cfg.GetDouble("audio.sfx_volume", 1.0),
				cfg.GetDouble("audio.ui_volume", 1.0),
				cfg.GetDouble("camera.mouse_sensitivity", 0.002),
				cfg.GetBool("controls.invert_y", false),
				cfg.GetString("controls.movement_layout", "wasd"),
				cfg.GetBool("client.gameplay_udp.enabled", false),
				cfg.GetBool("client.allow_insecure_dev", true),
				cfg.GetInt("client.auth_ui.timeout_ms", 5000),
				cfg.GetBool("render.auth_ui.background_blit.enabled", true),
				cfg.GetString("render.auth_ui.background_blit.fit", "cover_height"));
		}

		enum class AuthBackgroundBlitFit
		{
			Stretch,
			Contain,
			Cover,
			CoverHeight,
		};

		AuthBackgroundBlitFit ParseAuthBackgroundBlitFit(std::string_view s)
		{
			if (s == "contain")
			{
				return AuthBackgroundBlitFit::Contain;
			}
			if (s == "cover_height" || s == "height")
			{
				return AuthBackgroundBlitFit::CoverHeight;
			}
			if (s == "cover")
			{
				return AuthBackgroundBlitFit::Cover;
			}
			if (s == "stretch")
			{
				return AuthBackgroundBlitFit::Stretch;
			}
			return AuthBackgroundBlitFit::CoverHeight;
		}

		/// Remplit \p blit pour \c vkCmdBlitImage : \c stretch (étire), \c contain (image entière, bandes),
		/// \c cover (remplit la surface, rogne l’excédent au centre),
		/// \c cover_height (remplit la hauteur, rogne la largeur de la source si l’écran est plus large ; sinon bandes latérales).
		void BuildAuthBackgroundBlit(
			AuthBackgroundBlitFit fit,
			uint32_t srcW,
			uint32_t srcH,
			uint32_t dstW,
			uint32_t dstH,
			VkImageBlit& blit)
		{
			blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			blit.srcSubresource.mipLevel = 0;
			blit.srcSubresource.baseArrayLayer = 0;
			blit.srcSubresource.layerCount = 1;
			blit.dstSubresource = blit.srcSubresource;
			blit.srcOffsets[0] = { 0, 0, 0 };
			blit.srcOffsets[1] = { static_cast<int32_t>(srcW), static_cast<int32_t>(srcH), 1 };
			blit.dstOffsets[0] = { 0, 0, 0 };
			blit.dstOffsets[1] = { static_cast<int32_t>(dstW), static_cast<int32_t>(dstH), 1 };

			const float sw = static_cast<float>(srcW);
			const float sh = static_cast<float>(srcH);
			const float dw = static_cast<float>(dstW);
			const float dh = static_cast<float>(dstH);
			if (sw <= 0.f || sh <= 0.f || dw <= 0.f || dh <= 0.f || fit == AuthBackgroundBlitFit::Stretch)
			{
				return;
			}

			if (fit == AuthBackgroundBlitFit::Contain)
			{
				const float scale = std::min(dw / sw, dh / sh);
				const int32_t outW = static_cast<int32_t>(std::lround(sw * scale));
				const int32_t outH = static_cast<int32_t>(std::lround(sh * scale));
				const int32_t dx = (static_cast<int32_t>(dstW) - outW) / 2;
				const int32_t dy = (static_cast<int32_t>(dstH) - outH) / 2;
				blit.dstOffsets[0] = { dx, dy, 0 };
				blit.dstOffsets[1] = { dx + outW, dy + outH, 1 };
				return;
			}

			if (fit == AuthBackgroundBlitFit::CoverHeight)
			{
				const float scale = dh / sh;
				const float scaledW = sw * scale;
				if (scaledW >= dw)
				{
					const float cropW = dw / scale;
					float sx0 = (sw - cropW) * 0.5f;
					sx0 = std::clamp(sx0, 0.f, sw - 1.f);
					float sx1 = sx0 + cropW;
					sx1 = std::min(sx1, sw);
					if (sx1 <= sx0)
					{
						sx1 = std::min(sx0 + 1.f, sw);
					}
					blit.srcOffsets[0] = { static_cast<int32_t>(std::floor(sx0)), 0, 0 };
					blit.srcOffsets[1] = { static_cast<int32_t>(std::ceil(sx1)), static_cast<int32_t>(srcH), 1 };
					return;
				}
				const int32_t outW = static_cast<int32_t>(std::lround(scaledW));
				const int32_t dx = (static_cast<int32_t>(dstW) - outW) / 2;
				blit.dstOffsets[0] = { dx, 0, 0 };
				blit.dstOffsets[1] = { dx + outW, static_cast<int32_t>(dstH), 1 };
				return;
			}

			// Cover : rogne la source au centre pour remplir le framebuffer sans étirement.
			const float scale = std::max(dw / sw, dh / sh);
			const float cropW = dw / scale;
			const float cropH = dh / scale;
			float sx0 = (sw - cropW) * 0.5f;
			float sy0 = (sh - cropH) * 0.5f;
			sx0 = std::clamp(sx0, 0.f, sw - 1.f);
			sy0 = std::clamp(sy0, 0.f, sh - 1.f);
			float sx1 = sx0 + cropW;
			float sy1 = sy0 + cropH;
			sx1 = std::min(sx1, sw);
			sy1 = std::min(sy1, sh);
			if (sx1 <= sx0)
			{
				sx1 = std::min(sx0 + 1.f, sw);
			}
			if (sy1 <= sy0)
			{
				sy1 = std::min(sy0 + 1.f, sh);
			}
			blit.srcOffsets[0] = { static_cast<int32_t>(std::floor(sx0)), static_cast<int32_t>(std::floor(sy0)), 0 };
			blit.srcOffsets[1] = { static_cast<int32_t>(std::ceil(sx1)), static_cast<int32_t>(std::ceil(sy1)), 1 };
		}

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

		/// Match \ref engine::server::VendorCatalog::ComputeSellPrice (client-side preview only).
		uint32_t ClientVendorSellUnitGold(uint32_t buyPrice)
		{
			if (buyPrice == 0u)
			{
				return 0u;
			}
			const uint64_t sp = (static_cast<uint64_t>(buyPrice) * 25ull) / 100ull;
			const uint32_t out = static_cast<uint32_t>(std::min<uint64_t>(sp, static_cast<uint64_t>(0xFFFFFFFFu)));
			return out > 0u ? out : 1u;
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
					LOG_WARN(Render, "[GpuDrivenCulling] Mesh local bounds missing, using unit bounds fallback");
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
			LOG_WARN(Core, "[ZoneProbes] Runtime manifest fallback sky (path={}, reason={})", zoneMetaPath, error);
		}
		else if (engine::world::LoadProbeSet(m_cfg, probesPath, zoneHeader.contentHash, true, m_zoneProbes, error))
		{
			LOG_INFO(Core, "[ZoneProbes] Runtime probes ready (path={}, count={})", probesPath, m_zoneProbes.probes.size());
		}
		else
		{
			LOG_WARN(Core, "[ZoneProbes] Runtime probes fallback sky (path={}, reason={})", probesPath, error);
		}

		error.clear();
		if (engine::world::LoadAtmosphereSettings(m_cfg, atmospherePath, m_zoneAtmosphere, error))
		{
			LOG_INFO(Core, "[ZoneProbes] Runtime atmosphere ready (path={})", atmospherePath);
		}
		else
		{
			LOG_WARN(Core, "[ZoneProbes] Runtime atmosphere defaults active (path={}, reason={})", atmospherePath, error);
		}
	}

	Engine::Engine(int argc, char** argv)
		: m_cfg(engine::core::Config::Load("config.json", argc, argv))
		, m_time(120)
		, m_frameArena(/*framesInFlight*/ 2, /*perFrameCapacityBytes*/ 1024 * 1024)
	{

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
		if (logToFile)
		{
			const std::string relPath = engine::core::Log::MakeTimestampedFilename("lcdlln.exe");
			std::error_code ec;
			const auto absPath = std::filesystem::absolute(relPath, ec);
			logSettings.filePath = ec ? relPath : absPath.string();
			std::fprintf(stderr, "[Log] Log file: %s\n", logSettings.filePath.c_str());
			std::fflush(stderr);
		}
		logSettings.console     = logToConsole;
		logSettings.flushAlways = true;
		logSettings.level       = engine::core::LogLevel::Info;
		logSettings.rotation_size_mb = static_cast<size_t>(std::max(static_cast<int64_t>(0), m_cfg.GetInt("log.rotation_size_mb", 10)));
		logSettings.retention_days   = static_cast<int>(m_cfg.GetInt("log.retention_days", 7));

		engine::core::Log::Init(logSettings);

		if (!logSettings.filePath.empty() || logSettings.console)
		{
			LOG_INFO(Core, "[Boot] Log initialized (console={}, file={})", logToConsole ? "on" : "off", logSettings.filePath);
		}

		// ------------------------------------------------------------------
		// Config + subsystems
		// ------------------------------------------------------------------
		ApplyUserSettingsOverrides(m_cfg);
		m_vsync   = m_cfg.GetBool("render.vsync", true);
		m_fixedDt = m_cfg.GetDouble("time.fixed_dt", 0.0);
		m_editorEnabled = HasCliFlag(argc, argv, "--editor") || m_cfg.GetBool("editor.enabled", false);

		if (!logSettings.filePath.empty() || logSettings.console)
		{
			LOG_INFO(Core, "[Boot] Config loaded (vsync={}, fixed_dt={})", m_vsync ? "on" : "off", m_fixedDt);
		}

		if (m_editorEnabled)
		{
			m_editorMode = std::make_unique<engine::editor::EditorMode>();
			if (!m_editorMode->Init(m_cfg))
			{
				LOG_WARN(Core, "[Boot] EditorMode init failed; editor disabled");
				m_editorMode.reset();
				m_editorEnabled = false;
			}
			else
			{
				const engine::render::Camera editorCamera = m_editorMode->BuildInitialCamera();
				m_renderStates[0].camera = editorCamera;
				m_renderStates[1].camera = editorCamera;
				LOG_INFO(Core, "[Boot] Editor mode enabled (--editor)");
			}
		}

		m_chunkStats.Init(m_cfg);
		m_lodConfig.Init(m_cfg);
		m_hlodRuntime.Init(m_cfg);
		LoadZoneProbeAssets();
		m_streamCache.Init(m_cfg);
		m_streamingScheduler.SetStreamCache(&m_streamCache);
		m_gpuUploadQueue.Init(m_cfg);
		LOG_INFO(Core, "[Boot] Streaming subsystems ready (ChunkStats, LOD, HLOD, StreamCache, GpuUploadQueue)");

		// ------------------------------------------------------------------
		// Window
		// ------------------------------------------------------------------
		engine::platform::Window::CreateDesc desc{};
		desc.title  = "LCDLLN Engine";
		desc.width  = 1280;
		desc.height = 720;

		if (!m_window.Create(desc))
		{
			LOG_FATAL(Platform, "[Boot] Window::Create failed");
		}
		LOG_INFO(Core, "[Boot] Window::Create OK");
		if (m_cfg.GetBool("render.fullscreen", true))
		{
			m_window.ToggleFullscreen();
		}

		m_window.SetOnResize([this](int w, int h) { OnResize(w, h); });
		m_window.SetOnClose([this]() { OnQuit(); });
		m_window.SetMessageHook([this](uint32_t msg, uint64_t wp, int64_t lp)
		{
			m_input.HandleMessage(msg, wp, lp);
		});
		m_window.GetClientSize(m_width, m_height);

		if (!m_chatUi.Init())
		{
			LOG_WARN(Core, "[Boot] ChatUiPresenter init FAILED — M29.1 chat panel disabled");
		}
		else if (!m_chatUi.SetViewportSize(static_cast<uint32_t>(std::max(1, m_width)), static_cast<uint32_t>(std::max(1, m_height))))
		{
			LOG_WARN(Core, "[Boot] ChatUiPresenter viewport FAILED — using fallback layout");
		}

		if (!m_authUi.Init(m_cfg))
		{
			LOG_WARN(Core, "[Boot] AuthUiPresenter init FAILED — STAB.13 gate disabled");
		}
		else if (!m_authUi.SetViewportSize(static_cast<uint32_t>(std::max(1, m_width)), static_cast<uint32_t>(std::max(1, m_height))))
		{
			LOG_WARN(Core, "[Boot] AuthUiPresenter viewport FAILED — using fallback layout");
		}

		InitGameplayNet();

		// -----------------------------------------------------------------
		// Vulkan init
		// -----------------------------------------------------------------
		if (glfwInit() != GLFW_TRUE)
		{
			LOG_WARN(Platform, "[Boot] glfwInit failed");
		}
		else
		{
			LOG_INFO(Core, "[Boot] glfwInit OK");
			bool surfaceReady = false;
#if defined(_WIN32)
			surfaceReady = true;
#else
			glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
			glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
			m_glfwWindowForVk = glfwCreateWindow(1, 1, "VkSurface", nullptr, nullptr);
			if (!m_glfwWindowForVk)
			{
				LOG_WARN(Platform, "[Boot] glfwCreateWindow returned null");
			}
			else
			{
				LOG_INFO(Core, "[Boot] glfwCreateWindow OK");
				surfaceReady = true;
			}
#endif

			if (surfaceReady && m_vkInstance.Create())
			{
				LOG_INFO(Core, "[Boot] VkInstance::Create OK");
#if defined(_WIN32)
				const bool surfaceOk = m_vkInstance.CreateSurface(m_window.GetNativeHandle());
#else
				const bool surfaceOk = m_vkInstance.CreateSurface(m_glfwWindowForVk);
#endif
				if (!surfaceOk)
				{
					LOG_WARN(Platform, "[Boot] VkInstance::CreateSurface failed");
				}
				else
				{
					LOG_INFO(Core, "[Boot] VkInstance::CreateSurface OK");
					if (!m_vkDeviceContext.Create(m_vkInstance.GetHandle(), m_vkInstance.GetSurface()))
					{
						LOG_WARN(Platform, "[Boot] VkDeviceContext::Create failed");
					}
					else
					{
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
							VkExtent2D swapExtent = m_vkSwapchain.GetExtent();
							LOG_INFO(Core, "[Boot] VkSwapchain::Create OK (extent={}x{}, images={})",
								swapExtent.width, swapExtent.height, m_vkSwapchain.GetImageCount());

							if (!engine::render::CreateFrameResources(
								m_vkDeviceContext.GetDevice(),
								m_vkDeviceContext.GetGraphicsQueueFamilyIndex(),
								m_frameResources))
							{
								LOG_WARN(Platform, "[Boot] FrameSync::Init failed");
							}
							else
							{
								LOG_INFO(Core, "[Boot] FrameSync::Init OK");

								if (m_vkSwapchain.IsValid())
								{
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
										LOG_ERROR(Render, "[Boot] VMA allocator creation failed — GPU memory unavailable");
										}
										else
										{
											LOG_INFO(Render, "[Boot] VMA allocator created OK");
									}

									// Vérification VmaAllocatorInfo
									
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
										if (tmpBuf != VK_NULL_HANDLE) vkDestroyBuffer(m_vkDeviceContext.GetDevice(), tmpBuf, nullptr);

										// M10.4: réactivation du StagingAllocator avec budget streaming.
										const size_t stagingBudget = m_gpuUploadQueue.GetBudgetBytes();
										if (!m_stagingAllocator.Init(m_vkDeviceContext.GetDevice(), m_vmaAllocator, stagingBudget))
										{
											LOG_WARN(Render, "[Boot] StagingAllocator init FAILED (budget={} bytes) — streaming GPU uploads disabled", stagingBudget);
										}
										else
										{
											LOG_INFO(Render, "[Boot] StagingAllocator ready (budget={} bytes)", stagingBudget);
										}

										m_pipeline = std::make_unique<engine::render::DeferredPipeline>();

										m_assetRegistry.Init(m_vkDeviceContext.GetDevice(), m_vkDeviceContext.GetPhysicalDevice(), m_vmaAllocator, m_cfg);

										if (!m_profiler.Init(m_vkDeviceContext.GetDevice(), m_vkDeviceContext.GetPhysicalDevice(), 2u))
										{
											LOG_WARN(Core, "[Boot] Profiler init failed — GPU timestamp profiling disabled");
										}
										if (!m_profilerHud.Init())
										{
											LOG_WARN(Core, "[Boot] ProfilerHud init failed — in-game profiler overlay disabled");
										    m_profiler.Shutdown(m_vkDeviceContext.GetDevice());
										}
										if (!m_audioEngine.Init(m_cfg))
										{
											LOG_WARN(Core, "[Boot] AudioEngine init failed — no sound");
										}
										else
										{
											m_audioEngine.SetMasterVolume(static_cast<float>(m_cfg.GetDouble("audio.master_volume", 1.0)));
											m_audioEngine.SetBusVolume("Music", static_cast<float>(m_cfg.GetDouble("audio.music_volume", 1.0)));
											m_audioEngine.SetBusVolume("SFX", static_cast<float>(m_cfg.GetDouble("audio.sfx_volume", 1.0)));
											m_audioEngine.SetBusVolume("UI", static_cast<float>(m_cfg.GetDouble("audio.ui_volume", 1.0)));
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
										{
											const std::string authBgPath = m_cfg.GetString("render.auth_ui.background_path", "ui/login/background.png");
											if (!authBgPath.empty())
											{
												const std::filesystem::path authBgResolved = engine::platform::FileSystem::ResolveContentPath(m_cfg, authBgPath);
												LOG_INFO(Render, "[Boot] Auth UI background file: {}", authBgResolved.string());
												m_authUiBackgroundTexture = m_assetRegistry.LoadTextureForPresentBlit(authBgPath, m_vkSwapchain.GetImageFormat());
												if (!m_authUiBackgroundTexture.IsValid())
												{
													LOG_WARN(Render, "[Boot] Auth UI background not loaded (decode/path) — check file exists: {}", authBgPath);
												}
												else
												{
													if (!m_assetRegistry.FinalizePresentBlitTextureUpload(
															m_vkDeviceContext.GetGraphicsQueue(),
															m_vkDeviceContext.GetGraphicsQueueFamilyIndex()))
													{
														LOG_WARN(Render, "[Boot] Auth UI background GPU upload FAILED — fond absent jusqu'à correction");
													}
													else
													{
														m_authUiBackgroundLayoutReady = true;
														LOG_INFO(Render, "[Boot] Auth UI background prêt (GPU OK): {}", authBgPath);
														LOG_INFO(Render, "[Boot] Auth UI background_blit.fit={} (cover_height|cover|contain|stretch)",
															m_cfg.GetString("render.auth_ui.background_blit.fit", "cover_height"));
													}
												}
											}
										}
										{
											const std::string authLogoPath = m_cfg.GetString("render.auth_ui.logo_path", "ui/login/logo_login.png");
											if (!authLogoPath.empty())
											{
												m_authLogoTexture = m_assetRegistry.LoadTexture(authLogoPath, true);
												if (!m_authLogoTexture.IsValid())
												{
													LOG_WARN(Render, "[Boot] Auth UI logo not loaded: {}", authLogoPath);
												}
											}
											m_authLogoSuccessTexture = m_assetRegistry.LoadTexture("ui/login/success.png", true);
											if (!m_authLogoSuccessTexture.IsValid())
											{
												LOG_WARN(Render, "[Boot] Auth UI status OK logo not loaded: ui/login/success.png");
											}
											m_authLogoErrorTexture = m_assetRegistry.LoadTexture("ui/login/error.png", true);
											if (!m_authLogoErrorTexture.IsValid())
											{
												LOG_WARN(Render, "[Boot] Auth UI status error logo not loaded: ui/login/error.png");
											}
										}

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
										sceneColorLDRDesc.format = m_vkSwapchain.GetImageFormat();
										sceneColorLDRDesc.usage  = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT
										                         | VK_IMAGE_USAGE_SAMPLED_BIT
										                         | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
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
										historyDesc.format = m_vkSwapchain.GetImageFormat();
										historyDesc.usage  = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT
										                   | VK_IMAGE_USAGE_SAMPLED_BIT
										                   | VK_IMAGE_USAGE_TRANSFER_DST_BIT
										                   | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
										historyDesc.transient = false;
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
										LOG_INFO(Render, "[Bloom] FrameGraph resources registered: {} down + {} up mips",
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

										{
											engine::render::ShaderCompiler sc;
											if (sc.LocateCompiler())
												LOG_INFO(Core, "[Boot] ShaderCompiler OK");
											else
												LOG_WARN(Render, "[Boot] ShaderCompiler glslangValidator not found");
										}

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

											LOG_WARN(Render, "Shader SPIR-V not found or invalid: {}", spvPath);
											return {};
										};

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
										LOG_INFO(Core, "[Boot] DeferredPipeline init OK");

										if (m_vkDeviceContext.SupportsDynamicRendering())
										{
											std::vector<uint32_t> authGlyphVert = loadSpirv("shaders/auth_glyph.vert.spv");
											std::vector<uint32_t> authGlyphFrag = loadSpirv("shaders/auth_glyph.frag.spv");
											std::vector<uint32_t> authTtfFrag = loadSpirv("shaders/auth_ttf.frag.spv");
											if (!authGlyphVert.empty() && !authGlyphFrag.empty())
											{
												const uint32_t* ttfFragPtr = authTtfFrag.empty() ? nullptr : authTtfFrag.data();
												const size_t ttfFragWords = authTtfFrag.size();
												if (m_authGlyphPass.Init(
														m_vkDeviceContext.GetDevice(),
														m_vkDeviceContext.GetPhysicalDevice(),
														m_vkSwapchain.GetImageFormat(),
														authGlyphVert.data(), authGlyphVert.size(),
														authGlyphFrag.data(), authGlyphFrag.size(),
														8192u,
														VK_NULL_HANDLE,
														ttfFragPtr,
														ttfFragWords))
												{
													LOG_INFO(Render, "[Boot] AuthGlyphPass OK");
													const std::string uiFontPath = m_cfg.GetString("render.auth_ui.font_path", "");
													if (!uiFontPath.empty() && ttfFragPtr != nullptr)
													{
														std::vector<uint8_t> fontBytes = engine::platform::FileSystem::ReadAllBytesContent(m_cfg, uiFontPath);
														if (!fontBytes.empty())
														{
															const float fontPx = static_cast<float>(std::clamp<int64_t>(
																m_cfg.GetInt("render.auth_ui.font_pixel_height", 28), 12, 96));
															if (m_authGlyphPass.UploadUiFontFromTtf(
																	m_vkDeviceContext.GetDevice(),
																	m_vkDeviceContext.GetPhysicalDevice(),
																	m_vkDeviceContext.GetGraphicsQueue(),
																	m_vkDeviceContext.GetGraphicsQueueFamilyIndex(),
																	fontBytes.data(),
																	fontBytes.size(),
																	fontPx))
															{
																LOG_INFO(Render, "[Boot] Auth UI font loaded: {}", uiFontPath);
															}
															else
															{
																LOG_WARN(Render, "[Boot] Auth UI font upload failed: {}", uiFontPath);
															}
														}
														else
														{
															LOG_WARN(Render, "[Boot] Auth UI font file missing or empty: {}", uiFontPath);
														}
													}
													else if (!uiFontPath.empty() && ttfFragPtr == nullptr)
													{
														LOG_WARN(Render, "[Boot] auth_ttf.frag.spv missing — place compiled SPIR-V under game/data/shaders/");
													}
												}
												else
												{
													LOG_WARN(Render, "[Boot] AuthGlyphPass init failed");
												}
											}
											else
											{
												LOG_WARN(Render, "[Boot] AuthGlyphPass shaders missing");
											}
											std::vector<uint32_t> authLogoVert = loadSpirv("shaders/auth_logo.vert.spv");
											std::vector<uint32_t> authLogoFrag = loadSpirv("shaders/auth_logo.frag.spv");
											if (!authLogoVert.empty() && !authLogoFrag.empty())
											{
												if (m_authLogoPass.Init(
														m_vkDeviceContext.GetDevice(),
														m_vkSwapchain.GetImageFormat(),
														authLogoVert.data(),
														authLogoVert.size(),
														authLogoFrag.data(),
														authLogoFrag.size()))
												{
													LOG_INFO(Render, "[Boot] AuthLogoPass OK");
												}
												else
												{
													LOG_WARN(Render, "[Boot] AuthLogoPass init failed");
												}
											}
											else
											{
												LOG_WARN(Render, "[Boot] AuthLogoPass shaders missing");
											}
										}

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

										m_geometryMeshHandle = m_assetRegistry.LoadMesh("meshes/test.mesh");


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
													LOG_WARN(Render, "[GpuDrivenCulling] Draw-item upload failed");
													return;
												}

												const auto& hiZPass = m_pipeline->GetHiZPyramidPass();
												cullingPass.Record(
													m_vkDeviceContext.GetDevice(), cmd, rs.viewProjMatrix.m, m_currentFrame,
													hiZPass.GetImageView(m_currentFrame),
													hiZPass.GetExtent(),
													hiZPass.GetMipCount());
											});

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
													LOG_DEBUG(Render, "[LOD] Geometry test mesh lod={} dist_m={:.2f}", lodLevel, distCam);
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
											LOG_INFO(Render, "[Engine] Decal frame-graph pass registered");
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
											LOG_WARN(Render, "[Engine] Decal pass disabled, overlay clear fallback registered");
										}

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
												// Bootstrap the history on first use, even if Update() already cleared
												// m_taaHistoryInvalid for this frame.
												if (!m_taaHistoryEverFilled || m_taaHistoryInvalid)
												{
													VkImage srcImg = reg.getImage(m_fgSceneColorLDRId);
													if (srcImg != VK_NULL_HANDLE)
													{
														VkExtent2D ext = m_vkSwapchain.GetExtent();
														VkImageCopy region{};
														region.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
														region.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
														region.extent        = { ext.width, ext.height, 1 };

														// Helper: COLOR_ATTACHMENT_OPTIMAL -> TRANSFER_DST_OPTIMAL
													auto toTransferDst = [&](VkImage img) {
														VkImageMemoryBarrier bar{};
														bar.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
														bar.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
														bar.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
														bar.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
														bar.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
														bar.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
														bar.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
														bar.image = img;
														bar.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
														vkCmdPipelineBarrier(cmd,
															VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
															VK_PIPELINE_STAGE_TRANSFER_BIT,
															0, 0, nullptr, 0, nullptr, 1, &bar);
													};
													// Helper: TRANSFER_DST_OPTIMAL -> COLOR_ATTACHMENT_OPTIMAL
													auto toColorAttachment = [&](VkImage img) {
														VkImageMemoryBarrier bar{};
														bar.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
														bar.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
														bar.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
														bar.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
														bar.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
														bar.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
														bar.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
														bar.image = img;
														bar.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
														vkCmdPipelineBarrier(cmd,
															VK_PIPELINE_STAGE_TRANSFER_BIT,
															VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
															0, 0, nullptr, 0, nullptr, 1, &bar);
													};

													if (!m_taaHistoryEverFilled)
													{
														VkImage dstA = reg.getImage(m_fgHistoryAId);
														VkImage dstB = reg.getImage(m_fgHistoryBId);
														if (dstA != VK_NULL_HANDLE && dstB != VK_NULL_HANDLE)
														{
															toTransferDst(dstA);
															toTransferDst(dstB);
															vkCmdCopyImage(cmd, srcImg, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
															               dstA,   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
															               1, &region);
															vkCmdCopyImage(cmd, srcImg, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
															               dstB,   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
															               1, &region);
															toColorAttachment(dstA);
															toColorAttachment(dstB);
															m_taaHistoryEverFilled = true;
															m_taaHistoryInvalid = false;
															return;
														}
													}
													else
													{
														engine::render::ResourceId nextId = GetTaaHistoryNextId();
														VkImage dstNext = reg.getImage(nextId);
														if (dstNext != VK_NULL_HANDLE)
														{
															toTransferDst(dstNext);
															vkCmdCopyImage(cmd, srcImg, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
															               dstNext, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
															               1, &region);
															toColorAttachment(dstNext);
															m_taaHistoryInvalid = false;
															return;
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
												bool authPhotoBackdrop = false;
												const bool presentSolidColorDebug = m_cfg.GetBool("render.debug_present_solid_color.enabled", false);
												if (presentSolidColorDebug)
												{
													LOG_WARN(Render, "[CopyPresent] debug solid-color present enabled; skipping scene copy");
													const VkClearColorValue debugColor = { { 0.9f, 0.0f, 0.9f, 1.0f } };
													VkImageSubresourceRange clearRange{};
													clearRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
													clearRange.baseMipLevel = 0;
													clearRange.levelCount = 1;
													clearRange.baseArrayLayer = 0;
													clearRange.layerCount = 1;
													vkCmdClearColorImage(cmd, dstImg, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &debugColor, 1, &clearRange);
													LOG_INFO(Render, "[CopyPresent] debug clear color applied");
												}
												else
												{
													LOG_INFO(Render, "[CopyPresent] vkCmdCopyImage begin");
													// Use a direct copy for presentation. Some Intel/swapchain combinations are fragile
													// with vkCmdBlitImage here even when source and destination extents match.
													VkImageCopy region{};
													region.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
													region.srcOffset = { 0, 0, 0 };
													region.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
													region.dstOffset = { 0, 0, 0 };
													region.extent = { ext.width, ext.height, 1 };
													vkCmdCopyImage(cmd, srcImg, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dstImg, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
													LOG_INFO(Render, "[CopyPresent] vkCmdCopyImage done");
												}
												const engine::client::AuthUiPresenter::VisualState authVisualState = m_authUi.GetVisualState();
												const bool authBgBlitWanted = authVisualState.active && m_authUiBackgroundTexture.IsValid()
													&& m_cfg.GetBool("render.auth_ui.background_blit.enabled", true) && !presentSolidColorDebug;
												if (authBgBlitWanted)
												{
													engine::render::TextureAsset* bgTex = m_authUiBackgroundTexture.Get();
													if (bgTex && bgTex->image != VK_NULL_HANDLE && bgTex->width > 0u && bgTex->height > 0u)
													{
														const AuthBackgroundBlitFit authBgFit = ParseAuthBackgroundBlitFit(
															m_cfg.GetString("render.auth_ui.background_blit.fit", "cover_height"));
														static bool s_authBgBlitLogOnce = false;
														if (!s_authBgBlitLogOnce)
														{
															s_authBgBlitLogOnce = true;
															const char* fitName = "cover_height";
															switch (authBgFit)
															{
															case AuthBackgroundBlitFit::Stretch:
																fitName = "stretch";
																break;
															case AuthBackgroundBlitFit::Contain:
																fitName = "contain";
																break;
															case AuthBackgroundBlitFit::Cover:
																fitName = "cover";
																break;
															case AuthBackgroundBlitFit::CoverHeight:
															default:
																fitName = "cover_height";
																break;
															}
															LOG_INFO(Render,
																"[CopyPresent] auth fond: blit {}x{} → {}x{} fit={} (log unique; blit chaque frame)",
																bgTex->width,
																bgTex->height,
																ext.width,
																ext.height,
																fitName);
														}
														VkImageMemoryBarrier bgSrc{};
														bgSrc.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
														bgSrc.srcAccessMask = m_authUiBackgroundLayoutReady ? VK_ACCESS_TRANSFER_READ_BIT : 0;
														bgSrc.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
														bgSrc.oldLayout = m_authUiBackgroundLayoutReady ? VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL : VK_IMAGE_LAYOUT_PREINITIALIZED;
														bgSrc.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
														bgSrc.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
														bgSrc.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
														bgSrc.image = bgTex->image;
														bgSrc.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
														const VkPipelineStageFlags bgSrcStages = m_authUiBackgroundLayoutReady ? VK_PIPELINE_STAGE_TRANSFER_BIT : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
														vkCmdPipelineBarrier(cmd, bgSrcStages, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &bgSrc);
														VkImageBlit blit{};
														BuildAuthBackgroundBlit(authBgFit, bgTex->width, bgTex->height, ext.width, ext.height, blit);
														vkCmdBlitImage(cmd, bgTex->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dstImg, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);
														m_authUiBackgroundLayoutReady = true;
														authPhotoBackdrop = true;
													}
												}
												const bool authUiDynamicRenderingEnabled = m_cfg.GetBool("render.auth_ui_dynamic_rendering.enabled", true);
												const VkImageView backbufferView = reg.getImageView(m_fgBackbufferId);

												bool renderedAuthUi = false;
												if (authVisualState.active
													&& authUiDynamicRenderingEnabled
													&& backbufferView != VK_NULL_HANDLE
													&& m_vkDeviceContext.SupportsDynamicRendering())
												{
													LOG_INFO(Render, "[CopyPresent] auth overlay enabled; building model");
													const engine::client::AuthUiPresenter::RenderModel authRenderModel = m_authUi.BuildRenderModel();
													LOG_INFO(Render, "[CopyPresent] auth render model built; loading theme");
													const engine::render::AuthUiTheme authTheme = engine::render::LoadAuthUiTheme(m_cfg);
													LOG_INFO(Render, "[CopyPresent] auth theme loaded; issuing barriers");
													VkImageMemoryBarrier toColor{};
													toColor.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
													toColor.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
													toColor.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
													toColor.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
													toColor.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
													toColor.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
													toColor.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
													toColor.image = dstImg;
													toColor.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
													vkCmdPipelineBarrier(cmd,
														VK_PIPELINE_STAGE_TRANSFER_BIT,
														VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
														0, 0, nullptr, 0, nullptr, 1, &toColor);

													LOG_INFO(Render, "[CopyPresent] begin rendering attachment");
													// IMPORTANT: If we end up calling the KHR entrypoints, we must pass the KHR
													// structs/sType values (some drivers crash if you mix core structs with KHR fns).
													VkRenderingAttachmentInfo colorAttachment{};
													colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
													colorAttachment.imageView = backbufferView;
													colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
													colorAttachment.loadOp = authPhotoBackdrop ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_CLEAR;
													colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
													colorAttachment.clearValue.color = { { 0.0f, 0.0f, 0.0f, 0.0f } };

													VkRenderingAttachmentInfoKHR colorAttachmentKHR{};
													colorAttachmentKHR.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR;
													colorAttachmentKHR.imageView = backbufferView;
													colorAttachmentKHR.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
													colorAttachmentKHR.loadOp = authPhotoBackdrop ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_CLEAR;
													colorAttachmentKHR.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
													colorAttachmentKHR.clearValue.color = { { 0.0f, 0.0f, 0.0f, 0.0f } };
													LOG_INFO(Render, "[CopyPresent] attachment info ready (view={})", (void*)backbufferView);

													VkRenderingInfo renderingInfo{};
													renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
													renderingInfo.renderArea.offset = { 0, 0 };
													renderingInfo.renderArea.extent = ext;
													renderingInfo.layerCount = 1;
													renderingInfo.colorAttachmentCount = 1;
													renderingInfo.pColorAttachments = &colorAttachment;

													VkRenderingInfoKHR renderingInfoKHR{};
													renderingInfoKHR.sType = VK_STRUCTURE_TYPE_RENDERING_INFO_KHR;
													renderingInfoKHR.renderArea.offset = { 0, 0 };
													renderingInfoKHR.renderArea.extent = ext;
													renderingInfoKHR.layerCount = 1;
													renderingInfoKHR.colorAttachmentCount = 1;
													renderingInfoKHR.pColorAttachments = &colorAttachmentKHR;
													LOG_INFO(Render,
														"[CopyPresent] renderingInfo renderArea.extent width={} height={}",
														ext.width,
														ext.height);
													LOG_INFO(Render, "[CopyPresent] vkCmdBeginRendering call (proc lookup)");

													// Pointeurs résolus à la création du device (VkDeviceContext) — évite les nullptr
													// du loader et les incohérences avec une instance Vulkan < 1.3.
													const PFN_vkCmdBeginRendering pfnBeginCore = m_vkDeviceContext.GetCmdBeginRenderingCore();
													const PFN_vkCmdEndRendering pfnEndCoreStored = m_vkDeviceContext.GetCmdEndRenderingCore();
													const PFN_vkCmdBeginRenderingKHR pfnBeginKHR = m_vkDeviceContext.GetCmdBeginRenderingKHR();
													const PFN_vkCmdEndRenderingKHR pfnEndKHRStored = m_vkDeviceContext.GetCmdEndRenderingKHR();
													LOG_INFO(Render,
														"[CopyPresent] proc addresses (device ctx): beginCore={} endCore={} beginKHR={} endKHR={}",
														(void*)pfnBeginCore, (void*)pfnEndCoreStored, (void*)pfnBeginKHR, (void*)pfnEndKHRStored);

													bool didBeginRendering = false;
													bool beganWithKHR = false;
													PFN_vkCmdEndRendering pfnEndCore = nullptr;
													PFN_vkCmdEndRenderingKHR pfnEndKHR = nullptr;
													// Préférer KHR si les deux paires sont présentes (certains loaders / ICD).
													if (pfnBeginKHR && pfnEndKHRStored)
													{
														pfnBeginKHR(cmd, &renderingInfoKHR);
														didBeginRendering = true;
														beganWithKHR = true;
														pfnEndKHR = pfnEndKHRStored;
													}
													else if (pfnBeginCore && pfnEndCoreStored)
													{
														pfnBeginCore(cmd, &renderingInfo);
														didBeginRendering = true;
														beganWithKHR = false;
														pfnEndCore = pfnEndCoreStored;
													}
													else
													{
														LOG_ERROR(Render, "[CopyPresent] dynamic rendering entrypoints not found; skipping auth UI overlay");
													}

													if (!didBeginRendering)
													{
														// No active rendering: do not clear attachments / do not end rendering.
													}
													else
													{
														LOG_INFO(Render, "[CopyPresent] vkCmdBeginRendering returned");

														LOG_INFO(Render, "[CopyPresent] building UI layers");
														const bool authCalibOverlay = m_cfg.GetBool("render.auth_ui_calibration_overlay.enabled", false);
														const std::vector<engine::render::AuthUiLayer> layers =
															engine::render::BuildAuthUiLayers(ext, authVisualState, authRenderModel, authTheme, authCalibOverlay, authPhotoBackdrop);
														LOG_INFO(Render, "[CopyPresent] UI layers built; clearing attachments");
														for (const engine::render::AuthUiLayer& layer : layers)
														{
															VkClearAttachment clearAttachment{};
															clearAttachment.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
															clearAttachment.colorAttachment = 0;
															clearAttachment.clearValue.color = layer.color;
															vkCmdClearAttachments(cmd, 1, &clearAttachment, 1, &layer.rect);
														}
														LOG_INFO(Render, "[CopyPresent] UI layers cleared; recording glyphs (if valid)");
														// Dessiner le logo AVANT le texte pour éviter qu’un PNG opaque ne recouvre les glyphes.
														const bool showAuthStatusLogo = authVisualState.login
															&& (authVisualState.authLogoSpin || authVisualState.authStatusKnown);
														if (m_authLogoPass.IsValid() && showAuthStatusLogo)
														{
															engine::render::TextureAsset* logoTex = nullptr;
															if (authVisualState.authLogoSpin && m_authLogoTexture.IsValid())
															{
																logoTex = m_authLogoTexture.Get();
															}
															else if (authVisualState.authStatusOk && m_authLogoSuccessTexture.IsValid())
															{
																logoTex = m_authLogoSuccessTexture.Get();
															}
															else if (!authVisualState.authStatusOk && m_authLogoErrorTexture.IsValid())
															{
																logoTex = m_authLogoErrorTexture.Get();
															}
															else if (m_authLogoTexture.IsValid())
															{
																logoTex = m_authLogoTexture.Get();
															}
															if (logoTex && logoTex->image != VK_NULL_HANDLE && logoTex->view != VK_NULL_HANDLE)
															{
																const float half = static_cast<float>(m_authUi.GetAuthLogoSizePx()) * 0.5f;
																const float cx = 24.f + half;
																// Ajustement repère vertical : le shader du logo attend un centre "haut-gauche"
																// alors que le rendu actuel le place en bas-gauche.
																const float cy = static_cast<float>(ext.height) - (24.f + half);
																constexpr float kAuthLogoOrientRad = 3.14159265f;
																const float spin = authVisualState.authLogoSpin ? m_authUi.GetAuthLogoRotationRadians() : 0.f;
																m_authLogoPass.Record(
																	m_vkDeviceContext.GetDevice(),
																	cmd,
																	ext,
																	logoTex->image,
																	logoTex->view,
																	m_authLogoImageLayoutReady,
																	cx,
																	cy,
																	half,
																	spin + kAuthLogoOrientRad);
															}
														}
														if (m_authGlyphPass.IsValid())
														{
															m_authGlyphPass.RecordModel(
																m_vkDeviceContext.GetDevice(),
																cmd,
																ext,
																authVisualState,
																authRenderModel,
																authTheme);
														}

														if (beganWithKHR && pfnEndKHR)
															pfnEndKHR(cmd);
														else if (!beganWithKHR && pfnEndCore)
															pfnEndCore(cmd);
														LOG_INFO(Render, "[CopyPresent] vkCmdEndRendering done; barrier to present");

														VkImageMemoryBarrier toPresent{};
														toPresent.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
														toPresent.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
														toPresent.dstAccessMask = 0;
														toPresent.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
														toPresent.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
														toPresent.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
														toPresent.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
														toPresent.image = dstImg;
														toPresent.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
														vkCmdPipelineBarrier(cmd,
															VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
															VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
															0, 0, nullptr, 0, nullptr, 1, &toPresent);
														renderedAuthUi = true;
													}
												}

												if (!renderedAuthUi)
												{
													if (authVisualState.active && !authUiDynamicRenderingEnabled)
														LOG_WARN(Render, "[CopyPresent] auth dynamic rendering disabled by config; using present-only path");
													if (authVisualState.active && backbufferView == VK_NULL_HANDLE)
														LOG_WARN(Render, "[CopyPresent] backbuffer imageView is null; skipping auth UI overlay");

													VkImageMemoryBarrier barrier{};
													barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
													barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
													barrier.dstAccessMask = 0;
													barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
													barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
													barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
													barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
													barrier.image = dstImg;
													barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
													vkCmdPipelineBarrier(cmd,
														VK_PIPELINE_STAGE_TRANSFER_BIT,
														VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
														0, 0, nullptr, 0, nullptr, 1, &barrier);
												}
											});

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
		{
			const auto cfgText = engine::platform::FileSystem::ReadAllText("config.json");
			LOG_INFO(Platform, "FS ReadAllText('config.json'): {} bytes", cfgText.size());
			const auto contentCfgText = engine::platform::FileSystem::ReadAllTextContent(m_cfg, "config.json");
			LOG_INFO(Platform, "FS ReadAllTextContent(paths.content/'config.json'): {} bytes", contentCfgText.size());
		}

		LOG_INFO(Core, "Engine init: vsync={} (present mode from swapchain)", m_vsync ? "on" : "off");
		LOG_INFO(Core, "[Boot] Engine boot COMPLETE");
	}

	Engine::~Engine() = default;

	int Engine::Run()
	{
		LOG_DEBUG(Core, "[Engine] Entering render loop");

		auto lastFpsLog  = std::chrono::steady_clock::now();
		auto lastPresent = lastFpsLog;

		while (!m_quitRequested && !m_window.ShouldClose())
		{
			BeginFrame();
			Update();
			SwapRenderState();
			//			
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

		LOG_INFO(Core, "[Engine] Render loop exited cleanly");

		if (m_vkDeviceContext.IsValid())
		{
			vkDeviceWaitIdle(m_vkDeviceContext.GetDevice());
			m_authGlyphPass.Destroy(m_vkDeviceContext.GetDevice());
			m_authLogoPass.Destroy(m_vkDeviceContext.GetDevice());
			if (m_pipeline)
			{
				m_pipeline->Destroy(m_vkDeviceContext.GetDevice());
				m_pipeline.reset();
			}
			m_profilerHud.Shutdown();
			m_profiler.Shutdown(m_vkDeviceContext.GetDevice());
			m_audioEngine.Shutdown();
			m_decalSystem.Shutdown();
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

		if (m_editorMode)
		{
			m_editorMode->Shutdown(m_window);
			m_editorMode.reset();
		}
		ShutdownGameplayNet();
		m_authUi.Shutdown();
		m_chatUi.Shutdown();
		m_window.Destroy();
		LOG_INFO(Core, "[Engine] Shutdown complete");
		return 0;
	}

	void Engine::BeginFrame()
	{
		// PROFILE_FUNCTION();
		m_input.BeginFrame();
		m_window.PollEvents();

		if (m_authUi.IsInitialized() && !m_authUi.IsFlowComplete())
		{
			if (m_input.WasPressed(engine::platform::Key::Escape))
			{
				if (!m_authUi.OnEscape())
					OnQuit();
			}
		}
		else if (m_chatUi.IsInitialized() && m_chatUi.IsChatFocusActive())
		{
			if (m_input.WasPressed(engine::platform::Key::Escape))
			{
				m_chatUi.SetChatFocus(false);
			}
		}
		else if (m_gameplayNetInitialized && m_uiModelBinding.GetModel().auction.isOpen
			&& m_input.WasPressed(engine::platform::Key::Escape))
		{
			(void)m_uiModelBinding.CloseAuction();
			m_invUi.CancelDrag();
			m_pendingSellActive = false;
			LOG_INFO(Core, "[GameplayNet] Auction closed (Escape)");
		}
		else if (m_gameplayNetInitialized && m_uiModelBinding.GetModel().shop.isOpen
			&& m_input.WasPressed(engine::platform::Key::Escape))
		{
			(void)m_uiModelBinding.CloseShop();
			m_invUi.CancelDrag();
			m_pendingSellActive = false;
			LOG_INFO(Core, "[GameplayNet] Shop closed (Escape)");
		}
		else if (!m_editorEnabled && m_input.WasPressed(engine::platform::Key::Escape))
		{
			OnQuit();
		}

		if (m_input.WasPressed(engine::platform::Key::F_11))
    		m_window.ToggleFullscreen();

		m_shaderHotReload.Poll(m_cfg);
		m_shaderHotReload.ApplyPending(m_shaderCache);

		if (m_swapchainResizeRequested)
		{
    		LOG_INFO(Platform, "[Resize] Swapchain recreate requested");

			m_swapchainResizeRequested = false;
			if (m_vkDeviceContext.IsValid() && m_vkSwapchain.IsValid() && m_width > 0 && m_height > 0)
			{
				vkDeviceWaitIdle(m_vkDeviceContext.GetDevice());
				if (m_pipeline)
					m_pipeline->InvalidateFramebufferCaches(m_vkDeviceContext.GetDevice());

				m_frameGraph.destroy(m_vkDeviceContext.GetDevice(), m_vmaAllocator);
				// All frame-graph images are recreated after a resize/out-of-date event, so the
				// TAA history must be rebuilt from scratch on the next frame.
				m_taaHistoryInvalid = true;
				m_taaHistoryEverFilled = false;

				bool ok = m_vkSwapchain.Recreate(static_cast<uint32_t>(m_width), static_cast<uint32_t>(m_height));
				if (ok)
				{
					m_suboptimalStreak = 0;
					m_suboptimalWidth = m_width;
					m_suboptimalHeight = m_height;
					LOG_INFO(Platform, "[Resize] Swapchain recreated OK");
				}
				else
					LOG_WARN(Platform, "[Resize] Swapchain recreate FAILED");
			}
			else
			{
				LOG_WARN(Platform, "[Resize] Swapchain recreate skipped — device/swapchain not ready or invalid size");
			}
		}

		m_time.BeginFrame();
		if (m_profiler.IsInitialized())
		{
			m_profiler.BeginFrame(m_currentFrame);
		}
		m_frameArena.BeginFrame(m_time.FrameIndex());
		m_chunkStats.ResetPerFrame();
		PumpGameplayPackets();
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
		const bool invertY = m_cfg.GetBool("controls.invert_y", false);
		const engine::render::MovementLayout movementLayout =
			(m_cfg.GetString("controls.movement_layout", "wasd") == "zqsd")
			? engine::render::MovementLayout::ZQSD
			: engine::render::MovementLayout::WASD;

		out.camera = readState.camera;
		out.profilerDebugText = m_profilerHud.IsInitialized() ? m_profilerHud.GetState().debugText : std::string{};
		out.chatDebugText = m_chatUi.IsInitialized() ? m_chatUi.BuildPanelText() : std::string{};
		out.authHudText.clear();
		out.gameplayHudDebugText.clear();
		if (m_gameplayNetInitialized)
		{
			out.gameplayHudDebugText += m_shopUi.GetState().debugText;
			out.gameplayHudDebugText += '\n';
			out.gameplayHudDebugText += m_auctionUi.GetState().debugText;
			out.gameplayHudDebugText += '\n';
			out.gameplayHudDebugText += m_invUi.GetState().debugText;
			if (m_pendingSellActive)
			{
				out.gameplayHudDebugText += "\n[PENDING SELL] vendor=";
				out.gameplayHudDebugText += std::to_string(m_pendingSellVendorId);
				out.gameplayHudDebugText += " item=";
				out.gameplayHudDebugText += std::to_string(m_pendingSellItemId);
				out.gameplayHudDebugText += " qty=";
				out.gameplayHudDebugText += std::to_string(m_pendingSellQty);
				out.gameplayHudDebugText += " unit_gold=";
				out.gameplayHudDebugText += std::to_string(m_pendingSellUnitGold);
				out.gameplayHudDebugText += "  -> Y confirm / N cancel\n";
			}
		}

		const bool authGateActive = m_authUi.IsInitialized() && !m_authUi.IsFlowComplete();
		if (authGateActive)
		{
			// DIAG ENG-UPD-PRE
			LOG_WARN(Core, "[Engine] ENG-UPD-PRE calling authUi.Update frame={}", m_currentFrame);
			m_authUi.Update(m_input, static_cast<float>(dt), m_window, m_cfg);
			// DIAG ENG-UPD-POST
			LOG_WARN(Core, "[Engine] ENG-UPD-POST authUi.Update returned frame={}", m_currentFrame);
			const engine::client::AuthUiPresenter::VideoSettingsCommand videoCmd = m_authUi.ConsumePendingVideoSettings();
			const engine::client::AuthUiPresenter::AudioSettingsCommand audioCmd = m_authUi.ConsumePendingAudioSettings();
			const engine::client::AuthUiPresenter::ControlSettingsCommand controlCmd = m_authUi.ConsumePendingControlSettings();
			const engine::client::AuthUiPresenter::GameSettingsCommand gameCmd = m_authUi.ConsumePendingGameSettings();
			if (videoCmd.applyRequested)
			{
				const bool fullscreenChanged = (videoCmd.fullscreen != m_window.IsFullscreen());
				const bool vsyncChanged = (videoCmd.vsync != m_vsync);
				m_cfg.SetValue("render.fullscreen", videoCmd.fullscreen);
				m_cfg.SetValue("render.vsync", videoCmd.vsync);
				m_vsync = videoCmd.vsync;
				if (fullscreenChanged)
				{
					m_window.ToggleFullscreen();
					LOG_INFO(Core, "[Options] Fullscreen applied ({})", videoCmd.fullscreen ? "on" : "off");
				}
				if (vsyncChanged)
				{
					m_swapchainResizeRequested = true;
					LOG_INFO(Core, "[Options] VSync applied ({}) -> swapchain recreate requested", videoCmd.vsync ? "on" : "off");
				}
				if (!fullscreenChanged && !vsyncChanged)
				{
					LOG_INFO(Core, "[Options] Video apply requested but values unchanged");
				}
			}
			if (audioCmd.applyRequested)
			{
				m_cfg.SetValue("audio.master_volume", static_cast<double>(audioCmd.masterVolume));
				m_cfg.SetValue("audio.music_volume", static_cast<double>(audioCmd.musicVolume));
				m_cfg.SetValue("audio.sfx_volume", static_cast<double>(audioCmd.sfxVolume));
				m_cfg.SetValue("audio.ui_volume", static_cast<double>(audioCmd.uiVolume));
				const bool masterOk = m_audioEngine.SetMasterVolume(audioCmd.masterVolume);
				const bool musicOk = m_audioEngine.SetBusVolume("Music", audioCmd.musicVolume);
				const bool sfxOk = m_audioEngine.SetBusVolume("SFX", audioCmd.sfxVolume);
				const bool uiOk = m_audioEngine.SetBusVolume("UI", audioCmd.uiVolume);
				LOG_INFO(Core, "[Options] Audio applied (master={:.1f}, music={:.1f}, sfx={:.1f}, ui={:.1f}, ok={})",
					audioCmd.masterVolume, audioCmd.musicVolume, audioCmd.sfxVolume, audioCmd.uiVolume,
					(masterOk && musicOk && sfxOk && uiOk) ? "yes" : "partial");
			}
			if (controlCmd.applyRequested)
			{
				m_cfg.SetValue("camera.mouse_sensitivity", static_cast<double>(controlCmd.mouseSensitivity));
				m_cfg.SetValue("controls.invert_y", controlCmd.invertY);
				m_cfg.SetValue("controls.movement_layout", controlCmd.useZqsd ? std::string("zqsd") : std::string("wasd"));
				LOG_INFO(Core, "[Options] Controls applied (sens={:.4f}, invert_y={}, layout={})",
					controlCmd.mouseSensitivity, controlCmd.invertY, controlCmd.useZqsd ? "zqsd" : "wasd");
			}
			if (gameCmd.applyRequested)
			{
				const bool gameplayWasEnabled = m_cfg.GetBool("client.gameplay_udp.enabled", false);
				m_cfg.SetValue("client.gameplay_udp.enabled", gameCmd.gameplayUdpEnabled);
				m_cfg.SetValue("client.allow_insecure_dev", gameCmd.allowInsecureDev);
				m_cfg.SetValue("client.auth_ui.timeout_ms", static_cast<int64_t>(gameCmd.authTimeoutMs));
				if (gameplayWasEnabled != gameCmd.gameplayUdpEnabled)
				{
					if (gameCmd.gameplayUdpEnabled)
						InitGameplayNet();
					else
						ShutdownGameplayNet();
				}
				LOG_INFO(Core, "[Options] Game applied (gameplay_udp={}, allow_insecure_dev={}, timeout_ms={})",
					gameCmd.gameplayUdpEnabled, gameCmd.allowInsecureDev, gameCmd.authTimeoutMs);
			}
			if (m_chatUi.IsInitialized())
			{
				m_chatUi.Update(m_input, static_cast<float>(dt));
			}
			out.authHudText = m_authUi.BuildPanelText();
			out.chatDebugText.clear();
			const bool authUiDynamicRenderingEnabled = m_cfg.GetBool("render.auth_ui_dynamic_rendering.enabled", true);
			if (m_authUi.GetVisualState().active
				&& authUiDynamicRenderingEnabled
				&& m_vkDeviceContext.SupportsDynamicRendering())
				m_window.SetOverlayText({});
			else
				m_window.SetOverlayText(out.authHudText);
		}
		else
		{
			m_window.SetOverlayText({});
		}

		if (!m_editorEnabled)
		{
			if (!authGateActive && !m_chatUi.IsChatFocusActive())
			{
				m_fpsCameraController.Update(m_input, dt, mouseSensitivity, invertY, movementLayout, out.camera);
			}

			if (!authGateActive && m_chatUi.IsInitialized())
			{
				m_chatUi.Update(m_input, static_cast<float>(dt));
			}
		}

		if (m_gameplayNetInitialized)
		{
			UpdateGameplayNet(static_cast<float>(dt));
		}
		m_world.Update(out.camera.position);

		// On aligne l'aspect sur la taille réelle de la swapchain, pas sur le size "client" du window.
		// Sinon on obtient des barres noires / RT non alignés après resize/DPI.
		if (m_vkSwapchain.IsValid())
		{
			const VkExtent2D ext = m_vkSwapchain.GetExtent();
			if (ext.width > 0 && ext.height > 0)
				out.camera.aspect = static_cast<float>(ext.width) / static_cast<float>(ext.height);
		}
		else if (m_width > 0 && m_height > 0)
		{
			out.camera.aspect = static_cast<float>(m_width) / static_cast<float>(m_height);
		}

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
				LOG_DEBUG(World, "M09.5 {}", out.hlodDebugText);
			if ((m_currentFrame % 60) == 0 && !out.profilerDebugText.empty())
				LOG_DEBUG(Core, "M18.1 {}", out.profilerDebugText);
			if ((m_currentFrame % 60) == 0 && !out.chatDebugText.empty())
				LOG_DEBUG(Core, "M29.1 {}", out.chatDebugText);
			if ((m_currentFrame % 60) == 0 && !out.gameplayHudDebugText.empty())
				LOG_DEBUG(Core, "M35.2 {}", out.gameplayHudDebugText);
			if ((m_currentFrame % 60) == 0 && !out.authHudText.empty())
				LOG_INFO(Core, "[AuthUi] {}", out.authHudText);
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
	    if (!m_vkDeviceContext.IsValid() || !m_vkSwapchain.IsValid() || m_frameResources[0].cmdPool == VK_NULL_HANDLE)
	    {
	        LOG_WARN(Render, "[Engine] Render early return: device/swapchain not ready frame={}", m_currentFrame);
	        return;
	    }
	    LOG_INFO(Render, "[Engine] Render begin frame={}", m_currentFrame);
	    const uint32_t frameIndex          = m_currentFrame % 2;
	    engine::render::FrameResources& fr = m_frameResources[frameIndex];
	    ::VkDevice     device              = m_vkDeviceContext.GetDevice();
	    VkQueue        graphicsQueue       = m_vkDeviceContext.GetGraphicsQueue();
	    VkQueue        presentQueue        = m_vkDeviceContext.GetPresentQueue();
	    VkSwapchainKHR swapchain           = m_vkSwapchain.GetSwapchain();
	    // Utiliser l'extent réel de la swapchain pour que le FrameGraph alloue/recrée
	    // ses rendertargets avec les bonnes dimensions.
	    VkExtent2D extent = m_vkSwapchain.GetExtent();
	
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
	    m_deferredDestroyQueue.Collect(device, m_currentFrame > 0 ? m_currentFrame - 1 : 0);
	    m_stagingAllocator.BeginFrame(frameIndex);
	    (void)m_gpuUploadQueue.PlanFrameUploads();
	
	    if (m_pipeline->GetAutoExposure().IsValid())
	    {
	        const float dt    = static_cast<float>(m_time.DeltaSeconds());
	        const float key   = static_cast<float>(m_cfg.GetDouble("exposure.key", 0.18));
	        const float speed = static_cast<float>(m_cfg.GetDouble("exposure.speed", 2.0));
	        m_pipeline->GetAutoExposure().Update(device, dt, key, speed, frameIndex);
	    }
	
	    auto handleSuboptimal = [this](const char* phase)
	    {
	        int clientW = 0;
	        int clientH = 0;
	        m_window.GetClientSize(clientW, clientH);
	        clientW = std::max(1, clientW);
	        clientH = std::max(1, clientH);
	        if (clientW != m_width || clientH != m_height)
	        {
	            LOG_INFO(Render, "[SWAPCHAIN] {} returned SUBOPTIMAL and window client size changed {}x{} -> {}x{}",
	                phase, m_width, m_height, clientW, clientH);
	            m_width = clientW;
	            m_height = clientH;
	        }
	        const bool needsResize = m_vkSwapchain.NeedsRecreateForSurfaceExtent(
	            static_cast<uint32_t>(clientW),
	            static_cast<uint32_t>(clientH));
	        const bool degenerateSurfaceExtent = m_vkSwapchain.HasDegenerateSurfaceExtent(
	            static_cast<uint32_t>(clientW),
	            static_cast<uint32_t>(clientH));
	        if (m_suboptimalWidth == clientW && m_suboptimalHeight == clientH)
	        {
	            ++m_suboptimalStreak;
	        }
	        else
	        {
	            m_suboptimalWidth = clientW;
	            m_suboptimalHeight = clientH;
	            m_suboptimalStreak = 1;
	        }
	        if (needsResize)
	        {
	            LOG_INFO(Render, "[SWAPCHAIN] {} returned SUBOPTIMAL with extent mismatch -> recreate requested (client={}x{}, streak={})",
	                phase, clientW, clientH, m_suboptimalStreak);
	            m_swapchainResizeRequested = true;
	            return;
	        }
	        if (degenerateSurfaceExtent)
	        {
	            LOG_INFO(Render, "[SWAPCHAIN] {} returned SUBOPTIMAL with degenerate surface caps -> keep current swapchain (client={}x{}, streak={})",
	                phase, clientW, clientH, m_suboptimalStreak);
	            return;
	        }
	        LOG_INFO(Render, "[SWAPCHAIN] {} returned SUBOPTIMAL but extent is unchanged -> keep current swapchain (client={}x{}, streak={})",
	            phase, clientW, clientH, m_suboptimalStreak);
	    };

	    uint32_t imageIndex = 0;
	    LOG_INFO(Render, "[Engine] Render vkAcquireNextImageKHR begin frame={}", m_currentFrame);
	    VkResult result = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, fr.imageAvailable, VK_NULL_HANDLE, &imageIndex);
	    if (result == VK_ERROR_OUT_OF_DATE_KHR) { m_swapchainResizeRequested = true; LOG_WARN(Render, "[Engine] Render vkAcquireNextImageKHR OUT_OF_DATE frame={}", m_currentFrame); return; }
	    if (result == VK_SUBOPTIMAL_KHR)
	    {
	        handleSuboptimal("Acquire");
	    }
	    else if (result != VK_SUCCESS)
	    {
	        LOG_WARN(Render, "[Engine] Render vkAcquireNextImageKHR failed result={} frame={}", static_cast<int>(result), m_currentFrame);
	        return;
	    }
	    else
	    {
	        m_suboptimalStreak = 0;
	    }
	    LOG_INFO(Render, "[Engine] Render vkAcquireNextImageKHR OK imageIndex={} frame={}", imageIndex, m_currentFrame);

	    vkResetCommandPool(device, fr.cmdPool, 0);
	
	    VkCommandBufferBeginInfo beginInfo{};
	    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	    if (vkBeginCommandBuffer(fr.cmdBuffer, &beginInfo) != VK_SUCCESS) return;
	    if (m_profiler.IsInitialized())
	    {
	        m_profiler.BeginGpuFrame(fr.cmdBuffer, frameIndex);
	    }
	
	    if (m_fgSceneColorHDRId != engine::render::kInvalidResourceId && m_fgBackbufferId != engine::render::kInvalidResourceId)
	    {
	        VkImage     backbufferImage = m_vkSwapchain.GetImage(imageIndex);
	        VkImageView backbufferView  = m_vkSwapchain.GetImageView(imageIndex);
	        m_fgRegistry.bindImage(m_fgBackbufferId, backbufferImage, backbufferView);
	        m_frameGraph.execute(m_vkDeviceContext.GetDevice(), m_vkDeviceContext.GetPhysicalDevice(), m_vmaAllocator, fr.cmdBuffer, m_fgRegistry, frameIndex, extent, 2u, m_vkDeviceContext.SupportsSynchronization2(), m_profiler.IsInitialized() ? &m_profiler : nullptr);
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
	    VkResult submitResult = vkQueueSubmit(graphicsQueue, 1, &submitInfo, fr.fence);
	    if (submitResult != VK_SUCCESS) return;

	    VkPresentInfoKHR presentInfo{};
	    presentInfo.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	    presentInfo.waitSemaphoreCount = 1;
	    presentInfo.pWaitSemaphores    = signalSemaphores;
	    presentInfo.swapchainCount     = 1;
	    presentInfo.pSwapchains        = &swapchain;
	    presentInfo.pImageIndices      = &imageIndex;
	    result = vkQueuePresentKHR(presentQueue, &presentInfo);
	    if (result == VK_ERROR_OUT_OF_DATE_KHR)
	    {
	        m_swapchainResizeRequested = true;
	    }
	    else if (result == VK_SUBOPTIMAL_KHR)
	    {
	        handleSuboptimal("Present");
	    }
	    else
	    {
	        m_suboptimalStreak = 0;
	    }
	
	    m_currentFrame++;
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
    	LOG_INFO(Platform, "[Resize] OnResize");
		m_width  = w;
		m_height = h;
		m_suboptimalStreak = 0;
		m_suboptimalWidth = w;
		m_suboptimalHeight = h;
		m_taaHistoryInvalid        = true;
		m_swapchainResizeRequested = true;
		if (m_chatUi.IsInitialized())
		{
			(void)m_chatUi.SetViewportSize(static_cast<uint32_t>(std::max(1, w)), static_cast<uint32_t>(std::max(1, h)));
		}
		if (m_authUi.IsInitialized())
		{
			(void)m_authUi.SetViewportSize(static_cast<uint32_t>(std::max(1, w)), static_cast<uint32_t>(std::max(1, h)));
		}
		if (m_gameplayNetInitialized)
		{
			(void)m_shopUi.SetViewportSize(static_cast<uint32_t>(std::max(1, w)), static_cast<uint32_t>(std::max(1, h)));
			(void)m_auctionUi.SetViewportSize(static_cast<uint32_t>(std::max(1, w)), static_cast<uint32_t>(std::max(1, h)));
			(void)m_invUi.SetViewportSize(static_cast<uint32_t>(std::max(1, w)), static_cast<uint32_t>(std::max(1, h)));
			const engine::client::UIModel& mdl = m_uiModelBinding.GetModel();
			(void)m_shopUi.ApplyModel(mdl, engine::client::UIModelChangeShop);
			(void)m_auctionUi.ApplyModel(mdl, engine::client::UIModelChangeAuction);
			(void)m_invUi.ApplyModel(mdl, engine::client::UIModelChangeInventory);
		}
	}

	void Engine::OnQuit()
	{
		m_quitRequested = true;
	}

	void Engine::WatchShader(std::string_view relativePath, engine::render::ShaderStage stage, std::string_view defines)
	{
		m_shaderHotReload.Watch(relativePath, stage, defines);
	}

	void Engine::InitGameplayNet()
	{
		if (!m_cfg.GetBool("client.gameplay_udp.enabled", false))
		{
			LOG_INFO(Core, "[GameplayNet] Disabled (set client.gameplay_udp.enabled=true with shard server running)");
			return;
		}

		if (!m_uiModelBinding.Init())
		{
			LOG_ERROR(Core, "[GameplayNet] Init FAILED: UIModelBinding");
			return;
		}

		if (!m_shopUi.Init())
		{
			LOG_ERROR(Core, "[GameplayNet] Init FAILED: ShopUiPresenter");
			m_uiModelBinding.Shutdown();
			return;
		}

		if (!m_auctionUi.Init())
		{
			LOG_ERROR(Core, "[GameplayNet] Init FAILED: AuctionUiPresenter");
			m_shopUi.Shutdown();
			m_uiModelBinding.Shutdown();
			return;
		}

		if (!m_invUi.Init(m_cfg))
		{
			LOG_ERROR(Core, "[GameplayNet] Init FAILED: InventoryUiPresenter");
			m_auctionUi.Shutdown();
			m_shopUi.Shutdown();
			m_uiModelBinding.Shutdown();
			return;
		}

		const uint32_t vw = static_cast<uint32_t>(std::max(1, m_width));
		const uint32_t vh = static_cast<uint32_t>(std::max(1, m_height));
		if (!m_shopUi.SetViewportSize(vw, vh))
		{
			LOG_WARN(Core, "[GameplayNet] ShopUiPresenter viewport FAILED — using fallback layout");
		}
		if (!m_auctionUi.SetViewportSize(vw, vh))
		{
			LOG_WARN(Core, "[GameplayNet] AuctionUiPresenter viewport FAILED — using fallback layout");
		}
		if (!m_invUi.SetViewportSize(vw, vh))
		{
			LOG_WARN(Core, "[GameplayNet] InventoryUiPresenter viewport FAILED — using fallback layout");
		}

		m_uiObserverHandle = m_uiModelBinding.AddObserver(
			[this](const engine::client::UIModel& model, uint32_t changeMask)
			{
				(void)m_shopUi.ApplyModel(model, changeMask);
				(void)m_auctionUi.ApplyModel(model, changeMask);
				(void)m_invUi.ApplyModel(model, changeMask);
			});
		if (m_uiObserverHandle == 0u)
		{
			LOG_ERROR(Core, "[GameplayNet] Init FAILED: UI observer not registered");
			m_invUi.Shutdown();
			m_auctionUi.Shutdown();
			m_shopUi.Shutdown();
			m_uiModelBinding.Shutdown();
			return;
		}

		const std::string host = m_cfg.GetString("client.gameplay_udp.host", "127.0.0.1");
		const int64_t portCfg = m_cfg.GetInt("client.gameplay_udp.port", 27015);
		const uint16_t port = static_cast<uint16_t>(
			std::clamp(portCfg, static_cast<int64_t>(1), static_cast<int64_t>(65535)));
		m_gameplayVendorTalkTarget = m_cfg.GetString("client.gameplay_udp.vendor_talk_target", "vendor:1");
		m_gameplayAuctionTalkTarget = m_cfg.GetString("client.gameplay_udp.auction_talk_target", "auction");
		if (!m_gameplayUdp.Init(host, port))
		{
			LOG_ERROR(Core, "[GameplayNet] Init FAILED: UDP connect {}", host);
			(void)m_uiModelBinding.RemoveObserver(m_uiObserverHandle);
			m_uiObserverHandle = 0;
			m_invUi.Shutdown();
			m_auctionUi.Shutdown();
			m_shopUi.Shutdown();
			m_uiModelBinding.Shutdown();
			return;
		}

		const int64_t tickHzCfg = m_cfg.GetInt("client.gameplay_udp.request_tick_hz", 20);
		const uint16_t reqTick = static_cast<uint16_t>(
			std::clamp(tickHzCfg, static_cast<int64_t>(1), static_cast<int64_t>(120)));
		const int64_t snapHzCfg = m_cfg.GetInt("client.gameplay_udp.request_snapshot_hz", 10);
		const uint16_t reqSnap = static_cast<uint16_t>(
			std::clamp(snapHzCfg, static_cast<int64_t>(1), static_cast<int64_t>(60)));
		const int64_t charKeyCfg = m_cfg.GetInt("client.gameplay_udp.character_key", 1);
		const uint32_t charKey =
			static_cast<uint32_t>(std::max(static_cast<int64_t>(1), charKeyCfg));
		(void)m_gameplayUdp.SendHello(reqTick, reqSnap, charKey);

		m_gameplayNetInitialized = true;
		LOG_INFO(Core,
			"[GameplayNet] Init OK (host={}, port={}, vendor_target='{}', auction_target='{}')",
			host,
			port,
			m_gameplayVendorTalkTarget,
			m_gameplayAuctionTalkTarget);
	}

	void Engine::ShutdownGameplayNet()
	{
		if (!m_gameplayNetInitialized)
		{
			return;
		}

		if (m_uiObserverHandle != 0u)
		{
			(void)m_uiModelBinding.RemoveObserver(m_uiObserverHandle);
			m_uiObserverHandle = 0;
		}

		m_invUi.Shutdown();
		m_auctionUi.Shutdown();
		m_shopUi.Shutdown();
		m_uiModelBinding.Shutdown();
		m_gameplayUdp.Shutdown();
		m_gameplayNetInitialized = false;
		m_pendingSellActive = false;
		m_pendingSellVendorId = 0;
		m_pendingSellItemId = 0;
		m_pendingSellQty = 0;
		m_pendingSellUnitGold = 0;
		m_gameplayVendorTalkTarget.clear();
		m_gameplayAuctionTalkTarget.clear();
		LOG_INFO(Core, "[GameplayNet] Shutdown complete");
	}

	void Engine::PumpGameplayPackets()
	{
		if (!m_gameplayNetInitialized || !m_gameplayUdp.IsActive())
		{
			return;
		}

		std::vector<std::vector<std::byte>> packets = m_gameplayUdp.PollIncoming();
		for (std::vector<std::byte>& packet : packets)
		{
			engine::server::MessageKind kind{};
			if (engine::server::PeekMessageKind(packet, kind) && kind == engine::server::MessageKind::Welcome)
			{
				continue;
			}
			(void)m_uiModelBinding.ApplyPacket(packet);
		}
	}

	void Engine::UpdateGameplayNet(float deltaSeconds)
	{
		(void)deltaSeconds;
		if (!m_gameplayNetInitialized || m_editorEnabled)
		{
			return;
		}

		const uint32_t clientId = m_gameplayUdp.ServerClientId();
		if (clientId == 0u)
		{
			return;
		}

		const float mx = static_cast<float>(m_input.MouseX());
		const float my = static_cast<float>(m_input.MouseY());
		(void)m_invUi.UpdateHover(mx, my);

		const engine::client::UIModel& ui = m_uiModelBinding.GetModel();
		const bool chatBlocks = m_chatUi.IsInitialized() && m_chatUi.IsChatFocusActive();

		if (m_pendingSellActive && !chatBlocks)
		{
			if (m_input.WasPressed(engine::platform::Key::Y))
			{
				(void)m_gameplayUdp.SendShopSellRequest(
					clientId,
					m_pendingSellVendorId,
					m_pendingSellItemId,
					m_pendingSellQty);
				m_pendingSellActive = false;
				LOG_INFO(Core,
					"[GameplayNet] Shop sell confirmed (vendor_id={}, item_id={}, qty={})",
					m_pendingSellVendorId,
					m_pendingSellItemId,
					m_pendingSellQty);
			}
			else if (m_input.WasPressed(engine::platform::Key::N))
			{
				m_pendingSellActive = false;
				LOG_INFO(Core, "[GameplayNet] Shop sell cancelled by player");
			}
			return;
		}

		auto sendAuctionBrowseFromModel = [&]()
		{
			const engine::client::UIModel& m = m_uiModelBinding.GetModel();
			engine::server::AuctionBrowseRequestMessage req{};
			req.clientId = clientId;
			req.minPrice = m.auction.filterMinPrice;
			req.maxPrice = m.auction.filterMaxPrice;
			req.itemIdFilter = m.auction.filterItemId;
			req.sortMode = m.auction.sortMode;
			req.maxRows = engine::server::kMaxAuctionBrowseRowsWire;
			(void)m_gameplayUdp.SendAuctionBrowseRequest(req);
		};

		auto clientAuctionMinNextBid = [](uint32_t startBid, uint32_t currentBid) -> uint32_t
		{
			if (currentBid == 0u)
			{
				return startBid;
			}
			const uint32_t inc = std::max(1u, (currentBid * 5u) / 100u);
			const uint64_t sum = static_cast<uint64_t>(currentBid) + static_cast<uint64_t>(inc);
			return static_cast<uint32_t>(std::min<uint64_t>(
				sum,
				static_cast<uint64_t>(std::numeric_limits<uint32_t>::max())));
		};

		if (!chatBlocks && m_input.WasPressed(engine::platform::Key::V))
		{
			(void)m_gameplayUdp.SendTalkRequest(clientId, m_gameplayVendorTalkTarget);
			LOG_INFO(Core, "[GameplayNet] Vendor talk requested ({})", m_gameplayVendorTalkTarget);
		}

		if (!chatBlocks && m_input.WasPressed(engine::platform::Key::H))
		{
			(void)m_gameplayUdp.SendTalkRequest(clientId, m_gameplayAuctionTalkTarget);
			LOG_INFO(Core, "[GameplayNet] Auction talk requested ({})", m_gameplayAuctionTalkTarget);
		}

		auto tryBuyIndex = [&](size_t offerIndex)
		{
			if (!ui.shop.isOpen || offerIndex >= ui.shop.offers.size())
			{
				return;
			}
			const uint32_t itemId = ui.shop.offers[offerIndex].itemId;
			const uint32_t vendorId = ui.shop.vendorId;
			(void)m_gameplayUdp.SendShopBuyRequest(clientId, vendorId, itemId, 1u);
		};

		const engine::platform::Key digitKeys[9] = {
			engine::platform::Key::Digit1,
			engine::platform::Key::Digit2,
			engine::platform::Key::Digit3,
			engine::platform::Key::Digit4,
			engine::platform::Key::Digit5,
			engine::platform::Key::Digit6,
			engine::platform::Key::Digit7,
			engine::platform::Key::Digit8,
			engine::platform::Key::Digit9
		};

		if (!chatBlocks && ui.auction.isOpen && !ui.auction.listings.empty())
		{
			for (int d = 0; d < 9; ++d)
			{
				if (m_input.WasPressed(digitKeys[d]))
				{
					const size_t idx = static_cast<size_t>(d);
					if (idx < ui.auction.listings.size())
					{
						(void)m_uiModelBinding.SelectAuctionRow(static_cast<uint32_t>(idx));
						LOG_INFO(Core, "[GameplayNet] Auction row selected ({})", idx);
					}
					break;
				}
			}
		}

		if (!chatBlocks && ui.auction.isOpen)
		{
			if (m_input.WasPressed(engine::platform::Key::G))
			{
				sendAuctionBrowseFromModel();
				LOG_INFO(Core, "[GameplayNet] Auction browse refresh (G)");
			}
			if (m_input.WasPressed(engine::platform::Key::F))
			{
				const uint32_t nextSort = (ui.auction.sortMode + 1u) % 3u;
				(void)m_uiModelBinding.ConfigureAuctionBrowse(
					ui.auction.filterMinPrice,
					ui.auction.filterMaxPrice,
					ui.auction.filterItemId,
					nextSort);
				sendAuctionBrowseFromModel();
				LOG_INFO(Core, "[GameplayNet] Auction sort mode -> {}", nextSort);
			}
			if (m_input.WasPressed(engine::platform::Key::Q))
			{
				const uint32_t nmin =
					ui.auction.filterMinPrice > 100u ? ui.auction.filterMinPrice - 100u : 0u;
				(void)m_uiModelBinding.ConfigureAuctionBrowse(
					nmin,
					ui.auction.filterMaxPrice,
					ui.auction.filterItemId,
					ui.auction.sortMode);
				sendAuctionBrowseFromModel();
			}
			if (m_input.WasPressed(engine::platform::Key::E))
			{
				const uint32_t nmin = ui.auction.filterMinPrice + 100u;
				(void)m_uiModelBinding.ConfigureAuctionBrowse(
					nmin,
					ui.auction.filterMaxPrice,
					ui.auction.filterItemId,
					ui.auction.sortMode);
				sendAuctionBrowseFromModel();
			}
			if (m_input.WasPressed(engine::platform::Key::PageUp))
			{
				const uint32_t nmax = ui.auction.filterMaxPrice + 500u;
				(void)m_uiModelBinding.ConfigureAuctionBrowse(
					ui.auction.filterMinPrice,
					nmax,
					ui.auction.filterItemId,
					ui.auction.sortMode);
				sendAuctionBrowseFromModel();
			}
			if (m_input.WasPressed(engine::platform::Key::PageDown))
			{
				const uint32_t nmax =
					ui.auction.filterMaxPrice > 500u ? ui.auction.filterMaxPrice - 500u : 0u;
				(void)m_uiModelBinding.ConfigureAuctionBrowse(
					ui.auction.filterMinPrice,
					nmax,
					ui.auction.filterItemId,
					ui.auction.sortMode);
				sendAuctionBrowseFromModel();
			}
			if (m_input.WasPressed(engine::platform::Key::M))
			{
				const uint32_t nextFilter = ui.auction.filterItemId == 0u ? 1u : 0u;
				(void)m_uiModelBinding.ConfigureAuctionBrowse(
					ui.auction.filterMinPrice,
					ui.auction.filterMaxPrice,
					nextFilter,
					ui.auction.sortMode);
				sendAuctionBrowseFromModel();
				LOG_INFO(Core, "[GameplayNet] Auction item_id filter -> {}", nextFilter);
			}
			if (m_input.WasPressed(engine::platform::Key::B) && !ui.auction.listings.empty())
			{
				const uint32_t sel = std::min(ui.auction.selectedRow,
					static_cast<uint32_t>(ui.auction.listings.size() - 1u));
				const engine::client::UIAuctionListingLine& line = ui.auction.listings[sel];
				const uint32_t bidAmt = clientAuctionMinNextBid(line.startBid, line.currentBid);
				engine::server::AuctionBidRequestMessage msg{};
				msg.clientId = clientId;
				msg.listingId = line.listingId;
				msg.bidAmount = bidAmt;
				(void)m_gameplayUdp.SendAuctionBidRequest(msg);
			}
			if (m_input.WasPressed(engine::platform::Key::O) && !ui.auction.listings.empty())
			{
				const uint32_t sel = std::min(ui.auction.selectedRow,
					static_cast<uint32_t>(ui.auction.listings.size() - 1u));
				const engine::client::UIAuctionListingLine& line = ui.auction.listings[sel];
				if (line.buyoutPrice == 0u)
				{
					LOG_WARN(Core, "[GameplayNet] Buyout ignored: no buyout on row");
				}
				else
				{
					engine::server::AuctionBuyoutRequestMessage msg{};
					msg.clientId = clientId;
					msg.listingId = line.listingId;
					(void)m_gameplayUdp.SendAuctionBuyoutRequest(msg);
				}
			}
			if (m_input.WasPressed(engine::platform::Key::L))
			{
				for (const engine::client::InventorySlotState& slot : m_invUi.GetState().slots)
				{
					if (slot.hovered && slot.occupied && slot.itemId != 0u && slot.quantity > 0u)
					{
						engine::server::AuctionListItemRequestMessage msg{};
						msg.clientId = clientId;
						msg.itemId = slot.itemId;
						msg.quantity = 1u;
						msg.startBid = 10u;
						msg.buyoutPrice = 0u;
						msg.durationHours = 24u;
						(void)m_gameplayUdp.SendAuctionListItemRequest(msg);
						LOG_INFO(Core, "[GameplayNet] Auction list from hover (item_id={}, qty=1)", slot.itemId);
						break;
					}
				}
			}
		}

		if (!chatBlocks && ui.shop.isOpen)
		{
			for (int d = 0; d < 9; ++d)
			{
				if (m_input.WasPressed(digitKeys[d]))
				{
					tryBuyIndex(static_cast<size_t>(d));
					break;
				}
			}
		}

		if ((ui.shop.isOpen || ui.auction.isOpen) && m_input.WasMousePressed(engine::platform::MouseButton::Right))
		{
			(void)m_invUi.TryBeginDrag(mx, my);
		}

		if (m_input.WasMouseReleased(engine::platform::MouseButton::Left))
		{
			if (m_invUi.IsDragging() && ui.shop.isOpen && m_shopUi.HitSellDropZone(mx, my))
			{
				uint32_t slot = 0;
				uint32_t itemId = 0;
				uint32_t qty = 0;
				if (m_invUi.GetDragSource(slot, itemId, qty))
				{
					uint32_t buyPrice = 0;
					bool offerFound = false;
					for (const engine::client::UIShopOfferLine& line : ui.shop.offers)
					{
						if (line.itemId == itemId)
						{
							buyPrice = line.buyPrice;
							offerFound = true;
							break;
						}
					}
					if (!offerFound)
					{
						LOG_WARN(Core, "[GameplayNet] Sell-back rejected: item_id={} not listed by vendor", itemId);
					}
					else
					{
						m_pendingSellVendorId = ui.shop.vendorId;
						m_pendingSellItemId = itemId;
						m_pendingSellQty = qty;
						m_pendingSellUnitGold = ClientVendorSellUnitGold(buyPrice);
						m_pendingSellActive = true;
						LOG_INFO(Core,
							"[GameplayNet] Pending sell (item_id={}, qty={}, unit_gold={}) — confirm Y/N",
							itemId,
							qty,
							m_pendingSellUnitGold);
					}
				}
				m_invUi.CancelDrag();
			}
			else if (m_invUi.IsDragging() && ui.auction.isOpen && m_auctionUi.HitPostDropZone(mx, my))
			{
				uint32_t slot = 0;
				uint32_t itemId = 0;
				uint32_t qty = 0;
				if (m_invUi.GetDragSource(slot, itemId, qty) && itemId != 0u && qty > 0u)
				{
					engine::server::AuctionListItemRequestMessage msg{};
					msg.clientId = clientId;
					msg.itemId = itemId;
					msg.quantity = std::min(qty, 100u);
					msg.startBid = 10u;
					msg.buyoutPrice = 0u;
					msg.durationHours = 24u;
					(void)m_gameplayUdp.SendAuctionListItemRequest(msg);
					LOG_INFO(Core, "[GameplayNet] Auction list drag-drop (item_id={}, qty={})", itemId, msg.quantity);
				}
				m_invUi.CancelDrag();
			}
			else if (m_invUi.IsDragging())
			{
				m_invUi.CancelDrag();
			}
			else if (ui.auction.isOpen)
			{
				const int ahHit = m_auctionUi.HitTestRow(mx, my);
				if (ahHit >= 0)
				{
					(void)m_uiModelBinding.SelectAuctionRow(static_cast<uint32_t>(ahHit));
				}
			}
			else if (ui.shop.isOpen)
			{
				const int hit = m_shopUi.HitTestOfferLine(mx, my);
				if (hit >= 0)
				{
					tryBuyIndex(static_cast<size_t>(hit));
				}
			}
		}
	}

} // namespace engine

