#include "engine/editor/WorldEditorImGui.h"

#include "engine/core/Config.h"
#include "engine/core/Log.h"
#include "engine/editor/TreeSpeciesCatalog.h"
#include "engine/editor/WorldEditorSession.h"
#include "engine/render/DayNightCycle.h"
#include "engine/platform/FileSystem.h"
#include "engine/platform/Window.h"
#include "engine/render/SharedFontHandles.h"
#include "engine/render/terrain/HeightmapLoader.h"
#include "engine/render/vk/VkDeviceContext.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#if defined(_WIN32)
#	ifndef NOMINMAX
#		define NOMINMAX
#	endif
#	define WIN32_LEAN_AND_MEAN
#	include <windows.h>

#	include "imgui.h"
#	include "imgui_internal.h" // DockBuilder* (public mais declare dans l'en-tete internal)
#	include "imgui_impl_vulkan.h"
#	include "imgui_impl_win32.h"
	// ImGui 1.91+ : la declaration n'est plus dans l'en-tete (#if 0) pour eviter HWND dans l'API publique.
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
				LOG_WARN(Core, "[WorldEditor] Ecriture impossible : {} (deplacement non persiste)", path);
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
		// fine sur un grand terrain genere trop de primitives ; un plafond trop bas rend la maille
		// "figee" (espacement minimal ~= tailleTerrain / (plafond - 1)).
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
			LOG_WARN(Render, "[WorldEditorImGui] Shutdown non appele avant destruction");
		}
