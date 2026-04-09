#include "engine/editor/WorldEditorImGui.h"

#include "engine/core/Config.h"
#include "engine/core/Log.h"
#include "engine/editor/WorldEditorSession.h"
#include "engine/platform/Window.h"
#include "engine/render/terrain/HeightmapLoader.h"
#include "engine/render/vk/VkDeviceContext.h"

#include <algorithm>
#include <cmath>

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
	void WorldEditorImGui::SetEditorContext(WorldEditorSession* session, const engine::core::Config* cfg)
	{
		m_session = session;
		m_cfg = cfg;
	}

#if defined(_WIN32)
	namespace
	{
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
				const int maxLinesPerAxis = 96;
				int nz = static_cast<int>(std::ceil(ws / cell)) + 1;
				if (nz > maxLinesPerAxis)
				{
					nz = maxLinesPerAxis;
				}
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
					DrawSegmentedWorldLine(dl, vp, vw, vh, gridCol, ox, y0, z, ox + ws, y1, z, 48);
				}
				int nx = static_cast<int>(std::ceil(ws / cell)) + 1;
				if (nx > maxLinesPerAxis)
				{
					nx = maxLinesPerAxis;
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
					DrawSegmentedWorldLine(dl, vp, vw, vh, gridCol, x, y0, oz, x, y1, oz + ws, 48);
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
		void* hwndNative)
	{
#if !defined(_WIN32)
		(void)instance;
		(void)deviceContext;
		(void)swapchainFormat;
		(void)swapchainImageCount;
		(void)vulkanApiVersion;
		(void)hwndNative;
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
		io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
		ImGui::StyleColorsDark();

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
				ImGui::TextUnformatted("Peinture splat: à brancher TerrainSplatting.");
				ImGui::EndMenu();
			}
			if (ImGui::BeginMenu("Affichage"))
			{
				ImGui::TextUnformatted("Cycle jour/nuit: à brancher atmosphère.");
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

		ImGui::Begin("Scène");
		ImGui::TextUnformatted("Vue 3D Vulkan (même moteur que le jeu).");
		ImGui::TextUnformatted("WASD + souris : caméra si l’UI ne capture pas le pointeur.");
		ImGui::TextUnformatted("Molette : zoom FOV (champ de vision).");
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
			if (ImGui::Button("Recharger terrain GPU"))
			{
				m_session->RequestTerrainGpuReload();
			}
			ImGui::End();

			ImGui::Begin("Affichage & grille");
			ImGui::Checkbox("Grille (aperçu écran)", &m_session->ShowGrid());
			ImGui::SliderFloat("Maille grille (m)", &m_session->GridCellMeters(), 1.f, 128.f, "%.1f");
			ImGui::TextUnformatted("La grille est dessinée en surimpression (projection caméra) lorsque le terrain GPU est chargé.");
			ImGui::End();

			ImGui::Begin("Terrain (sculpt)");
			const char* ops[] = { "Élever", "Abaisser", "Lisser", "Aplatir" };
			ImGui::Combo("Opération", &m_session->BrushOp(), ops, IM_ARRAYSIZE(ops));
			ImGui::SliderFloat("Rayon (m)", &m_session->BrushRadius(), 0.5f, 200.f, "%.1f");
			ImGui::SliderFloat("Intensité", &m_session->BrushStrength(), 0.01f, 1.f, "%.2f");
			ImGui::TextUnformatted("Clic gauche (hors UI) : peint sur le MNT (heightmap CPU).");
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

			ImGui::Begin("Préfabs / objets");
			ImGui::TextUnformatted("MVP : liste vide. Pipeline : définitions JSON + placement instancié (fichiers uniquement).");
			ImGui::End();

			ImGui::Begin("Statut");
			ImGui::TextWrapped("%s", m_session->Status().c_str());
			ImGui::End();
		}
		else
		{
			ImGui::Begin("Propriétés");
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
