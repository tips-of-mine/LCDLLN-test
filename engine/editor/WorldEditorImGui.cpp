#include "engine/editor/WorldEditorImGui.h"

#include "engine/core/Config.h"
#include "engine/core/Log.h"
#include "engine/editor/TreeSpeciesCatalog.h"
#include "engine/editor/WorldEditorSession.h"
#include "engine/platform/FileSystem.h"
#include "engine/platform/Window.h"
#include "engine/render/terrain/HeightmapLoader.h"
#include "engine/render/vk/VkDeviceContext.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

#if defined(_WIN32)
#	ifndef NOMINMAX
#		define NOMINMAX
#	endif
#	define WIN32_LEAN_AND_MEAN
#	include <windows.h>

#	include "imgui.h"
#	include "imgui_impl_vulkan.h"
#	include "imgui_impl_win32.h"
	// ImGui 1.91+ : la déclaration n’est plus dans l’en-tête (#if 0) pour éviter HWND dans l’API publique.
	extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

#	include <vulkan/vulkan.h>
#endif

namespace engine::editor
{
	void WorldEditorImGui::SetEditorContext(WorldEditorSession* session, engine::core::Config* cfg)
	{
		m_session = session;
		m_cfg = cfg;
	}

#if defined(_WIN32)
	namespace
	{
		void TryPersistMovementLayoutToUserSettings(std::string_view layout)
		{
			const char* path = "user_settings.json";
			std::ifstream in(path, std::ios::binary);
			if (!in)
			{
				return;
			}
			std::string json((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
			const std::string needle = "\"movement_layout\": \"";
			const size_t start = json.find(needle);
			if (start == std::string::npos)
			{
				return;
			}
			const size_t valueStart = start + needle.size();
			const size_t valueEnd = json.find('"', valueStart);
			if (valueEnd == std::string::npos)
			{
				return;
			}
			json.replace(valueStart, valueEnd - valueStart, layout);
			std::ofstream out(path, std::ios::binary | std::ios::trunc);
			if (!out)
			{
				LOG_WARN(Core, "[WorldEditor] Écriture impossible : {} (déplacement non persisté)", path);
				return;
			}
			out << json;
		}

		void CheckVk(VkResult err)
		{
			if (err == VK_SUCCESS)
			{
				return;
			}
			LOG_ERROR(Render, "[WorldEditorImGui] Vulkan erreur: {}", static_cast<int>(err));
		}

		bool BeginDynamicRenderingUi(VkCommandBuffer cmd,
			VkImageView backbufferView,
			VkExtent2D ext,
			const engine::render::VkDeviceContext& ctx,
			bool& outUsedKhr)
		{
			outUsedKhr = false;
			VkRenderingAttachmentInfo colorAttachment{};
			colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
			colorAttachment.imageView = backbufferView;
			colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
			colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

			VkRenderingAttachmentInfoKHR colorAttachmentKHR{};
			colorAttachmentKHR.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR;
			colorAttachmentKHR.imageView = backbufferView;
			colorAttachmentKHR.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			colorAttachmentKHR.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
			colorAttachmentKHR.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

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

			const PFN_vkCmdBeginRendering pfnBeginCore = ctx.GetCmdBeginRenderingCore();
			const PFN_vkCmdEndRendering pfnEndCoreStored = ctx.GetCmdEndRenderingCore();
			const PFN_vkCmdBeginRenderingKHR pfnBeginKHR = ctx.GetCmdBeginRenderingKHR();
			const PFN_vkCmdEndRenderingKHR pfnEndKHRStored = ctx.GetCmdEndRenderingKHR();

			if (pfnBeginKHR && pfnEndKHRStored)
			{
				pfnBeginKHR(cmd, &renderingInfoKHR);
				outUsedKhr = true;
				return true;
			}
			if (pfnBeginCore && pfnEndCoreStored)
			{
				pfnBeginCore(cmd, &renderingInfo);
				outUsedKhr = false;
				return true;
			}
			return false;
		}

		void EndDynamicRenderingUi(VkCommandBuffer cmd,
			const engine::render::VkDeviceContext& ctx,
			bool usedKhr)
		{
			const PFN_vkCmdEndRendering pfnEndCore = ctx.GetCmdEndRenderingCore();
			const PFN_vkCmdEndRenderingKHR pfnEndKHR = ctx.GetCmdEndRenderingKHR();
			if (usedKhr && pfnEndKHR)
			{
				pfnEndKHR(cmd);
			}
			else if (!usedKhr && pfnEndCore)
			{
				pfnEndCore(cmd);
			}
		}
	} // namespace
#endif

#if defined(_WIN32)
	namespace
	{
		// Plafond du nombre de lignes de grille par axe (surimpression ImGui). Sans cela, une maille
		// fine sur un grand terrain génère trop de primitives ; un plafond trop bas rend la maille
		// « figée » (espacement minimal ≈ tailleTerrain / (plafond - 1)).
		constexpr int kWorldEditorGridMaxLinesPerAxis = 2048;

		bool WorldToScreen(const float vp[16], float wx, float wy, float wz, int vw, int vh, float& sx, float& sy)
		{
			const float cx = vp[0] * wx + vp[4] * wy + vp[8] * wz + vp[12];
			const float cy = vp[1] * wx + vp[5] * wy + vp[9] * wz + vp[13];
			const float cz = vp[2] * wx + vp[6] * wy + vp[10] * wz + vp[14];
			const float cw = vp[3] * wx + vp[7] * wy + vp[11] * wz + vp[15];
			if (cw <= 1e-5f)
			{
				return false;
			}
			const float invW = 1.0f / cw;
			const float ndcX = cx * invW;
			const float ndcY = cy * invW;
			const float ndcZ = cz * invW;
			if (ndcZ < 0.0f || ndcZ > 1.0f)
			{
				return false;
			}
			sx = (ndcX * 0.5f + 0.5f) * static_cast<float>(vw);
			sy = (1.0f - (ndcY * 0.5f + 0.5f)) * static_cast<float>(vh);
			return true;
		}

		bool TerrainWorldY(const engine::render::terrain::HeightmapData* hm,
			float ox, float oz, float ws, float hScale, float wx, float wz, float& yOut)
		{
			if (!hm || hm->width == 0 || hm->height == 0 || ws <= 0.0f)
			{
				return false;
			}
			const float u = (wx - ox) / ws;
			const float v = (wz - oz) / ws;
			if (u < 0.0f || u > 1.0f || v < 0.0f || v > 1.0f)
			{
				return false;
			}
			yOut = hm->SampleBilinearNorm(u, v) * hScale;
			return true;
		}

		void DrawSegmentedWorldLine(ImDrawList* dl, const float vp[16], int vw, int vh, ImU32 col,
			float x0, float y0, float z0, float x1, float y1, float z1, int segments)
		{
			if (segments < 1)
			{
				return;
			}
			ImVec2 prev{};
			bool has = false;
			for (int i = 0; i <= segments; ++i)
			{
				const float t = static_cast<float>(i) / static_cast<float>(segments);
				const float x = x0 + (x1 - x0) * t;
				const float y = y0 + (y1 - y0) * t;
				const float z = z0 + (z1 - z0) * t;
				float sx, sy;
				if (WorldToScreen(vp, x, y, z, vw, vh, sx, sy))
				{
					if (has)
					{
						dl->AddLine(prev, ImVec2(sx, sy), col);
					}
					prev = ImVec2(sx, sy);
					has = true;
				}
				else
				{
					has = false;
				}
			}
		}

		void DrawViewportOverlaysImpl(const WorldEditorViewportOverlayDesc& d)
		{
			if (!d.viewProjColMajor || d.viewportWidth <= 0 || d.viewportHeight <= 0)
			{
				return;
			}
			ImDrawList* dl = ImGui::GetForegroundDrawList();
			const float* vp = d.viewProjColMajor;
			const int vw = d.viewportWidth;
			const int vh = d.viewportHeight;
			const float ox = d.terrainOriginX;
			const float oz = d.terrainOriginZ;
			const float ws = d.terrainWorldSize;
			const float cell = std::max(0.5f, d.gridCellMeters);
			const ImU32 gridCol = IM_COL32(180, 220, 255, 110);
			const ImU32 brushCol = IM_COL32(255, 200, 80, 200);

			if (d.showGrid && d.heightmap && ws > 0.0f)
			{
				int nz = static_cast<int>(std::ceil(ws / cell)) + 1;
				if (nz > kWorldEditorGridMaxLinesPerAxis)
				{
					nz = kWorldEditorGridMaxLinesPerAxis;
				}
				int nx = static_cast<int>(std::ceil(ws / cell)) + 1;
				if (nx > kWorldEditorGridMaxLinesPerAxis)
				{
					nx = kWorldEditorGridMaxLinesPerAxis;
				}
				const int lineSegments = std::clamp(200000 / std::max(1, nx + nz), 8, 48);
				const float stepZ = ws / static_cast<float>(std::max(1, nz - 1));
				for (int iz = 0; iz < nz; ++iz)
				{
					const float z = oz + static_cast<float>(iz) * stepZ;
					float y0 = 0.f, y1 = 0.f;
					if (!TerrainWorldY(d.heightmap, ox, oz, ws, d.heightScale, ox, z, y0)
						|| !TerrainWorldY(d.heightmap, ox, oz, ws, d.heightScale, ox + ws, z, y1))
					{
						continue;
					}
					DrawSegmentedWorldLine(dl, vp, vw, vh, gridCol, ox, y0, z, ox + ws, y1, z, lineSegments);
				}
				const float stepX = ws / static_cast<float>(std::max(1, nx - 1));
				for (int ix = 0; ix < nx; ++ix)
				{
					const float x = ox + static_cast<float>(ix) * stepX;
					float y0 = 0.f, y1 = 0.f;
					if (!TerrainWorldY(d.heightmap, ox, oz, ws, d.heightScale, x, oz, y0)
						|| !TerrainWorldY(d.heightmap, ox, oz, ws, d.heightScale, x, oz + ws, y1))
					{
						continue;
					}
					DrawSegmentedWorldLine(dl, vp, vw, vh, gridCol, x, y0, oz, x, y1, oz + ws, lineSegments);
				}
			}

			if (d.layoutInstancesOverlay != nullptr)
			{
				const ImU32 colSel = IM_COL32(255, 90, 255, 230);
				const ImU32 colNorm = IM_COL32(90, 255, 140, 200);
				for (size_t ii = 0; ii < d.layoutInstancesOverlay->size(); ++ii)
				{
					const engine::editor::WorldMapEditLayoutInstance& inst = (*d.layoutInstancesOverlay)[ii];
					const float wx = static_cast<float>(inst.worldX);
					const float wy = static_cast<float>(inst.worldY);
					const float wz = static_cast<float>(inst.worldZ);
					float sx = 0.f;
					float sy = 0.f;
					if (WorldToScreen(vp, wx, wy, wz, vw, vh, sx, sy))
					{
						const ImU32 c = (static_cast<int>(ii) == d.selectedLayoutInstanceOverlay) ? colSel : colNorm;
						dl->AddCircleFilled(ImVec2(sx, sy), 7.f, c, 16);
						dl->AddCircle(ImVec2(sx, sy), 8.f, IM_COL32(255, 255, 255, 160), 16, 1.5f);
					}
				}
			}

			if (d.showBrushPreview && d.heightmap && d.brushRadiusMeters > 0.0f)
			{
				float cy = 0.f;
				if (TerrainWorldY(d.heightmap, ox, oz, ws, d.heightScale, d.brushWorldX, d.brushWorldZ, cy))
				{
					float sx0, sy0, sx1, sy1;
					if (WorldToScreen(vp, d.brushWorldX, cy, d.brushWorldZ, vw, vh, sx0, sy0))
					{
						float yEdge = cy;
						const float wx1 = d.brushWorldX + d.brushRadiusMeters;
						(void)TerrainWorldY(d.heightmap, ox, oz, ws, d.heightScale, wx1, d.brushWorldZ, yEdge);
						if (WorldToScreen(vp, wx1, yEdge, d.brushWorldZ, vw, vh, sx1, sy1))
						{
							const float dx = sx1 - sx0;
							const float dy = sy1 - sy0;
							const float radPix = std::sqrt(dx * dx + dy * dy);
							if (radPix > 1.5f)
							{
								dl->AddCircle(ImVec2(sx0, sy0), radPix, brushCol, 0, 2.0f);
							}
						}
					}
				}
			}
		}
	} // namespace
#endif

	WorldEditorImGui::~WorldEditorImGui()
	{
#if defined(_WIN32)
		if (m_ready)
		{
			LOG_WARN(Render, "[WorldEditorImGui] Shutdown non appelé avant destruction");
		}
#endif
	}

	bool WorldEditorImGui::Init(VkInstance instance,
		const engine::render::VkDeviceContext& deviceContext,
		VkFormat swapchainFormat,
		uint32_t swapchainImageCount,
		uint32_t vulkanApiVersion,
		void* hwndNative,
		const engine::core::Config* cfg)
	{
#if !defined(_WIN32)
		(void)instance;
		(void)deviceContext;
		(void)swapchainFormat;
		(void)swapchainImageCount;
		(void)vulkanApiVersion;
		(void)hwndNative;
		(void)cfg;
		return false;
#else
		if (m_ready)
		{
			return true;
		}
		if (!deviceContext.IsValid() || !deviceContext.SupportsDynamicRendering())
		{
			LOG_WARN(Render, "[WorldEditorImGui] Init ignoré: device ou dynamic rendering indisponible");
			return false;
		}
		HWND hwnd = hwndNative ? static_cast<HWND>(hwndNative) : nullptr;
		if (!hwnd)
		{
			LOG_WARN(Render, "[WorldEditorImGui] Init ignoré: HWND nul");
			return false;
		}
		const uint32_t imgCount = std::max(2u, swapchainImageCount);

		VkDescriptorPoolSize poolSizes[] = {
			{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 64 },
		};
		VkDescriptorPoolCreateInfo poolInfo{};
		poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
		poolInfo.maxSets = 64;
		poolInfo.poolSizeCount = static_cast<uint32_t>(std::size(poolSizes));
		poolInfo.pPoolSizes = poolSizes;
		if (vkCreateDescriptorPool(deviceContext.GetDevice(), &poolInfo, nullptr, &m_descriptorPool) != VK_SUCCESS)
		{
			LOG_ERROR(Render, "[WorldEditorImGui] vkCreateDescriptorPool a échoué");
			return false;
		}

		IMGUI_CHECKVERSION();
		ImGui::CreateContext();
		ImGuiIO& io = ImGui::GetIO();
		io.IniFilename = "world_editor_imgui.ini";
		io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
		// Pas de NavEnableKeyboard : sinon io.WantCaptureKeyboard reste souvent vrai et bloque WASD (caméra FPS).
		ImGui::StyleColorsDark();

		// Polices auth (Windlass / Morpheus) chargées dans l'atlas ImGui AVANT ImGui_ImplVulkan_Init —
		// celui-ci construit la texture des fonts une seule fois à partir de l'atlas. Sans ce chargement,
		// l'UI auth utilise la police ImGui par défaut (ProggyClean ~13 px) qui ne ressemble pas à la
		// maquette Lune Noire. La piste Vulkan/AuthGlyphPass utilise déjà ces mêmes fichiers (Engine.cpp).
		// On charge Windlass en premier : elle devient la police par défaut d'ImGui.
		auto loadAuthFontFromConfig = [&io, cfg](std::string_view relativePath, float pixelHeight, const char* role) -> bool {
			if (cfg == nullptr || relativePath.empty())
			{
				return false;
			}
			std::vector<uint8_t> bytes = engine::platform::FileSystem::ReadAllBytesContent(*cfg, relativePath);
			if (bytes.empty())
			{
				LOG_WARN(Render, "[WorldEditorImGui] Police {} introuvable ou vide : {}", role, relativePath);
				return false;
			}
			// L'atlas ImGui prend la propriété du buffer (FontDataOwnedByAtlas par défaut = true) et le
			// libérera via IM_FREE — on doit donc l'allouer via IM_ALLOC, pas réutiliser le std::vector
			// (sinon UB : la mémoire serait libérée deux fois ou utilisée après libération).
			void* atlasOwned = IM_ALLOC(bytes.size());
			std::memcpy(atlasOwned, bytes.data(), bytes.size());
			ImFontConfig fcfg{};
			ImFont* font = io.Fonts->AddFontFromMemoryTTF(atlasOwned, static_cast<int>(bytes.size()), pixelHeight,
				&fcfg, io.Fonts->GetGlyphRangesDefault());
			if (font == nullptr)
			{
				IM_FREE(atlasOwned);
				LOG_WARN(Render, "[WorldEditorImGui] Police {} : AddFontFromMemoryTTF a échoué pour {}", role, relativePath);
				return false;
			}
			LOG_INFO(Render, "[WorldEditorImGui] Police {} chargée dans l'atlas ImGui : {} ({}px)", role, relativePath, pixelHeight);
			return true;
		};

		if (cfg != nullptr)
		{
			// Clé spécifique à la piste ImGui : la piste Vulkan/AuthGlyphPass utilise
			// render.auth_ui.font_pixel_height = 28 px par défaut, mais en ImGui les facteurs
			// SetWindowFontScale (1.62 pour le titre, 1.12 pour le sous-titre…) étaient calibrés
			// pour la police par défaut 13 px — un atlas Windlass à 28 px multiplié par 1.62
			// donnait un titre à 45 px qui débordait des panneaux. On charge donc Windlass plus
			// petit pour ImGui et on laisse la piste Vulkan inchangée.
			const std::string uiFontPath = cfg->GetString("render.auth_ui.font_path", "");
			const float uiFontPx = static_cast<float>(std::clamp<int64_t>(
				cfg->GetInt("render.auth_ui.imgui.font_pixel_height", 16), 12, 64));
			loadAuthFontFromConfig(uiFontPath, uiFontPx, "UI");

			const std::string valueFontPath = cfg->GetString("render.auth_ui.value_font_path", "");
			const float valueFontPx = static_cast<float>(std::clamp<int64_t>(
				cfg->GetInt("render.auth_ui.imgui.value_font_pixel_height", 14), 12, 64));
			loadAuthFontFromConfig(valueFontPath, valueFontPx, "valeurs");
		}

		ImGui_ImplWin32_Init(hwnd);

		VkPipelineRenderingCreateInfoKHR pipelineRendering{};
		pipelineRendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR;
		pipelineRendering.colorAttachmentCount = 1;
		pipelineRendering.pColorAttachmentFormats = &swapchainFormat;

		ImGui_ImplVulkan_InitInfo vulkanInfo{};
		vulkanInfo.ApiVersion = vulkanApiVersion;
		vulkanInfo.Instance = instance;
		vulkanInfo.PhysicalDevice = deviceContext.GetPhysicalDevice();
		vulkanInfo.Device = deviceContext.GetDevice();
		vulkanInfo.QueueFamily = deviceContext.GetGraphicsQueueFamilyIndex();
		vulkanInfo.Queue = deviceContext.GetGraphicsQueue();
		vulkanInfo.DescriptorPool = m_descriptorPool;
		vulkanInfo.MinImageCount = imgCount;
		vulkanInfo.ImageCount = imgCount;
		vulkanInfo.PipelineCache = VK_NULL_HANDLE;
		vulkanInfo.Subpass = 0;
		vulkanInfo.DescriptorPoolSize = 0;
		vulkanInfo.UseDynamicRendering = true;
		vulkanInfo.PipelineRenderingCreateInfo = pipelineRendering;
		vulkanInfo.Allocator = nullptr;
		vulkanInfo.CheckVkResultFn = CheckVk;
		vulkanInfo.MinAllocationSize = 0;

		if (!ImGui_ImplVulkan_Init(&vulkanInfo))
		{
			LOG_ERROR(Render, "[WorldEditorImGui] ImGui_ImplVulkan_Init a échoué");
			ImGui_ImplWin32_Shutdown();
			ImGui::DestroyContext();
			vkDestroyDescriptorPool(deviceContext.GetDevice(), m_descriptorPool, nullptr);
			m_descriptorPool = VK_NULL_HANDLE;
			return false;
		}

		m_ready = true;
		LOG_INFO(Render, "[WorldEditorImGui] Init OK (ImGui + Vulkan dynamic rendering)");
		return true;
#endif
	}

	void WorldEditorImGui::Shutdown(VkDevice device)
	{
#if defined(_WIN32)
		if (!m_ready)
		{
			return;
		}
		vkDeviceWaitIdle(device);
		ImGui_ImplVulkan_Shutdown();
		ImGui_ImplWin32_Shutdown();
		ImGui::DestroyContext();
		if (m_descriptorPool != VK_NULL_HANDLE)
		{
			vkDestroyDescriptorPool(device, m_descriptorPool, nullptr);
			m_descriptorPool = VK_NULL_HANDLE;
		}
		m_ready = false;
		LOG_INFO(Render, "[WorldEditorImGui] Shutdown OK");
#else
		(void)device;
#endif
	}

	void WorldEditorImGui::OnSwapchainRecreate(uint32_t swapchainImageCount)
	{
#if defined(_WIN32)
		if (m_ready && swapchainImageCount > 0)
		{
			ImGui_ImplVulkan_SetMinImageCount(std::max(2u, swapchainImageCount));
		}
#else
		(void)swapchainImageCount;
#endif
	}

	void WorldEditorImGui::NewFrame(float deltaSeconds, float displayWidth, float displayHeight)
	{
#if defined(_WIN32)
		if (!m_ready)
		{
			return;
		}
		ImGui_ImplVulkan_NewFrame();
		ImGui_ImplWin32_NewFrame();
		ImGui::NewFrame();
		ImGuiIO& io = ImGui::GetIO();
		io.DisplaySize = ImVec2(displayWidth, displayHeight);
		io.DeltaTime = deltaSeconds > 1e-6f ? deltaSeconds : 1.f / 60.f;
#else
		(void)deltaSeconds;
		(void)displayWidth;
		(void)displayHeight;
#endif
	}

	void WorldEditorImGui::BuildUi(const WorldEditorViewportOverlayDesc* viewportOverlay)
	{
#if defined(_WIN32)
		if (!m_ready)
		{
			return;
		}

		if (ImGui::BeginMainMenuBar())
		{
			if (ImGui::BeginMenu("Fichier"))
			{
				if (ImGui::MenuItem("Importer texture PNG → TEXR…", nullptr, false, m_session != nullptr && m_cfg != nullptr)
					&& m_session && m_cfg)
				{
					(void)m_session->ActionImportTexture(*m_cfg);
				}
				if (ImGui::MenuItem("Importer audio (copie fichier)…", nullptr, false, m_session != nullptr && m_cfg != nullptr)
					&& m_session && m_cfg)
				{
					(void)m_session->ActionImportAudio(*m_cfg);
				}
				ImGui::Separator();
				ImGui::MenuItem("Quitter", nullptr, false, false);
				ImGui::EndMenu();
			}
			if (ImGui::BeginMenu("Outils"))
			{
				ImGui::TextUnformatted("Peinture splat : panneau « Terrain (sculpt) », mode Splat.");
				ImGui::EndMenu();
			}
			if (ImGui::BeginMenu("Affichage"))
			{
				ImGui::TextUnformatted("Cycle jour/nuit: à brancher atmosphère.");
				ImGui::EndMenu();
			}
			if (m_cfg && ImGui::BeginMenu("Options"))
			{
				const std::string cur = m_cfg->GetString("controls.movement_layout", "wasd");
				const bool zqsdActive = (cur == "zqsd");
				if (ImGui::MenuItem("Déplacement : QWERTY (WASD)", nullptr, !zqsdActive))
				{
					m_cfg->SetValue("controls.movement_layout", std::string("wasd"));
					TryPersistMovementLayoutToUserSettings("wasd");
				}
				if (ImGui::MenuItem("Déplacement : AZERTY (ZQSD)", nullptr, zqsdActive))
				{
					m_cfg->SetValue("controls.movement_layout", std::string("zqsd"));
					TryPersistMovementLayoutToUserSettings("zqsd");
				}
				ImGui::EndMenu();
			}
			ImGui::EndMainMenuBar();
		}

		ImGuiViewport* vp = ImGui::GetMainViewport();
		ImGui::SetNextWindowPos(vp->WorkPos);
		ImGui::SetNextWindowSize(vp->WorkSize);
		ImGui::SetNextWindowViewport(vp->ID);
		ImGuiWindowFlags hostFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize
			| ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus
			| ImGuiWindowFlags_NoBackground;
		ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.f, 0.f));
		if (ImGui::Begin("WorldEditorDockSpaceHost", nullptr, hostFlags))
		{
			const ImGuiID dockId = ImGui::GetID("WorldEditorDockSpace");
			// ImGuiDockNodeFlags_PassthroughCentralNode (1<<4) — littéral pour éviter les divergences d’en-têtes.
			ImGui::DockSpace(dockId, ImVec2(0.f, 0.f), static_cast<ImGuiDockNodeFlags>(1u << 4));
		}
		ImGui::End();
		ImGui::PopStyleVar(3);

