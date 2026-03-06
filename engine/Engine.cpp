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

					// Load test mesh.
					m_geometryMeshHandle = m_assetRegistry.LoadMesh("meshes/test.mesh");

					// --------------------------------------------------
					// Frame graph passes
					// --------------------------------------------------

					// Pass: Geometry — fills GBuffer A/B/C + Depth.
					m_frameGraph.addPass("Geometry",
						[this](engine::render::PassBuilder& b) {
							b.write(m_fgGBufferAId, engine::render::ImageUsage::ColorWrite);
							b.write(m_fgGBufferBId, engine::render::ImageUsage::ColorWrite);
							b.write(m_fgGBufferCId, engine::render::ImageUsage::ColorWrite);
							b.write(m_fgDepthId,    engine::render::ImageUsage::DepthWrite);
						},
						[this](VkCommandBuffer cmd, engine::render::Registry& reg) {
							const uint32_t readIdx = m_renderReadIndex.load(std::memory_order_acquire);
							const engine::math::Mat4& viewProj = m_renderStates[readIdx].viewProjMatrix;
							engine::render::MeshAsset* mesh = m_geometryMeshHandle.Get();
							m_geometryPass.Record(
								m_vkDeviceContext.GetDevice(), cmd, reg,
								m_vkSwapchain.GetExtent(),
								m_fgGBufferAId, m_fgGBufferBId, m_fgGBufferCId, m_fgDepthId,
								viewProj.m, mesh);
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

					// Pass: Lighting (M03.2) — reads GBuffer + Depth, writes SceneColor_HDR.
					m_frameGraph.addPass("Lighting",
						[this](engine::render::PassBuilder& b) {
							b.read(m_fgGBufferAId,       engine::render::ImageUsage::SampledRead);
							b.read(m_fgGBufferBId,       engine::render::ImageUsage::SampledRead);
							b.read(m_fgGBufferCId,       engine::render::ImageUsage::SampledRead);
							b.read(m_fgDepthId,          engine::render::ImageUsage::SampledRead);
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

							// Constant ambient (IBL placeholder — very dark blue-grey).
							lp.ambientColor[0] = 0.03f;
							lp.ambientColor[1] = 0.03f;
							lp.ambientColor[2] = 0.05f;
							lp.ambientColor[3] = 0.0f;

							const uint32_t frameIdx = m_currentFrame % 2;
							m_lightingPass.Record(
								m_vkDeviceContext.GetDevice(), cmd, reg,
								m_vkSwapchain.GetExtent(),
								m_fgGBufferAId, m_fgGBufferBId, m_fgGBufferCId, m_fgDepthId,
								m_fgSceneColorHDRId, lp, frameIdx);
						});

					// Pass: Tonemap (M03.4) — reads SceneColor_HDR, applies ACES filmic +
					// exposure + gamma 2.2, writes SceneColor_LDR.
					m_frameGraph.addPass("Tonemap",
						[this](engine::render::PassBuilder& b) {
							b.read(m_fgSceneColorHDRId,  engine::render::ImageUsage::SampledRead);
							b.write(m_fgSceneColorLDRId, engine::render::ImageUsage::ColorWrite);
						},
						[this](VkCommandBuffer cmd, engine::render::Registry& reg) {
							if (!m_tonemapPass.IsValid()) return;

							// Read exposure from config (default 1.0, adjustable via config.json).
							engine::render::TonemapPass::TonemapParams tp{};
							tp.exposure = static_cast<float>(m_cfg.GetDouble("tonemap.exposure", 1.0));

							const uint32_t frameIdx = m_currentFrame % 2;
							m_tonemapPass.Record(
								m_vkDeviceContext.GetDevice(), cmd, reg,
								m_vkSwapchain.GetExtent(),
								m_fgSceneColorHDRId,
								m_fgSceneColorLDRId,
								tp, frameIdx);
						});

					// Pass: CopyPresent — blit SceneColor_LDR (UNORM) → swapchain.
					m_frameGraph.addPass("CopyPresent",
						[this](engine::render::PassBuilder& b) {
							b.read(m_fgSceneColorLDRId, engine::render::ImageUsage::TransferSrc);
							b.write(m_fgBackbufferId,   engine::render::ImageUsage::TransferDst);
						},
						[this](VkCommandBuffer cmd, engine::render::Registry& reg) {
							VkImage srcImg = reg.getImage(m_fgSceneColorLDRId);
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
			m_brdfLutPass.Destroy(m_vkDeviceContext.GetDevice());   // M05.1
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

		if (m_width > 0 && m_height > 0)
			out.camera.aspect = static_cast<float>(m_width) / static_cast<float>(m_height);

		out.viewMatrix     = out.camera.ComputeViewMatrix();
		out.projMatrix     = out.camera.ComputeProjectionMatrix();
		out.viewProjMatrix = out.projMatrix * out.viewMatrix;
		out.frustum.ExtractFromMatrix(out.viewProjMatrix);

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
		// Nothing to do post-present for now.
	}

	void Engine::SwapRenderState()
	{
		const uint32_t readIdx  = m_renderReadIndex.load(std::memory_order_acquire);
		const uint32_t writeIdx = 1u - (readIdx & 1u);
		m_renderReadIndex.store(writeIdx, std::memory_order_release);
	}

	void Engine::OnResize(int w, int h)
	{
		m_width  = w;
		m_height = h;
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
