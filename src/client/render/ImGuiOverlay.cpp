// Overlay ImGui commun (Vulkan, rendu dynamique) : contexte ImGui, polices,
// backends Win32 / Vulkan et dessin par-dessus le backbuffer. Partagé par les
// fenêtres de jeu (chat, HUD, guilde…) et par l'éditeur monde.
//
// Extrait de WorldEditorImGui (src/world_editor/ui) sans changement de
// comportement : l'infrastructure ImGui ne dépend plus de l'éditeur, première
// étape pour sortir l'éditeur de lcdlln.exe.
#include "src/client/render/ImGuiOverlay.h"

#include "src/shared/core/Config.h"
#include "src/shared/core/Log.h"
#include "src/shared/platform/FileSystem.h"
#include "src/shared/platform/Window.h"
#include "src/client/render/SharedFontHandles.h"
#include "src/client/render/vk/VkDeviceContext.h"

#include <algorithm>
#include <cstring>
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
	// ImGui 1.91+ : la declaration n'est plus dans l'en-tete (#if 0) pour eviter HWND dans l'API publique.
	extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

#	include <vulkan/vulkan.h>
#endif

namespace engine::render
{
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

	ImGuiOverlay::ImGuiOverlay() = default;

	ImGuiOverlay::~ImGuiOverlay()
	{
#if defined(_WIN32)
		if (m_ready)
		{
			LOG_WARN(Render, "[WorldEditorImGui] Shutdown non appele avant destruction");
		}
#endif
	}

	bool ImGuiOverlay::Init(::VkInstance instance,
		const engine::render::VkDeviceContext& deviceContext,
		VkFormat swapchainFormat,
		uint32_t swapchainImageCount,
		uint32_t vulkanApiVersion,
		void* hwndNative,
		const engine::core::Config* cfg,
		bool isWorldEditorExe,
		const std::function<void()>& styleHook)
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
		(void)styleHook;
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

