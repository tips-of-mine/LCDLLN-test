#include "engine/Engine.h"

#include "engine/core/Log.h"
#include "engine/core/memory/Memory.h"
#include "engine/platform/FileSystem.h"

#include <GLFW/glfw3.h>
#include <vulkan/vulkan.h>

#include <chrono>
#include <cstring>
#include <filesystem>
#include <thread>
#include <vector>

namespace engine
{
	Engine::Engine(int argc, char** argv)
		: m_cfg(engine::core::Config::Load("config.json", argc, argv))
		, m_time(120)
		, m_frameArena(/*framesInFlight*/ 2, /*perFrameCapacityBytes*/ 1024 * 1024)
	{
		m_vsync  = m_cfg.GetBool("render.vsync", true);
		m_fixedDt = m_cfg.GetDouble("time.fixed_dt", 0.0);
		m_chunkStats.Init(m_cfg);
		m_lodConfig.Init(m_cfg);
		m_hlodRuntime.Init(m_cfg);

		engine::platform::Window::CreateDesc desc{};
		desc.title  = "LCDLLN Engine";
		desc.width  = 1280;
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

		// -----------------------------------------------------------------
		// Vulkan init: instance → surface → device → swapchain → FG resources
		// -----------------------------------------------------------------
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

					// M08.4: Optional color grading LUT (strip 256x16 .texr from paths.content).
					{
						std::string lutPath = m_cfg.GetString("color_grading.lut_path", "");
						if (!lutPath.empty())
							m_colorGradingLutHandle = m_assetRegistry.LoadTexture(lutPath, true);
					}

					// SceneColor (swapchain-compatible, kept for legacy Clear pass).
					engine::render::ImageDesc sceneColorDesc{};
					sceneColorDesc.format   = m_vkSwapchain.GetImageFormat();
					sceneColorDesc.usage    = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
					sceneColorDesc.transient = true;
					m_fgSceneColorId = m_frameGraph.createImage("SceneColor", sceneColorDesc);

					m_fgBackbufferId = m_frameGraph.createExternalImage("Backbuffer");

					// GBuffer: A=albedo (SRGB), B=normal (packed), C=ORM, Depth.
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

					// M07.3: GBufferVelocity — motion vectors (currNDC - prevNDC), R16G16F.
					engine::render::ImageDesc gbufVelDesc{};
					gbufVelDesc.format = VK_FORMAT_R16G16_SFLOAT;
					gbufVelDesc.usage  = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
					m_fgGBufferVelocityId = m_frameGraph.createImage("GBufferVelocity", gbufVelDesc);

					// M03.2: depth also needs SAMPLED_BIT for the lighting pass to read it.
					engine::render::ImageDesc depthDesc{};
					depthDesc.format            = VK_FORMAT_D32_SFLOAT;
					depthDesc.usage             = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT
					                            | VK_IMAGE_USAGE_SAMPLED_BIT;
					depthDesc.isDepthAttachment = true;
					m_fgDepthId = m_frameGraph.createImage("Depth", depthDesc);

					// M03.2: SceneColor_HDR — output of the deferred lighting pass (R16G16B16A16_SFLOAT).
					// M03.4: SAMPLED_BIT added so the tonemap pass can read it as a texture.
					engine::render::ImageDesc sceneColorHDRDesc{};
					sceneColorHDRDesc.format  = VK_FORMAT_R16G16B16A16_SFLOAT;
					sceneColorHDRDesc.usage   = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT
					                          | VK_IMAGE_USAGE_SAMPLED_BIT;
					m_fgSceneColorHDRId = m_frameGraph.createImage("SceneColor_HDR", sceneColorHDRDesc);

					// M03.4: SceneColor_LDR — output of the tonemap pass (R8G8B8A8_UNORM).
					engine::render::ImageDesc sceneColorLDRDesc{};
					sceneColorLDRDesc.format = VK_FORMAT_R8G8B8A8_UNORM;
					sceneColorLDRDesc.usage  = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT
					                         | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
					m_fgSceneColorLDRId = m_frameGraph.createImage("SceneColor_LDR", sceneColorLDRDesc);

					// M06.2: SSAO_Raw — output of SSAO generate pass (R16F occlusion 0..1).
					engine::render::ImageDesc ssaoRawDesc{};
					ssaoRawDesc.format = VK_FORMAT_R16_SFLOAT;
					ssaoRawDesc.usage  = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
					m_fgSsaoRawId = m_frameGraph.createImage("SSAO_Raw", ssaoRawDesc);

					// M06.3: SSAO_Blur_Temp + SSAO_Blur — bilateral blur intermediate and output (R16F).
					engine::render::ImageDesc ssaoBlurDesc{};
					ssaoBlurDesc.format = VK_FORMAT_R16_SFLOAT;
					ssaoBlurDesc.usage  = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
					m_fgSsaoBlurTempId = m_frameGraph.createImage("SSAO_Blur_Temp", ssaoBlurDesc);
					m_fgSsaoBlurId     = m_frameGraph.createImage("SSAO_Blur", ssaoBlurDesc);

					// M07.2: TAA history ping-pong (format LDR = input TAA format).
					engine::render::ImageDesc historyDesc{};
					historyDesc.format = VK_FORMAT_R8G8B8A8_UNORM;
					historyDesc.usage  = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT
					                   | VK_IMAGE_USAGE_SAMPLED_BIT
					                   | VK_IMAGE_USAGE_TRANSFER_DST_BIT
					                   | VK_IMAGE_USAGE_TRANSFER_SRC_BIT; // M07.4: CopyPresent blit from HistoryNext
					m_fgHistoryAId = m_frameGraph.createImage("HistoryA", historyDesc);
					m_fgHistoryBId = m_frameGraph.createImage("HistoryB", historyDesc);

					// M08.1: Bloom mip pyramid (full, 1/2, 1/4, 1/8, 1/16, 1/32).
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

					// M08.2: SceneColor_HDR_WithBloom — output of bloom combine (HDR); tonemap reads this.
					engine::render::ImageDesc sceneColorHDRWithBloomDesc{};
					sceneColorHDRWithBloomDesc.format = VK_FORMAT_R16G16B16A16_SFLOAT;
					sceneColorHDRWithBloomDesc.usage  = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT
					                                   | VK_IMAGE_USAGE_SAMPLED_BIT;
					m_fgSceneColorHDRWithBloomId = m_frameGraph.createImage("SceneColor_HDR_WithBloom", sceneColorHDRWithBloomDesc);

					// M04.2: ShadowMap[0..3] — depth-only cascades (D32, depth attachment + sampled).
					const uint32_t shadowRes =
						static_cast<uint32_t>(m_cfg.GetInt("shadows.resolution", 1024));
					engine::render::ImageDesc shadowDesc{};
					shadowDesc.format            = VK_FORMAT_D32_SFLOAT;
					shadowDesc.width             = shadowRes;
					shadowDesc.height            = shadowRes;
					shadowDesc.usage             = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT
					                             | VK_IMAGE_USAGE_SAMPLED_BIT;
					shadowDesc.isDepthAttachment = true;
					for (uint32_t i = 0; i < engine::render::kCascadeCount; ++i)
					{
						char name[32];
						std::snprintf(name, sizeof(name), "ShadowMap_%u", i);
						m_fgShadowMapIds[i] = m_frameGraph.createImage(name, shadowDesc);
					}

					// --------------------------------------------------
					// Helper: load pre-compiled SPV from content path.
					// --------------------------------------------------
					auto loadSpv = [&](const char* path) -> std::vector<uint32_t>
					{
						std::vector<uint8_t> bytes = engine::platform::FileSystem::ReadAllBytesContent(m_cfg, path);
						if (bytes.size() % 4 != 0) return {};
						std::vector<uint32_t> out(bytes.size() / 4);
						std::memcpy(out.data(), bytes.data(), bytes.size());
						return out;
					};

					// --------------------------------------------------
					// M05.1: BRDF LUT compute (256x256 RG16F, split-sum GGX).
					// --------------------------------------------------
					{
						std::vector<uint32_t> brdfComp = loadSpv("shaders/brdf_lut.comp.spv");
						if (brdfComp.empty())
						{
							engine::render::ShaderCompiler compiler;
							if (compiler.LocateCompiler())
							{
								std::filesystem::path cp = engine::platform::FileSystem::ResolveContentPath(m_cfg, "shaders/brdf_lut.comp");
								auto c = compiler.CompileGlslToSpirv(cp, engine::render::ShaderStage::Compute);
								if (c.has_value() && !c->empty())
									brdfComp = std::move(*c);
							}
						}

						if (!brdfComp.empty())
						{
							const uint32_t lutSize = 256u;
							if (m_brdfLutPass.Init(
								m_vkDeviceContext.GetDevice(),
								m_vkDeviceContext.GetPhysicalDevice(),
								lutSize,
								brdfComp.data(), brdfComp.size(),
								m_vkDeviceContext.GetGraphicsQueueFamilyIndex()))
							{
								m_brdfLutPass.Generate(
									m_vkDeviceContext.GetDevice(),
									m_vkDeviceContext.GetGraphicsQueue());
							}
							else
							{
								LOG_WARN(Render, "M05.1: BRDF LUT init failed — LUT disabled");
							}
						}
						else
						{
							LOG_WARN(Render, "M05.1: BRDF LUT shader not found — LUT disabled");
						}
					}

					// --------------------------------------------------
					// M05.3: Specular prefilter pass (prefiltered GGX cubemap + mips).
					// Generate() is only called when a source env cubemap is available (e.g. M05.2).
					// --------------------------------------------------
					{
						std::vector<uint32_t> specPrefilterComp = loadSpv("shaders/specular_prefilter.comp.spv");
						if (specPrefilterComp.empty())
						{
							engine::render::ShaderCompiler compiler;
							if (compiler.LocateCompiler())
							{
								std::filesystem::path cp = engine::platform::FileSystem::ResolveContentPath(m_cfg, "shaders/specular_prefilter.comp");
								auto c = compiler.CompileGlslToSpirv(cp, engine::render::ShaderStage::Compute);
								if (c.has_value() && !c->empty())
									specPrefilterComp = std::move(*c);
							}
						}
						if (!specPrefilterComp.empty())
						{
							const uint32_t specSize = 256u;
							const uint32_t specMipCount = 6u;
							if (m_specularPrefilterPass.Init(
								m_vkDeviceContext.GetDevice(),
								m_vkDeviceContext.GetPhysicalDevice(),
								specSize, specMipCount,
								specPrefilterComp.data(), specPrefilterComp.size(),
								m_vkDeviceContext.GetGraphicsQueueFamilyIndex()))
							{
								// Generate() requires source env cubemap view/sampler (e.g. from M05.2).
								// When available, call: m_specularPrefilterPass.Generate(device, queue, envView, envSampler);
							}
							else
							{
								LOG_WARN(Render, "M05.3: Specular prefilter init failed — disabled");
							}
						}
						else
						{
							LOG_WARN(Render, "M05.3: specular_prefilter.comp not found — disabled");
						}
					}

					// --------------------------------------------------
					// M06.1: SSAO kernel + 4x4 noise (UBO + texture), generated at boot.
					// --------------------------------------------------
					m_ssaoKernelNoise.Init(
						m_vkDeviceContext.GetDevice(),
						m_vkDeviceContext.GetPhysicalDevice(),
						m_cfg,
						m_vkDeviceContext.GetGraphicsQueue(),
						m_vkDeviceContext.GetGraphicsQueueFamilyIndex());

					// --------------------------------------------------
					// M06.2: SSAO generate pass (depth + normal -> SSAO_Raw).
					// --------------------------------------------------
					{
						std::vector<uint32_t> ssaoVert = loadSpv("shaders/ssao.vert.spv");
						std::vector<uint32_t> ssaoFrag = loadSpv("shaders/ssao.frag.spv");
						if (ssaoVert.empty() || ssaoFrag.empty())
						{
							engine::render::ShaderCompiler compiler;
							if (compiler.LocateCompiler())
							{
								std::filesystem::path vp = engine::platform::FileSystem::ResolveContentPath(m_cfg, "shaders/ssao.vert");
								std::filesystem::path fp = engine::platform::FileSystem::ResolveContentPath(m_cfg, "shaders/ssao.frag");
								auto v = compiler.CompileGlslToSpirv(vp, engine::render::ShaderStage::Vertex);
								auto f = compiler.CompileGlslToSpirv(fp, engine::render::ShaderStage::Fragment);
								if (v.has_value() && !v->empty()) ssaoVert = std::move(*v);
								if (f.has_value() && !f->empty()) ssaoFrag = std::move(*f);
							}
						}
						if (!ssaoVert.empty() && !ssaoFrag.empty() && m_ssaoKernelNoise.IsValid())
						{
							if (m_ssaoPass.Init(
								m_vkDeviceContext.GetDevice(),
								m_vkDeviceContext.GetPhysicalDevice(),
								VK_FORMAT_R16_SFLOAT,
								ssaoVert.data(), ssaoVert.size(),
								ssaoFrag.data(), ssaoFrag.size(),
								2))
								LOG_INFO(Render, "M06.2: SSAO generate pass ready");
							else
								LOG_WARN(Render, "M06.2: SSAO pass init failed");
						}
						else if (!m_ssaoKernelNoise.IsValid())
							LOG_WARN(Render, "M06.2: SSAO pass skipped (kernel/noise not ready)");
					}

					// --------------------------------------------------
					// M06.3: SSAO bilateral blur pass (2 passes: H then V, depth-aware).
					// --------------------------------------------------
					{
						std::vector<uint32_t> blurVert = loadSpv("shaders/ssao_blur.vert.spv");
						std::vector<uint32_t> blurFrag = loadSpv("shaders/ssao_blur.frag.spv");
						if (blurVert.empty() || blurFrag.empty())
						{
							engine::render::ShaderCompiler compiler;
							if (compiler.LocateCompiler())
							{
								std::filesystem::path vp = engine::platform::FileSystem::ResolveContentPath(m_cfg, "shaders/ssao_blur.vert");
								std::filesystem::path fp = engine::platform::FileSystem::ResolveContentPath(m_cfg, "shaders/ssao_blur.frag");
								auto v = compiler.CompileGlslToSpirv(vp, engine::render::ShaderStage::Vertex);
								auto f = compiler.CompileGlslToSpirv(fp, engine::render::ShaderStage::Fragment);
								if (v.has_value() && !v->empty()) blurVert = std::move(*v);
								if (f.has_value() && !f->empty()) blurFrag = std::move(*f);
							}
						}
						if (!blurVert.empty() && !blurFrag.empty())
						{
							if (m_ssaoBlurPass.Init(
								m_vkDeviceContext.GetDevice(),
								m_vkDeviceContext.GetPhysicalDevice(),
								VK_FORMAT_R16_SFLOAT,
								blurVert.data(), blurVert.size(),
								blurFrag.data(), blurFrag.size(),
								2))
								LOG_INFO(Render, "M06.3: SSAO bilateral blur pass ready");
							else
								LOG_WARN(Render, "M06.3: SSAO blur pass init failed");
						}
						else
							LOG_WARN(Render, "M06.3: SSAO blur shaders not found — blur disabled");
					}

					// --------------------------------------------------
					// Clear pass (legacy, clears SceneColor swapchain image).
					// --------------------------------------------------
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

					// --------------------------------------------------
					// Geometry pass: load/compile shaders + init pipeline.
					// --------------------------------------------------
					{
						std::vector<uint32_t> vertSpirv = loadSpv("shaders/gbuffer_geometry.vert.spv");
						std::vector<uint32_t> fragSpirv = loadSpv("shaders/gbuffer_geometry.frag.spv");

						if (vertSpirv.empty() || fragSpirv.empty())
						{
							engine::render::ShaderCompiler compiler;
							if (compiler.LocateCompiler())
							{
								std::filesystem::path vp = engine::platform::FileSystem::ResolveContentPath(m_cfg, "shaders/gbuffer_geometry.vert");
								std::filesystem::path fp = engine::platform::FileSystem::ResolveContentPath(m_cfg, "shaders/gbuffer_geometry.frag");
								auto v = compiler.CompileGlslToSpirv(vp, engine::render::ShaderStage::Vertex);
								auto f = compiler.CompileGlslToSpirv(fp, engine::render::ShaderStage::Fragment);
								if (v.has_value() && !v->empty()) vertSpirv = std::move(*v);
								if (f.has_value() && !f->empty()) fragSpirv = std::move(*f);
							}
						}

						if (!vertSpirv.empty() && !fragSpirv.empty())
						{
							m_geometryPass.Init(
								m_vkDeviceContext.GetDevice(),
								m_vkDeviceContext.GetPhysicalDevice(),
								VK_FORMAT_R8G8B8A8_SRGB,
								VK_FORMAT_A2B10G10R10_UNORM_PACK32,
								VK_FORMAT_R8G8B8A8_UNORM,
								VK_FORMAT_R16G16_SFLOAT,  // M07.3: velocity
								VK_FORMAT_D32_SFLOAT,
								vertSpirv.data(), vertSpirv.size(),
								fragSpirv.data(), fragSpirv.size());
						}
					}

					// M04.2: Shadow map pass shaders — load SPV or compile at runtime.
					{
						std::vector<uint32_t> smVert = loadSpv("shaders/shadow_depth.vert.spv");
						std::vector<uint32_t> smFrag = loadSpv("shaders/shadow_depth.frag.spv");

						if (smVert.empty() || smFrag.empty())
						{
							engine::render::ShaderCompiler smCompiler;
							if (smCompiler.LocateCompiler())
							{
								std::filesystem::path vp = engine::platform::FileSystem::ResolveContentPath(m_cfg, "shaders/shadow_depth.vert");
								std::filesystem::path fp = engine::platform::FileSystem::ResolveContentPath(m_cfg, "shaders/shadow_depth.frag");
								auto v = smCompiler.CompileGlslToSpirv(vp, engine::render::ShaderStage::Vertex);
								auto f = smCompiler.CompileGlslToSpirv(fp, engine::render::ShaderStage::Fragment);
								if (v.has_value() && !v->empty()) smVert = std::move(*v);
								if (f.has_value() && !f->empty()) smFrag = std::move(*f);
							}
						}

						if (!smVert.empty() && !smFrag.empty())
						{
							m_shadowMapPass.Init(
								m_vkDeviceContext.GetDevice(),
								m_vkDeviceContext.GetPhysicalDevice(),
								VK_FORMAT_D32_SFLOAT,
								shadowRes,
								smVert.data(), smVert.size(),
								smFrag.data(), smFrag.size());
						}
						else
						{
							LOG_WARN(Render, "M04.2: shadow map shaders not found — shadow pass disabled");
						}
					}

					// M03.2: Lighting pass shaders — load SPV or compile at runtime.
					{
						std::vector<uint32_t> litVert = loadSpv("shaders/lighting.vert.spv");
						std::vector<uint32_t> litFrag = loadSpv("shaders/lighting.frag.spv");

						if (litVert.empty() || litFrag.empty())
						{
							engine::render::ShaderCompiler litCompiler;
							if (litCompiler.LocateCompiler())
							{
								std::filesystem::path vp = engine::platform::FileSystem::ResolveContentPath(m_cfg, "shaders/lighting.vert");
								std::filesystem::path fp = engine::platform::FileSystem::ResolveContentPath(m_cfg, "shaders/lighting.frag");
								auto v = litCompiler.CompileGlslToSpirv(vp, engine::render::ShaderStage::Vertex);
								auto f = litCompiler.CompileGlslToSpirv(fp, engine::render::ShaderStage::Fragment);
								if (v.has_value() && !v->empty()) litVert = std::move(*v);
								if (f.has_value() && !f->empty()) litFrag = std::move(*f);
							}
						}

						if (!litVert.empty() && !litFrag.empty())
						{
							m_lightingPass.Init(
								m_vkDeviceContext.GetDevice(),
								m_vkDeviceContext.GetPhysicalDevice(),
								VK_FORMAT_R16G16B16A16_SFLOAT,
								litVert.data(), litVert.size(),
								litFrag.data(), litFrag.size(),
								2u);
						}
						else
						{
							LOG_WARN(Render, "M03.2: lighting shaders not found — lighting pass disabled");
						}
					}

					// M03.4: Tonemap pass shaders — load SPV or compile at runtime.
					{
						std::vector<uint32_t> tmVert = loadSpv("shaders/tonemap.vert.spv");
						std::vector<uint32_t> tmFrag = loadSpv("shaders/tonemap.frag.spv");

						if (tmVert.empty() || tmFrag.empty())
						{
							engine::render::ShaderCompiler tmCompiler;
							if (tmCompiler.LocateCompiler())
							{
								std::filesystem::path vp = engine::platform::FileSystem::ResolveContentPath(m_cfg, "shaders/tonemap.vert");
								std::filesystem::path fp = engine::platform::FileSystem::ResolveContentPath(m_cfg, "shaders/tonemap.frag");
								auto v = tmCompiler.CompileGlslToSpirv(vp, engine::render::ShaderStage::Vertex);
								auto f = tmCompiler.CompileGlslToSpirv(fp, engine::render::ShaderStage::Fragment);
								if (v.has_value() && !v->empty()) tmVert = std::move(*v);
								if (f.has_value() && !f->empty()) tmFrag = std::move(*f);
							}
						}

						if (!tmVert.empty() && !tmFrag.empty())
						{
							m_tonemapPass.Init(
								m_vkDeviceContext.GetDevice(),
								m_vkDeviceContext.GetPhysicalDevice(),
								VK_FORMAT_R8G8B8A8_UNORM,
								tmVert.data(), tmVert.size(),
								tmFrag.data(), tmFrag.size(),
								2u);
						}
						else
						{
							LOG_WARN(Render, "M03.4: tonemap shaders not found — tonemap pass disabled");
						}
					}

					// M08.1: Bloom prefilter + downsample shaders.
					{
						std::vector<uint32_t> bpVert = loadSpv("shaders/bloom_prefilter.vert.spv");
						std::vector<uint32_t> bpFrag = loadSpv("shaders/bloom_prefilter.frag.spv");
						std::vector<uint32_t> bdVert = loadSpv("shaders/bloom_downsample.vert.spv");
						std::vector<uint32_t> bdFrag = loadSpv("shaders/bloom_downsample.frag.spv");
						if (bpVert.empty() || bpFrag.empty() || bdVert.empty() || bdFrag.empty())
						{
							engine::render::ShaderCompiler compiler;
							if (compiler.LocateCompiler())
							{
								auto resolve = [&](const char* p) {
									return engine::platform::FileSystem::ResolveContentPath(m_cfg, p);
								};
								auto vbp = compiler.CompileGlslToSpirv(resolve("shaders/bloom_prefilter.vert"), engine::render::ShaderStage::Vertex);
								auto fbp = compiler.CompileGlslToSpirv(resolve("shaders/bloom_prefilter.frag"), engine::render::ShaderStage::Fragment);
								auto vbd = compiler.CompileGlslToSpirv(resolve("shaders/bloom_downsample.vert"), engine::render::ShaderStage::Vertex);
								auto fbd = compiler.CompileGlslToSpirv(resolve("shaders/bloom_downsample.frag"), engine::render::ShaderStage::Fragment);
								if (vbp.has_value() && !vbp->empty()) bpVert = std::move(*vbp);
								if (fbp.has_value() && !fbp->empty()) bpFrag = std::move(*fbp);
								if (vbd.has_value() && !vbd->empty()) bdVert = std::move(*vbd);
								if (fbd.has_value() && !fbd->empty()) bdFrag = std::move(*fbd);
							}
						}
						const VkFormat bloomFmt = VK_FORMAT_R16G16B16A16_SFLOAT;
						if (!bpVert.empty() && !bpFrag.empty())
						{
							m_bloomPrefilterPass.Init(
								m_vkDeviceContext.GetDevice(),
								m_vkDeviceContext.GetPhysicalDevice(),
								bloomFmt,
								bpVert.data(), bpVert.size(),
								bpFrag.data(), bpFrag.size(),
								2u);
						}
						if (!bdVert.empty() && !bdFrag.empty())
						{
							m_bloomDownsamplePass.Init(
								m_vkDeviceContext.GetDevice(),
								m_vkDeviceContext.GetPhysicalDevice(),
								bloomFmt,
								bdVert.data(), bdVert.size(),
								bdFrag.data(), bdFrag.size(),
								2u);
						}
					}

					// M08.2: Bloom upsample + combine shaders.
					{
						std::vector<uint32_t> buVert = loadSpv("shaders/bloom_upsample.vert.spv");
						std::vector<uint32_t> buFrag = loadSpv("shaders/bloom_upsample.frag.spv");
						std::vector<uint32_t> bcVert = loadSpv("shaders/bloom_combine.vert.spv");
						std::vector<uint32_t> bcFrag = loadSpv("shaders/bloom_combine.frag.spv");
						if (buVert.empty() || buFrag.empty() || bcVert.empty() || bcFrag.empty())
						{
							engine::render::ShaderCompiler compiler;
							if (compiler.LocateCompiler())
							{
								auto resolve = [&](const char* p) {
									return engine::platform::FileSystem::ResolveContentPath(m_cfg, p);
								};
								auto vbu = compiler.CompileGlslToSpirv(resolve("shaders/bloom_upsample.vert"), engine::render::ShaderStage::Vertex);
								auto fbu = compiler.CompileGlslToSpirv(resolve("shaders/bloom_upsample.frag"), engine::render::ShaderStage::Fragment);
								auto vbc = compiler.CompileGlslToSpirv(resolve("shaders/bloom_combine.vert"), engine::render::ShaderStage::Vertex);
								auto fbc = compiler.CompileGlslToSpirv(resolve("shaders/bloom_combine.frag"), engine::render::ShaderStage::Fragment);
								if (vbu.has_value() && !vbu->empty()) buVert = std::move(*vbu);
								if (fbu.has_value() && !fbu->empty()) buFrag = std::move(*fbu);
								if (vbc.has_value() && !vbc->empty()) bcVert = std::move(*vbc);
								if (fbc.has_value() && !fbc->empty()) bcFrag = std::move(*fbc);
							}
						}
						const VkFormat bloomFmt = VK_FORMAT_R16G16B16A16_SFLOAT;
						if (!buVert.empty() && !buFrag.empty())
						{
							m_bloomUpsamplePass.Init(
								m_vkDeviceContext.GetDevice(),
								m_vkDeviceContext.GetPhysicalDevice(),
								bloomFmt,
								buVert.data(), buVert.size(),
								buFrag.data(), buFrag.size(),
								2u);
						}
						if (!bcVert.empty() && !bcFrag.empty())
						{
							m_bloomCombinePass.Init(
								m_vkDeviceContext.GetDevice(),
								m_vkDeviceContext.GetPhysicalDevice(),
								bloomFmt,
								bcVert.data(), bcVert.size(),
								bcFrag.data(), bcFrag.size(),
								2u);
						}
					}

					// M08.3: Auto-exposure — luminance reduce compute.
					{
						std::vector<uint32_t> lumComp = loadSpv("shaders/luminance_reduce.comp.spv");
						if (lumComp.empty())
						{
							engine::render::ShaderCompiler compiler;
							if (compiler.LocateCompiler())
							{
								std::filesystem::path cp = engine::platform::FileSystem::ResolveContentPath(m_cfg, "shaders/luminance_reduce.comp");
								auto c = compiler.CompileGlslToSpirv(cp, engine::render::ShaderStage::Compute);
								if (c.has_value() && !c->empty()) lumComp = std::move(*c);
							}
						}
						if (!lumComp.empty())
						{
							if (m_autoExposure.Init(
								m_vkDeviceContext.GetDevice(),
								m_vkDeviceContext.GetPhysicalDevice(),
								lumComp.data(), lumComp.size()))
								LOG_INFO(Render, "M08.3: Auto-exposure ready");
						}
					}

					// M07.4: TAA pass shaders — reprojection + clamp + blend.
					{
						std::vector<uint32_t> taaVert = loadSpv("shaders/taa.vert.spv");
						std::vector<uint32_t> taaFrag = loadSpv("shaders/taa.frag.spv");
						if (taaVert.empty() || taaFrag.empty())
						{
							engine::render::ShaderCompiler compiler;
							if (compiler.LocateCompiler())
							{
								std::filesystem::path vp = engine::platform::FileSystem::ResolveContentPath(m_cfg, "shaders/taa.vert");
								std::filesystem::path fp = engine::platform::FileSystem::ResolveContentPath(m_cfg, "shaders/taa.frag");
								auto v = compiler.CompileGlslToSpirv(vp, engine::render::ShaderStage::Vertex);
								auto f = compiler.CompileGlslToSpirv(fp, engine::render::ShaderStage::Fragment);
								if (v.has_value() && !v->empty()) taaVert = std::move(*v);
								if (f.has_value() && !f->empty()) taaFrag = std::move(*f);
							}
						}
						if (!taaVert.empty() && !taaFrag.empty())
						{
							if (m_taaPass.Init(
								m_vkDeviceContext.GetDevice(),
								m_vkDeviceContext.GetPhysicalDevice(),
								VK_FORMAT_R8G8B8A8_UNORM,
								taaVert.data(), taaVert.size(),
								taaFrag.data(), taaFrag.size(),
								2u))
								LOG_INFO(Render, "M07.4: TAA pass ready");
							else
								LOG_WARN(Render, "M07.4: TAA pass init failed");
						}
						else
							LOG_WARN(Render, "M07.4: TAA shaders not found — TAA disabled");
					}

					// Load test mesh.
					m_geometryMeshHandle = m_assetRegistry.LoadMesh("meshes/test.mesh");

					// --------------------------------------------------
					// Frame graph passes
					// --------------------------------------------------

					// Pass: Geometry — fills GBuffer A/B/C + Velocity (M07.3) + Depth.
					m_frameGraph.addPass("Geometry",
						[this](engine::render::PassBuilder& b) {
							b.write(m_fgGBufferAId,       engine::render::ImageUsage::ColorWrite);
							b.write(m_fgGBufferBId,       engine::render::ImageUsage::ColorWrite);
							b.write(m_fgGBufferCId,       engine::render::ImageUsage::ColorWrite);
							b.write(m_fgGBufferVelocityId, engine::render::ImageUsage::ColorWrite);
							b.write(m_fgDepthId,          engine::render::ImageUsage::DepthWrite);
						},
						[this](VkCommandBuffer cmd, engine::render::Registry& reg) {
							const uint32_t readIdx = m_renderReadIndex.load(std::memory_order_acquire);
							const engine::RenderState& rs = m_renderStates[readIdx];
							engine::render::MeshAsset* mesh = m_geometryMeshHandle.Get();
							// M09.2: tag draw with chunk/ring and record stats.
							const engine::world::ChunkCoord chunk = engine::world::WorldToChunkCoord(rs.camera.position.x, rs.camera.position.z);
							const engine::world::ChunkRing ring = m_world.GetRingForChunk(chunk);
							const uint32_t triCount = (mesh && mesh->indexCount > 0) ? (mesh->indexCount / 3) : 0;
							m_chunkStats.RecordDraw(chunk, ring, 1, triCount);
							const int lodLevel = m_lodConfig.GetLodLevel(0.0f);
							m_geometryPass.Record(
								m_vkDeviceContext.GetDevice(), cmd, reg,
								m_vkSwapchain.GetExtent(),
								m_fgGBufferAId, m_fgGBufferBId, m_fgGBufferCId, m_fgGBufferVelocityId, m_fgDepthId,
								rs.prevViewProjMatrix.m, rs.viewProjMatrix.m, mesh,
								static_cast<uint32_t>(lodLevel));
						});

					// Passes: ShadowMap[0..3] (M04.2) — depth-only render per cascade.
					for (uint32_t cascade = 0; cascade < engine::render::kCascadeCount; ++cascade)
					{
						const std::string passName = std::string("ShadowMap_") + std::to_string(cascade);
						m_frameGraph.addPass(passName,
							[this, cascade](engine::render::PassBuilder& b) {
								b.write(m_fgShadowMapIds[cascade], engine::render::ImageUsage::DepthWrite);
							},
							[this, cascade](VkCommandBuffer cmd, engine::render::Registry& reg) {
								if (!m_shadowMapPass.IsValid())
									return;

								const uint32_t readIdx = m_renderReadIndex.load(std::memory_order_acquire);
								const engine::RenderState& rs = m_renderStates[readIdx];
								engine::render::MeshAsset* mesh = m_geometryMeshHandle.Get();
								// M09.2: tag draw with chunk/ring and record stats.
								const engine::world::ChunkCoord chunk = engine::world::WorldToChunkCoord(rs.camera.position.x, rs.camera.position.z);
								const engine::world::ChunkRing ring = m_world.GetRingForChunk(chunk);
								const uint32_t triCount = (mesh && mesh->indexCount > 0) ? (mesh->indexCount / 3) : 0;
								m_chunkStats.RecordDraw(chunk, ring, 1, triCount);

								const float depthBiasConstant =
									static_cast<float>(m_cfg.GetDouble("shadows.depth_bias_constant", 1.25));
								const float depthBiasSlope =
									static_cast<float>(m_cfg.GetDouble("shadows.depth_bias_slope", 1.75));
								const bool cullFrontFaces =
									m_cfg.GetBool("shadows.cull_front_faces", false);

								m_shadowMapPass.Record(
									m_vkDeviceContext.GetDevice(), cmd, reg,
									m_fgShadowMapIds[cascade],
									rs.cascades.lightViewProj[cascade].m,
									mesh,
									depthBiasConstant,
									depthBiasSlope,
									cullFrontFaces);
							});
					}

					// Pass: SSAO_Generate (M06.2) — reads Depth + Normal, writes SSAO_Raw.
					m_frameGraph.addPass("SSAO_Generate",
						[this](engine::render::PassBuilder& b) {
							b.read(m_fgDepthId,     engine::render::ImageUsage::SampledRead);
							b.read(m_fgGBufferBId,  engine::render::ImageUsage::SampledRead);
							b.write(m_fgSsaoRawId, engine::render::ImageUsage::ColorWrite);
						},
						[this](VkCommandBuffer cmd, engine::render::Registry& reg) {
							if (!m_ssaoPass.IsValid()) return;

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
							m_ssaoPass.Record(
								m_vkDeviceContext.GetDevice(), cmd, reg,
								m_vkSwapchain.GetExtent(),
								m_fgDepthId, m_fgGBufferBId, m_fgSsaoRawId,
								m_ssaoKernelNoise.GetKernelBuffer(),
								m_ssaoKernelNoise.GetNoiseImageView(),
								m_ssaoKernelNoise.GetNoiseSampler(),
								sp, frameIdx);
						});

					// Pass: SSAO_BlurH (M06.3) — reads SSAO_Raw + Depth, writes SSAO_Blur_Temp.
					m_frameGraph.addPass("SSAO_BlurH",
						[this](engine::render::PassBuilder& b) {
							b.read(m_fgSsaoRawId,      engine::render::ImageUsage::SampledRead);
							b.read(m_fgDepthId,       engine::render::ImageUsage::SampledRead);
							b.write(m_fgSsaoBlurTempId, engine::render::ImageUsage::ColorWrite);
						},
						[this](VkCommandBuffer cmd, engine::render::Registry& reg) {
							if (!m_ssaoBlurPass.IsValid()) return;
							VkExtent2D extent = m_vkSwapchain.GetExtent();
							const uint32_t frameIdx = m_currentFrame % 2;
							m_ssaoBlurPass.Record(
								m_vkDeviceContext.GetDevice(), cmd, reg, extent,
								m_fgSsaoRawId, m_fgDepthId, m_fgSsaoBlurTempId,
								true, frameIdx);
						});

					// Pass: SSAO_BlurV (M06.3) — reads SSAO_Blur_Temp + Depth, writes SSAO_Blur.
					m_frameGraph.addPass("SSAO_BlurV",
						[this](engine::render::PassBuilder& b) {
							b.read(m_fgSsaoBlurTempId, engine::render::ImageUsage::SampledRead);
							b.read(m_fgDepthId,       engine::render::ImageUsage::SampledRead);
							b.write(m_fgSsaoBlurId,  engine::render::ImageUsage::ColorWrite);
						},
						[this](VkCommandBuffer cmd, engine::render::Registry& reg) {
							if (!m_ssaoBlurPass.IsValid()) return;
							VkExtent2D extent = m_vkSwapchain.GetExtent();
							const uint32_t frameIdx = m_currentFrame % 2;
							m_ssaoBlurPass.Record(
								m_vkDeviceContext.GetDevice(), cmd, reg, extent,
								m_fgSsaoBlurTempId, m_fgDepthId, m_fgSsaoBlurId,
								false, frameIdx);
						});

					// Pass: Lighting (M03.2) — reads GBuffer + Depth + SSAO_Blur (M06.4), writes SceneColor_HDR.
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
							if (!m_lightingPass.IsValid()) return;

							const uint32_t readIdx = m_renderReadIndex.load(std::memory_order_acquire);
							const engine::RenderState& rs = m_renderStates[readIdx];

							// ---- Build LightParams ----------------------------------------
							engine::render::LightingPass::LightParams lp{};

							// Compute inverse view-projection (Cramer's rule, column-major).
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

							// Camera world position.
							lp.cameraPos[0] = rs.camera.position.x;
							lp.cameraPos[1] = rs.camera.position.y;
							lp.cameraPos[2] = rs.camera.position.z;
							lp.cameraPos[3] = 0.0f;

							// Default directional light: warm sun from upper-right.
							// Direction is normalised world-space vector pointing TOWARD the light.
							lp.lightDir[0] = 0.5774f;   // normalize(1,1,1)
							lp.lightDir[1] = 0.5774f;
							lp.lightDir[2] = 0.5774f;
							lp.lightDir[3] = 0.0f;
							lp.lightColor[0] = 1.0f;    // warm white
							lp.lightColor[1] = 0.95f;
							lp.lightColor[2] = 0.85f;
							lp.lightColor[3] = 0.0f;

							// Constant ambient (fallback when IBL absent). M05.4.
							lp.ambientColor[0] = 0.03f;
							lp.ambientColor[1] = 0.03f;
							lp.ambientColor[2] = 0.05f;
							lp.ambientColor[3] = 0.0f;

							// M05.4: IBL when irradiance + prefilter + BRDF LUT all available; else fallback.
							VkImageView irrView = VK_NULL_HANDLE;  // M05.2 not implemented yet
							VkSampler   irrSamp = VK_NULL_HANDLE;
							VkImageView prefilterView = m_specularPrefilterPass.IsValid()
								? m_specularPrefilterPass.GetImageView() : VK_NULL_HANDLE;
							VkSampler   prefilterSamp = m_specularPrefilterPass.IsValid()
								? m_specularPrefilterPass.GetSampler() : VK_NULL_HANDLE;
							VkImageView brdfView = m_brdfLutPass.GetImageView();
							VkSampler   brdfSamp = m_brdfLutPass.GetSampler();
							lp.useIBL = (irrView != VK_NULL_HANDLE && prefilterView != VK_NULL_HANDLE && brdfView != VK_NULL_HANDLE) ? 1.0f : 0.0f;

							const uint32_t frameIdx = m_currentFrame % 2;
							m_lightingPass.Record(
								m_vkDeviceContext.GetDevice(), cmd, reg,
								m_vkSwapchain.GetExtent(),
								m_fgGBufferAId, m_fgGBufferBId, m_fgGBufferCId, m_fgDepthId,
								m_fgSceneColorHDRId, m_fgSsaoBlurId,
								irrView, irrSamp, prefilterView, prefilterSamp, brdfView, brdfSamp,
								lp, frameIdx);
						});

					// M08.1: Bloom prefilter — read SceneColor_HDR, write BloomMip0 (threshold + knee).
					m_frameGraph.addPass("Bloom_Prefilter",
						[this](engine::render::PassBuilder& b) {
							b.read(m_fgSceneColorHDRId, engine::render::ImageUsage::SampledRead);
							b.write(m_fgBloomMipIds[0], engine::render::ImageUsage::ColorWrite);
						},
						[this](VkCommandBuffer cmd, engine::render::Registry& reg) {
							if (!m_bloomPrefilterPass.IsValid()) return;
							engine::render::BloomPrefilterPass::PrefilterParams pp{};
							pp.threshold = static_cast<float>(m_cfg.GetDouble("bloom.threshold", 1.0));
							pp.knee      = static_cast<float>(m_cfg.GetDouble("bloom.knee", 0.5));
							const uint32_t frameIdx = m_currentFrame % 2;
							m_bloomPrefilterPass.Record(
								m_vkDeviceContext.GetDevice(), cmd, reg,
								m_vkSwapchain.GetExtent(),
								m_fgSceneColorHDRId, m_fgBloomMipIds[0],
								pp, frameIdx);
						});

					// M08.1: Bloom downsample chain (BloomMip_i -> BloomMip_{i+1}).
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
								if (!m_bloomDownsamplePass.IsValid()) return;
								VkExtent2D ext = m_vkSwapchain.GetExtent();
								VkExtent2D extentDst;
								extentDst.width  = ext.width >> (i + 1);
								extentDst.height = ext.height >> (i + 1);
								if (extentDst.width < 1) extentDst.width = 1;
								if (extentDst.height < 1) extentDst.height = 1;
								const uint32_t frameIdx = m_currentFrame % 2;
								m_bloomDownsamplePass.Record(
									m_vkDeviceContext.GetDevice(), cmd, reg,
									extentDst, idSrc, idDst, frameIdx);
							});
					}

					// M08.2: Bloom upsample chain (smallest → mip0): add upsampled(Mip_{i+1}) into Mip_i.
					// Order: Upsample_4 (Mip5→Mip4), then Upsample_3 (Mip4→Mip3), ..., Upsample_0 (Mip1→Mip0).
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
								if (!m_bloomUpsamplePass.IsValid()) return;
								VkExtent2D ext = m_vkSwapchain.GetExtent();
								VkExtent2D extentDst;
								extentDst.width  = ext.width >> i;
								extentDst.height = ext.height >> i;
								if (extentDst.width < 1) extentDst.width = 1;
								if (extentDst.height < 1) extentDst.height = 1;
								const uint32_t frameIdx = m_currentFrame % 2;
								m_bloomUpsamplePass.Record(
									m_vkDeviceContext.GetDevice(), cmd, reg,
									extentDst, idSrc, idDst, frameIdx);
							});
					}

					// M08.2: Bloom combine — SceneColor_HDR + bloom*intensity → SceneColor_HDR_WithBloom.
					m_frameGraph.addPass("Bloom_Combine",
						[this](engine::render::PassBuilder& b) {
							b.read(m_fgSceneColorHDRId,       engine::render::ImageUsage::SampledRead);
							b.read(m_fgBloomMipIds[0],       engine::render::ImageUsage::SampledRead);
							b.write(m_fgSceneColorHDRWithBloomId, engine::render::ImageUsage::ColorWrite);
						},
						[this](VkCommandBuffer cmd, engine::render::Registry& reg) {
							if (!m_bloomCombinePass.IsValid()) return;
							engine::render::BloomCombinePass::CombineParams cp{};
							cp.intensity = static_cast<float>(m_cfg.GetDouble("bloom.intensity", 1.0));
							const uint32_t frameIdx = m_currentFrame % 2;
							m_bloomCombinePass.Record(
								m_vkDeviceContext.GetDevice(), cmd, reg,
								m_vkSwapchain.GetExtent(),
								m_fgSceneColorHDRId, m_fgBloomMipIds[0],
								m_fgSceneColorHDRWithBloomId,
								cp, frameIdx);
						});

					// M08.3: Auto-exposure luminance reduce — read HDR, compute log(L) grid -> staging for readback.
					m_frameGraph.addPass("AutoExposure_Luminance",
						[this](engine::render::PassBuilder& b) {
							b.read(m_fgSceneColorHDRWithBloomId, engine::render::ImageUsage::SampledRead);
						},
						[this](VkCommandBuffer cmd, engine::render::Registry& reg) {
							if (!m_autoExposure.IsValid()) return;
							m_autoExposure.Record(
								m_vkDeviceContext.GetDevice(), cmd, reg,
								m_fgSceneColorHDRWithBloomId,
								m_vkSwapchain.GetExtent());
						});

					// Pass: Tonemap (M03.4) — reads SceneColor_HDR_WithBloom (M08.2), applies ACES filmic +
					// exposure (M08.3 auto-exposure when valid) + gamma 2.2, writes SceneColor_LDR.
					m_frameGraph.addPass("Tonemap",
						[this](engine::render::PassBuilder& b) {
							b.read(m_fgSceneColorHDRWithBloomId, engine::render::ImageUsage::SampledRead);
							b.write(m_fgSceneColorLDRId, engine::render::ImageUsage::ColorWrite);
						},
						[this](VkCommandBuffer cmd, engine::render::Registry& reg) {
							if (!m_tonemapPass.IsValid()) return;

							engine::render::TonemapPass::TonemapParams tp{};
							tp.exposure = m_autoExposure.IsValid()
								? m_autoExposure.GetExposure()
								: static_cast<float>(m_cfg.GetDouble("tonemap.exposure", 1.0));
							bool lutEnabled = m_cfg.GetBool("color_grading.enable", false)
								&& m_colorGradingLutHandle.IsValid();
							tp.strength = lutEnabled
								? static_cast<float>(m_cfg.GetDouble("color_grading.strength", 1.0))
								: 0.0f;

							VkImageView lutView = VK_NULL_HANDLE;
							if (lutEnabled)
							{
								engine::render::TextureAsset* lutTex = m_colorGradingLutHandle.Get();
								if (lutTex && lutTex->view != VK_NULL_HANDLE)
									lutView = lutTex->view;
							}

							const uint32_t frameIdx = m_currentFrame % 2;
							m_tonemapPass.Record(
								m_vkDeviceContext.GetDevice(), cmd, reg,
								m_vkSwapchain.GetExtent(),
								m_fgSceneColorHDRWithBloomId,
								m_fgSceneColorLDRId,
								tp, lutView, frameIdx);
						});

					// Pass: TAA_InitHistory (M07.2) — when init/reset, copy current LDR to both history buffers.
					m_frameGraph.addPass("TAA_InitHistory",
						[this](engine::render::PassBuilder& b) {
							b.read(m_fgSceneColorLDRId, engine::render::ImageUsage::TransferSrc);
							b.write(m_fgHistoryAId, engine::render::ImageUsage::TransferDst);
							b.write(m_fgHistoryBId, engine::render::ImageUsage::TransferDst);
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
								// First frame: init both history buffers with current.
								VkImage dstA = reg.getImage(m_fgHistoryAId);
								VkImage dstB = reg.getImage(m_fgHistoryBId);
								if (dstA != VK_NULL_HANDLE && dstB != VK_NULL_HANDLE)
								{
									vkCmdCopyImage(cmd, srcImg, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
										dstA, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
									vkCmdCopyImage(cmd, srcImg, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
										dstB, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
									m_taaHistoryEverFilled = true;
								}
							}
							else
							{
								// Reset: copy current -> history next only.
								engine::render::ResourceId nextId = GetTaaHistoryNextId();
								VkImage dstNext = reg.getImage(nextId);
								if (dstNext != VK_NULL_HANDLE)
									vkCmdCopyImage(cmd, srcImg, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
										dstNext, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
							}
						});

					// Pass: TAA (M07.4) — read LDR + HistoryPrev + Velocity + Depth, write HistoryNext.
					m_frameGraph.addPass("TAA",
						[this](engine::render::PassBuilder& b) {
							b.read(m_fgSceneColorLDRId,  engine::render::ImageUsage::SampledRead);
							b.read(m_fgHistoryAId,      engine::render::ImageUsage::SampledRead);
							b.read(m_fgHistoryBId,      engine::render::ImageUsage::SampledRead);
							b.read(m_fgGBufferVelocityId, engine::render::ImageUsage::SampledRead);
							b.read(m_fgDepthId,         engine::render::ImageUsage::SampledRead);
							b.write(m_fgHistoryAId,     engine::render::ImageUsage::ColorWrite);
							b.write(m_fgHistoryBId,     engine::render::ImageUsage::ColorWrite);
						},
						[this](VkCommandBuffer cmd, engine::render::Registry& reg) {
							if (!m_taaPass.IsValid()) return;
							VkExtent2D extent = m_vkSwapchain.GetExtent();
							const uint32_t frameIdx = m_currentFrame % 2u;
							engine::render::TaaPass::TaaParams tp{};
							tp.alpha = 0.9f;
							tp._pad[0] = tp._pad[1] = tp._pad[2] = 0.0f;
							m_taaPass.Record(
								m_vkDeviceContext.GetDevice(), cmd, reg, extent,
								m_fgSceneColorLDRId, GetTaaHistoryPrevId(), m_fgGBufferVelocityId, m_fgDepthId,
								GetTaaHistoryNextId(), tp, frameIdx);
						});

					// Pass: CopyPresent — blit TAA output (HistoryNext) or LDR fallback → swapchain.
					m_frameGraph.addPass("CopyPresent",
						[this](engine::render::PassBuilder& b) {
							b.read(m_fgHistoryAId,       engine::render::ImageUsage::TransferSrc);
							b.read(m_fgHistoryBId,      engine::render::ImageUsage::TransferSrc);
							b.read(m_fgSceneColorLDRId,  engine::render::ImageUsage::TransferSrc);
							b.write(m_fgBackbufferId,    engine::render::ImageUsage::TransferDst);
						},
						[this](VkCommandBuffer cmd, engine::render::Registry& reg) {
							// TAA output is in HistoryNext; when TAA disabled, use LDR.
							engine::render::ResourceId srcId = m_taaPass.IsValid() ? GetTaaHistoryNextId() : m_fgSceneColorLDRId;
							VkImage srcImg = reg.getImage(srcId);
							VkImage dstImg = reg.getImage(m_fgBackbufferId);
							if (srcImg == VK_NULL_HANDLE || dstImg == VK_NULL_HANDLE) return;

							VkExtent2D ext = m_vkSwapchain.GetExtent();

							VkImageBlit region{};
							region.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
							region.srcOffsets[0]  = { 0, 0, 0 };
							region.srcOffsets[1]  = { static_cast<int32_t>(ext.width), static_cast<int32_t>(ext.height), 1 };
							region.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
							region.dstOffsets[0]  = { 0, 0, 0 };
							region.dstOffsets[1]  = { static_cast<int32_t>(ext.width), static_cast<int32_t>(ext.height), 1 };
							vkCmdBlitImage(cmd,
								srcImg, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
								dstImg, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
								1, &region, VK_FILTER_LINEAR);

							// Transition backbuffer to PRESENT_SRC for presentation.
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
							vkCmdPipelineBarrier(cmd,
								VK_PIPELINE_STAGE_TRANSFER_BIT,
								VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
								0, 0, nullptr, 0, nullptr, 1, &barrier);
						});

					// Validation pass (read-only, ensures 3+ passes compile and topological order).
					m_frameGraph.addPass("PostRead",
						[this](engine::render::PassBuilder& b) {
							b.read(m_fgSceneColorId, engine::render::ImageUsage::SampledRead);
						},
						[](VkCommandBuffer, engine::render::Registry&) {});

					// Asset cache smoke test.
					engine::render::MeshHandle    h2 = m_assetRegistry.LoadMesh("meshes/test.mesh");
					engine::render::TextureHandle t1 = m_assetRegistry.LoadTexture("textures/test.texr", false);
					engine::render::TextureHandle t2 = m_assetRegistry.LoadTexture("textures/test.texr", false);
					if (m_geometryMeshHandle.IsValid() && h2.IsValid() && m_geometryMeshHandle.Id() == h2.Id()) { /* cache OK */ }
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

		// FS smoke.
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

			if (m_vsync)
			{
				constexpr auto target = std::chrono::microseconds(16666);
				const auto elapsed = now - lastPresent;
				if (elapsed < target)
					std::this_thread::sleep_for(target - elapsed);
				lastPresent = std::chrono::steady_clock::now();
			}
			else
			{
				lastPresent = now;
			}
		}

		// Destroy in reverse order: passes → frame graph → swapchain → device → instance.
		if (m_vkDeviceContext.IsValid())
		{
			vkDeviceWaitIdle(m_vkDeviceContext.GetDevice());
			m_ssaoBlurPass.Destroy(m_vkDeviceContext.GetDevice());        // M06.3
			m_ssaoPass.Destroy(m_vkDeviceContext.GetDevice());           // M06.2
			m_ssaoKernelNoise.Destroy(m_vkDeviceContext.GetDevice());     // M06.1
			m_specularPrefilterPass.Destroy(m_vkDeviceContext.GetDevice()); // M05.3
			m_brdfLutPass.Destroy(m_vkDeviceContext.GetDevice());   // M05.1
			m_taaPass.Destroy(m_vkDeviceContext.GetDevice());      // M07.4
			m_autoExposure.Destroy(m_vkDeviceContext.GetDevice());  // M08.3
			m_bloomCombinePass.Destroy(m_vkDeviceContext.GetDevice());   // M08.2
			m_bloomUpsamplePass.Destroy(m_vkDeviceContext.GetDevice()); // M08.2
			m_bloomDownsamplePass.Destroy(m_vkDeviceContext.GetDevice()); // M08.1
			m_bloomPrefilterPass.Destroy(m_vkDeviceContext.GetDevice());  // M08.1
			m_tonemapPass.Destroy(m_vkDeviceContext.GetDevice());  // M03.4
			m_lightingPass.Destroy(m_vkDeviceContext.GetDevice()); // M03.2
			m_geometryPass.Destroy(m_vkDeviceContext.GetDevice());
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
			OnQuit();

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

		// M09.1: World model — update required chunks from player position (hysteresis).
		m_world.Update(out.camera.position);

		if (m_width > 0 && m_height > 0)
			out.camera.aspect = static_cast<float>(m_width) / static_cast<float>(m_height);

		// M07.1: Reset TAA history on FOV change (resize handled in OnResize).
		if (m_width > 0 && m_height > 0
			&& std::abs(out.camera.fovYDeg - readState.camera.fovYDeg) > 0.0001f)
			m_taaHistoryInvalid = true;

		out.viewMatrix = out.camera.ComputeViewMatrix();
		out.projMatrix = out.camera.ComputeProjectionMatrix();

		// M07.1: Halton jitter in NDC, apply to projection; store prev/curr ViewProj + jitter.
		const uint32_t taaSampleIndex = m_currentFrame % engine::render::kTaaHaltonN;
		float jitterX = 0.0f;
		float jitterY = 0.0f;
		if (!m_taaHistoryInvalid && m_width > 0 && m_height > 0)
			engine::render::GetJitterNdc(taaSampleIndex,
				static_cast<uint32_t>(m_width), static_cast<uint32_t>(m_height),
				jitterX, jitterY);
		// Apply NDC jitter to projection (column-major: add to z column so NDC.x += jitterX, NDC.y += jitterY).
		out.projMatrix.m[8] += jitterX;
		out.projMatrix.m[9] += jitterY;

		out.viewProjMatrix = out.projMatrix * out.viewMatrix;
		out.jitterCurrNdc[0] = jitterX;
		out.jitterCurrNdc[1] = jitterY;
		out.prevViewProjMatrix = m_taaHistoryInvalid ? out.viewProjMatrix : readState.viewProjMatrix;
		if (m_taaHistoryInvalid)
			m_taaHistoryInvalid = false;

		out.frustum.ExtractFromMatrix(out.viewProjMatrix);

		// M09.5: build draw list (HLOD vs instances, culling); debug overlay text.
		{
			const float maxDrawDist = static_cast<float>(m_cfg.GetDouble("world.max_draw_distance_m", 0.0));
			out.hlodDebugText = engine::world::BuildChunkDrawList(
				m_world.GetPendingChunkRequests(),
				out.camera.position,
				out.frustum,
				m_hlodRuntime,
				maxDrawDist,
				m_chunkDrawDecisions);
			if ((m_currentFrame % 60) == 0 && !out.hlodDebugText.empty())
				LOG_DEBUG(World, "M09.5 {}", out.hlodDebugText);
		}

		// M04.1: compute cascaded shadow matrices and split distances for a default sun light.
		{
			const engine::math::Vec3 lightDirTowardLight(0.5774f, 0.5774f, 0.5774f);
			const float lambda = 0.7f;
			const float worldUnitsPerTexel =
				static_cast<float>(m_cfg.GetDouble("shadows.csm_world_units_per_texel", 1.0));
			engine::render::ComputeCascades(out.camera, lightDirTowardLight, lambda,
				worldUnitsPerTexel, out.cascades);
		}

		// Placeholder: simulate some frame-arena allocations for MemTag::Temp test.
		for (int i = 0; i < 256; ++i)
			(void)m_frameArena.alloc(64, alignof(std::max_align_t), engine::core::memory::MemTag::Temp);
		out.drawItemCount = 256;
	}

	void Engine::Render()
	{
		if (!m_vkDeviceContext.IsValid() || !m_vkSwapchain.IsValid() || m_frameResources[0].cmdPool == VK_NULL_HANDLE)
			return;

		const uint32_t frameIndex    = m_currentFrame % 2;
		engine::render::FrameResources& fr = m_frameResources[frameIndex];
		::VkDevice device            = m_vkDeviceContext.GetDevice();
		VkQueue    graphicsQueue     = m_vkDeviceContext.GetGraphicsQueue();
		VkQueue    presentQueue      = m_vkDeviceContext.GetPresentQueue();
		VkSwapchainKHR swapchain     = m_vkSwapchain.GetSwapchain();
		VkExtent2D extent            = m_vkSwapchain.GetExtent();

		vkWaitForFences(device, 1, &fr.fence, VK_TRUE, UINT64_MAX);

		// M08.3: After fence, staging has last frame's luminance; adapt exposure (key/speed from config).
		if (m_autoExposure.IsValid())
		{
			const float dt    = static_cast<float>(m_time.DeltaSeconds());
			const float key   = static_cast<float>(m_cfg.GetDouble("exposure.key", 0.18));
			const float speed = static_cast<float>(m_cfg.GetDouble("exposure.speed", 2.0));
			m_autoExposure.Update(device, dt, key, speed);
		}

		uint32_t imageIndex = 0;
		VkResult result = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, fr.imageAvailable, VK_NULL_HANDLE, &imageIndex);
		if (result == VK_ERROR_OUT_OF_DATE_KHR)
		{
			m_swapchainResizeRequested = true;
			return;
		}
		if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
			return;

		vkResetCommandPool(device, fr.cmdPool, 0);

		VkCommandBufferBeginInfo beginInfo{};
		beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		if (vkBeginCommandBuffer(fr.cmdBuffer, &beginInfo) != VK_SUCCESS)
			return;

		if (m_fgSceneColorHDRId != engine::render::kInvalidResourceId
			&& m_fgBackbufferId != engine::render::kInvalidResourceId)
		{
			VkImage     backbufferImage = m_vkSwapchain.GetImage(imageIndex);
			VkImageView backbufferView  = m_vkSwapchain.GetImageView(imageIndex);
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
			return;

		VkSemaphore          waitSemaphores[]  = { fr.imageAvailable };
		VkPipelineStageFlags waitStages[]      = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
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
		if (vkQueueSubmit(graphicsQueue, 1, &submitInfo, fr.fence) != VK_SUCCESS)
			return;

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
		// M09.2: log chunk/ring stats periodically (e.g. ~1 Hz at 60 fps).
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
		// prev = idx^1, next = idx; idx = m_currentFrame % 2 (write target this frame).
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
		m_taaHistoryInvalid = true; // M07.1: reset TAA history on resize
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