#endif
	}

	bool WorldEditorImGui::Init(VkInstance instance,
		const engine::render::VkDeviceContext& deviceContext,
		VkFormat swapchainFormat,
		uint32_t swapchainImageCount,
		uint32_t vulkanApiVersion,
		void* hwndNative,
		const engine::core::Config* cfg,
		bool isWorldEditorExe)
	{
#if !defined(_WIN32)
		(void)instance;
		(void)deviceContext;
		(void)swapchainFormat;
		(void)swapchainImageCount;
		(void)vulkanApiVersion;
		(void)hwndNative;
		(void)cfg;
		(void)isWorldEditorExe;
		return false;
#else
		if (m_ready)
		{
			return true;
		}
		if (!deviceContext.IsValid() || !deviceContext.SupportsDynamicRendering())
		{
			LOG_WARN(Render, "[WorldEditorImGui] Init ignore: device ou dynamic rendering indisponible");
			return false;
		}
		HWND hwnd = hwndNative ? static_cast<HWND>(hwndNative) : nullptr;
		if (!hwnd)
		{
			LOG_WARN(Render, "[WorldEditorImGui] Init ignore: HWND nul");
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
			LOG_ERROR(Render, "[WorldEditorImGui] vkCreateDescriptorPool a echoue");
			return false;
		}

		IMGUI_CHECKVERSION();
		ImGui::CreateContext();
		ImGuiIO& io = ImGui::GetIO();
		io.IniFilename = "world_editor_imgui.ini";
		io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
		// Pas de NavEnableKeyboard : sinon io.WantCaptureKeyboard reste souvent vrai et bloque WASD (camera FPS).
		ImGui::StyleColorsDark();

		// Polices auth (Windlass / Morpheus) chargees dans l'atlas ImGui AVANT ImGui_ImplVulkan_Init -
		// celui-ci construit la texture des fonts une seule fois a partir de l'atlas. Sans ce chargement,
		// l'UI auth utilise la police ImGui par defaut (ProggyClean ~13 px) qui ne ressemble pas a la
		// maquette Lune Noire. La piste Vulkan/AuthGlyphPass utilise deja ces memes fichiers (Engine.cpp).
		// On charge Windlass en premier : elle devient la police par defaut d'ImGui.
		// Range restreint pour les fontes decoratives Lune Noire (Windlass) : A-Z, a-z,
		// 0-9, espace, et ponctuation basique presente dans la fonte. Tout le reste est
		// laisse a ProggyClean en MergeMode (cf. plus bas) - sinon ImGui reserve un slot
		// vide pour chaque codepoint demande (ex. % et [ et ]) et le merge ne remplace
		// jamais les slots existants, ce qui produisait des "?" a l'affichage.
		static const ImWchar kWindlassRanges[] = {
			0x0020, 0x0020, // espace
			0x0027, 0x0027, // '
			0x002C, 0x003F, // , - . / 0-9 : ; < = > ?
			0x0041, 0x005A, // A-Z
			0x0061, 0x007A, // a-z
			0,
		};
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
			// L'atlas ImGui prend la propriete du buffer (FontDataOwnedByAtlas par defaut = true) et le
			// liberera via IM_FREE - on doit donc l'allouer via IM_ALLOC, pas reutiliser le std::vector
			// (sinon UB : la memoire serait liberee deux fois ou utilisee apres liberation).
			void* atlasOwned = IM_ALLOC(bytes.size());
			std::memcpy(atlasOwned, bytes.data(), bytes.size());
			ImFontConfig fcfg{};
			ImFont* font = io.Fonts->AddFontFromMemoryTTF(atlasOwned, static_cast<int>(bytes.size()), pixelHeight,
				&fcfg, kWindlassRanges);
			if (font == nullptr)
			{
				IM_FREE(atlasOwned);
				LOG_WARN(Render, "[WorldEditorImGui] Police {} : AddFontFromMemoryTTF a echoue pour {}", role, relativePath);
				return false;
			}
			LOG_INFO(Render, "[WorldEditorImGui] Police {} chargee dans l'atlas ImGui : {} ({}px)", role, relativePath, pixelHeight);
			return true;
		};

		if (cfg != nullptr)
		{
			// Editeur monde (lcdlln_world_editor.exe) : on substitue Arial a Windlass
			// comme police par defaut. Windlass est decorative (faite pour l'UI auth /
			// in-game qui evoque le lore Lune Noire) — illisible et limitee en glyphs
			// pour un editeur de carte technique. Arial est neutre, riche en glyphs
			// (accents, ponctuation), et standard sur Windows (C:/Windows/Fonts/arial.ttf).
			//
			// Sequence : si isWorldEditorExe ET le fichier Arial est lisible, on charge
			// Arial en PREMIER (devient ImGui::GetFont() par defaut). Le bloc Windlass
			// existant en dessous est court-circuite par un flag local pour eviter que
			// Windlass se retrouve par-dessus dans l'atlas. Si Arial absent, fallback
			// Windlass comme avant (degradation gracieuse).
			bool arialLoaded = false;
			if (isWorldEditorExe)
			{
				const std::string arialPath = cfg->GetString("editor.font.arial_path", "C:/Windows/Fonts/arial.ttf");
				const float arialPx = static_cast<float>(std::clamp<int64_t>(
					cfg->GetInt("editor.font.arial_pixel_height", 14), 11, 32));
				// arial_path : chemin absolu (defaut C:/Windows/Fonts/arial.ttf) OU
				// chemin relatif a paths.content. On tente d'abord absolu puis content.
				std::vector<uint8_t> bytesArial = engine::platform::FileSystem::ReadAllBytes(std::filesystem::path(arialPath));
				if (bytesArial.empty())
				{
					bytesArial = engine::platform::FileSystem::ReadAllBytesContent(*cfg, arialPath);
				}
				if (!bytesArial.empty())
				{
					void* atlasArial = IM_ALLOC(bytesArial.size());
					std::memcpy(atlasArial, bytesArial.data(), bytesArial.size());
					ImFontConfig acfg{};
					// Range standard latin1 (couvre A-Z, a-z, 0-9, accents FR, ponctuation).
					ImFont* arialFont = io.Fonts->AddFontFromMemoryTTF(atlasArial, static_cast<int>(bytesArial.size()),
						arialPx, &acfg, io.Fonts->GetGlyphRangesDefault());
					if (arialFont != nullptr)
					{
						arialLoaded = true;
						LOG_INFO(Render, "[WorldEditorImGui] Police editeur Arial chargee : {} ({}px)", arialPath, arialPx);
					}
					else
					{
						IM_FREE(atlasArial);
						LOG_WARN(Render, "[WorldEditorImGui] AddFontFromMemoryTTF Arial a echoue : {}", arialPath);
					}
				}
				else
				{
					LOG_WARN(Render, "[WorldEditorImGui] Police editeur Arial introuvable : {} (fallback Windlass)", arialPath);
				}
			}

			// Cles specifiques a la piste ImGui (la piste Vulkan/AuthGlyphPass garde sa propre
			// taille via render.auth_ui.font_pixel_height = 28). En ImGui les facteurs
			// SetWindowFontScale (1.62 pour le titre, 1.12 pour le sous-titre, 1.15 pour le titre
			// du panneau, 0.78-0.95 pour les libelles...) sont calibres pour la police par defaut
			// ProggyClean ~13 px ; charger Windlass a la meme taille respecte donc tous les
			// gabarits existants. On peut surcharger via render.auth_ui.imgui.font_pixel_height
			// si on veut grossir/reduire globalement l'UI auth ImGui sans toucher aux scales.
			//
			// arialLoaded=true (editeur monde) court-circuite Windlass pour qu'Arial reste
			// la police par defaut. L'editeur n'utilise pas les ecrans auth, donc Windlass
			// n'a pas d'usage la-bas.
			const std::string uiFontPath = cfg->GetString("render.auth_ui.font_path", "");
			const float uiFontPx = static_cast<float>(std::clamp<int64_t>(
				cfg->GetInt("render.auth_ui.imgui.font_pixel_height", 13), 11, 32));
			if (!arialLoaded)
			{
				loadAuthFontFromConfig(uiFontPath, uiFontPx, "UI");
			}

			// Fallback merge : Windlass.ttf ne contient pas les caracteres accentues, '*' (utilise
			// par ImGuiInputTextFlags_Password), '[' ']' '%' '@' et autres ponctuations etendues.
			// ImGui les rendait alors comme '?'. On merge ProggyClean (ImGui built-in) IMMEDIATEMENT
			// apres Windlass (sinon le merge prend la fonte precedente, pas Windlass) : Windlass
			// reste prioritaire pour A-Z/a-z/0-9 et ProggyClean prend le relais pour les autres.
			// Visuellement les glyphes de fallback ne matchent pas la maquette mais c'est mieux
			// que des '?'.
			//
			// Si arialLoaded (editeur monde), on saute ProggyClean : Arial couvre deja les
			// accents et ponctuations etendues, pas besoin de fallback.
			if (!arialLoaded)
			{
				ImFontConfig fallbackCfg{};
				fallbackCfg.MergeMode = true;
				io.Fonts->AddFontDefault(&fallbackCfg);
				LOG_INFO(Render, "[WorldEditorImGui] Fallback ProggyClean merge sur Windlass (couvre * [ ] @ etc.)");
			}

			const std::string valueFontPath = cfg->GetString("render.auth_ui.value_font_path", "");
			const float valueFontPx = static_cast<float>(std::clamp<int64_t>(
				cfg->GetInt("render.auth_ui.imgui.value_font_pixel_height", 12), 11, 32));
			if (!arialLoaded)
			{
				// Police "valeurs" Morpheus : utile uniquement pour l'UI auth (nom du
				// perso, montant en or...). Pas pertinent dans l'editeur monde.
				loadAuthFontFromConfig(valueFontPath, valueFontPx, "valeurs");
			}

			// Fonte password : 2eme passe sur Windlass a 24 px (plus large que la
			// fonte UI standard 13 px), avec un merge ProggyClean immediat pour le
			// glyph '*' qui n'est pas dans Windlass. Le pointeur est partage via
			// SharedFontHandles::g_largePasswordFont pour que DrawAuthGoldField
			// puisse PushFont autour de l'InputText password.
			//
			// Skipped en mode editeur monde : pas d'ecran auth, pas d'InputText
			// password. Le pointeur partage reste a nullptr.
			if (!arialLoaded && !uiFontPath.empty())
			{
				std::vector<uint8_t> bytesPwd = engine::platform::FileSystem::ReadAllBytesContent(*cfg, uiFontPath);
				if (!bytesPwd.empty())
				{
					void* atlasPwd = IM_ALLOC(bytesPwd.size());
					std::memcpy(atlasPwd, bytesPwd.data(), bytesPwd.size());
					ImFontConfig pwdCfg{};
					ImFont* pwdFont = io.Fonts->AddFontFromMemoryTTF(atlasPwd, static_cast<int>(bytesPwd.size()),
						24.0f, &pwdCfg, kWindlassRanges);
					if (pwdFont != nullptr)
					{
						// Merge ProggyClean a 24px pour fournir '*' (et autres glyphs
						// hors range Windlass) a la meme taille que Windlass.
						// Note : ProggyClean est bitmap fixe a 13px, donc l'agrandissement
						// est crenele. Acceptable pour un masque password.
						ImFontConfig mergePwd{};
						mergePwd.MergeMode = true;
						mergePwd.SizePixels = 24.0f;
						io.Fonts->AddFontDefault(&mergePwd);
						engine::render::SharedFontHandles::g_largePasswordFont = static_cast<void*>(pwdFont);
						LOG_INFO(Render, "[WorldEditorImGui] Police password (Windlass 24px + ProggyClean merge) prete");
					}
					else
					{
						IM_FREE(atlasPwd);
					}
				}
			}
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
			LOG_ERROR(Render, "[WorldEditorImGui] ImGui_ImplVulkan_Init a echoue");
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
				if (ImGui::MenuItem("Sauvegarder la carte courante", "Ctrl+S", false, m_session != nullptr && m_cfg != nullptr)
					&& m_session && m_cfg)
				{
					(void)m_session->ActionSaveCurrentMap(*m_cfg);
				}
				if (ImGui::MenuItem("Exporter en runtime", nullptr, false, m_session != nullptr && m_cfg != nullptr)
					&& m_session && m_cfg)
				{
					(void)m_session->ActionExportRuntime(*m_cfg);
				}
				ImGui::Separator();
				if (ImGui::MenuItem("Importer une texture (PNG/JPG/TGA/BMP)...", nullptr, false, m_session != nullptr && m_cfg != nullptr)
					&& m_session && m_cfg)
				{
					(void)m_session->ActionImportTexture(*m_cfg);
				}
				if (ImGui::MenuItem("Importer un son (WAV/OGG)...", nullptr, false, m_session != nullptr && m_cfg != nullptr)
					&& m_session && m_cfg)
				{
					(void)m_session->ActionImportAudio(*m_cfg);
				}
				ImGui::Separator();
				ImGui::MenuItem("Quitter", nullptr, false, false);
				ImGui::EndMenu();
			}
			if (ImGui::BeginMenu("Vue"))
			{
				if (ImGui::MenuItem("Reinitialiser la disposition des fenetres"))
				{
					// Reinitialisation in-process : on retire le node DockBuilder courant et on
					// repasse m_defaultLayoutAttempted a false. La frame suivante reconstruit la
					// disposition par defaut via le bloc DockBuilder en haut de BuildUi().
					const ImGuiID dockId = ImGui::GetID("WorldEditorDockSpaceV2");
					ImGui::DockBuilderRemoveNode(dockId);
					m_defaultLayoutAttempted = false;
					// Supprime aussi le fichier .ini pour que la reinitialisation persiste apres
					// le redemarrage (sinon ImGui rechargerait l'ancienne disposition au prochain run).
					std::error_code ec;
					std::filesystem::remove("world_editor_imgui.ini", ec);
					if (m_session)
					{
						m_session->SetStatus("Disposition reinitialisee.");
					}
				}
				ImGui::Separator();
				if (m_cfg)
				{
					float mult = static_cast<float>(m_cfg->GetDouble("controls.editor_camera_speed_multiplier", 1.0));
					ImGui::TextDisabled("Vitesse de deplacement (Shift = course) :");
					if (ImGui::SliderFloat("Vitesse camera (x)", &mult, 0.25f, 5.0f, "%.2f"))
					{
						mult = std::clamp(mult, 0.25f, 5.0f);
						m_cfg->SetValue("controls.editor_camera_speed_multiplier", static_cast<double>(mult));
					}
					ImGui::TextDisabled("Astuce : montez ce curseur pour traverser plus vite les");
					ImGui::TextDisabled("grandes cartes pendant la creation.");
					ImGui::Separator();
				}
				ImGui::TextDisabled("Astuce : faites glisser une fenetre par sa barre de titre");
				ImGui::TextDisabled("pour la docker a gauche, a droite ou en bas.");
				ImGui::EndMenu();
			}
			if (m_cfg && ImGui::BeginMenu("Options"))
			{
				const std::string cur = m_cfg->GetString("controls.movement_layout", "wasd");
				const bool zqsdActive = (cur == "zqsd");
				if (ImGui::MenuItem("Deplacement : QWERTY (WASD)", nullptr, !zqsdActive))
				{
					m_cfg->SetValue("controls.movement_layout", std::string("wasd"));
					TryPersistMovementLayoutToUserSettings("wasd");
				}
				if (ImGui::MenuItem("Deplacement : AZERTY (ZQSD)", nullptr, zqsdActive))
				{
					m_cfg->SetValue("controls.movement_layout", std::string("zqsd"));
					TryPersistMovementLayoutToUserSettings("zqsd");
				}
				ImGui::EndMenu();
			}
			// Barre d'outils rapide a droite du menu : sauvegarde 1-clic + chargement carte.
			if (m_session != nullptr && m_cfg != nullptr)
			{
				ImGui::Separator();
				if (ImGui::MenuItem("Sauvegarder"))
				{
					(void)m_session->ActionSaveCurrentMap(*m_cfg);
				}
				if (!m_session->AvailableMapsScanned())
				{
					m_session->RefreshAvailableMaps(*m_cfg);
				}
				if (ImGui::BeginMenu("Charger une carte"))
				{
					const std::vector<std::string>& mapIds = m_session->AvailableMapIds();
					if (mapIds.empty())
					{
						ImGui::TextDisabled("Aucune carte. Creez-en une via le panneau 'Carte'.");
					}
					else
					{
						for (size_t i = 0; i < mapIds.size(); ++i)
						{
							if (ImGui::MenuItem(mapIds[i].c_str()))
							{
								(void)m_session->ActionLoadMapByZoneId(*m_cfg, mapIds[i]);
								m_session->SelectedAvailableMapIndex() = static_cast<int>(i);
							}
						}
					}
					ImGui::Separator();
					if (ImGui::MenuItem("Rafraichir la liste"))
					{
						m_session->RefreshAvailableMaps(*m_cfg);
					}
					ImGui::EndMenu();
				}
				// Statut court a droite
				if (!m_session->Status().empty())
				{
					ImGui::Separator();
					ImGui::TextUnformatted(m_session->Status().c_str());
				}
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
			const ImGuiID dockId = ImGui::GetID("WorldEditorDockSpaceV2");
			// ImGuiDockNodeFlags_PassthroughCentralNode (1<<4) - litteral pour eviter les divergences d'en-tetes.
			constexpr ImGuiDockNodeFlags kDockSpaceFlags = static_cast<ImGuiDockNodeFlags>(1u << 4);

			// Detection de resize de fenetre : si la taille du viewport a change
			// depuis la derniere frame (apres initialisation), on force
			// DockBuilderSetNodeSize sur le node racine pour que les panneaux dockes
			// se relayent automatiquement. Sans ce relayout, les panneaux restent
			// ancres a l'ancienne taille (lue dans world_editor_imgui.ini), ce qui
			// les place hors du viewport et donne l'impression que l'UI a disparu
			// apres un drag de bord de fenetre.
			//
			// IMPORTANT : on ne fire pas la premiere frame (m_lastDockSpaceWidth==0
			// par defaut) car le node racine n'existe pas encore (cree par le bloc
			// m_defaultLayoutAttempted juste au-dessus). DockBuilderSetNodeSize
			// avant que la layout par defaut ait ete posee bouilli les sizes
			// proportionnelles -> ecran noir signale par l'utilisateur.
			constexpr float kResizeEpsilonPx = 0.5f;
			const bool dockSizeInitialized = (m_lastDockSpaceWidth > kResizeEpsilonPx);
			if (dockSizeInitialized
			 && (std::fabs(vp->WorkSize.x - m_lastDockSpaceWidth)  > kResizeEpsilonPx
			  || std::fabs(vp->WorkSize.y - m_lastDockSpaceHeight) > kResizeEpsilonPx))
			{
				if (ImGui::DockBuilderGetNode(dockId) != nullptr)
				{
					ImGui::DockBuilderSetNodeSize(dockId, vp->WorkSize);
				}
			}
			m_lastDockSpaceWidth  = vp->WorkSize.x;
			m_lastDockSpaceHeight = vp->WorkSize.y;

			// Disposition par defaut : si ImGui n'a pas charge de layout depuis world_editor_imgui.ini
			// (premier demarrage, fichier supprime via le menu Vue, ou nouveau dockId), on pose une
			// disposition propre (palette gauche, inspecteur droite, scene centrale, statut bas) via
			// l'API DockBuilder. La tentative ne se fait qu'une fois par session - sinon, l'utilisateur
			// qui re-dispose ses panneaux les verrait reposer chaque frame.
			if (!m_defaultLayoutAttempted)
			{
				m_defaultLayoutAttempted = true;
				if (ImGui::DockBuilderGetNode(dockId) == nullptr)
				{
					ImGui::DockBuilderRemoveNode(dockId);
					ImGui::DockBuilderAddNode(dockId, kDockSpaceFlags | ImGuiDockNodeFlags_DockSpace);
					ImGui::DockBuilderSetNodeSize(dockId, vp->WorkSize);

					ImGuiID idLeft = 0;
					ImGuiID idCenter = dockId;
					ImGui::DockBuilderSplitNode(idCenter, ImGuiDir_Left, 0.22f, &idLeft, &idCenter);

					ImGuiID idRight = 0;
					ImGui::DockBuilderSplitNode(idCenter, ImGuiDir_Right, 0.30f, &idRight, &idCenter);

					ImGuiID idBottom = 0;
					ImGui::DockBuilderSplitNode(idCenter, ImGuiDir_Down, 0.18f, &idBottom, &idCenter);

					// Palette outils : a gauche, c'est la zone d'action principale.
					ImGui::DockBuilderDockWindow("Outils", idLeft);

					// Inspecteur : panneaux carte / affichage / import / objets / scene sont des onglets a droite.
					// Scene rejoint cette pile : la docker dans le node central annulait le passthrough et
					// bloquait l'interaction 3D (regression P1). En tant qu'onglet a droite, son contenu
					// (diagnostic camera) reste accessible et le node central reste vide -> 3D visible
					// et cliquable au travers du DockSpace.
					ImGui::DockBuilderDockWindow("Carte", idRight);
					ImGui::DockBuilderDockWindow("Affichage & grille", idRight);
					ImGui::DockBuilderDockWindow("Atmosphere", idRight);
					ImGui::DockBuilderDockWindow("Import assets", idRight);
					ImGui::DockBuilderDockWindow("Objets sur la carte", idRight);
					ImGui::DockBuilderDockWindow("Scene", idRight);

					// Statut en bas, plein largeur.
					ImGui::DockBuilderDockWindow("Statut", idBottom);

					ImGui::DockBuilderFinish(dockId);
				}
			}

			ImGui::DockSpace(dockId, ImVec2(0.f, 0.f), kDockSpaceFlags);
		}
		ImGui::End();
		ImGui::PopStyleVar(3);

		// NoBackground : si la fenetre Scene est dockee dans un node qui n'est plus passthrough,
		// son fond ne masque pas la 3D dessous. NoMouseInputs : les clics passent au travers vers
		// la couche viewport overlay (raycast terrain, peinture splat, etc.).
		ImGui::Begin("Scene", nullptr, ImGuiWindowFlags_NoMouseInputs | ImGuiWindowFlags_NoBackground);
		ImGui::TextUnformatted("Vue 3D Vulkan (meme moteur que le jeu).");
		ImGui::TextUnformatted(
			"Deplacement : menu 'Options' -> QWERTY (WASD) ou AZERTY (ZQSD), un seul a la fois. Shift = plus rapide ; "
			"vitesse de base augmente avec la taille du terrain charge.");
		ImGui::TextUnformatted(
			"Orientation : maintenez le clic droit et deplacez la souris (meme au-dessus des fenetres ImGui) ; "
			"sinon la souris n'oriente que lorsqu'ImGui ne la capture pas. Molette : zoom FOV.");
		ImGui::TextUnformatted(
			"Si la vue monte quand vous baissez la souris : dans user_settings.json ou options client, "
			"activez controls.invert_y.");
		if (viewportOverlay != nullptr && viewportOverlay->viewProjColMajor != nullptr)
		{
			ImGui::Separator();
			ImGui::Text("Camera (monde) : (%.2f, %.2f, %.2f) m", static_cast<double>(viewportOverlay->cameraWorldX),
				static_cast<double>(viewportOverlay->cameraWorldY), static_cast<double>(viewportOverlay->cameraWorldZ));
			ImGui::Text("Orientation : yaw %.1fdeg, pitch %.1fdeg", static_cast<double>(viewportOverlay->cameraYawDeg),
				static_cast<double>(viewportOverlay->cameraPitchDeg));
			ImGui::TextDisabled(
				"La grille et le rendu 3D utilisent la meme matrice vue x projection chaque frame : ce n'est pas une grille 'figee' "
				"qui oublierait de se rafraichir. Si ces nombres ne bougent pas avec WASD / souris, la camera ne recoit pas l'entree "
				"(focus ImGui, etc.). S'ils bougent mais l'image ne change pas, il s'agit d'un autre probleme de rendu.");
		}
		ImGui::End();

		if (m_session && m_cfg)
		{
			if (!m_session->AvailableMapsScanned())
			{
				m_session->RefreshAvailableMaps(*m_cfg);
			}

			ImGui::Begin("Carte");

			// Section 1 - Cartes existantes (chemin canonique world_editor/maps/<zone_id>/).
			ImGui::TextUnformatted("Cartes disponibles");
			if (ImGui::Button("Rafraichir la liste"))
			{
				m_session->RefreshAvailableMaps(*m_cfg);
			}
			const std::vector<std::string>& mapIds = m_session->AvailableMapIds();
			if (mapIds.empty())
			{
				ImGui::TextDisabled("Aucune carte trouvee. Creez-en une ci-dessous, ou cliquez sur 'Rafraichir la liste'.");
			}
			else
			{
				std::string itemsZ;
				for (const std::string& id : mapIds)
				{
					itemsZ += id;
					itemsZ.push_back('\0');
				}
				itemsZ.push_back('\0');
				int& sel = m_session->SelectedAvailableMapIndex();
				sel = std::clamp(sel, 0, static_cast<int>(mapIds.size()) - 1);
				ImGui::Combo("Carte", &sel, itemsZ.c_str());
				sel = std::clamp(sel, 0, static_cast<int>(mapIds.size()) - 1);
				if (ImGui::Button("Charger la carte selectionnee"))
				{
					(void)m_session->ActionLoadMapByZoneId(*m_cfg, mapIds[static_cast<size_t>(sel)]);
				}
			}
			ImGui::Separator();

			// Section 2 - Sauvegarde 1-clic dans le chemin canonique de la carte courante.
			ImGui::TextUnformatted("Carte courante");
			ImGui::InputText("Nom (zone_id)", m_session->BufZoneId().data(), m_session->BufZoneId().size());
			if (ImGui::Button("Sauvegarder"))
			{
				(void)m_session->ActionSaveCurrentMap(*m_cfg);
			}
			ImGui::SameLine();
			if (ImGui::Button("Exporter runtime"))
			{
				(void)m_session->ActionExportRuntime(*m_cfg);
			}
			if (!m_session->EditFileAbsolutePath().empty())
			{
				ImGui::TextDisabled("Fichier: %s", m_session->EditFileAbsolutePath().c_str());
			}
			ImGui::Separator();

			// Section 3 - Creation d'une nouvelle carte.
			ImGui::TextUnformatted("Nouvelle carte");
			ImGui::InputText("Taille (NxN)", m_session->BufSize().data(), m_session->BufSize().size());
			ImGui::InputTextWithHint("Seed (optionnel)", "vide = aleatoire non fixe",
				m_session->BufSeed().data(), m_session->BufSeed().size());
			if (ImGui::Button("Creer une nouvelle carte"))
			{
				if (m_session->ActionNewMap(*m_cfg))
				{
					m_session->RefreshAvailableMaps(*m_cfg);
				}
			}
			ImGui::Separator();

			// Section 4 - Details fichiers + recharge terrain GPU (avance, replie par defaut).
			if (ImGui::CollapsingHeader("Details fichiers (avance)"))
			{
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
				ImGui::TextDisabled(
					"Chemins canoniques : <content>/world_editor/maps/<zone_id>/{map.lcdlln_edit.json, height.r16h, splat.slap, grass.grms}.");
			}

			ImGui::End();

			ImGui::Begin("Affichage & grille");
			ImGui::Checkbox("Grille (apercu ecran)", &m_session->ShowGrid());
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
						"La grille est limitee a %d lignes par axe (performances). Avec un terrain de %.0f m, "
						"la maille affichee ne peut pas etre plus fine qu'environ %.1f m tant que ce plafond s'applique.",
						kWorldEditorGridMaxLinesPerAxis, static_cast<double>(ws), static_cast<double>(minSpacing));
				}
			}
			ImGui::TextUnformatted("La grille est dessinee en surimpression (projection camera) lorsque le terrain GPU est charge.");
			ImGui::End();

			// ── Panneau Atmosphere ─────────────────────────────────────────────────
			// Permet a l'utilisateur de modifier en direct le cycle jour/nuit :
			// time-of-day (0-24h), timeScale (vitesse du temps), et lecture des
			// valeurs derivees (direction soleil, couleurs ciel zenith/horizon,
			// ambient, isDaytime).
			ImGui::Begin("Atmosphere");
			if (m_dayNight == nullptr)
			{
				ImGui::TextDisabled("DayNightCycle non branche.");
				ImGui::TextDisabled("(SetDayNightCycle pas appele depuis Engine)");
			}
			else
			{
				const engine::render::DayNightCycle::State& dn = m_dayNight->GetState();
				float tod = dn.timeOfDay;
				if (ImGui::SliderFloat("Heure (0-24)", &tod, 0.0f, 24.0f, "%.2f h"))
				{
					m_dayNight->SetTime(tod);
				}
				float ts = m_dayNight->GetTimeScale();
				if (ImGui::SliderFloat("Vitesse temps (s/heure)", &ts, 0.1f, 600.0f, "%.1f"))
				{
					m_dayNight->SetTimeScale(ts);
				}
				ImGui::TextDisabled("60 = 1 min reel = 1 heure jeu (24 min reel = 1 jour jeu)");
				ImGui::TextDisabled("3600 = temps reel 1:1 (24 h reel = 1 jour jeu)");
				ImGui::Separator();
				ImGui::TextUnformatted("Etat calcule (lecture seule) :");
				ImGui::Text("Jour : %s", dn.isDaytime ? "oui (soleil)" : "non (lune)");
				ImGui::Text("Direction soleil : (%.2f, %.2f, %.2f)", dn.lightDir[0], dn.lightDir[1], dn.lightDir[2]);
				const float lightCol[3] = { dn.lightColor[0], dn.lightColor[1], dn.lightColor[2] };
				ImGui::ColorButton("##lightCol", ImVec4(lightCol[0], lightCol[1], lightCol[2], 1.0f));
				ImGui::SameLine(); ImGui::TextUnformatted("Couleur lumiere");
				const float ambCol[3] = { dn.ambientColor[0], dn.ambientColor[1], dn.ambientColor[2] };
				ImGui::ColorButton("##ambCol", ImVec4(ambCol[0], ambCol[1], ambCol[2], 1.0f));
				ImGui::SameLine(); ImGui::TextUnformatted("Couleur ambiente");
				const float zen[3] = { dn.skyZenith[0], dn.skyZenith[1], dn.skyZenith[2] };
				ImGui::ColorButton("##skyZen", ImVec4(zen[0], zen[1], zen[2], 1.0f));
				ImGui::SameLine(); ImGui::TextUnformatted("Ciel (zenith)");
				const float hor[3] = { dn.skyHorizon[0], dn.skyHorizon[1], dn.skyHorizon[2] };
				ImGui::ColorButton("##skyHor", ImVec4(hor[0], hor[1], hor[2], 1.0f));
				ImGui::SameLine(); ImGui::TextUnformatted("Ciel (horizon)");
				ImGui::TextDisabled("La couleur de fond du viewport prend skyHorizon a chaque frame.");
			}
			ImGui::End();

			ImGui::Begin("Outils");
			// Etat du terrain - visible en permanence, pour expliquer pourquoi le clic peut etre inactif.
			{
				const bool terrainReady = !m_session->Doc().heightmapContentRelativePath.empty();
				if (terrainReady)
				{
					ImGui::TextColored(ImVec4(0.5f, 0.95f, 0.55f, 1.f), "Terrain : pret");
				}
				else
				{
					ImGui::TextColored(ImVec4(1.f, 0.55f, 0.3f, 1.f),
						"Terrain : aucun. Creez ou chargez une carte avant de peindre / sculpter.");
				}
				ImGui::TextDisabled("Le clic gauche est ignore quand la souris est au-dessus de l'UI ; visez la zone 3D.");
				ImGui::Separator();
			}
			if (ImGui::BeginTabBar("OutilsTabs"))
			{
				int& tm = m_session->TerrainEditMode();

				if (ImGui::BeginTabItem("Sculpter"))
				{
					tm = 0;
					ImGui::TextDisabled("Modifie la hauteur du sol au pinceau.");
					const char* ops[] = { "Monter", "Descendre", "Adoucir", "Niveler" };
					ImGui::Combo("Action", &m_session->BrushOp(), ops, IM_ARRAYSIZE(ops));
					ImGui::SliderFloat("Rayon du pinceau (m)", &m_session->BrushRadius(), 0.5f, 200.f, "%.1f");
					ImGui::SliderFloat("Force", &m_session->BrushStrength(), 0.01f, 1.f, "%.2f");
					ImGui::Separator();
					ImGui::TextWrapped("Maintenez le clic gauche sur le sol pour appliquer.");
					ImGui::EndTabItem();
				}

				if (ImGui::BeginTabItem("Peindre"))
				{
					tm = 1;
					ImGui::TextDisabled("Peint le type de sol (herbe, terre, roc, neige).");
					const char* layers[] = { "Herbe", "Terre", "Roc", "Neige" };
					ImGui::Combo("Type de sol", &m_session->SplatLayer(), layers, IM_ARRAYSIZE(layers));
					ImGui::SliderFloat("Rayon du pinceau (m)", &m_session->BrushRadius(), 0.5f, 200.f, "%.1f");
					ImGui::SliderFloat("Force", &m_session->BrushStrength(), 0.01f, 1.f, "%.2f");

					ImGui::Separator();
					if (ImGui::CollapsingHeader("Textures personnalisees (par couche)"))
					{
						ImGui::TextDisabled("Associez une texture importee a chaque type de sol.");
						const std::vector<std::string>& imported = m_session->Doc().textureAssets;
						std::array<std::string, 4>& refs = m_session->MutableDoc().splatLayerTextureRefs;
						std::string itemsZ;
						itemsZ += "(par defaut moteur)";
						itemsZ.push_back('\0');
						for (const std::string& t : imported)
						{
							itemsZ += t;
							itemsZ.push_back('\0');
						}
						itemsZ.push_back('\0');
						for (int li = 0; li < 4; ++li)
						{
							int sel = 0;
							if (!refs[static_cast<size_t>(li)].empty())
							{
								for (size_t i = 0; i < imported.size(); ++i)
								{
									if (imported[i] == refs[static_cast<size_t>(li)])
									{
										sel = static_cast<int>(i + 1);
										break;
									}
								}
							}
							char lbl[32];
							std::snprintf(lbl, sizeof(lbl), "%s##splatTex%d", layers[li], li);
							if (ImGui::Combo(lbl, &sel, itemsZ.c_str()))
							{
								if (sel <= 0)
								{
									refs[static_cast<size_t>(li)].clear();
								}
								else if (static_cast<size_t>(sel - 1) < imported.size())
								{
									refs[static_cast<size_t>(li)] = imported[static_cast<size_t>(sel - 1)];
								}
							}
						}
						ImGui::TextDisabled("Le mapping est persiste dans la carte (champ JSON splat_layer_texture_refs).");
					}

					if (ImGui::CollapsingHeader("Sons de pas (par couche)"))
					{
						ImGui::TextDisabled("Choisissez le son entendu par le joueur sur chaque type de sol.");
						const std::vector<std::string>& imported = m_session->Doc().audioAssets;
						std::array<std::string, 4>& refs = m_session->MutableDoc().splatLayerFootstepAudioRefs;
						std::string itemsZ;
						itemsZ += "(aucun)";
						itemsZ.push_back('\0');
						for (const std::string& t : imported)
						{
							itemsZ += t;
							itemsZ.push_back('\0');
						}
						itemsZ.push_back('\0');
						if (imported.empty())
						{
							ImGui::TextColored(ImVec4(1.f, 0.7f, 0.3f, 1.f),
								"Importez d'abord des sons via le panneau 'Import assets'.");
						}
						for (int li = 0; li < 4; ++li)
						{
							int sel = 0;
							if (!refs[static_cast<size_t>(li)].empty())
							{
								for (size_t i = 0; i < imported.size(); ++i)
								{
									if (imported[i] == refs[static_cast<size_t>(li)])
									{
										sel = static_cast<int>(i + 1);
										break;
									}
								}
							}
							char lbl[40];
							std::snprintf(lbl, sizeof(lbl), "%s##splatAudio%d", layers[li], li);
							if (ImGui::Combo(lbl, &sel, itemsZ.c_str()))
							{
								if (sel <= 0)
								{
									refs[static_cast<size_t>(li)].clear();
								}
								else if (static_cast<size_t>(sel - 1) < imported.size())
								{
									refs[static_cast<size_t>(li)] = imported[static_cast<size_t>(sel - 1)];
								}
							}
						}
						ImGui::TextDisabled(
							"Persiste en JSON. Lecture cote gameplay (deplacement joueur -> couche splat dominante)"
							" sera branchee dans une iteration moteur ulterieure.");
					}

					ImGui::Separator();
					ImGui::TextWrapped("Maintenez le clic gauche sur le sol pour peindre. La sauvegarde ecrit le fichier splat.");
					ImGui::EndTabItem();
				}

				if (ImGui::BeginTabItem("Herbe"))
				{
					tm = 2;
					ImGui::TextDisabled("Definit ou des touffes d'herbe poussent.");
					ImGui::Checkbox("Mode gomme (efface l'herbe)", &m_session->GrassMaskEraseBrush());
					ImGui::SliderFloat("Rayon du pinceau (m)", &m_session->BrushRadius(), 0.5f, 200.f, "%.1f");
					ImGui::SliderFloat("Force", &m_session->BrushStrength(), 0.01f, 1.f, "%.2f");
					ImGui::Separator();
					ImGui::TextWrapped("Maintenez le clic gauche pour appliquer. La sauvegarde ecrit grass.grms.");
					ImGui::EndTabItem();
				}

				if (ImGui::BeginTabItem("Objets"))
				{
					tm = 3;
					ImGui::TextDisabled("Pose des arbres, rochers ou autres objets.");
					ImGui::TextWrapped(
						"Choisissez le type d'objet dans le panneau 'Objets sur la carte' a droite.");
					ImGui::Separator();
					ImGui::TextWrapped(
						"Clic gauche sur le sol : pose un nouvel objet, ou deplace l'objet selectionne dans la liste.");
					ImGui::EndTabItem();
				}

				if (ImGui::BeginTabItem("Eau"))
				{
					// Pas un mode de pinceau : on ne change pas tm.
					ImGui::TextDisabled("Active une surface d'eau plane a un Y donne (lac, mer).");
					bool waterOn = m_session->Doc().waterEnabled;
					if (ImGui::Checkbox("Eau active", &waterOn))
					{
						m_session->MutableDoc().waterEnabled = waterOn;
					}
					float lvl = static_cast<float>(m_session->Doc().waterLevelMeters);
					if (ImGui::SliderFloat("Niveau de l'eau (Y, m)", &lvl, -200.f, 500.f, "%.2f"))
					{
						m_session->MutableDoc().waterLevelMeters = static_cast<double>(lvl);
					}
					ImGui::Separator();
					ImGui::TextDisabled(
						"Ces reglages sont persistes dans la carte (champs JSON water_enabled,");
					ImGui::TextDisabled(
						"water_level_m). La passe Vulkan eau (surface transparente, reflets simples)");
					ImGui::TextDisabled(
						"sera branchee dans une iteration moteur ulterieure.");
					ImGui::EndTabItem();
				}

				if (ImGui::BeginTabItem("Routes"))
				{
					tm = 4;
					ImGui::TextDisabled("Trace des chemins / routes en peignant une bande de splat.");
					const char* rlayers[] = { "Herbe", "Terre / chemin", "Roc", "Neige" };
					ImGui::Combo("Sol de la route", &m_session->RouteSplatLayer(), rlayers, IM_ARRAYSIZE(rlayers));
					{
						int& rl = m_session->RouteSplatLayer();
						rl = std::clamp(rl, 0, 3);
					}
					ImGui::SliderFloat("Largeur (m)", &m_session->RouteStripWidthM(), 0.5f, 64.f, "%.1f");
					ImGui::SliderFloat("Intensite", &m_session->BrushStrength(), 0.01f, 1.f, "%.2f");
					ImGui::Separator();
					if (ImGui::Button("Effacer les points"))
					{
						m_session->ClearRouteDraft();
					}
					ImGui::SameLine();
					if (ImGui::Button("Tracer la route"))
					{
						m_session->RequestApplyRouteDraftToSplat();
					}
					ImGui::Text("Points places : %zu", m_session->RouteDraftPoints().size());
					ImGui::Text("Routes enregistrees : %zu", m_session->Doc().routes.size());
					ImGui::Separator();
					ImGui::TextWrapped("Cliquez sur le sol pour ajouter un sommet de la route, puis 'Tracer la route'.");
					ImGui::EndTabItem();
				}

				tm = std::clamp(tm, 0, 4);
				ImGui::EndTabBar();
			}
			ImGui::End();

			ImGui::Begin("Import assets");
			ImGui::TextUnformatted("Texture (PNG / JPG / TGA / BMP)");
			ImGui::InputTextWithHint("Source", "C:/chemin/vers/image.png (guillemets autorises)",
				m_session->BufPngPath().data(), m_session->BufPngPath().size());
			ImGui::InputTextWithHint("Nom dans textures/", "vide = deduit automatiquement (ex: image.texr)",
				m_session->BufTexrName().data(), m_session->BufTexrName().size());
			if (ImGui::Button("Importer cette texture"))
			{
				(void)m_session->ActionImportTexture(*m_cfg);
			}
			ImGui::TextDisabled("L'extension .texr est ajoutee si absente. Le fichier est ecrit dans <content>/textures/.");
			ImGui::Separator();

			ImGui::TextUnformatted("Audio (WAV / OGG)");
			ImGui::InputTextWithHint("Source##audio", "C:/chemin/vers/son.wav (guillemets autorises)",
				m_session->BufAudioSrc().data(), m_session->BufAudioSrc().size());
			ImGui::InputTextWithHint("Nom dans audio/", "vide = meme nom que la source (ex: footstep/sand.wav)",
				m_session->BufAudioDest().data(), m_session->BufAudioDest().size());
			if (ImGui::Button("Importer cet audio"))
			{
				(void)m_session->ActionImportAudio(*m_cfg);
			}
			ImGui::TextDisabled("Le fichier est copie dans <content>/audio/. Aucune transcompression.");
			ImGui::Separator();

			ImGui::TextUnformatted("Textures deja importees sur cette carte:");
			if (m_session->Doc().textureAssets.empty())
			{
				ImGui::TextDisabled("(aucune)");
			}
			else
			{
				for (const std::string& t : m_session->Doc().textureAssets)
				{
					ImGui::BulletText("%s", t.c_str());
				}
			}
			ImGui::End();

			ImGui::Begin("Objets sur la carte");
			if (m_cfg != nullptr)
			{
				m_session->EnsureTreeCatalogLoaded(*m_cfg);
			}
			const char* placeKinds[] = { "Arbre (catalogue 013)", "Rocher (legacy zone_1)" };
			{
				int& pk = m_session->InstancePlacementKind();
				ImGui::Combo("Type a placer", &pk, placeKinds, IM_ARRAYSIZE(placeKinds));
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
					ImGui::Combo("Espece", &si, speciesItems.c_str());
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
					ImGui::SliderFloat("Taille (min-max espece)", &m_session->TreeScaleT01(), 0.f, 1.f, "%.2f");
					ImGui::Checkbox("Echelle aleatoire a la pose", &m_session->TreeRandomizeScaleOnPlace());
					if (m_session->TreeRandomizeScaleOnPlace())
					{
						ImGui::TextDisabled("Le curseur taille est ignore si aleatoire est coche.");
					}
				}
			}
			else
			{
				ImGui::TextUnformatted("Pose un rocher (zones/zone_1/zone_1.gltf), sans espece catalogue.");
			}
			ImGui::TextUnformatted(
				"Selectionnez une ligne pour deplacer au prochain clic. Sans selection : nouvelle instance. Coordonnees monde + snap hauteur MNT.");
			ImGui::Separator();
			for (size_t i = 0; i < m_session->MutableDoc().layoutInstances.size(); ++i)
			{
				const engine::editor::WorldMapEditLayoutInstance& inst = m_session->MutableDoc().layoutInstances[i];
				char label[256];
				if (!inst.speciesId.empty())
				{
					std::snprintf(label, sizeof(label), "%s - %s #%u - scale %.3f##instrow%zu", inst.guid.c_str(), inst.speciesId.c_str(),
						static_cast<unsigned>(inst.shapeVariantIndex), inst.uniformScale, i);
				}
				else
				{
					std::snprintf(label, sizeof(label), "%s - %s##instrow%zu", inst.guid.c_str(), inst.gltfContentRelativePath.c_str(), i);
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
							ImGui::TextUnformatted("Edition instance selectionnee (arbre)");
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
							(void)ImGui::SliderFloat("Echelle##edit_sel", &scf, static_cast<float>(spec->scaleMin), static_cast<float>(spec->scaleMax),
								"%.3f");
							inst.uniformScale = static_cast<double>(scf);
						}
					}
				}
			}
			if (ImGui::Button("Aucune selection (pose toujours une nouvelle instance)"))
			{
				m_session->SelectedLayoutInstanceIndex() = -1;
			}
			ImGui::End();

			ImGui::Begin("Statut", nullptr, ImGuiWindowFlags_NoMouseInputs);
			ImGui::TextWrapped("%s", m_session->Status().c_str());
			ImGui::End();
		}
		else
		{
			ImGui::Begin("Proprietes", nullptr, ImGuiWindowFlags_NoMouseInputs);
			ImGui::TextUnformatted("Session editeur non initialisee.");
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