		// 1024 sets : la fonte ImGui + l'apercu de race + les icones de competences
		// (arbre/Grimoire/barre via SkillIconCache) allouent chacun 1 descripteur.
		// L'arbre peut afficher ~180 icones a la fois ; 64 (valeur initiale) etait
		// trop juste. SkillIconCache plafonne a 900 (< 1024) pour rester sous cette
		// borne. FREE_DESCRIPTOR_SET_BIT conserve pour RemoveTexture.
		VkDescriptorPoolSize poolSizes[] = {
			{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1024 },
		};
		VkDescriptorPoolCreateInfo poolInfo{};
		poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
		poolInfo.maxSets = 1024;
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
		// Polish UI 2026-07-17 — thème sombre dédié à l'éditeur monde
		// (anthracite + accent doré, arrondis, paddings). Uniquement pour le
		// binaire éditeur : le client de jeu garde ses styles d'UI propres.
		if (isWorldEditorExe && styleHook)
		{
			styleHook();
		}

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
		// Plage etendue pour la fonte de repli riche (Arial). Couvre tout ce que
		// Windlass (decorative, A-Z/a-z/0-9 uniquement) ne fournit pas :
		//   - symboles ASCII : * [ ] @ % { } etc. ;
		//   - supplement Latin-1 : accents FR (é è à ç û ï ô...) ;
		//   - ponctuation generale : tiret cadratin/demi-cadratin (— –), apostrophes
		//     et guillemets courbes (' ' " "), puce (•), points de suspension (…) ;
		//   - euro et quelques fleches.
		// Sans ces plages, ImGui rend '?' pour tout caractere non-ASCII — c'est la
		// cause des '?' visibles dans TOUTES les interfaces in-game (titres de
		// panneaux, separateurs, libelles accentues).
		static const ImWchar kRichFallbackRanges[] = {
			0x0020, 0x00FF, // ASCII + supplement Latin-1 (symboles + accents FR)
			0x2010, 0x2027, // tirets, apostrophes/guillemets courbes, puce, points de suspension
			0x20AC, 0x20AC, // € (euro)
			0x2190, 0x2193, // fleches gauche/haut/droite/bas
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
					// Plage riche (A-Z, a-z, 0-9, accents FR, ponctuation typographique
					// — tiret cadratin, guillemets courbes — et fleches) : evite les '?'
					// dans l'editeur aussi (meme cause racine que l'in-game).
					ImFont* arialFont = io.Fonts->AddFontFromMemoryTTF(atlasArial, static_cast<int>(bytesArial.size()),
						arialPx, &acfg, kRichFallbackRanges);
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
				// Fonte de repli pour tous les glyphes hors Windlass. On merge en
				// priorite Arial (Latin-1 complet + ponctuation typographique, present
				// sur tout Windows) : c'est ce qui elimine les '?' affiches pour les
				// accents et le tiret cadratin dans TOUTES les interfaces in-game.
				// Si Arial est introuvable, repli sur ProggyClean (ASCII seulement —
				// couvre * [ ] @ mais laisse accents/typographie en '?').
				// Lettres accentuées (é, è, à, ç, ê…) : Windlass n'en a pas et Arial les
				// rendait en minuscules d'une autre police au milieu des petites
				// capitales de Windlass (« FéMININ », « LUMIèRE »). On merge d'abord une
				// police à petites capitales de même esprit (Cinzel, déjà livrée) pour la
				// plage des lettres Latin-1 : ImGui garde le premier glyphe fusionné, Arial
				// ne complète ensuite que le reste (symboles, ponctuation typographique).
				{
					static const ImWchar kAccentRanges[] = {
						0x00C0, 0x00FF, // lettres accentuées Latin-1 (À-ÿ)
						0x0152, 0x0153, // Œ œ
						0,
					};
					const std::string accentPath = cfg->GetString("render.auth_ui.accent_font_path",
						"fonts/Cinzel-Regular.ttf");
					std::vector<uint8_t> bytesAccent = engine::platform::FileSystem::ReadAllBytesContent(*cfg, accentPath);
					if (!bytesAccent.empty())
					{
						void* atlasAccent = IM_ALLOC(bytesAccent.size());
						std::memcpy(atlasAccent, bytesAccent.data(), bytesAccent.size());
						ImFontConfig accCfg{};
						accCfg.MergeMode = true;
						// Cinzel dessine ses petites capitales un peu plus bas que Windlass :
						// léger agrandissement pour aligner les hauteurs de lettres.
						ImFont* accFont = io.Fonts->AddFontFromMemoryTTF(atlasAccent, static_cast<int>(bytesAccent.size()),
							uiFontPx * 1.08f, &accCfg, kAccentRanges);
						if (accFont != nullptr)
							LOG_INFO(Render, "[WorldEditorImGui] Accents : {} fusionnée sur Windlass", accentPath);
						else
						{
							IM_FREE(atlasAccent);
							LOG_WARN(Render, "[WorldEditorImGui] Accents : AddFontFromMemoryTTF a échoué ({})", accentPath);
						}
					}
					else
					{
						LOG_WARN(Render, "[WorldEditorImGui] Police des accents introuvable : {} (repli Arial)", accentPath);
					}
				}
				bool richFallbackLoaded = false;
				const std::string fbArialPath = cfg->GetString("editor.font.arial_path", "C:/Windows/Fonts/arial.ttf");
				std::vector<uint8_t> bytesFbArial = engine::platform::FileSystem::ReadAllBytes(std::filesystem::path(fbArialPath));
				if (bytesFbArial.empty())
				{
					bytesFbArial = engine::platform::FileSystem::ReadAllBytesContent(*cfg, fbArialPath);
				}
				if (!bytesFbArial.empty())
				{
					// L'atlas prend la propriete du buffer (IM_FREE) → allouer via IM_ALLOC.
					void* atlasFbArial = IM_ALLOC(bytesFbArial.size());
					std::memcpy(atlasFbArial, bytesFbArial.data(), bytesFbArial.size());
					ImFontConfig fbCfg{};
					fbCfg.MergeMode = true; // fusionne dans la fonte Windlass par defaut
					// Taille alignee sur la fonte UI Windlass pour une hauteur coherente.
					ImFont* fbFont = io.Fonts->AddFontFromMemoryTTF(atlasFbArial, static_cast<int>(bytesFbArial.size()),
						uiFontPx, &fbCfg, kRichFallbackRanges);
					if (fbFont != nullptr)
					{
						richFallbackLoaded = true;
						LOG_INFO(Render, "[WorldEditorImGui] Fallback Arial merge sur Windlass (accents FR + ponctuation typographique)");
					}
					else
					{
						IM_FREE(atlasFbArial);
						LOG_WARN(Render, "[WorldEditorImGui] Fallback Arial : AddFontFromMemoryTTF a echoue ({})", fbArialPath);
					}
				}
				if (!richFallbackLoaded)
				{
					ImFontConfig fallbackCfg{};
					fallbackCfg.MergeMode = true;
					io.Fonts->AddFontDefault(&fallbackCfg);
					LOG_INFO(Render, "[WorldEditorImGui] Fallback ProggyClean merge sur Windlass (Arial absent ; accents/typographie restent en '?')");
				}
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

	void ImGuiOverlay::Shutdown(VkDevice device)
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

	void ImGuiOverlay::OnSwapchainRecreate(uint32_t swapchainImageCount)
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

	void ImGuiOverlay::NewFrame(float deltaSeconds, float displayWidth, float displayHeight)
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

	bool ImGuiOverlay::WantsCaptureMouse() const
	{
#if defined(_WIN32)
		return m_ready && ImGui::GetCurrentContext() && ImGui::GetIO().WantCaptureMouse;
#else
		return false;
#endif
	}

	bool ImGuiOverlay::WantsCaptureKeyboard() const
	{
#if defined(_WIN32)
		return m_ready && ImGui::GetCurrentContext() && ImGui::GetIO().WantCaptureKeyboard;
#else
		return false;
#endif
	}

	bool ImGuiOverlay::RecordToBackbuffer(VkCommandBuffer cmd,
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

	void ImGuiOverlay::AttachPlatformWindow(void* hwndNative, engine::platform::Window& window)
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

	void ImGuiOverlay::DetachPlatformWindow(engine::platform::Window& window)
	{
#if defined(_WIN32)
		window.SetPreMessageInterceptor({});
#endif
		m_hwnd = nullptr;
		(void)window;
	}

} // namespace engine::render