		ImGui::Begin("Scène", nullptr, ImGuiWindowFlags_NoMouseInputs);
		ImGui::TextUnformatted("Vue 3D Vulkan (même moteur que le jeu).");
		ImGui::TextUnformatted(
			"Déplacement : menu « Options » → QWERTY (WASD) ou AZERTY (ZQSD), un seul à la fois. Shift = plus rapide ; "
			"vitesse de base augmente avec la taille du terrain chargé.");
		ImGui::TextUnformatted(
			"Orientation : maintenez le clic droit et déplacez la souris (même au-dessus des fenêtres ImGui) ; "
			"sinon la souris n’oriente que lorsqu’ImGui ne la capture pas. Molette : zoom FOV.");
		ImGui::TextUnformatted(
			"Si la vue monte quand vous baissez la souris : dans user_settings.json ou options client, "
			"activez controls.invert_y.");
		if (viewportOverlay != nullptr && viewportOverlay->viewProjColMajor != nullptr)
		{
			ImGui::Separator();
			ImGui::Text("Caméra (monde) : (%.2f, %.2f, %.2f) m", static_cast<double>(viewportOverlay->cameraWorldX),
				static_cast<double>(viewportOverlay->cameraWorldY), static_cast<double>(viewportOverlay->cameraWorldZ));
			ImGui::Text("Orientation : yaw %.1f°, pitch %.1f°", static_cast<double>(viewportOverlay->cameraYawDeg),
				static_cast<double>(viewportOverlay->cameraPitchDeg));
			ImGui::TextDisabled(
				"La grille et le rendu 3D utilisent la même matrice vue×projection chaque frame : ce n’est pas une grille « figée » "
				"qui oublierait de se rafraîchir. Si ces nombres ne bougent pas avec WASD / souris, la caméra ne reçoit pas l’entrée "
				"(focus ImGui, etc.). S’ils bougent mais l’image ne change pas, il s’agit d’un autre problème de rendu.");
		}
		ImGui::End();

		if (m_session && m_cfg)
		{
			ImGui::Begin("Carte (JSON édition)");
			ImGui::InputText("zone_id", m_session->BufZoneId().data(), m_session->BufZoneId().size());
			ImGui::InputText("taille (N×N)", m_session->BufSize().data(), m_session->BufSize().size());
			ImGui::InputTextWithHint("seed (optionnel)", "vide = aléatoire non fixé", m_session->BufSeed().data(), m_session->BufSeed().size());
			if (ImGui::Button("Nouvelle carte"))
			{
				(void)m_session->ActionNewMap(*m_cfg);
			}
			ImGui::Separator();
			ImGui::InputText("Charger JSON (chemin absolu)", m_session->BufLoadPath().data(), m_session->BufLoadPath().size());
			if (ImGui::Button("Charger"))
			{
				(void)m_session->ActionLoadEditJson(*m_cfg);
			}
			ImGui::InputText("Sauver JSON (chemin absolu)", m_session->BufSavePath().data(), m_session->BufSavePath().size());
			if (ImGui::Button("Sauvegarder édition"))
			{
				(void)m_session->ActionSaveEditJson(*m_cfg);
			}
			if (ImGui::Button("Exporter runtime (zones/<id>/)"))
			{
				(void)m_session->ActionExportRuntime(*m_cfg);
			}
			ImGui::Separator();
			ImGui::TextUnformatted("Heightmap (relatif content):");
			ImGui::TextWrapped("%s", m_session->Doc().heightmapContentRelativePath.c_str());
			ImGui::TextUnformatted("Splatmap SLAP (relatif content):");
			ImGui::TextWrapped("%s", m_session->Doc().splatmapContentRelativePath.c_str());
			ImGui::TextUnformatted("Masque herbe GRMS (relatif content):");
			ImGui::TextWrapped("%s", m_session->Doc().grassMaskContentRelativePath.c_str());
			if (ImGui::Button("Recharger terrain GPU"))
			{
				m_session->RequestTerrainGpuReload();
			}
			ImGui::End();

			ImGui::Begin("Affichage & grille");
			ImGui::Checkbox("Grille (aperçu écran)", &m_session->ShowGrid());
			ImGui::SliderFloat("Maille grille (m)", &m_session->GridCellMeters(), 1.f, 128.f, "%.1f");
			if (viewportOverlay && viewportOverlay->heightmap && viewportOverlay->terrainWorldSize > 0.f)
			{
				const float ws = viewportOverlay->terrainWorldSize;
				const float cellUi = std::max(0.5f, m_session->GridCellMeters());
				const int desiredLines = static_cast<int>(std::ceil(ws / cellUi)) + 1;
				if (desiredLines > kWorldEditorGridMaxLinesPerAxis)
				{
					const float minSpacing =
						ws / static_cast<float>(std::max(1, kWorldEditorGridMaxLinesPerAxis - 1));
					ImGui::TextColored(ImVec4(1.f, 0.82f, 0.35f, 1.f),
						"La grille est limitée à %d lignes par axe (performances). Avec un terrain de %.0f m, "
						"la maille affichée ne peut pas être plus fine qu’environ %.1f m tant que ce plafond s’applique.",
						kWorldEditorGridMaxLinesPerAxis, static_cast<double>(ws), static_cast<double>(minSpacing));
				}
			}
			ImGui::TextUnformatted("La grille est dessinée en surimpression (projection caméra) lorsque le terrain GPU est chargé.");
			ImGui::End();

			ImGui::Begin("Terrain (sculpt)");
			const char* modes[] = { "Heightmap (MNT)", "Splat (matériaux)", "Herbe (masque GRMS)", "Instances (layout)",
				"Routes (splat / 011)" };
			ImGui::Combo("Mode", &m_session->TerrainEditMode(), modes, IM_ARRAYSIZE(modes));
			{
				int& tm = m_session->TerrainEditMode();
				tm = std::clamp(tm, 0, 4);
			}
			if (m_session->TerrainEditMode() == 0)
			{
				const char* ops[] = { "Élever", "Abaisser", "Lisser", "Aplatir" };
				ImGui::Combo("Opération", &m_session->BrushOp(), ops, IM_ARRAYSIZE(ops));
			}
			else if (m_session->TerrainEditMode() == 1)
			{
				const char* layers[] = { "Herbe (R)", "Terre (G)", "Roc (B)", "Neige (A)" };
				ImGui::Combo("Couche splat", &m_session->SplatLayer(), layers, IM_ARRAYSIZE(layers));
			}
			else if (m_session->TerrainEditMode() == 2)
			{
				ImGui::Checkbox("Effacer (retire le masque)", &m_session->GrassMaskEraseBrush());
			}
			else if (m_session->TerrainEditMode() == 3)
			{
				ImGui::TextUnformatted("Utilisez le panneau « Instances (layout) » pour le type glTF.");
			}
			else
			{
				const char* rlayers[] = { "Herbe (R)", "Terre / macadam (G)", "Roc (B)", "Neige (A)" };
				ImGui::Combo("Couche route (splat)", &m_session->RouteSplatLayer(), rlayers, IM_ARRAYSIZE(rlayers));
				{
					int& rl = m_session->RouteSplatLayer();
					rl = std::clamp(rl, 0, 3);
				}
				ImGui::SliderFloat("Largeur bande (m)", &m_session->RouteStripWidthM(), 0.5f, 64.f, "%.1f");
				if (ImGui::Button("Effacer brouillon (points)"))
				{
					m_session->ClearRouteDraft();
				}
				ImGui::SameLine();
				if (ImGui::Button("Appliquer sur splat"))
				{
					m_session->RequestApplyRouteDraftToSplat();
				}
				ImGui::Text("Points brouillon : %zu", m_session->RouteDraftPoints().size());
				ImGui::Text("Routes enregistrées (doc) : %zu", m_session->Doc().routes.size());
			}
			if (m_session->TerrainEditMode() != 3 && m_session->TerrainEditMode() != 4)
			{
				ImGui::SliderFloat("Rayon (m)", &m_session->BrushRadius(), 0.5f, 200.f, "%.1f");
				ImGui::SliderFloat("Intensité", &m_session->BrushStrength(), 0.01f, 1.f, "%.2f");
			}
			else if (m_session->TerrainEditMode() == 4)
			{
				ImGui::SliderFloat("Intensité splat (route)", &m_session->BrushStrength(), 0.01f, 1.f, "%.2f");
			}
			if (m_session->TerrainEditMode() == 0)
			{
				ImGui::TextUnformatted("Clic gauche maintenu (hors UI) : sculpte la heightmap.");
			}
			else if (m_session->TerrainEditMode() == 1)
			{
				ImGui::TextUnformatted("Clic gauche maintenu (hors UI) : peint la couche splat (aperçu GPU immédiat).");
				ImGui::TextUnformatted("Sauvegarder l’édition écrit aussi le fichier SLAP (voir chemin splatmap).");
			}
			else if (m_session->TerrainEditMode() == 2)
			{
				ImGui::TextUnformatted("Clic gauche maintenu : peint le masque herbe (GRMS, UV = splat). Sauvegarde écrit grass.grms.");
			}
			else if (m_session->TerrainEditMode() == 3)
			{
				ImGui::TextUnformatted("Clic gauche (hors UI, une fois) : pose ou déplace une instance au sol.");
			}
			else
			{
				ImGui::TextUnformatted(
					"011 branche A : clics gauches sur le sol pour enchaîner les sommets ; « Appliquer » peint une bande splat (export SLAP inchangé).");
			}
			ImGui::End();

			ImGui::Begin("Import assets");
			ImGui::InputText("PNG (chemin absolu)", m_session->BufPngPath().data(), m_session->BufPngPath().size());
			ImGui::InputTextWithHint("Nom .texr sous textures/", "ex: ui/editor_import.texr", m_session->BufTexrName().data(),
				m_session->BufTexrName().size());
			if (ImGui::Button("Convertir PNG → TEXR"))
			{
				(void)m_session->ActionImportTexture(*m_cfg);
			}
			ImGui::Separator();
			ImGui::InputText("Audio source (absolu)", m_session->BufAudioSrc().data(), m_session->BufAudioSrc().size());
			ImGui::InputTextWithHint("Dest sous audio/", "ex: editor/import.wav", m_session->BufAudioDest().data(),
				m_session->BufAudioDest().size());
			if (ImGui::Button("Copier audio"))
			{
				(void)m_session->ActionImportAudio(*m_cfg);
			}
			ImGui::End();

			ImGui::Begin("Instances (layout)");
			if (m_cfg != nullptr)
			{
				m_session->EnsureTreeCatalogLoaded(*m_cfg);
			}
			const char* placeKinds[] = { "Arbre (catalogue 013)", "Rocher (legacy zone_1)" };
			{
				int& pk = m_session->InstancePlacementKind();
				ImGui::Combo("Type à placer", &pk, placeKinds, IM_ARRAYSIZE(placeKinds));
				pk = std::clamp(pk, 0, 1);
			}
			if (m_session->InstancePlacementKind() == 0)
			{
				const std::vector<TreeSpeciesSpec>& specs = m_session->TreeCatalog().Species();
				if (specs.empty())
				{
					ImGui::TextColored(ImVec4(1.f, 0.6f, 0.2f, 1.f),
						"Catalogue arbres vide ou invalide (voir world_editor/tree_species_catalog.json).");
				}
				else
				{
					std::string speciesItems;
					for (const TreeSpeciesSpec& s : specs)
					{
						speciesItems += s.id;
						speciesItems.push_back('\0');
					}
					speciesItems.push_back('\0');
					int& si = m_session->TreeSpeciesUiIndex();
					const int prevSi = si;
					si = std::clamp(si, 0, static_cast<int>(specs.size()) - 1);
					ImGui::Combo("Espèce", &si, speciesItems.c_str());
					si = std::clamp(si, 0, static_cast<int>(specs.size()) - 1);
					if (si != prevSi)
					{
						m_session->TreeShapeVariantUiIndex() = 0;
					}
					const TreeSpeciesSpec& sp = specs[static_cast<size_t>(si)];
					std::string shapeItems;
					for (const TreeSpeciesShapeSpec& sh : sp.shapes)
					{
						std::string lab = sh.gltfContentRelativePath;
						const size_t slash = lab.find_last_of("/\\");
						if (slash != std::string::npos)
						{
							lab = lab.substr(slash + 1);
						}
						shapeItems += lab;
						shapeItems.push_back('\0');
					}
					shapeItems.push_back('\0');
					int& shi = m_session->TreeShapeVariantUiIndex();
					shi = std::clamp(shi, 0, static_cast<int>(sp.shapes.size()) - 1);
					ImGui::Combo("Forme (variante glTF)", &shi, shapeItems.c_str());
					shi = std::clamp(shi, 0, static_cast<int>(sp.shapes.size()) - 1);
					ImGui::SliderFloat("Taille (min–max espèce)", &m_session->TreeScaleT01(), 0.f, 1.f, "%.2f");
					ImGui::Checkbox("Échelle aléatoire à la pose", &m_session->TreeRandomizeScaleOnPlace());
					if (m_session->TreeRandomizeScaleOnPlace())
					{
						ImGui::TextDisabled("Le curseur taille est ignoré si aléatoire est coché.");
					}
				}
			}
			else
			{
				ImGui::TextUnformatted("Pose un rocher (zones/zone_1/zone_1.gltf), sans espèce catalogue.");
			}
			ImGui::TextUnformatted(
				"Sélectionnez une ligne pour déplacer au prochain clic. Sans sélection : nouvelle instance. Coordonnées monde + snap hauteur MNT.");
			ImGui::Separator();
			for (size_t i = 0; i < m_session->MutableDoc().layoutInstances.size(); ++i)
			{
				const engine::editor::WorldMapEditLayoutInstance& inst = m_session->MutableDoc().layoutInstances[i];
				char label[256];
				if (!inst.speciesId.empty())
				{
					std::snprintf(label, sizeof(label), "%s — %s #%u — scale %.3f##instrow%zu", inst.guid.c_str(), inst.speciesId.c_str(),
						static_cast<unsigned>(inst.shapeVariantIndex), inst.uniformScale, i);
				}
				else
				{
					std::snprintf(label, sizeof(label), "%s — %s##instrow%zu", inst.guid.c_str(), inst.gltfContentRelativePath.c_str(), i);
				}
				const bool sel = (m_session->SelectedLayoutInstanceIndex() == static_cast<int>(i));
				if (ImGui::Selectable(label, sel))
				{
					m_session->SelectedLayoutInstanceIndex() = static_cast<int>(i);
				}
				ImGui::SameLine();
				char delId[48];
				std::snprintf(delId, sizeof(delId), "Suppr##%zu", i);
				if (ImGui::SmallButton(delId))
				{
					m_session->RemoveLayoutInstance(i);
					break;
				}
			}
			{
				const int sel = m_session->SelectedLayoutInstanceIndex();
				if (sel >= 0 && static_cast<size_t>(sel) < m_session->MutableDoc().layoutInstances.size())
				{
					engine::editor::WorldMapEditLayoutInstance& inst = m_session->MutableDoc().layoutInstances[static_cast<size_t>(sel)];
					if (!inst.speciesId.empty())
					{
						const TreeSpeciesSpec* spec = m_session->TreeCatalog().FindById(inst.speciesId);
						if (spec != nullptr && !spec->shapes.empty())
						{
							ImGui::Separator();
							ImGui::TextUnformatted("Édition instance sélectionnée (arbre)");
							std::string shapeItemsSel;
							for (const TreeSpeciesShapeSpec& sh : spec->shapes)
							{
								std::string lab = sh.gltfContentRelativePath;
								const size_t slash = lab.find_last_of("/\\");
								if (slash != std::string::npos)
								{
									lab = lab.substr(slash + 1);
								}
								shapeItemsSel += lab;
								shapeItemsSel.push_back('\0');
							}
							shapeItemsSel.push_back('\0');
							int sh = static_cast<int>(inst.shapeVariantIndex);
							sh = std::clamp(sh, 0, static_cast<int>(spec->shapes.size()) - 1);
							(void)ImGui::Combo("Forme##edit_sel", &sh, shapeItemsSel.c_str());
							sh = std::clamp(sh, 0, static_cast<int>(spec->shapes.size()) - 1);
							inst.shapeVariantIndex = static_cast<uint32_t>(sh);
							inst.gltfContentRelativePath = spec->shapes[static_cast<size_t>(sh)].gltfContentRelativePath;
							float scf = static_cast<float>(inst.uniformScale);
							(void)ImGui::SliderFloat("Échelle##edit_sel", &scf, static_cast<float>(spec->scaleMin), static_cast<float>(spec->scaleMax),
								"%.3f");
							inst.uniformScale = static_cast<double>(scf);
						}
					}
				}
			}
			if (ImGui::Button("Aucune sélection (pose toujours une nouvelle instance)"))
			{
				m_session->SelectedLayoutInstanceIndex() = -1;
			}
			ImGui::End();

			ImGui::Begin("Préfabs / objets", nullptr, ImGuiWindowFlags_NoMouseInputs);
			ImGui::TextUnformatted("Champ JSON « objects » (préfabs) — hors périmètre placement 009.");
			ImGui::End();

			ImGui::Begin("Statut", nullptr, ImGuiWindowFlags_NoMouseInputs);
			ImGui::TextWrapped("%s", m_session->Status().c_str());
			ImGui::End();
		}
		else
		{
			ImGui::Begin("Propriétés", nullptr, ImGuiWindowFlags_NoMouseInputs);
			ImGui::TextUnformatted("Session éditeur non initialisée.");
			ImGui::End();
		}

		if (viewportOverlay)
		{
			DrawViewportOverlaysImpl(*viewportOverlay);
		}

		ImGui::Render();
#else
		(void)viewportOverlay;
#endif
	}

	bool WorldEditorImGui::WantsCaptureMouse() const
	{
#if defined(_WIN32)
		return m_ready && ImGui::GetCurrentContext() && ImGui::GetIO().WantCaptureMouse;
#else
		return false;
#endif
	}

	bool WorldEditorImGui::WantsCaptureKeyboard() const
	{
#if defined(_WIN32)
		return m_ready && ImGui::GetCurrentContext() && ImGui::GetIO().WantCaptureKeyboard;
#else
		return false;
#endif
	}

	bool WorldEditorImGui::RecordToBackbuffer(VkCommandBuffer cmd,
		VkImage backbufferImage,
		VkImageView backbufferView,
		VkExtent2D extent,
		const engine::render::VkDeviceContext& deviceContext)
	{
#if !defined(_WIN32)
		(void)cmd;
		(void)backbufferImage;
		(void)backbufferView;
		(void)extent;
		(void)deviceContext;
		return false;
#else
		if (!m_ready || cmd == VK_NULL_HANDLE || backbufferImage == VK_NULL_HANDLE || backbufferView == VK_NULL_HANDLE
			|| extent.width == 0 || extent.height == 0)
		{
			return false;
		}
		ImDrawData* dd = ImGui::GetDrawData();
		if (!dd || dd->DisplaySize.x <= 0.f || dd->DisplaySize.y <= 0.f || dd->CmdListsCount == 0)
		{
			return false;
		}

		VkImageMemoryBarrier toColor{};
		toColor.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		toColor.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		toColor.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		toColor.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		toColor.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		toColor.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		toColor.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		toColor.image = backbufferImage;
		toColor.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
		vkCmdPipelineBarrier(cmd,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			0, 0, nullptr, 0, nullptr, 1, &toColor);

		bool usedKhr = false;
		if (!BeginDynamicRenderingUi(cmd, backbufferView, extent, deviceContext, usedKhr))
		{
			LOG_ERROR(Render, "[WorldEditorImGui] dynamic rendering indisponible pour ImGui");
			return false;
		}

		ImGui_ImplVulkan_RenderDrawData(dd, cmd, VK_NULL_HANDLE);

		EndDynamicRenderingUi(cmd, deviceContext, usedKhr);

		VkImageMemoryBarrier toPresent{};
		toPresent.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		toPresent.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		toPresent.dstAccessMask = 0;
		toPresent.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		toPresent.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
		toPresent.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		toPresent.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		toPresent.image = backbufferImage;
		toPresent.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
		vkCmdPipelineBarrier(cmd,
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
			0, 0, nullptr, 0, nullptr, 1, &toPresent);

		return true;
#endif
	}

	void WorldEditorImGui::AttachPlatformWindow(void* hwndNative, engine::platform::Window& window)
	{
#if defined(_WIN32)
		m_hwnd = hwndNative;
		window.SetPreMessageInterceptor([this](uint32_t msg, uint64_t wp, int64_t lp) -> intptr_t {
			if (!m_ready || !m_hwnd)
			{
				return 0;
			}
			return static_cast<intptr_t>(ImGui_ImplWin32_WndProcHandler(
				static_cast<HWND>(m_hwnd), msg, static_cast<WPARAM>(wp), static_cast<LPARAM>(lp)));
		});
#else
		(void)hwndNative;
		(void)window;
#endif
	}

	void WorldEditorImGui::DetachPlatformWindow(engine::platform::Window& window)
	{
#if defined(_WIN32)
		window.SetPreMessageInterceptor({});
#endif
		m_hwnd = nullptr;
		(void)window;
	}

} // namespace engine::editor
