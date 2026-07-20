#include "src/client/app/Engine.h"

#include "src/client/render/static_mesh/StaticMeshLoader.h"
#include "src/client/world/instances/Buildings.h"
#include "src/client/world/instances/BuildingTemplateLibrary.h"
#include "src/world_editor/panels/BuildingEditorPanel.h"
#include "src/shared/core/Log.h"
#include "src/world_editor/ui/EditorMode.h"
#include "src/world_editor/ui/WorldEditorImGui.h"
#include "src/world_editor/ui/WorldEditorSession.h"
#include "src/world_editor/ui/WorldMapIo.h" // lot B3 : SanitizeZoneId (namespacing zone)
#include "src/world_editor/core/WorldEditorShell.h"
#include "src/world_editor/core/CompositeCommand.h"            // Roadmap-6 : undo groupé du gizmo
#include "src/world_editor/HazardCommand.h"                    // Roadmap-8 : pose de danger undoable
#include "src/world_editor/inspector/SetEntityTransformCommand.h" // Roadmap-6 : undo du drag de gizmo
#include "src/world_editor/scene/LayoutPlacementCommands.h"    // Roadmap-6 : placement undoable
#include "src/world_editor/panels/ScenePanel.h"
#include "src/shared/core/memory/Memory.h"
#include "src/shared/platform/FileSystem.h"
#include "src/shared/network/ChatPayloads.h"
#include "src/shared/network/IgnoreListPayloads.h"
#include "src/shared/network/MailPayloads.h"
#include "src/shared/network/GmTicketPayloads.h"
#include "src/shared/network/ReputationPayloads.h"
#include "src/shared/network/ExploitPayloads.h" // SP2 anniversaires (2026-07-18)
#include "src/shared/anniversary/CakeItemToken.h" // SP3 anniversaires (2026-07-18)
#include "src/shared/network/ArenaPayloads.h"
#include "src/shared/network/BattleGroundPayloads.h"
#include "src/shared/network/OutdoorPvpPayloads.h"
#include "src/shared/network/WeatherPayloads.h"
#include "src/shared/network/GameEventPayloads.h"
#include "src/shared/network/GuildPayloads.h"
#include "src/shared/network/AuctionPayloads.h"
#include "src/shared/network/LootPayloads.h"
#include "src/shared/network/LunarPayloads.h"
#include "src/shared/network/WorldClockPayloads.h"
#include "src/shared/world/WorldClock.h"
#include "src/shared/network/AdminCommandPayloads.h"
#include "src/shared/network/LfgPayloads.h"
#include "src/shared/network/CinematicPayloads.h"
#include "src/shared/network/SkillPayloads.h"
#include "src/shared/network/TradePayloads.h"
#include "src/shared/network/PacketBuilder.h"
#include "src/shared/network/ProtocolV1Constants.h"
#include "src/client/render/AuthImGuiRenderer.h"
#include "src/client/dialogue/DialogueConfigLoader.h"
#include "src/client/render/DialogueImGuiRenderer.h"
#include "src/client/render/ChatImGuiRenderer.h"
#include "src/client/render/QuestImGuiRenderer.h"
#include "src/client/render/MailImGuiRenderer.h"
#include "src/client/render/GmTicketImGuiRenderer.h"
#include "src/client/render/ReputationImGuiRenderer.h"
#include "src/client/render/CharacterSheetImGuiRenderer.h"
#include "src/client/render/ArenaImGuiRenderer.h"
#include "src/client/render/BattleGroundImGuiRenderer.h"
#include "src/client/render/OutdoorPvpImGuiRenderer.h"
#include "src/client/render/WeatherImGuiRenderer.h"
#include "src/client/render/clouds/WeatherKindMap.h"
#include "src/client/render/clouds/CloudParams.h"
#include "src/client/render/clouds/CloudWeatherMapper.h"
#include "src/client/render/CloudPass.h"
#include "src/client/render/GameEventImGuiRenderer.h"
#include "src/client/render/GuildImGuiRenderer.h"
#include "src/client/render/AuctionImGuiRenderer.h"
#include "src/client/render/LootRollImGuiRenderer.h"
#include "src/client/render/LfgImGuiRenderer.h"
#include "src/client/render/CinematicImGuiRenderer.h"
#include "src/client/render/SkillBookImGuiRenderer.h"
#include "src/client/render/GrimoireImGuiRenderer.h"
#include "src/client/render/ClassSkillTreeImGuiRenderer.h"
#include "src/client/render/CharacterWindowImGuiRenderer.h"
#include "src/client/render/EditorHubImGuiRenderer.h"
#include "src/client/gameplay/ActionBarLayout.h"
#include "src/client/ui_common/CurrencyFormat.h"
#include "src/client/render/skinned/PlaceholderPart.h"
#include "src/client/render/LnTheme.h"
#include "src/client/render/AuthUiRenderer.h"
#include "src/client/render/DeferredPipeline.h"
#include "src/client/render/ShaderCompiler.h"
#include "src/client/render/terrain/HeightmapLoader.h"
#include "src/client/render/terrain/TerrainEditingTools.h"
#include "src/client/render/terrain/TerrainRenderSelection.h"
#include "src/client/character_creation/CharacterCreationUi.h"
#include "src/shared/network/ServerProtocol.h"

#include <GLFW/glfw3.h>
#include <vulkan/vulkan.h>

// vk_mem_alloc.h removed: VMA is disabled (STAB.7) — all subsystems use raw Vulkan allocations.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <list>
#include <filesystem>
#include <fstream>
#include <optional>
#include <span>
#include <sstream>
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
#	include "imgui.h"
#	include "src/client/render/CompassHud.h"
#endif

namespace engine
{
	namespace
	{
		constexpr float kWorldEditorPickPi = 3.14159265f;

		/// Crée une image Vulkan OPTIMAL-tiling remplie d'une couleur unie, avec
		/// VkImageView + VkSampler. Supporte images 2D (arrayLayers=1) et cubes
		/// (arrayLayers=6, viewType=VK_IMAGE_VIEW_TYPE_CUBE).
		/// Upload via staging buffer + one-shot command sur \p pool / \p queue.
		/// Toutes les sorties sont VK_NULL_HANDLE en cas d'échec.
		static bool CreateSolidColorTexture(
			VkDevice device, VkPhysicalDevice physDev,
			VkCommandPool pool, VkQueue queue,
			uint32_t arrayLayers, VkImageViewType viewType,
			uint8_t r, uint8_t g, uint8_t b, uint8_t a,
			VkImage& outImage, VkDeviceMemory& outMemory,
			VkImageView& outView, VkSampler& outSampler)
		{
			outImage = VK_NULL_HANDLE; outMemory = VK_NULL_HANDLE;
			outView  = VK_NULL_HANDLE; outSampler = VK_NULL_HANDLE;

			constexpr VkFormat kFmt = VK_FORMAT_R8G8B8A8_UNORM;
			const uint8_t pixel[4] = {r, g, b, a};
			const VkDeviceSize kPixelBytes = 4u;
			const VkDeviceSize stagingBytes = kPixelBytes * arrayLayers;
			const bool isCube = (arrayLayers == 6);

			// 1. Staging buffer HOST_VISIBLE + HOST_COHERENT
			VkBuffer      stagBuf  = VK_NULL_HANDLE;
			VkDeviceMemory stagMem  = VK_NULL_HANDLE;
			{
				VkBufferCreateInfo bi{};
				bi.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
				bi.size        = stagingBytes;
				bi.usage       = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
				bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
				if (vkCreateBuffer(device, &bi, nullptr, &stagBuf) != VK_SUCCESS) return false;

				VkMemoryRequirements req{};
				vkGetBufferMemoryRequirements(device, stagBuf, &req);

				VkPhysicalDeviceMemoryProperties props{};
				vkGetPhysicalDeviceMemoryProperties(physDev, &props);
				uint32_t memType = UINT32_MAX;
				constexpr VkMemoryPropertyFlags kHost =
					VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
				for (uint32_t i = 0; i < props.memoryTypeCount; ++i)
				{
					if ((req.memoryTypeBits & (1u << i)) &&
					    (props.memoryTypes[i].propertyFlags & kHost) == kHost)
					{ memType = i; break; }
				}
				if (memType == UINT32_MAX) { vkDestroyBuffer(device, stagBuf, nullptr); return false; }

				VkMemoryAllocateInfo ai{};
				ai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
				ai.allocationSize  = req.size;
				ai.memoryTypeIndex = memType;
				if (vkAllocateMemory(device, &ai, nullptr, &stagMem) != VK_SUCCESS)
				{ vkDestroyBuffer(device, stagBuf, nullptr); return false; }
				vkBindBufferMemory(device, stagBuf, stagMem, 0);

				void* mapped = nullptr;
				vkMapMemory(device, stagMem, 0, stagingBytes, 0, &mapped);
				for (uint32_t i = 0; i < arrayLayers; ++i)
					memcpy(static_cast<uint8_t*>(mapped) + i * kPixelBytes, pixel, kPixelBytes);
				vkUnmapMemory(device, stagMem);
			}

			auto cleanup = [&]()
			{
				vkDestroyBuffer(device, stagBuf, nullptr);
				vkFreeMemory(device, stagMem, nullptr);
				if (outView)    { vkDestroyImageView(device, outView, nullptr);  outView    = VK_NULL_HANDLE; }
				if (outSampler) { vkDestroySampler(device, outSampler, nullptr); outSampler = VK_NULL_HANDLE; }
				if (outImage)   { vkDestroyImage(device, outImage, nullptr);     outImage   = VK_NULL_HANDLE; }
				if (outMemory)  { vkFreeMemory(device, outMemory, nullptr);      outMemory  = VK_NULL_HANDLE; }
			};

			// 2. Image DEVICE_LOCAL OPTIMAL
			{
				VkImageCreateInfo imgInfo{};
				imgInfo.sType       = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
				if (isCube) imgInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
				imgInfo.imageType   = VK_IMAGE_TYPE_2D;
				imgInfo.format      = kFmt;
				imgInfo.extent      = {1, 1, 1};
				imgInfo.mipLevels   = 1;
				imgInfo.arrayLayers = arrayLayers;
				imgInfo.samples     = VK_SAMPLE_COUNT_1_BIT;
				imgInfo.tiling      = VK_IMAGE_TILING_OPTIMAL;
				imgInfo.usage       = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
				imgInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
				if (vkCreateImage(device, &imgInfo, nullptr, &outImage) != VK_SUCCESS)
				{ cleanup(); return false; }

				VkMemoryRequirements req{};
				vkGetImageMemoryRequirements(device, outImage, &req);

				VkPhysicalDeviceMemoryProperties props{};
				vkGetPhysicalDeviceMemoryProperties(physDev, &props);
				uint32_t memType = UINT32_MAX;
				for (uint32_t i = 0; i < props.memoryTypeCount; ++i)
				{
					if ((req.memoryTypeBits & (1u << i)) &&
					    (props.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))
					{ memType = i; break; }
				}
				if (memType == UINT32_MAX) { cleanup(); return false; }

				VkMemoryAllocateInfo ai{};
				ai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
				ai.allocationSize  = req.size;
				ai.memoryTypeIndex = memType;
				if (vkAllocateMemory(device, &ai, nullptr, &outMemory) != VK_SUCCESS)
				{ cleanup(); return false; }
				vkBindImageMemory(device, outImage, outMemory, 0);
			}

			// 3. One-shot command : layout UNDEFINED→TRANSFER_DST + copy + TRANSFER_DST→SHADER_READ
			{
				VkCommandBufferAllocateInfo cbai{};
				cbai.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
				cbai.commandPool        = pool;
				cbai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
				cbai.commandBufferCount = 1;
				VkCommandBuffer cmd = VK_NULL_HANDLE;
				if (vkAllocateCommandBuffers(device, &cbai, &cmd) != VK_SUCCESS)
				{ cleanup(); return false; }

				VkCommandBufferBeginInfo bi{};
				bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
				bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
				vkBeginCommandBuffer(cmd, &bi);

				VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, arrayLayers};

				VkImageMemoryBarrier bar{};
				bar.sType            = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
				bar.srcAccessMask    = 0;
				bar.dstAccessMask    = VK_ACCESS_TRANSFER_WRITE_BIT;
				bar.oldLayout        = VK_IMAGE_LAYOUT_UNDEFINED;
				bar.newLayout        = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
				bar.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				bar.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				bar.image            = outImage;
				bar.subresourceRange = range;
				vkCmdPipelineBarrier(cmd,
					VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
					0, 0, nullptr, 0, nullptr, 1, &bar);

				std::vector<VkBufferImageCopy> copies(arrayLayers);
				for (uint32_t i = 0; i < arrayLayers; ++i)
				{
					VkBufferImageCopy& c = copies[i];
					c = {};
					c.bufferOffset         = i * kPixelBytes;
					c.imageSubresource     = {VK_IMAGE_ASPECT_COLOR_BIT, 0, i, 1};
					c.imageExtent          = {1, 1, 1};
				}
				vkCmdCopyBufferToImage(cmd, stagBuf, outImage,
					VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
					static_cast<uint32_t>(copies.size()), copies.data());

				bar.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
				bar.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
				bar.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
				bar.newLayout     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
				vkCmdPipelineBarrier(cmd,
					VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
					0, 0, nullptr, 0, nullptr, 1, &bar);

				vkEndCommandBuffer(cmd);

				VkFence fence = VK_NULL_HANDLE;
				VkFenceCreateInfo fi{};
				fi.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
				vkCreateFence(device, &fi, nullptr, &fence);

				VkSubmitInfo si{};
				si.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
				si.commandBufferCount = 1;
				si.pCommandBuffers    = &cmd;
				vkQueueSubmit(queue, 1, &si, fence);
				vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX);

				vkDestroyFence(device, fence, nullptr);
				vkFreeCommandBuffers(device, pool, 1, &cmd);
			}

			// 4. Libère le staging
			vkDestroyBuffer(device, stagBuf, nullptr);
			vkFreeMemory(device, stagMem, nullptr);

			// 5. ImageView
			{
				VkImageViewCreateInfo vi{};
				vi.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
				vi.image    = outImage;
				vi.viewType = viewType;
				vi.format   = kFmt;
				vi.components = {VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
				                 VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY};
				vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, arrayLayers};
				if (vkCreateImageView(device, &vi, nullptr, &outView) != VK_SUCCESS)
				{
					vkDestroyImage(device, outImage, nullptr);  outImage  = VK_NULL_HANDLE;
					vkFreeMemory(device, outMemory, nullptr);   outMemory = VK_NULL_HANDLE;
					return false;
				}
			}

			// 6. Sampler linéaire
			{
				VkSamplerCreateInfo si{};
				si.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
				si.magFilter    = VK_FILTER_LINEAR;
				si.minFilter    = VK_FILTER_LINEAR;
				si.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_NEAREST;
				si.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
				si.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
				si.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
				si.maxLod       = 0.0f;
				si.maxAnisotropy = 1.0f;
				if (vkCreateSampler(device, &si, nullptr, &outSampler) != VK_SUCCESS)
				{
					vkDestroyImageView(device, outView, nullptr);   outView   = VK_NULL_HANDLE;
					vkDestroyImage(device, outImage, nullptr);      outImage  = VK_NULL_HANDLE;
					vkFreeMemory(device, outMemory, nullptr);       outMemory = VK_NULL_HANDLE;
					return false;
				}
			}

			return true;
		}

		/// Découpe une chaîne CSV en tokens nettoyés (espaces de début/fin
		/// retirés), en ignorant les tokens vides. Utilisé pour lire la liste
		/// `client.character_creation.body_material_names` (noms de matériaux
		/// glTF qui reçoivent la peau). Renvoie des std::string possédés (pas
		/// de string_view) car la source est une valeur de config temporaire.
		std::vector<std::string> SplitCsv(const std::string& csv)
		{
			std::vector<std::string> out;
			size_t start = 0;
			while (start <= csv.size())
			{
				const size_t comma = csv.find(',', start);
				const size_t end = (comma == std::string::npos) ? csv.size() : comma;
				size_t a = start, b = end;
				while (a < b && std::isspace(static_cast<unsigned char>(csv[a]))) ++a;
				while (b > a && std::isspace(static_cast<unsigned char>(csv[b - 1]))) --b;
				if (b > a) out.emplace_back(csv.substr(a, b - a));
				if (comma == std::string::npos) break;
				start = comma + 1;
			}
			return out;
		}

		/// Retourne le temps ecoule en secondes depuis le premier appel a cette
		/// fonction, en float 32 bits. Normalise par un temps de reference
		/// capture au demarrage de l'application (static init).
		///
		/// Necessaire pour `AnimationCrossfade` : passer directement
		/// `steady_clock::now().time_since_epoch()` en float donnerait des
		/// valeurs ~10^8 - 10^9 (depuis boot machine), qui depassent la
		/// precision de la mantissa float32 (~7 chiffres significatifs).
		/// Resultat : (now - startTime) sautait par pas de plusieurs ms voire
		/// des dizaines de ms entre frames -> mesh qui tremble. Avec cette
		/// fonction, les valeurs restent < 10^4 secondes en pratique (uptime
		/// session), precision microseconde -> animation lisse.
		float EngineNowSec()
		{
			static const auto kStart = std::chrono::steady_clock::now();
			return std::chrono::duration<float>(std::chrono::steady_clock::now() - kStart).count();
		}

		/// Sous-projet B.1 (Task 9) — Projette l'input clavier WASD/ZQSD dans
		/// le repere camera courant pour produire la `MoveInput` passee au
		/// `CharacterController::Update`. Touche les directions XZ (Y ignore),
		/// normalise pour eviter la diagonale rapide (1.41x), et capture aussi
		/// le run (Shift) et le saut (Space, edge-triggered).
		///
		/// Le mapping des touches d'avant et de gauche depend de `layout`
		/// (cf. `controls.movement_layout` dans config) :
		/// - WASD : forward=W, left=A (clavier US par defaut)
		/// - ZQSD : forward=Z, left=Q (clavier FR/BE AZERTY)
		/// S et D sont identiques dans les deux layouts. L'ancien code mappait
		/// W ET Z (ou A ET Q) en OR sans discriminer ; resultat sur AZERTY :
		/// la touche W (qui devrait ne rien faire en ZQSD) faisait aussi
		/// avancer parce qu'elle envoie VK_W (rapport user 2026-05-19).
		///
		/// \param input  Input snapshot de la frame courante (clavier + souris).
		/// \param camera Camera orbitale (utilise GetForwardXZ / GetRightXZ pour
		///               le repere local au lieu d'un yaw monde fixe — les
		///               touches restent "vers ce que je regarde" meme apres
		///               rotation de la camera).
		/// \param layout Layout clavier (WASD ou ZQSD).
		/// \return MoveInput pret a etre consomme par CharacterController.
		///         `swim*/fly` sont hors-scope B.1 et restent false.
		///
		/// Effet de bord : aucun. Pure projection input -> intention.
		// Touches remappables (nom config <-> enum platform::Key). Limitee aux
		// Key reellement definies dans l'enum. Sert au panneau Options (affichage
		// + capture du rebind) et a la lecture des binds gameplay depuis
		// controls.keybind.* (sprint / crouch / cast / grimoire / skilltree).
		struct RebindableKey { engine::platform::Key key; const char* name; };
		const RebindableKey kRebindableKeys[] = {
			{engine::platform::Key::A,"A"},{engine::platform::Key::B,"B"},{engine::platform::Key::C,"C"},
			{engine::platform::Key::D,"D"},{engine::platform::Key::E,"E"},{engine::platform::Key::F,"F"},
			{engine::platform::Key::G,"G"},{engine::platform::Key::H,"H"},{engine::platform::Key::I,"I"},
			{engine::platform::Key::L,"L"},
			{engine::platform::Key::M,"M"},{engine::platform::Key::N,"N"},{engine::platform::Key::O,"O"},
			{engine::platform::Key::P,"P"},{engine::platform::Key::Q,"Q"},{engine::platform::Key::R,"R"},
			{engine::platform::Key::S,"S"},{engine::platform::Key::U,"U"},{engine::platform::Key::V,"V"},
			{engine::platform::Key::W,"W"},{engine::platform::Key::X,"X"},{engine::platform::Key::Y,"Y"},
			{engine::platform::Key::Z,"Z"},
			{engine::platform::Key::Digit0,"0"},{engine::platform::Key::Digit1,"1"},{engine::platform::Key::Digit2,"2"},
			{engine::platform::Key::Digit3,"3"},{engine::platform::Key::Digit4,"4"},{engine::platform::Key::Digit5,"5"},
			{engine::platform::Key::Digit6,"6"},{engine::platform::Key::Digit7,"7"},{engine::platform::Key::Digit8,"8"},
			{engine::platform::Key::Digit9,"9"},
			{engine::platform::Key::Control,"Ctrl"},{engine::platform::Key::Alt,"Alt"},
			{engine::platform::Key::Shift,"Shift"},{engine::platform::Key::Space,"Espace"},
			{engine::platform::Key::Tab,"Tab"},
			{engine::platform::Key::F_1,"F1"},{engine::platform::Key::F_2,"F2"},{engine::platform::Key::F_3,"F3"},
			{engine::platform::Key::F_4,"F4"},{engine::platform::Key::F_5,"F5"},{engine::platform::Key::F_6,"F6"},
			{engine::platform::Key::F_7,"F7"},{engine::platform::Key::F_8,"F8"},{engine::platform::Key::F_9,"F9"},
			{engine::platform::Key::F_10,"F10"},{engine::platform::Key::F_11,"F11"},{engine::platform::Key::F_12,"F12"},
		};

		/// Nom affichable/config d'une touche (ou "?" si hors table).
		const char* KeyName(engine::platform::Key k)
		{
			for (const auto& e : kRebindableKeys)
				if (e.key == k) return e.name;
			return "?";
		}

		/// Resout un nom de touche (config) en `Key`, `fallback` si inconnu.
		engine::platform::Key KeyFromName(const std::string& name, engine::platform::Key fallback)
		{
			for (const auto& e : kRebindableKeys)
				if (name == e.name) return e.key;
			return fallback;
		}

		/// Glyphe d'AFFICHAGE d'une touche selon la disposition clavier active
		/// (AZERTY → la rangée du haut donne & é " ' ( - è _ ç à). Distinct de
		/// KeyName (nom de config stable/portable). Fallback : KeyName.
		std::string KeyGlyph(engine::platform::Key k)
		{
#if defined(_WIN32)
			const UINT vk = static_cast<UINT>(k); // l'enum Key == codes VK_* Win32
			const UINT ch = ::MapVirtualKeyW(vk, MAPVK_VK_TO_CHAR) & 0x7FFFu;
			if (ch != 0)
			{
				const wchar_t w = static_cast<wchar_t>(ch);
				char utf8[8] = {0};
				const int n = ::WideCharToMultiByte(CP_UTF8, 0, &w, 1, utf8, sizeof(utf8) - 1, nullptr, nullptr);
				if (n > 0)
				{
					return std::string(utf8, static_cast<size_t>(n));
				}
			}
#endif
			return KeyName(k);
		}

		// Projette une position monde -> pixels ecran. Formule alignee sur
		// WorldEditorImGui::WorldToScreen (viewProj col-major .m, convention Vulkan).
		// false si derriere la camera ou hors near/far. Sert aux marqueurs interactibles.
		[[maybe_unused]] bool WorldToScreenPx(const float vp[16], float wx, float wy, float wz,
			int vw, int vh, float& sx, float& sy)
		{
			const float cx = vp[0]*wx + vp[4]*wy + vp[8]*wz + vp[12];
			const float cy = vp[1]*wx + vp[5]*wy + vp[9]*wz + vp[13];
			const float cz = vp[2]*wx + vp[6]*wy + vp[10]*wz + vp[14];
			const float cw = vp[3]*wx + vp[7]*wy + vp[11]*wz + vp[15];
			if (cw <= 1e-5f) return false;
			const float invW = 1.0f / cw;
			const float ndcX = cx * invW, ndcY = cy * invW, ndcZ = cz * invW;
			if (ndcZ < 0.0f || ndcZ > 1.0f) return false;
			sx = (ndcX * 0.5f + 0.5f) * static_cast<float>(vw);
			sy = (ndcY * 0.5f + 0.5f) * static_cast<float>(vh);
			return true;
		}

		engine::gameplay::MoveInput BuildMoveInput(
			const engine::platform::Input& input,
			const engine::render::OrbitalCameraController& camera,
			engine::render::MovementLayout layout,
			engine::platform::Key sprintKey,
			engine::platform::Key crouchKey)
		{
			engine::gameplay::MoveInput out{};

			const engine::math::Vec3 forward = camera.GetForwardXZ();
			const engine::math::Vec3 right   = camera.GetRightXZ();

			const engine::platform::Key forwardKey =
				(layout == engine::render::MovementLayout::ZQSD) ? engine::platform::Key::Z : engine::platform::Key::W;
			const engine::platform::Key leftKey =
				(layout == engine::render::MovementLayout::ZQSD) ? engine::platform::Key::Q : engine::platform::Key::A;
			const engine::platform::Key backKey  = engine::platform::Key::S;
			const engine::platform::Key rightKey = engine::platform::Key::D;

			engine::math::Vec3 dir{ 0.0f, 0.0f, 0.0f };
			if (input.IsDown(forwardKey))
			{
				dir.x += forward.x;
				dir.z += forward.z;
			}
			if (input.IsDown(backKey))
			{
				dir.x -= forward.x;
				dir.z -= forward.z;
			}
			if (input.IsDown(rightKey))
			{
				dir.x += right.x;
				dir.z += right.z;
			}
			if (input.IsDown(leftKey))
			{
				dir.x -= right.x;
				dir.z -= right.z;
			}

			const float lenSq = dir.x * dir.x + dir.z * dir.z;
			if (lenSq > 0.0f)
			{
				const float invLen = 1.0f / std::sqrt(lenSq);
				out.moveDirXZ = engine::math::Vec3{ dir.x * invLen, 0.0f, dir.z * invLen };
			}

			out.run         = input.IsDown(engine::platform::Key::Shift);
			out.sprint      = input.IsDown(sprintKey);
			out.crouch      = input.IsDown(crouchKey);
			out.jumpPressed = input.WasPressed(engine::platform::Key::Space);
			// Nage : contrôle vertical. Espace = remonter, touche Crouch = descendre.
			// Le CharacterController ne les consomme qu'en mode Water (cf. QueryWater) ;
			// hors eau, Espace reste le saut et Crouch l'accroupi.
			out.swimUpPressed   = input.IsDown(engine::platform::Key::Space);
			out.swimDownPressed = input.IsDown(crouchKey);
			return out;
		}

		/// Sous-projet B.1 — Detecte "pure back step" : touche back enfoncee
		/// seule, sans aucune autre direction de mouvement. La state machine
		/// de locomotion utilise ce flag pour declencher l'etat WalkBack
		/// (mesh inchange + anim Walking Backwards) ; tout autre input
		/// (forward, strafe, diagonale) repasse en free-mover (pivot mesh +
		/// Walk standard). On detecte les deux mappings AZERTY/QWERTY pour
		/// rester robuste quel que soit `controls.movement_layout`.
		bool IsPureBackInput(const engine::platform::Input& input)
		{
			const bool back = input.IsDown(engine::platform::Key::S);
			if (!back) return false;
			const bool anyForward = input.IsDown(engine::platform::Key::W) || input.IsDown(engine::platform::Key::Z);
			const bool anyLeft    = input.IsDown(engine::platform::Key::A) || input.IsDown(engine::platform::Key::Q);
			const bool anyRight   = input.IsDown(engine::platform::Key::D);
			return !anyForward && !anyLeft && !anyRight;
		}

		/// Sous-projet B.1 (Task 11) — Mapping etat de locomotion -> nom du clip
		/// charge dans `m_currentSkinnedMesh->clips`. Le nom doit matcher exactement
		/// la cle utilisee a l'insertion (cf. boot ~ligne 3863 ou les clips Mixamo
		/// "mixamo.com" sont renommes en "Idle" / "StartWalking" / "Walk").
		///
		/// Retourne "Idle" en fallback si un nouvel etat est ajoute a l'enum sans
		/// que ce switch soit mis a jour (defensif : evite un nullptr et un
		/// comportement non defini en runtime).
		const char* StateToClipName(engine::Engine::AvatarLocomotionState s)
		{
			switch (s) {
				case engine::Engine::AvatarLocomotionState::Idle:         return "Idle";
				case engine::Engine::AvatarLocomotionState::StartWalking: return "StartWalking";
				case engine::Engine::AvatarLocomotionState::Walk:         return "Walk";
				case engine::Engine::AvatarLocomotionState::WalkBack:     return "WalkBack";
				case engine::Engine::AvatarLocomotionState::Run:          return "Run";
				case engine::Engine::AvatarLocomotionState::Sprint:       return "Sprint";
				case engine::Engine::AvatarLocomotionState::CrouchIdle:   return "CrouchIdle";
				case engine::Engine::AvatarLocomotionState::CrouchWalk:   return "CrouchWalk";
				case engine::Engine::AvatarLocomotionState::Roll:         return "Roll";
				case engine::Engine::AvatarLocomotionState::Emote:        return "Emote";
				case engine::Engine::AvatarLocomotionState::Attack:       return "Attack";
				case engine::Engine::AvatarLocomotionState::Cast:         return "Cast";
				case engine::Engine::AvatarLocomotionState::Interact:     return "Interact";
				case engine::Engine::AvatarLocomotionState::Punch:        return "Punch";
				case engine::Engine::AvatarLocomotionState::Jump:         return "Jump";
				case engine::Engine::AvatarLocomotionState::Fall:         return "Fall";
				case engine::Engine::AvatarLocomotionState::Land:         return "Land";
				case engine::Engine::AvatarLocomotionState::SwimIdle:     return "SwimIdle";
				case engine::Engine::AvatarLocomotionState::SwimForward:  return "SwimForward";
			}
			return "Idle";
		}

		/// Sous-projet B.1 (Task 11) — Indique si le clip associe a un etat doit
		/// looper ou s'arreter a sa derniere keyframe (clamp). Consomme par
		/// `AnimationCrossfade::Play(clip, loops, now)`.
		///
		/// Idle / Walk / Run / Fall sont des etats "tenus" -> loop.
		/// StartWalking / Jump / Land sont des transitions one-shot -> clamp (la
		/// state machine transite hors de l'etat avant la fin du clip dans le cas
		/// normal ; le clamp evite un wrap visuel si la transition tarde).
		bool ClipLoops(engine::Engine::AvatarLocomotionState s)
		{
			return s == engine::Engine::AvatarLocomotionState::Idle
				|| s == engine::Engine::AvatarLocomotionState::Walk
				|| s == engine::Engine::AvatarLocomotionState::WalkBack
				|| s == engine::Engine::AvatarLocomotionState::Run
				|| s == engine::Engine::AvatarLocomotionState::Sprint
				|| s == engine::Engine::AvatarLocomotionState::CrouchIdle
				|| s == engine::Engine::AvatarLocomotionState::CrouchWalk
				|| s == engine::Engine::AvatarLocomotionState::Emote
				|| s == engine::Engine::AvatarLocomotionState::Fall
				|| s == engine::Engine::AvatarLocomotionState::SwimIdle
				|| s == engine::Engine::AvatarLocomotionState::SwimForward;
		}

		/// TD.8 — conversions entre l'état de locomotion local (Engine) et l'enum wire
		/// partagé (engine::server::AvatarAnimState). Les deux enums listent les mêmes états
		/// dans le même ordre ; les static_assert garantissent l'alignement à la compilation
		/// (un état inséré d'un seul côté casse la build au lieu d'un désync silencieux sur
		/// le wire). On caste sur l'uint8_t sous-jacent — pas de switch à maintenir en double.
		static_assert(static_cast<uint8_t>(engine::Engine::AvatarLocomotionState::Idle)
			== static_cast<uint8_t>(engine::server::AvatarAnimState::Idle), "AvatarAnimState desync (Idle)");
		static_assert(static_cast<uint8_t>(engine::Engine::AvatarLocomotionState::Emote)
			== static_cast<uint8_t>(engine::server::AvatarAnimState::Emote), "AvatarAnimState desync (Emote)");
		static_assert(static_cast<uint8_t>(engine::Engine::AvatarLocomotionState::Roll)
			== static_cast<uint8_t>(engine::server::AvatarAnimState::Roll), "AvatarAnimState desync (Roll)");
		static_assert(static_cast<uint8_t>(engine::Engine::AvatarLocomotionState::SwimForward)
			== static_cast<uint8_t>(engine::server::AvatarAnimState::SwimForward), "AvatarAnimState desync (dernier état)");

		uint8_t ToWireAnimState(engine::Engine::AvatarLocomotionState s)
		{
			return static_cast<uint8_t>(s);
		}

		engine::Engine::AvatarLocomotionState FromWireAnimState(uint8_t v)
		{
			// Borne défensive : une valeur hors enum (paquet corrompu) retombe sur Idle.
			if (v > static_cast<uint8_t>(engine::server::AvatarAnimState::SwimForward))
				return engine::Engine::AvatarLocomotionState::Idle;
			return static_cast<engine::Engine::AvatarLocomotionState>(v);
		}

		engine::core::LogLevel ParseLogLevelConfig(std::string_view text)
		{
			if (text == "Trace" || text == "trace") return engine::core::LogLevel::Trace;
			if (text == "Debug" || text == "debug") return engine::core::LogLevel::Debug;
			if (text == "Info" || text == "info") return engine::core::LogLevel::Info;
			if (text == "Warn" || text == "warn") return engine::core::LogLevel::Warn;
			if (text == "Error" || text == "error") return engine::core::LogLevel::Error;
			if (text == "Fatal" || text == "fatal") return engine::core::LogLevel::Fatal;
			if (text == "Off" || text == "off") return engine::core::LogLevel::Off;
			return engine::core::LogLevel::Info;
		}

		bool CameraViewportWorldDirection(const engine::render::Camera& camera, int viewportWidth, int viewportHeight,
			int mouseX, int mouseY, engine::math::Vec3& outDirection)
		{
			if (viewportWidth <= 0 || viewportHeight <= 0)
			{
				return false;
			}
			const float aspect = static_cast<float>(viewportWidth) / static_cast<float>(viewportHeight);
			const float halfTan = std::tan((camera.fovYDeg * kWorldEditorPickPi / 180.0f) * 0.5f);
			const float ndcX = ((static_cast<float>(mouseX) + 0.5f) / static_cast<float>(viewportWidth)) * 2.0f - 1.0f;
			const float ndcY = 1.0f - ((static_cast<float>(mouseY) + 0.5f) / static_cast<float>(viewportHeight)) * 2.0f;

			const float cy = std::cos(camera.yaw);
			const float sy = std::sin(camera.yaw);
			const float cp = std::cos(camera.pitch);
			const float sp = std::sin(camera.pitch);

			engine::math::Vec3 forward(-sy * cp, -sp, -cy * cp);
			// PR26.5 (M??.?) : alignement avec le fix Camera.cpp:22. La fonction
			// CameraViewportWorldDirection sert a calculer la direction du ray
			// pour le raycast camera->terrain (RaycastTerrainFromCamera). Doit
			// utiliser exactement les memes conventions que ComputeViewMatrix
			// pour que le raycast aligne avec ce que la camera voit a l'ecran.
			// Sans ce fix, le raycast utilisait un right_inverse alors que la
			// matrice view (post-PR26.5) a un right_standard, donc le pickX/Z
			// retourne par le raycast etait decale en X (souris a droite ->
			// pickX a gauche du sol). Possible cause partielle des items 6+7
			// (sculpt + splat ne fonctionnent pas) — a confirmer en PR27.
			engine::math::Vec3 right(-forward.z, 0.0f, forward.x);
			const float rightLen = right.Length();
			right = rightLen > 0.0f ? right * (1.0f / rightLen) : engine::math::Vec3(1.0f, 0.0f, 0.0f);

			engine::math::Vec3 up(
				right.y * forward.z - right.z * forward.y,
				right.z * forward.x - right.x * forward.z,
				right.x * forward.y - right.y * forward.x);
			const float upLen = up.Length();
			up = upLen > 0.0f ? up * (1.0f / upLen) : engine::math::Vec3(0.0f, 1.0f, 0.0f);

			outDirection = (forward + right * (ndcX * aspect * halfTan) + up * (ndcY * halfTan)).Normalized();
			return outDirection.LengthSq() > 1e-8f;
		}

		bool TryTerrainWorldY(const engine::render::terrain::HeightmapData& hm,
			float ox, float oz, float ws, float hScale, float wx, float wz, float& yOut)
		{
			if (hm.width == 0 || hm.height == 0 || ws <= 0.0f)
			{
				return false;
			}
			const float u = (wx - ox) / ws;
			const float v = (wz - oz) / ws;
			if (u < 0.0f || u > 1.0f || v < 0.0f || v > 1.0f)
			{
				return false;
			}
			yOut = hm.SampleBilinearNorm(u, v) * hScale;
			return true;
		}

		bool RaycastTerrainHeightmap(const engine::math::Vec3& O, const engine::math::Vec3& D,
			const engine::render::terrain::HeightmapData& hm,
			float ox, float oz, float ws, float hScale, float maxDistance,
			float& outHitX, float& outHitZ)
		{
			if (hm.width == 0 || maxDistance <= 0.0f)
			{
				return false;
			}
			constexpr int kSegments = 192;
			float prevT = 0.0f;
			float prevDiff = 0.0f;
			bool prevValid = false;
			{
				float h0 = 0.0f;
				const bool ok0 = TryTerrainWorldY(hm, ox, oz, ws, hScale, O.x, O.z, h0);
				if (ok0)
				{
					prevDiff = O.y - h0;
					prevValid = true;
				}
			}
			for (int i = 1; i <= kSegments; ++i)
			{
				const float t = maxDistance * (static_cast<float>(i) / static_cast<float>(kSegments));
				const float px = O.x + D.x * t;
				const float py = O.y + D.y * t;
				const float pz = O.z + D.z * t;
				float h = 0.0f;
				if (!TryTerrainWorldY(hm, ox, oz, ws, hScale, px, pz, h))
				{
					prevValid = false;
					continue;
				}
				const float diff = py - h;
				if (prevValid && prevDiff > 0.015f && diff <= 0.015f)
				{
					float t0 = prevT;
					float t1 = t;
					for (int b = 0; b < 14; ++b)
					{
						const float tm = 0.5f * (t0 + t1);
						const float mxp = O.x + D.x * tm;
						const float myp = O.y + D.y * tm;
						const float mzp = O.z + D.z * tm;
						float mh = 0.0f;
						if (!TryTerrainWorldY(hm, ox, oz, ws, hScale, mxp, mzp, mh))
						{
							t1 = tm;
							continue;
						}
						if (myp > mh)
						{
							t0 = tm;
						}
						else
						{
							t1 = tm;
						}
					}
					const float tf = 0.5f * (t0 + t1);
					outHitX = O.x + D.x * tf;
					outHitZ = O.z + D.z * tf;
					return true;
				}
				prevT = t;
				prevDiff = diff;
				prevValid = true;
			}
			return false;
		}

		bool RaycastTerrainFromCamera(const engine::render::Camera& camera, int vw, int vh, int mx, int my,
			const engine::render::terrain::HeightmapData& hm,
			float ox, float oz, float ws, float hScale,
			float& outX, float& outZ)
		{
			engine::math::Vec3 dir{};
			if (!CameraViewportWorldDirection(camera, vw, vh, mx, my, dir))
			{
				return false;
			}
			const float maxDist = std::max(ws * 4.0f, 8192.0f);
			return RaycastTerrainHeightmap(camera.position, dir, hm, ox, oz, ws, hScale, maxDist, outX, outZ);
		}

		// Détecte la disposition clavier par défaut au 1er lancement selon l'OS :
		// clavier français (AZERTY) -> "zqsd", sinon "wasd". N'est appliquée que si
		// ni config.json ni user_settings.json ne fixent controls.movement_layout ;
		// le choix du joueur (persisté dans user_settings.json) prime ensuite.
		std::string DetectDefaultMovementLayout()
		{
#if defined(_WIN32)
			const HKL  hkl    = ::GetKeyboardLayout(0);
			const WORD langId = LOWORD(reinterpret_cast<UINT_PTR>(hkl));
			if (PRIMARYLANGID(langId) == LANG_FRENCH)
				return "zqsd";
#endif
			return "wasd";
		}

		void ApplyUserSettingsOverrides(engine::core::Config& cfg)
		{
			// Binds clavier persistants (controls.keybind.*) : fichier dedie
			// keybinds.json, ecrit par le panneau Options au rebind. Merge par-dessus
			// les defauts de config.json. Absent/malforme -> ignore (on garde les defauts) ;
			// fichier dedie => un echec ne corrompt jamais user_settings.json.
			if (cfg.LoadFromFile("keybinds.json"))
				LOG_INFO(Core, "[Boot] keybinds.json applique (binds clavier persistants)");

			// ui_theme.json : préférence de thème UI (fichier dédié écrit par le
			// panneau Options, comme keybinds.json). Merge dans cfg puis applique.
			if (cfg.LoadFromFile("ui_theme.json"))
				LOG_INFO(Core, "[Boot] ui_theme.json applique (theme UI persistant)");
			// Applique le thème lu ; défaut or_royal si absent ou nom invalide
			// (SetActive renvoie false et conserve or_royal dans ce cas).
			const std::string uiTheme = cfg.GetString("ui.theme", "or_royal");
			if (!LnTheme::SetActive(uiTheme))
				LOG_WARN(Core, "[Boot] theme '{}' inconnu -> or_royal", uiTheme);

			// Apparence persistante (client.character_creation.gender) : fichier
			// dedie character_appearance.json, ecrit par le selecteur de genre de
			// l'ecran de creation. Merge par-dessus config.json. Meme logique que
			// keybinds.json (un echec ne corrompt jamais user_settings.json).
			if (cfg.LoadFromFile("character_appearance.json"))
				LOG_INFO(Core, "[Boot] character_appearance.json applique (genre avatar persistant)");

			// Zoom radar minimap (client.quest.minimap.zoom_level) : fichier dedie
			// minimap_settings.json, ecrit au changement de zoom (molette/clic sur le
			// radar). Meme logique que keybinds.json (un echec ne corrompt jamais
			// user_settings.json).
			if (cfg.LoadFromFile("minimap_settings.json"))
				LOG_INFO(Core, "[Boot] minimap_settings.json applique (zoom radar persistant)");

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
			if (persisted.Has("render.resolution_width"))
				cfg.SetValue("render.resolution_width", persisted.GetInt("render.resolution_width", cfg.GetInt("render.resolution_width", 1920)));
			if (persisted.Has("render.resolution_height"))
				cfg.SetValue("render.resolution_height", persisted.GetInt("render.resolution_height", cfg.GetInt("render.resolution_height", 1080)));
			if (persisted.Has("render.quality_preset"))
				cfg.SetValue("render.quality_preset", persisted.GetInt("render.quality_preset", cfg.GetInt("render.quality_preset", 2)));
			if (persisted.Has("render.fov"))
				cfg.SetValue("render.fov", persisted.GetDouble("render.fov", cfg.GetDouble("render.fov", 70.0)));
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

	// M38.1 — Initialise day/night cycle with parameters from config.json.
	{
		engine::render::DayNightCycle::Params dnParams{};
		dnParams.initialTimeOfDay = static_cast<float>(
			m_cfg.GetDouble("world.day_night.initial_time",   8.0));
		dnParams.timeScale        = static_cast<float>(
			m_cfg.GetDouble("world.day_night.time_scale",    60.0));
		m_dayNight.Init(dnParams);

		// WorldClock sync (Task 6.2) — intervalle de re-synchro de l'horloge
		// serveur (controle de derive). Defaut 300 s (5 min). La valeur init
		// reste le fallback local jusqu'a la 1ere reception WorldClock (204).
		m_worldClockDriftCheckSec = static_cast<float>(
			m_cfg.GetInt("game.worldclock.drift_check_sec", 300));
	}

	// M38.2 — Initialise weather system with parameters from config.json.
	{
		engine::render::WeatherConfig wCfg{};
		wCfg.transitionDuration = static_cast<float>(m_cfg.GetDouble("world.weather.transition_duration", 30.0));
		wCfg.rainSpawnRate      = static_cast<float>(m_cfg.GetDouble("world.weather.rain_spawn_rate",    1000.0));
		wCfg.snowSpawnRate      = static_cast<float>(m_cfg.GetDouble("world.weather.snow_spawn_rate",     500.0));
		wCfg.fogDensityMax      = static_cast<float>(m_cfg.GetDouble("world.weather.fog_density_max",      0.05));
		m_weatherSystem.Init(wCfg);
	}

	// M38.3 — Initialise dynamic point-light system (streetlamps, torches, windows).
	// The definitions JSON path defaults to "lights/dynamic_lights.json" relative to
	// paths.content, overridable via "world.dynamic_lights_path" in config.json.
	m_dynamicLights.Init(m_cfg);

	// Anti-occlusion caméra — fondu tramé des props occultant la vue caméra→joueur.
	// Réglable sans recompiler via les clés client.camera.occlusion_fade.* du config.
	{
		engine::render::CameraOcclusionFade::Config occCfg{};
		occCfg.fadeMin             = static_cast<float>(m_cfg.GetDouble("client.camera.occlusion_fade.fade_min", 0.15));
		occCfg.radiusMargin        = static_cast<float>(m_cfg.GetDouble("client.camera.occlusion_fade.radius_margin", 0.5));
		occCfg.fadeInPerSec        = static_cast<float>(m_cfg.GetDouble("client.camera.occlusion_fade.fade_in_per_sec", 6.0));
		occCfg.fadeOutPerSec       = static_cast<float>(m_cfg.GetDouble("client.camera.occlusion_fade.fade_out_per_sec", 8.0));
		occCfg.playerProtectRadius = static_cast<float>(m_cfg.GetDouble("client.camera.occlusion_fade.player_protect_radius", 0.6));
		m_cameraOcclusionFade.Init(occCfg);
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
		const bool worldEditorExeEarly = HasCliFlag(argc, argv, "--world-editor");
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
			const std::string relPath = engine::core::Log::MakeTimestampedFilename(
				worldEditorExeEarly ? "lcdlln_world_editor.exe" : "lcdlln.exe");
			std::error_code ec;
			const auto absPath = std::filesystem::absolute(relPath, ec);
			logSettings.filePath = ec ? relPath : absPath.string();
			std::fprintf(stderr, "[Log] Log file: %s\n", logSettings.filePath.c_str());
			std::fflush(stderr);
		}
		logSettings.console     = logToConsole;
		logSettings.flushAlways = true;
		logSettings.level       = ParseLogLevelConfig(m_cfg.GetString("log.level", "Info"));
		logSettings.rotation_size_mb = static_cast<size_t>(std::max(static_cast<int64_t>(0), m_cfg.GetInt("log.rotation_size_mb", 10)));
		logSettings.retention_days   = static_cast<int>(m_cfg.GetInt("log.retention_days", 7));
		// Les fichiers de log par sous-systeme (Smtp, gm_log, char_log...) sont une affaire
		// SERVEUR (master/shard). Le client n'ecrit jamais dans ces sous-systemes ; on ne les
		// route donc PAS cote client. Sinon Log::Init materialise des fichiers vides (ex.
		// "lcdlln-SMTP.log") des le boot du jeu, uniquement parce que la cle existe dans le
		// config.json partage entre client et serveur. subsystemFiles reste vide cote client
		// (intentionnel) ; le master continue de lire log.subsystem_files via sa propre config.
		// M44.4 — Format JSONL pour ingestion Loki/ELK (cf. tickets/issues/M44.4_*_Issue.md).
		logSettings.jsonOutput       = m_cfg.GetBool("log.json", false);

		engine::core::Log::Init(logSettings);

		if (!logSettings.filePath.empty() || logToConsole)
		{
			LOG_INFO(Core, "[Boot] Log initialized (console={}, file={}, level={} — use log.level=Debug for verbose render/auth traces)",
				logToConsole ? "on" : "off",
				logSettings.filePath.empty() ? "<none>" : logSettings.filePath,
				m_cfg.GetString("log.level", "Info"));
		}

		// Contenu de la zone active (scenery, interactables, réglages de zone) : fusionné
		// par-dessus config.json. Les clés gardent leur préfixe world.* → call-sites inchangés.
		// Couvre aussi l'éditeur (même constructeur Engine via --world-editor).
		if (!engine::core::Config::LoadActiveZone(m_cfg, m_cfg.GetString("paths.content", "game/data")))
		{
			LOG_WARN(Core, "[Engine] Zone active introuvable (world.active_zone='{}') : monde par defaut",
				m_cfg.GetString("world.active_zone", ""));
		}

		// ------------------------------------------------------------------
		// Config + subsystems
		// ------------------------------------------------------------------
		ApplyUserSettingsOverrides(m_cfg);
		// 1er lancement : si aucune source (config.json / user_settings.json) ne
		// fixe la disposition clavier, défaut basé sur le clavier OS (AZERTY -> zqsd).
		if (!m_cfg.Has("controls.movement_layout"))
		{
			const std::string osLayout = DetectDefaultMovementLayout();
			m_cfg.SetValue("controls.movement_layout", osLayout);
			LOG_INFO(Core, "[Boot] controls.movement_layout défaut OS (clavier) = {}", osLayout);
		}
		m_vsync   = m_cfg.GetBool("render.vsync", true);
		m_fixedDt = m_cfg.GetDouble("time.fixed_dt", 0.0);
		m_worldEditorExe = HasCliFlag(argc, argv, "--world-editor");
		m_editorEnabled = m_worldEditorExe || HasCliFlag(argc, argv, "--editor")
			|| m_cfg.GetBool("editor.enabled", false);

		// M100.1 — Branche éditeur monde "couche au-dessus" (distincte de
		// --world-editor qui active le shell M43.x). Activée par le flag CLI
		// --editor-world ou par editor.world.enabled = true dans config.json.
		// Les deux shells peuvent cohabiter pendant la transition.
		//
		// **Auto-activation pour `lcdlln_world_editor.exe`** : depuis M100.46
		// incrément 3 (dialog Zone Presets), l'éditeur monde a besoin du
		// `WorldEditorShell` pour résoudre les 4 documents + 4 catalogs. Sans
		// le Shell, l'entrée menu « Appliquer un preset de zone… » est grisée
		// (m_shell nul). Le binaire `--world-editor` implique donc
		// `--editor-world` par défaut. La warning « deux shells coexistent »
		// reste loguée plus bas en info — c'est attendu.
		const bool worldEditorWorldFlag =
			HasCliFlag(argc, argv, "--editor-world")
			|| m_cfg.GetBool("editor.world.enabled", false)
			|| m_worldEditorExe;

		if (m_worldEditorExe)
		{
			m_worldEditorSession = std::make_unique<engine::editor::WorldEditorSession>();
#if defined(_WIN32)
			m_worldEditorSession->SetTerrainSaveHook(
				[this](const engine::core::Config& cfg, const engine::editor::WorldMapEditDocument& doc) -> bool {
					if (!m_worldEditorTerrainTools.IsValid())
					{
						return true;
					}
					if (!doc.heightmapContentRelativePath.empty())
					{
						if (!m_worldEditorTerrainTools.SaveHeightmap(cfg, doc.heightmapContentRelativePath))
						{
							return false;
						}
					}
					if (!doc.splatmapContentRelativePath.empty())
					{
						if (!m_worldEditorTerrainTools.SaveSplatMap(cfg, doc.splatmapContentRelativePath))
						{
							return false;
						}
					}
					if (!doc.grassMaskContentRelativePath.empty())
					{
						if (!m_worldEditorTerrainTools.SaveGrassMask(cfg, doc.grassMaskContentRelativePath))
						{
							return false;
						}
					}
					return true;
				});
#else
			// `TerrainEditingTools` / flush disque ne sont branchés que sous Windows pour le WE actuel.
			m_worldEditorSession->SetTerrainSaveHook(
				[](const engine::core::Config&, const engine::editor::WorldMapEditDocument&) -> bool { return true; });
#endif
		}

		if (!logSettings.filePath.empty() || logSettings.console)
		{
			LOG_INFO(Core, "[Boot] Config loaded (vsync={}, fixed_dt={})", m_vsync ? "on" : "off", m_fixedDt);
		}

		if (m_editorEnabled)
		{
			m_editorMode = std::make_unique<engine::editor::EditorMode>();
			if (!m_editorMode->Init(m_cfg, m_worldEditorExe))
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
				if (m_worldEditorExe)
				{
					LOG_INFO(Core,
						"[Boot] World Editor exe — mode éditeur 3D (Vulkan), pas d’auth client ; flag interne --world-editor");
				}
				else
				{
					LOG_INFO(Core, "[Boot] Editor mode enabled (--editor ou editor.enabled=true)");
				}
			}
		}

		// M100.1 — Coquille du nouvel éditeur monde "couche au-dessus".
		// Indépendante de m_editorMode (qui sert le shell M43.x). Si
		// `--editor-world` ou `editor.world.enabled = true`, on instancie le
		// shell ; il vit en parallèle de WorldEditorImGui. Si les deux flags
		// sont actifs ensemble, c'est le cas non-supporté (logué en warning
		// ci-dessous). Le SetWorldEditorWorld sur m_editorMode sert d'accès
		// public si un sous-système a besoin de le savoir.
		if (worldEditorWorldFlag)
		{
			// `--world-editor` implique désormais `--editor-world` (M100.46
			// incrément 3 — la dialog Zone Presets exige le Shell). On
			// log seulement si l'utilisateur a explicitement combiné les
			// deux flags ou réglé `editor.world.enabled = true` ET lancé
			// l'exe éditeur — pour signaler la coexistence des deux shells
			// (M43.x toolbar + M100.1 WorldEditorShell).
			const bool worldEditorWorldExplicit =
				HasCliFlag(argc, argv, "--editor-world")
				|| m_cfg.GetBool("editor.world.enabled", false);
			if (m_worldEditorExe && worldEditorWorldExplicit)
			{
				LOG_INFO(Core,
					"[Boot] --world-editor + --editor-world (ou editor.world.enabled) — les deux shells coexistent");
			}
			if (m_editorMode)
			{
				m_editorMode->SetWorldEditorWorld(true);
			}
			m_worldEditorShell = std::make_unique<engine::editor::world::WorldEditorShell>();
			if (!m_worldEditorShell->Init(m_cfg))
			{
				LOG_ERROR(EditorWorld, "[Boot] WorldEditorShell::Init a échoué — shell désactivé");
				m_worldEditorShell.reset();
			}
			else
			{
				LOG_INFO(EditorWorld, "[Boot] WorldEditorShell M100.1 instancié (--editor-world)");
				// M100.46/47 — Quand le binaire éditeur monde gère lui-même la
				// barre de menu (français, plus complète), on supprime celle
				// de M100.1 pour éviter la duplication File/Fichier visible.
				if (m_worldEditorExe)
				{
					m_worldEditorShell->SetMenuBarSuppressed(true);
				}
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
		desc.title  = m_worldEditorExe ? "LCDLLN World Editor" : "LCDLLN Engine";
		// Editeur monde : 1920x1080 par defaut (panneaux dockes a droite + viewport
		// central genereux). Client de jeu : 1280x720 (pris en charge par fullscreen
		// reglable dans les Options).
		desc.width  = m_worldEditorExe ? 1920 : 1280;
		desc.height = m_worldEditorExe ? 1080 : 720;

		if (!m_window.Create(desc))
		{
			LOG_FATAL(Platform, "[Boot] Window::Create failed");
		}
		LOG_INFO(Core, "[Boot] Window::Create OK");
		if (!m_worldEditorExe && m_cfg.GetBool("render.fullscreen", true))
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

		// SP2 Task 5 — Init du presenter Quete (systeme B, shard). Charge aussi
		// les textes de quete (titre/description/etapes, resolus par locale) et
		// la table PNJ->quetes, tous deux consommes par le journal/panneau
		// donneur (QuestImGuiRenderer).
		if (!m_questUi.Init(m_cfg))
		{
			LOG_WARN(Core, "[Boot] QuestUiPresenter init FAILED — journal/tracker de quete desactives");
		}
		else if (!m_questUi.SetViewportSize(static_cast<uint32_t>(std::max(1, m_width)), static_cast<uint32_t>(std::max(1, m_height))))
		{
			LOG_WARN(Core, "[Boot] QuestUiPresenter viewport FAILED — using fallback layout");
		}
		{
			const std::string questLocale = m_cfg.GetString("client.locale", "fr");
			if (!m_questTextCatalog.Load(m_cfg, questLocale))
			{
				LOG_WARN(Core, "[Boot] QuestTextCatalog load FAILED (locale={}) — titres/descriptions retombent sur les ids", questLocale);
			}
			if (!m_questGiverTable.Load(m_cfg))
			{
				LOG_WARN(Core, "[Boot] QuestGiverTable load FAILED — table PNJ->quetes vide");
			}
			// SP3 Task 3 — Table des positions minimap (targetId -> [x,z]),
			// injectee dans le presenter pour que RebuildMinimap puisse resoudre
			// les marqueurs de POI des etapes de quete actives.
			if (!m_questPoiTable.Load(m_cfg))
			{
				LOG_WARN(Core, "[Boot] QuestPoiTable load FAILED — pas de marqueurs de POI sur la minimap");
			}
			m_questUi.SetPoiTable(&m_questPoiTable);
			// Zoom radar : cran persistant (0..4 -> 200..1000 m) ; défaut 600 m.
			// Remplace l'ancien rayon fixe client.quest.minimap.radius_m.
			m_minimapZoomIndex = engine::client::ClampZoomIndex(
				static_cast<int>(m_cfg.GetInt("client.quest.minimap.zoom_level",
					engine::client::kMinimapZoomDefaultIndex)));
			m_questUi.SetMinimapRadius(engine::client::RadiusForZoomIndex(m_minimapZoomIndex));
		}

		// CMANGOS.18 (Phase 3.18 step 4) — Init du presenter Mail. Doit etre
		// fait avant l'installation du push handler ci-dessous (qui dispatche
		// les opcodes Mail vers ce presenter).
		if (!m_mailUi.Init())
		{
			LOG_WARN(Core, "[Boot] MailUiPresenter init FAILED — boite mail desactivee");
		}
		else
		{
			m_mailUi.SetSendCallback([this](uint16_t opcode, const std::vector<uint8_t>& payload) -> bool {
				return m_authUi.SendGenericRequestAsync(opcode, payload);
			});
		}

		// CMANGOS.25 (Phase 3.25 step 3+4) — Init du presenter IgnoreList +
		// cable du send callback. La reception est dispatchee dans le
		// SetMasterPushHandler ci-dessous (opcodes 69/71/73). Le presenter
		// maintient une cache locale m_ignoredAccountIds.
		if (!m_ignoreListUi.Init())
		{
			LOG_WARN(Core, "[Boot] IgnoreListUiPresenter init FAILED — feature ignore desactivee");
		}
		else
		{
			m_ignoreListUi.SetSendCallback([this](uint16_t opcode, const std::vector<uint8_t>& payload) -> bool {
				return m_authUi.SendGenericRequestAsync(opcode, payload);
			});
		}

		// CMANGOS.32 (Phase 5.32 step 3+4) — Init du presenter GmTickets +
		// cable du send callback. Reception dispatchee dans le push handler
		// ci-dessous (opcodes 77/79/81/82). Fire-and-forget des requetes
		// 76/78/80 via SendGenericRequestAsync.
		if (!m_gmTicketUi.Init())
		{
			LOG_WARN(Core, "[Boot] GmTicketUiPresenter init FAILED — support GM desactive");
		}
		else
		{
			m_gmTicketUi.SetSendCallback([this](uint16_t opcode, const std::vector<uint8_t>& payload) -> bool {
				return m_authUi.SendGenericRequestAsync(opcode, payload);
			});
		}

		// CMANGOS.24 (Phase 3.24 step 3+4) — Init du presenter Reputation +
		// cable du send callback. Reception dispatchee dans le push handler
		// ci-dessous (opcodes 96/97). Fire-and-forget de la requete 95 via
		// SendGenericRequestAsync.
		if (!m_reputationUi.Init())
		{
			LOG_WARN(Core, "[Boot] ReputationUiPresenter init FAILED — panneau reputation desactive");
		}
		else
		{
			m_reputationUi.SetSendCallback([this](uint16_t opcode, const std::vector<uint8_t>& payload) -> bool {
				return m_authUi.SendGenericRequestAsync(opcode, payload);
			});
		}

		// SP2 anniversaires (2026-07-18) — send callback du presenter Exploits
		// (requête 206 fire-and-forget ; réponse 207 dispatchée plus bas).
		m_exploitsUi.SetSendCallback([this](uint16_t opcode, const std::vector<uint8_t>& payload) -> bool {
			return m_authUi.SendGenericRequestAsync(opcode, payload);
		});

		// CMANGOS.33 (Phase 5.33 step 3+4) — Init du presenter LFG + cable du
		// send callback pour les requetes 100/102/104/107. Reception dispatchee
		// dans le push handler ci-dessous (responses 101/103/105 + push 106).
		if (!m_lfgUi.Init())
		{
			LOG_WARN(Core, "[Boot] LfgUiPresenter init FAILED — panneau LFG desactive");
		}
		else
		{
			m_lfgUi.SetSendCallback([this](uint16_t opcode, const std::vector<uint8_t>& payload) -> bool {
				return m_authUi.SendGenericRequestAsync(opcode, payload);
			});
		}

		// CMANGOS.30 (Phase 5.30 step 3+4) — Init du presenter cinematique.
		// Le presenter envoie 109 (Ack) et 111 (SkipRequest) ; il recoit le
		// push 108 + responses 110/112 dans le push handler ci-dessous. Un
		// Tick(nowMs) est appele chaque frame depuis BeginFrame quand une
		// cinematique est en cours.
		if (!m_cinematicUi.Init())
		{
			LOG_WARN(Core, "[Boot] CinematicUiPresenter init FAILED — cinematiques desactivees");
		}
		else
		{
			m_cinematicUi.SetSendCallback([this](uint16_t opcode, const std::vector<uint8_t>& payload) -> bool {
				return m_authUi.SendGenericRequestAsync(opcode, payload);
			});
		}

		// CMANGOS.39 (Phase 4.39 step 3+4) — Init du presenter Skill Book +
		// cable du send callback. Reception dispatchee dans le push handler
		// ci-dessous (opcodes 114/116/118 responses + 119 push). Fire-and-forget
		// des requetes 113/115/117 via SendGenericRequestAsync.
		if (!m_skillBookUi.Init())
		{
			LOG_WARN(Core, "[Boot] SkillBookUiPresenter init FAILED — panneau skill book desactive");
		}
		else
		{
			m_skillBookUi.SetSendCallback([this](uint16_t opcode, const std::vector<uint8_t>& payload) -> bool {
				return m_authUi.SendGenericRequestAsync(opcode, payload);
			});
		}

		// Grimoire (Task 13) — Init du presenter Grimoire + câblage du send callback
		// vers GameplayUdpClient::SendSetActionBarLayout (kind 88). Le serveur renvoie
		// un ActionBarLayoutUpdate (kind 89) autoritaire capturé par UIModel.
		if (!m_grimoireUi.Init(&m_spellCatalog))
		{
			LOG_WARN(Core, "[Boot] GrimoireUiPresenter init FAILED — panneau Grimoire desactive");
		}
		else
		{
			m_grimoireUi.SetSendCallback([this](const std::array<std::string, 10>& slots) -> bool {
				const uint32_t clientId = m_gameplayUdp.ServerClientId();
				if (clientId == 0u)
				{
					return false;
				}
				return m_gameplayUdp.SendSetActionBarLayout(clientId, slots);
			});
		}

		// SP-D — Init du presenter arbre de compétences par-classe + câblage
		// du callback SendChooseClassSkill (GameplayUdpClient). Non bloquant.
		if (!m_classSkillTreeUi.Init(&m_classSkillCatalog))
		{
			LOG_WARN(Core, "[Boot] ClassSkillTreeUiPresenter init FAILED — arbre de compétences désactivé");
		}
		else
		{
			m_classSkillTreeUi.SetChooseCallback([this](uint32_t level, const std::string& skillId) -> bool {
				const uint32_t cid = m_gameplayUdp.ServerClientId();
				if (cid == 0u)
				{
					return false;
				}
				return m_gameplayUdp.SendChooseClassSkill(cid, level, skillId);
			});
		}

		// CMANGOS.21 (Phase 5.21 step 3+4) — Init du presenter Arena + cable
		// du send callback pour les requetes 120/122/124/127. Reception
		// dispatchee dans le push handler ci-dessous (responses 121/123/125/128
		// + push 126/129).
		if (!m_arenaUi.Init())
		{
			LOG_WARN(Core, "[Boot] ArenaUiPresenter init FAILED — panneau arena desactive");
		}
		else
		{
			m_arenaUi.SetSendCallback([this](uint16_t opcode, const std::vector<uint8_t>& payload) -> bool {
				return m_authUi.SendGenericRequestAsync(opcode, payload);
			});
		}

		// CMANGOS.10 (Phase 5 step 3+4) — Init du presenter BattleGround + cable
		// du send callback pour les requetes 130/132/134/139. Reception
		// dispatchee dans le push handler ci-dessous (responses 131/133/135
		// + push 136/137/138).
		if (!m_battleGroundUi.Init())
		{
			LOG_WARN(Core, "[Boot] BattleGroundUiPresenter init FAILED — panneau BG desactive");
		}
		else
		{
			m_battleGroundUi.SetSendCallback([this](uint16_t opcode, const std::vector<uint8_t>& payload) -> bool {
				return m_authUi.SendGenericRequestAsync(opcode, payload);
			});
		}

		// CMANGOS.36 (Phase 5.36 step 3+4) — Init du presenter OutdoorPvp + cable
		// du send callback pour les requetes 140/142/144/146. Reception
		// dispatchee dans le push handler ci-dessous (responses 141/143/145/147
		// + push 148/149).
		if (!m_outdoorPvpUi.Init())
		{
			LOG_WARN(Core, "[Boot] OutdoorPvpUiPresenter init FAILED — panneau OutdoorPvp desactive");
		}
		else
		{
			m_outdoorPvpUi.SetSendCallback([this](uint16_t opcode, const std::vector<uint8_t>& payload) -> bool {
				return m_authUi.SendGenericRequestAsync(opcode, payload);
			});
		}

		// CMANGOS.42 (Phase 4.42 step 3+4) — Init du presenter Weather + cable
		// du send callback pour les requetes 150/152/154. Reception
		// dispatchee dans le push handler ci-dessous (responses 151/153/155
		// + push 156).
		if (!m_weatherUi.Init())
		{
			LOG_WARN(Core, "[Boot] WeatherUiPresenter init FAILED — panneau Weather desactive");
		}
		else
		{
			m_weatherUi.SetSendCallback([this](uint16_t opcode, const std::vector<uint8_t>& payload) -> bool {
				return m_authUi.SendGenericRequestAsync(opcode, payload);
			});
		}

		// CMANGOS.31 (Phase 5.31 step 3+4) — Init du presenter GameEvents +
		// cable du send callback pour les requetes 157/159/161. Reception
		// dispatchee dans le push handler ci-dessous (responses 158/160/162
		// + push 163 StateChange).
		if (!m_gameEventUi.Init())
		{
			LOG_WARN(Core, "[Boot] GameEventUiPresenter init FAILED — panneau GameEvents desactive");
		}
		else
		{
			m_gameEventUi.SetSendCallback([this](uint16_t opcode, const std::vector<uint8_t>& payload) -> bool {
				return m_authUi.SendGenericRequestAsync(opcode, payload);
			});
		}

		// CMANGOS.21 (Phase 5.21 step 3+4 Guilds) — Init du presenter Guildes +
		// cable du send callback pour les requetes 164/166/168/170. Reception
		// dispatchee dans le push handler ci-dessous (responses 165/167/169/171
		// + push 172 MotdUpdate).
		if (!m_guildUi.Init())
		{
			LOG_WARN(Core, "[Boot] GuildUiPresenter init FAILED — panneau Guildes desactive");
		}
		else
		{
			m_guildUi.SetSendCallback([this](uint16_t opcode, const std::vector<uint8_t>& payload) -> bool {
				return m_authUi.SendGenericRequestAsync(opcode, payload);
			});
		}

		// CMANGOS.09 (Phase 5.09 step 3+4 AuctionHouse) — Init du presenter
		// Hotel des Ventes + cable du send callback pour les requetes
		// 173/175/177/179. Reception dispatchee dans le push handler ci-dessous
		// (responses 174/176/178/180 + push 181 AuctionExpired).
		if (!m_auctionHouseUi.Init())
		{
			LOG_WARN(Core, "[Boot] AuctionHousePresenter init FAILED — panneau Hotel des Ventes desactive");
		}
		else
		{
			m_auctionHouseUi.SetSendCallback([this](uint16_t opcode, const std::vector<uint8_t>& payload) -> bool {
				return m_authUi.SendGenericRequestAsync(opcode, payload);
			});
		}

		// CMANGOS.17 (Phase 3.17 step 3+4 Loot) — Init du presenter Loot Roll
		// + cable du send callback pour les requetes 183/186. Reception
		// dispatchee dans le push handler ci-dessous (responses 184/187 + push
		// 182 RollNotification + push 185 RollResultNotification).
		if (!m_lootRollUi.Init())
		{
			LOG_WARN(Core, "[Boot] LootRollUiPresenter init FAILED — fenetre Loot Roll desactivee");
		}
		else
		{
			m_lootRollUi.SetSendCallback([this](uint16_t opcode, const std::vector<uint8_t>& payload) -> bool {
				return m_authUi.SendGenericRequestAsync(opcode, payload);
			});
		}

		// CMANGOS.27 (Phase 4.27 step 3+4) — Init du presenter TradeWindow + cable
		// du send callback pour les requetes 83/86/88/91/93. Reception dispatchee
		// dans le push handler ci-dessous (responses 84/87/89/92 + push 85/90/94).
		// Le presenter Init() existant a la signature (config) ; on lui passe m_cfg.
		if (!m_tradeWindowUi.Init(m_cfg))
		{
			LOG_WARN(Core, "[Boot] TradeWindowUiPresenter init FAILED — trade desactive");
		}
		else
		{
			m_tradeWindowUi.SetSendCallback([this](uint16_t opcode, const std::vector<uint8_t>& payload) -> bool {
				return m_authUi.SendGenericRequestAsync(opcode, payload);
			});
		}

		// Chat MVP — câblage bidirectionnel ChatUi <-> AuthUi (master TCP).
		// Send : ChatUi::SubmitInputLine appelle AuthUi::SendChatAsync sur la connexion master vivante.
		// Receive : AuthUi::PumpPostAuthEvents dispatche les paquets push (CHAT_RELAY notamment)
		// vers un handler qui parse et appelle ChatUi::PushNetworkLine.
		m_chatUi.SetSendCallback([this](uint8_t channel, std::string_view targetToken, std::string_view text) -> bool {
			// Wave 3 RBAC migration : helper local pour envoyer un audit
			// AdminCommand au master pour les UI panel toggles. Fire-and-forget
			// (requestId=0, on n'attend pas l'ACK : le toggle visuel est
			// applique immediatement, le master log juste le geste).
			//
			// Le master valide l'auth + role (player suffit pour ces commandes)
			// et emet [AdminCommand] result=OK dans son log audit.
			auto sendAdminAudit = [this](std::string_view cmd) {
				engine::network::admin::AdminCommandRequest req;
				req.command = std::string(cmd);
				std::vector<uint8_t> payload;
				engine::network::admin::BuildAdminCommandRequestPayload(req, payload);
				(void)m_authUi.SendGenericRequestAsync(
					engine::network::kOpcodeAdminCommandRequest, payload);
			};

			// CMANGOS.18 (Phase 3.18 step 4) — Intercept /mail avant l'envoi
			// chat. ParseSlashPrefixes ne connait pas /mail, donc le texte
			// arrive sur le canal Say avec text == "/mail" (ou "/mail ...").
			// On consomme le slash command localement et retourne true (le
			// chat presenter pense que c'est envoye et clear l'input).
			if (channel == static_cast<uint8_t>(engine::net::ChatChannel::Say)
				&& (text == "/mail" || text.starts_with("/mail ") || text.starts_with("/mail\t")))
			{
				m_mailVisible = !m_mailVisible;
				if (m_mailVisible)
				{
					m_mailUi.RequestInbox();
				}
				LOG_INFO(Core, "[Engine] /mail toggle (visible={})", m_mailVisible);
				sendAdminAudit("/mail");
				return true;
			}
			// CHAR-MODEL — Emotes par slash command : posent un role d'emote consomme
			// par la state machine (avatar -> etat Emote, anim en boucle annulee au
			// moindre deplacement/saut). Locales (pas d'aller-retour serveur). Ajouter
			// une emote = une ligne dans kEmotes (+ addRole correspondant).
			{
				struct EmoteCmd { const char* cmd; const char* role; const char* feedback; };
				static const EmoteCmd kEmotes[] = {
					{ "/dance", "Dance", "Vous dansez." },
					{ "/sit",   "Sit",   "Vous vous asseyez." },
					{ "/assis", "Sit",   "Vous vous asseyez." },
					{ "/talk",  "Talk",  "Vous discutez." },
					{ "/torch", "Torch", "Vous brandissez une torche." },
					{ "/kneel", "Kneel", "Vous vous agenouillez." },
					{ "/sittalk", "SitTalk", "Vous discutez, assis." },
					{ "/push", "Push", "Vous poussez." },
				};
				if (channel == static_cast<uint8_t>(engine::net::ChatChannel::Say))
				{
					for (const auto& em : kEmotes)
					{
						const std::string c = em.cmd;
						if (text == c || text.starts_with(c + " ") || text.starts_with(c + "\t"))
						{
							m_pendingEmoteRole = em.role;
							LOG_INFO(Core, "[Engine] emote {} (role {}) requested", em.cmd, em.role);
							engine::net::ChatMessage emoteMsg;
							emoteMsg.timestampUnixMs = static_cast<uint64_t>(
								std::chrono::duration_cast<std::chrono::milliseconds>(
									std::chrono::system_clock::now().time_since_epoch()).count());
							emoteMsg.channel = engine::net::ChatChannel::Server;
							emoteMsg.sender  = "[Emote]";
							emoteMsg.text    = em.feedback;
							m_chatUi.PushNetworkLine(emoteMsg);
							return true;
						}
					}
				}
			}
			// CMANGOS.27 (Phase 4.27 step 3+4) — Slash command /trade <accountId>
			// pour initier un echange avec le joueur cible. V1 : resolution par
			// account_id direct (la resolution par character_name viendra avec
			// PartySystem display ulterieurement). "/trade cancel" annule la
			// trade en cours.
			if (channel == static_cast<uint8_t>(engine::net::ChatChannel::Say)
				&& (text == "/trade" || text.starts_with("/trade ") || text.starts_with("/trade\t")))
			{
				const auto spaceIdx = text.find_first_of(" \t");
				if (spaceIdx == std::string_view::npos)
				{
					LOG_INFO(Core, "[Engine] /trade : usage /trade <account_id> ou /trade cancel");
					return true;
				}
				std::string_view arg = text.substr(spaceIdx + 1);
				while (!arg.empty() && (arg.front() == ' ' || arg.front() == '\t'))
					arg.remove_prefix(1u);
				if (arg == "cancel")
				{
					m_tradeWindowUi.Cancel();
					LOG_INFO(Core, "[Engine] /trade cancel");
					return true;
				}
				uint64_t targetAccountId = 0u;
				bool parsed = false;
				try
				{
					size_t pos = 0;
					const std::string argStr(arg);
					targetAccountId = std::stoull(argStr, &pos, 10);
					parsed = (pos > 0u && targetAccountId != 0u);
				}
				catch (...)
				{
					parsed = false;
				}
				if (!parsed)
				{
					LOG_WARN(Core, "[Engine] /trade : argument '{}' n'est pas un account_id valide",
						std::string(arg));
					return true;
				}
				m_tradeWindowUi.RequestBeginTrade(targetAccountId);
				LOG_INFO(Core, "[Engine] /trade {}", targetAccountId);
				return true;
			}
			// CMANGOS.24 (Phase 3.24 step 3+4) — Slash command /rep et /reputation
			// pour ouvrir/fermer le panneau Reputation et synchroniser la liste
			// depuis le master au moment de l'ouverture.
			if (channel == static_cast<uint8_t>(engine::net::ChatChannel::Say)
				&& (text == "/rep" || text == "/reputation"
				    || text.starts_with("/rep ") || text.starts_with("/rep\t")
				    || text.starts_with("/reputation ") || text.starts_with("/reputation\t")))
			{
				m_reputationVisible = !m_reputationVisible;
				if (m_reputationVisible)
				{
					m_reputationUi.RequestReputationList();
				}
				LOG_INFO(Core, "[Engine] /rep toggle (visible={})", m_reputationVisible);
				sendAdminAudit("/rep");
				return true;
			}
			// CMANGOS.39 (Phase 4.39 step 3+4) — Slash command /skills pour
			// ouvrir/fermer le panneau Skill Book et synchroniser la liste
			// depuis le master au moment de l'ouverture. La touche B fait
			// la meme chose (cf. boucle input dans BeginFrame).
			if (channel == static_cast<uint8_t>(engine::net::ChatChannel::Say)
				&& (text == "/skills" || text == "/skill"
				    || text.starts_with("/skills ") || text.starts_with("/skills\t")
				    || text.starts_with("/skill ") || text.starts_with("/skill\t")))
			{
				// Chantier 1 — route vers la fenêtre Personnage unifiée (onglet Compétences).
				if (m_characterWindowImGui)
					m_characterWindowImGui->OpenAtTab(engine::render::CharacterWindowImGuiRenderer::Tab::Competences);
				m_skillBookUi.RequestList();
				LOG_INFO(Core, "[Engine] /skills -> fenetre Personnage (onglet Competences)");
				sendAdminAudit("/skills");
				return true;
			}
			// Grimoire (Task 13) — Slash commands /grimoire et /sorts pour
			// ouvrir/fermer le panneau Grimoire. Équivalent à la touche V remappable.
			if (channel == static_cast<uint8_t>(engine::net::ChatChannel::Say)
				&& (text == "/grimoire" || text == "/sorts"
				    || text.starts_with("/grimoire ") || text.starts_with("/grimoire\t")
				    || text.starts_with("/sorts ") || text.starts_with("/sorts\t")))
			{
				if (m_characterWindowImGui)
					m_characterWindowImGui->OpenAtTab(engine::render::CharacterWindowImGuiRenderer::Tab::Techniques);
				LOG_INFO(Core, "[Engine] /grimoire -> fenetre Personnage (onglet Techniques)");
				return true;
			}
			// SP-D — Slash commands /arbre et /competences pour l'arbre de compétences.
			if (channel == static_cast<uint8_t>(engine::net::ChatChannel::Say)
				&& (text == "/arbre" || text == "/competences"
				    || text.starts_with("/arbre ") || text.starts_with("/arbre	")
				    || text.starts_with("/competences ") || text.starts_with("/competences	")))
			{
				if (m_characterWindowImGui)
					m_characterWindowImGui->OpenAtTab(engine::render::CharacterWindowImGuiRenderer::Tab::Arbre);
				LOG_INFO(Core, "[Engine] /arbre -> fenetre Personnage (onglet Arbre)");
				return true;
			}
			// Chantier 2 SP1 — commande DEBUG /modular <a|b|off> : pose/echange/retire
			// une PARTIE placeholder (boite) sur la TETE de l'avatar. Preuve visuelle
			// du pipeline modulaire (avant les vrais assets). Gate derriere
			// client.debug.modular_test (defaut false) : outil de dev, pas une feature.
			if (channel == static_cast<uint8_t>(engine::net::ChatChannel::Say)
				&& (text == "/modular" || text.starts_with("/modular ")))
			{
				if (!m_cfg.GetBool("client.debug.modular_test", false))
				{
					LOG_INFO(Core, "[Engine] /modular ignore (client.debug.modular_test=false)");
					return true;
				}
				// Genere 2 variantes placeholder a la 1re utilisation (mesh + device valides).
				if (m_placeholderParts.empty() && m_currentSkinnedMesh != nullptr)
				{
					const engine::render::skinned::Skeleton& skel = m_currentSkinnedMesh->skeleton;
					int headBone = skel.FindBoneIndex("mixamorig:Head");
					if (headBone < 0) headBone = skel.FindBoneIndex("head");
					if (headBone < 0) headBone = skel.FindBoneIndex("Head");
					if (headBone < 0) headBone = static_cast<int>(skel.bones.size()) - 1;
					const float sizes[2] = { 0.16f, 0.24f };
					for (int i = 0; i < 2; ++i)
					{
						engine::render::skinned::SkinnedMeshCpuData cpu =
							engine::render::skinned::MakePlaceholderBoxPart(skel, headBone, sizes[i]);
						auto mesh = std::make_unique<engine::render::skinned::SkinnedMesh>();
						if (mesh->Upload(m_vkDeviceContext.GetDevice(),
							m_vkDeviceContext.GetPhysicalDevice(), cpu))
						{
							m_placeholderParts.push_back(std::move(mesh));
						}
					}
					LOG_INFO(Render, "[Modular] {} partie(s) placeholder generee(s) (os tete={})",
						m_placeholderParts.size(), headBone);
				}
				// `text` est un std::string_view -> substr renvoie un string_view (pas de
				// conversion implicite vers std::string). find() marche sur string_view.
				const std::string_view arg = (text.size() > 9u) ? text.substr(9) : std::string_view{};
				if (arg.find("off") != std::string_view::npos)
				{
					m_modularAvatar.ClearPart(engine::render::skinned::EquipVisualSlot::Head);
					LOG_INFO(Render, "[Modular] tete retiree");
				}
				else if ((arg.find('b') != std::string::npos || arg.find('B') != std::string::npos)
					&& m_placeholderParts.size() >= 2)
				{
					(void)m_modularAvatar.SetPart(
						engine::render::skinned::EquipVisualSlot::Head, m_placeholderParts[1].get());
					LOG_INFO(Render, "[Modular] tete = variante B");
				}
				else if (!m_placeholderParts.empty())
				{
					(void)m_modularAvatar.SetPart(
						engine::render::skinned::EquipVisualSlot::Head, m_placeholderParts[0].get());
					LOG_INFO(Render, "[Modular] tete = variante A");
				}
				return true;
			}
			// CMANGOS.33 (Phase 5.33 step 3+4) — Slash command /lfg pour
			// ouvrir/fermer la fenetre LFG et synchroniser le status depuis le
			// master au moment de l'ouverture.
			if (channel == static_cast<uint8_t>(engine::net::ChatChannel::Say)
				&& (text == "/lfg" || text.starts_with("/lfg ") || text.starts_with("/lfg\t")))
			{
				m_lfgVisible = !m_lfgVisible;
				if (m_lfgVisible)
				{
					m_lfgUi.RequestStatus();
				}
				LOG_INFO(Core, "[Engine] /lfg toggle (visible={})", m_lfgVisible);
				sendAdminAudit("/lfg");
				return true;
			}
			// CMANGOS.21 (Phase 5.21 step 3+4) — Slash command /arena pour
			// ouvrir/fermer la fenetre Arena et synchroniser la liste des
			// teams depuis le master au moment de l'ouverture. La touche A
			// fait la meme chose (cf. boucle input dans BeginFrame).
			if (channel == static_cast<uint8_t>(engine::net::ChatChannel::Say)
				&& (text == "/arena" || text.starts_with("/arena ") || text.starts_with("/arena\t")))
			{
				m_arenaVisible = !m_arenaVisible;
				if (m_arenaVisible)
				{
					m_arenaUi.RequestTeams();
				}
				LOG_INFO(Core, "[Engine] /arena toggle (visible={})", m_arenaVisible);
				sendAdminAudit("/arena");
				return true;
			}
			// CMANGOS.10 (Phase 5 step 3+4) — Slash command /bg pour ouvrir/
			// fermer la fenetre BattleGround et synchroniser la liste depuis
			// le master au moment de l'ouverture. La touche G fait la meme
			// chose (cf. boucle input dans BeginFrame).
			if (channel == static_cast<uint8_t>(engine::net::ChatChannel::Say)
				&& (text == "/bg" || text.starts_with("/bg ") || text.starts_with("/bg\t")))
			{
				m_battleGroundVisible = !m_battleGroundVisible;
				if (m_battleGroundVisible)
				{
					m_battleGroundUi.RequestList();
				}
				LOG_INFO(Core, "[Engine] /bg toggle (visible={})", m_battleGroundVisible);
				sendAdminAudit("/bg");
				return true;
			}
			// CMANGOS.36 (Phase 5.36 step 3+4) — Slash command /pvp pour
			// ouvrir/fermer la fenetre OutdoorPvp et synchroniser la liste
			// des zones depuis le master au moment de l'ouverture. La touche
			// P fait la meme chose (cf. boucle input dans BeginFrame).
			if (channel == static_cast<uint8_t>(engine::net::ChatChannel::Say)
				&& (text == "/pvp" || text.starts_with("/pvp ") || text.starts_with("/pvp\t")))
			{
				m_outdoorPvpVisible = !m_outdoorPvpVisible;
				if (m_outdoorPvpVisible)
				{
					m_outdoorPvpUi.RequestList();
				}
				LOG_INFO(Core, "[Engine] /pvp toggle (visible={})", m_outdoorPvpVisible);
				sendAdminAudit("/pvp");
				return true;
			}
			// CMANGOS.42 (Phase 4.42 step 3+4) — Slash command /weather pour
			// ouvrir/fermer le panneau Weather et synchroniser la liste des
			// zones meteo depuis le master au moment de l'ouverture. La
			// touche Y fait la meme chose (cf. boucle input dans BeginFrame).
			if (channel == static_cast<uint8_t>(engine::net::ChatChannel::Say)
				&& (text == "/weather" || text.starts_with("/weather ") || text.starts_with("/weather\t")))
			{
				m_weatherVisible = !m_weatherVisible;
				if (m_weatherVisible)
				{
					m_weatherUi.RequestList();
				}
				LOG_INFO(Core, "[Engine] /weather toggle (visible={})", m_weatherVisible);
				sendAdminAudit("/weather");
				return true;
			}
			// CMANGOS.31 (Phase 5.31 step 3+4) — Slash command /events pour
			// ouvrir/fermer le panneau GameEvents et synchroniser la liste
			// depuis le master au moment de l'ouverture. Le menu "Panneaux"
			// (barre de menus ImGui in-game) fait la meme chose (la touche E,
			// qui servait a cela, a ete liberee).
			if (channel == static_cast<uint8_t>(engine::net::ChatChannel::Say)
				&& (text == "/events" || text.starts_with("/events ") || text.starts_with("/events\t")))
			{
				m_gameEventVisible = !m_gameEventVisible;
				if (m_gameEventVisible)
				{
					m_gameEventUi.RequestList();
				}
				LOG_INFO(Core, "[Engine] /events toggle (visible={})", m_gameEventVisible);
				sendAdminAudit("/events");
				return true;
			}
			// CMANGOS.21 (Phase 5.21 step 3+4 Guilds) — Slash command /guild
			// pour ouvrir/fermer le panneau Guildes et synchroniser la liste
			// depuis le master au moment de l'ouverture. La touche U fait
			// la meme chose (cf. boucle input dans BeginFrame).
			if (channel == static_cast<uint8_t>(engine::net::ChatChannel::Say)
				&& (text == "/guild" || text == "/guilds"
				    || text.starts_with("/guild ") || text.starts_with("/guild\t")
				    || text.starts_with("/guilds ") || text.starts_with("/guilds\t")))
			{
				m_guildVisible = !m_guildVisible;
				if (m_guildVisible)
				{
					m_guildUi.RequestList();
				}
				LOG_INFO(Core, "[Engine] /guild toggle (visible={})", m_guildVisible);
				sendAdminAudit("/guild");
				return true;
			}
			// CMANGOS.09 (Phase 5.09 step 3+4 AuctionHouse) — Slash command /ah
			// pour ouvrir/fermer le panneau Hotel des Ventes et synchroniser
			// la liste des encheres depuis le master au moment de l'ouverture.
			// La touche H fait la meme chose (cf. boucle input dans BeginFrame).
			if (channel == static_cast<uint8_t>(engine::net::ChatChannel::Say)
				&& (text == "/ah" || text == "/auction"
				    || text.starts_with("/ah ") || text.starts_with("/ah\t")
				    || text.starts_with("/auction ") || text.starts_with("/auction\t")))
			{
				m_auctionHouseVisible = !m_auctionHouseVisible;
				if (m_auctionHouseVisible)
				{
					m_auctionHouseUi.RequestList(0u);
				}
				LOG_INFO(Core, "[Engine] /ah toggle (visible={})", m_auctionHouseVisible);
				sendAdminAudit("/ah");
				return true;
			}
			// CMANGOS.17 (Phase 3.17 step 3+4 Loot) — Slash command /loot
			// pour ouvrir/fermer la fenetre Loot Roll. La touche L fait la
			// meme chose (cf. boucle input dans BeginFrame).
			//
			// Wave 1 RBAC : /loot est admin-gated (le bouton Simulate Roll
			// bypass la logique loot serveur). On envoie une AdminCommand
			// au master pour audit + validation role administrator. Le
			// toggle UI s'applique localement independamment de la reponse
			// master (le panneau est purement visuel ; le bouton Simulate
			// reste lui-meme gate cote serveur via LootHandler).
			if (channel == static_cast<uint8_t>(engine::net::ChatChannel::Say)
				&& (text == "/loot"
				    || text.starts_with("/loot ") || text.starts_with("/loot\t")))
			{
				engine::network::admin::AdminCommandRequest req;
				req.command = "/loot";
				std::vector<uint8_t> payload;
				engine::network::admin::BuildAdminCommandRequestPayload(req, payload);
				(void)m_authUi.SendGenericRequestAsync(
					engine::network::kOpcodeAdminCommandRequest, payload);
				m_lootRollVisible = !m_lootRollVisible;
				LOG_INFO(Core, "[Engine] /loot toggle (visible={}) + admin audit envoye",
					m_lootRollVisible);
				return true;
			}
			// Phase 5 step 3+4 Lunar + M38.1 Sky : slash commands debug pour
			// inspecter et override le cycle jour/nuit + phase lunaire.
			// Wave 1 RBAC : toutes les sous-commandes /sky passent par
			// AdminCommand pour audit et validation role serveur (minRole
			// player pour /sky info, administrator pour /sky time et
			// /sky moon — cf. game/data/config/slash_commands.json).
			//   /sky info        : audit + applique l'affichage local sur ACK Ok.
			//   /sky time <h>    : audit + applique SetTime(h) sur ACK Ok.
			//   /sky moon <i>    : audit + OnLunarPhaseChange(i, calc(i)) sur ACK Ok.
			//
			// Chat echo immediat : chaque commande pousse une ligne sur le canal
			// Server (sender="[Sky]") via m_chatUi.PushNetworkLine pour feedback
			// utilisateur. La suite (Ok/Denied/etc.) est rendue par le case
			// kOpcodeAdminCommandResponse plus bas.
			auto pushSkyChatLine = [this](const char* fmt, auto... args) {
				char buf[256];
				std::snprintf(buf, sizeof(buf), fmt, args...);
				engine::net::ChatMessage msg;
				msg.timestampUnixMs = static_cast<uint64_t>(
					std::chrono::duration_cast<std::chrono::milliseconds>(
						std::chrono::system_clock::now().time_since_epoch()).count());
				msg.channel = engine::net::ChatChannel::Server;
				msg.sender  = "[Sky]";
				msg.text    = buf;
				m_chatUi.PushNetworkLine(msg);
			};

			if (channel == static_cast<uint8_t>(engine::net::ChatChannel::Say)
				&& (text == "/sky info" || text == "/sky"))
			{
				engine::network::admin::AdminCommandRequest req;
				req.command = "/sky info";
				std::vector<uint8_t> payload;
				engine::network::admin::BuildAdminCommandRequestPayload(req, payload);
				(void)m_authUi.SendGenericRequestAsync(
					engine::network::kOpcodeAdminCommandRequest, payload);
				LOG_INFO(Render, "[Sky] /sky info envoye au master (audit + RBAC)");
				pushSkyChatLine("/sky info envoye au master (audit + RBAC)");
				return true;
			}
			if (channel == static_cast<uint8_t>(engine::net::ChatChannel::Say)
				&& text.starts_with("/sky time "))
			{
				const auto rest = text.substr(10);
				// Validation cliente legere : on echoue tot si la valeur n'est
				// pas un float dans [0..24). La validation autoritative reste
				// cote master (DispatchSkyTime).
				float hours = -1.0f;
				try { hours = std::stof(std::string(rest)); } catch (...) { hours = -1.0f; }
				if (!(hours >= 0.0f) || hours >= 24.0f)
				{
					LOG_WARN(Render, "[Sky] /sky time : '{}' hors plage [0..24)",
						std::string(rest));
					pushSkyChatLine("/sky time '%s' hors plage [0..24)", std::string(rest).c_str());
					return true;
				}
				engine::network::admin::AdminCommandRequest req;
				req.command = "/sky time";
				req.args.push_back(std::string(rest));
				std::vector<uint8_t> payload;
				engine::network::admin::BuildAdminCommandRequestPayload(req, payload);
				(void)m_authUi.SendGenericRequestAsync(
					engine::network::kOpcodeAdminCommandRequest, payload);
				LOG_INFO(Render, "[Sky] /sky time {} envoye au master pour validation RBAC",
					std::string(rest));
				pushSkyChatLine("/sky time %.2fh envoye au master (validation RBAC en cours...)",
					static_cast<double>(hours));
				return true;
			}
			if (channel == static_cast<uint8_t>(engine::net::ChatChannel::Say)
				&& text.starts_with("/sky moon "))
			{
				const auto rest = text.substr(10);
				int phase = 0;
				try { phase = std::stoi(std::string(rest)); } catch (...) { phase = 0; }
				if (phase < 0 || phase > 15)
				{
					LOG_WARN(Render, "[Sky] phase {} hors plage [0..15]", phase);
					pushSkyChatLine("phase %d hors plage [0..15]", phase);
					return true;
				}
				// AdminCommand RBAC pilot : /sky moon est admin-only. On envoie au
				// master qui valide le role + log audit + retourne Ok/Denied.
				// Le client applique l'override visuel SEULEMENT apres ACK Ok
				// (voir dispatch kOpcodeAdminCommandResponse plus bas).
				// Chat echo immediat pour feedback : la suite (Ok/Denied) sera
				// rendue par le case kOpcodeAdminCommandResponse.
				engine::network::admin::AdminCommandRequest req;
				req.command = "/sky moon";
				req.args.push_back(std::to_string(phase));
				std::vector<uint8_t> payload;
				engine::network::admin::BuildAdminCommandRequestPayload(req, payload);
				(void)m_authUi.SendGenericRequestAsync(
					engine::network::kOpcodeAdminCommandRequest, payload);
				LOG_INFO(Render, "[Sky] /sky moon {} sent to master for RBAC validation", phase);
				pushSkyChatLine("/sky moon %d envoye au master (validation RBAC en cours...)", phase);
				return true;
			}
			// WorldClock client sync : commandes admin horloge monde. Calquees
			// EXACTEMENT sur /sky time et /sky moon : on intercepte le prefixe
			// tape, on extrait l'argument brut (texte apres la commande, trimme
			// des espaces de bord) et on envoie un AdminCommandRequest au master
			// via kOpcodeAdminCommandRequest (opcode 195). AUCUNE validation
			// metier cliente : le master valide RBAC + arguments, applique et
			// broadcast (205) ; le client verra l'effet via la sync horloge.
			// Le nom de commande envoye est la forme CANONIQUE (sans placeholder)
			// indexee par le registre serveur. La reponse 196 est traitee
			// generiquement par le case kOpcodeAdminCommandResponse (branche
			// "else" -> ligne chat OK, ou branche refus -> ligne chat DENIED).
			//
			// Petit helper local de trim (espaces de bord) pour extraire
			// l'argument tape proprement, sans dependre d'un util externe.
			auto trimEdges = [](std::string_view sv) -> std::string {
				const auto first = sv.find_first_not_of(" \t");
				if (first == std::string_view::npos)
					return std::string();
				const auto last = sv.find_last_not_of(" \t");
				return std::string(sv.substr(first, last - first + 1));
			};

			//   /settime HH:MM : regle l'heure du monde (master valide le format).
			if (channel == static_cast<uint8_t>(engine::net::ChatChannel::Say)
				&& text.starts_with("/settime "))
			{
				const std::string arg = trimEdges(text.substr(9));
				engine::network::admin::AdminCommandRequest req;
				req.command = "/settime";
				req.args.push_back(arg);
				std::vector<uint8_t> payload;
				engine::network::admin::BuildAdminCommandRequestPayload(req, payload);
				(void)m_authUi.SendGenericRequestAsync(
					engine::network::kOpcodeAdminCommandRequest, payload);
				LOG_INFO(Render, "[WorldClock] /settime '{}' envoye au master (validation RBAC)", arg);
				pushSkyChatLine("/settime %s envoye au master (validation RBAC en cours...)", arg.c_str());
				return true;
			}
			//   /pausetime on|off : met en pause ou reprend l'horloge monde.
			if (channel == static_cast<uint8_t>(engine::net::ChatChannel::Say)
				&& text.starts_with("/pausetime "))
			{
				const std::string arg = trimEdges(text.substr(11));
				engine::network::admin::AdminCommandRequest req;
				req.command = "/pausetime";
				req.args.push_back(arg);
				std::vector<uint8_t> payload;
				engine::network::admin::BuildAdminCommandRequestPayload(req, payload);
				(void)m_authUi.SendGenericRequestAsync(
					engine::network::kOpcodeAdminCommandRequest, payload);
				LOG_INFO(Render, "[WorldClock] /pausetime '{}' envoye au master (validation RBAC)", arg);
				pushSkyChatLine("/pausetime %s envoye au master (validation RBAC en cours...)", arg.c_str());
				return true;
			}
			//   /settimescale N : regle l'echelle de temps (min reels / jour).
			if (channel == static_cast<uint8_t>(engine::net::ChatChannel::Say)
				&& text.starts_with("/settimescale "))
			{
				const std::string arg = trimEdges(text.substr(14));
				engine::network::admin::AdminCommandRequest req;
				req.command = "/settimescale";
				req.args.push_back(arg);
				std::vector<uint8_t> payload;
				engine::network::admin::BuildAdminCommandRequestPayload(req, payload);
				(void)m_authUi.SendGenericRequestAsync(
					engine::network::kOpcodeAdminCommandRequest, payload);
				LOG_INFO(Render, "[WorldClock] /settimescale '{}' envoye au master (validation RBAC)", arg);
				pushSkyChatLine("/settimescale %s envoye au master (validation RBAC en cours...)", arg.c_str());
				return true;
			}
			// Wave 1 RBAC : /promote <account_id> <role> est un outil admin
			// permettant de promouvoir/retrograder un compte sans acces direct
			// DB. Le serveur valide le role administrator + applique la mise
			// a jour via AccountRoleService::SetRole (qui ecrit en DB + emet
			// l'audit role_change).
			if (channel == static_cast<uint8_t>(engine::net::ChatChannel::Say)
				&& text.starts_with("/promote "))
			{
				const auto rest = text.substr(9);
				const auto spacePos = rest.find(' ');
				if (spacePos == std::string_view::npos)
				{
					LOG_WARN(Core,
						"[Admin] /promote usage : /promote <account_id> <role>");
					return true;
				}
				std::string accIdStr(rest.substr(0, spacePos));
				std::string roleStr(rest.substr(spacePos + 1));
				engine::network::admin::AdminCommandRequest req;
				req.command = "/promote";
				req.args.push_back(accIdStr);
				req.args.push_back(roleStr);
				std::vector<uint8_t> payload;
				engine::network::admin::BuildAdminCommandRequestPayload(req, payload);
				(void)m_authUi.SendGenericRequestAsync(
					engine::network::kOpcodeAdminCommandRequest, payload);
				LOG_INFO(Core, "[Admin] /promote {} -> {} envoye au master (RBAC + audit)",
					accIdStr, roleStr);
				return true;
			}
			// Wave 2 RBAC migration : /who (player) -> AdminCommand.
			if (channel == static_cast<uint8_t>(engine::net::ChatChannel::Say)
				&& (text == "/who" || text.starts_with("/who ") || text.starts_with("/who\t")))
			{
				engine::network::admin::AdminCommandRequest req;
				req.command = "/who";
				std::vector<uint8_t> payload;
				engine::network::admin::BuildAdminCommandRequestPayload(req, payload);
				(void)m_authUi.SendGenericRequestAsync(
					engine::network::kOpcodeAdminCommandRequest, payload);
				LOG_INFO(Core, "[Engine] /who sent to master");
				return true;
			}
			// Wave 2 RBAC migration : /report <player> (player) -> AdminCommand.
			// Cree un ticket GM cote master ; la reponse affiche le ticket_id.
			if (channel == static_cast<uint8_t>(engine::net::ChatChannel::Say)
				&& text.starts_with("/report "))
			{
				std::string_view rest = text.substr(8);
				while (!rest.empty() && (rest.front() == ' ' || rest.front() == '\t'))
					rest.remove_prefix(1u);
				if (rest.empty())
				{
					LOG_WARN(Core, "[Engine] /report sans argument joueur");
					return true;
				}
				engine::network::admin::AdminCommandRequest req;
				req.command = "/report";
				req.args.push_back(std::string(rest));
				std::vector<uint8_t> payload;
				engine::network::admin::BuildAdminCommandRequestPayload(req, payload);
				(void)m_authUi.SendGenericRequestAsync(
					engine::network::kOpcodeAdminCommandRequest, payload);
				LOG_INFO(Core, "[Engine] /report {} sent to master", std::string(rest));
				return true;
			}
			// Wave 2 RBAC migration : /kick <player> (moderator) -> AdminCommand.
			if (channel == static_cast<uint8_t>(engine::net::ChatChannel::Say)
				&& text.starts_with("/kick "))
			{
				std::string_view rest = text.substr(6);
				while (!rest.empty() && (rest.front() == ' ' || rest.front() == '\t'))
					rest.remove_prefix(1u);
				if (rest.empty())
				{
					LOG_WARN(Core, "[Engine] /kick sans argument joueur");
					return true;
				}
				engine::network::admin::AdminCommandRequest req;
				req.command = "/kick";
				req.args.push_back(std::string(rest));
				std::vector<uint8_t> payload;
				engine::network::admin::BuildAdminCommandRequestPayload(req, payload);
				(void)m_authUi.SendGenericRequestAsync(
					engine::network::kOpcodeAdminCommandRequest, payload);
				LOG_INFO(Core, "[Engine] /kick {} sent to master", std::string(rest));
				return true;
			}
			// Wave 2 RBAC migration : /mute <player> <duration_min> (moderator).
			if (channel == static_cast<uint8_t>(engine::net::ChatChannel::Say)
				&& text.starts_with("/mute "))
			{
				std::string_view rest = text.substr(6);
				while (!rest.empty() && (rest.front() == ' ' || rest.front() == '\t'))
					rest.remove_prefix(1u);
				if (rest.empty())
				{
					LOG_WARN(Core, "[Engine] /mute sans arguments");
					return true;
				}
				// Split : premier mot = player, reste = duration (ou plus).
				auto firstSpace = rest.find_first_of(" \t");
				if (firstSpace == std::string_view::npos)
				{
					LOG_WARN(Core, "[Engine] /mute necessite <player> <duration_minutes>");
					return true;
				}
				std::string player(rest.substr(0, firstSpace));
				std::string_view durView = rest.substr(firstSpace + 1u);
				while (!durView.empty() && (durView.front() == ' ' || durView.front() == '\t'))
					durView.remove_prefix(1u);
				if (durView.empty())
				{
					LOG_WARN(Core, "[Engine] /mute : duree manquante");
					return true;
				}
				engine::network::admin::AdminCommandRequest req;
				req.command = "/mute";
				req.args.push_back(std::move(player));
				req.args.push_back(std::string(durView));
				std::vector<uint8_t> payload;
				engine::network::admin::BuildAdminCommandRequestPayload(req, payload);
				(void)m_authUi.SendGenericRequestAsync(
					engine::network::kOpcodeAdminCommandRequest, payload);
				LOG_INFO(Core, "[Engine] /mute sent to master");
				return true;
			}
			// Wave 2 RBAC migration : /ban <player> <reason...> (game_master).
			if (channel == static_cast<uint8_t>(engine::net::ChatChannel::Say)
				&& text.starts_with("/ban "))
			{
				std::string_view rest = text.substr(5);
				while (!rest.empty() && (rest.front() == ' ' || rest.front() == '\t'))
					rest.remove_prefix(1u);
				if (rest.empty())
				{
					LOG_WARN(Core, "[Engine] /ban sans argument joueur");
					return true;
				}
				auto firstSpace = rest.find_first_of(" \t");
				std::string player;
				std::string reason;
				if (firstSpace == std::string_view::npos)
				{
					player = std::string(rest);
				}
				else
				{
					player = std::string(rest.substr(0, firstSpace));
					std::string_view reasonView = rest.substr(firstSpace + 1u);
					while (!reasonView.empty() && (reasonView.front() == ' ' || reasonView.front() == '\t'))
						reasonView.remove_prefix(1u);
					reason = std::string(reasonView);
				}
				engine::network::admin::AdminCommandRequest req;
				req.command = "/ban";
				req.args.push_back(std::move(player));
				if (!reason.empty())
					req.args.push_back(std::move(reason));
				std::vector<uint8_t> payload;
				engine::network::admin::BuildAdminCommandRequestPayload(req, payload);
				(void)m_authUi.SendGenericRequestAsync(
					engine::network::kOpcodeAdminCommandRequest, payload);
				LOG_INFO(Core, "[Engine] /ban sent to master");
				return true;
			}
			// Wave 2 RBAC migration : /announce <message> (game_master).
			if (channel == static_cast<uint8_t>(engine::net::ChatChannel::Say)
				&& text.starts_with("/announce "))
			{
				std::string_view rest = text.substr(10);
				while (!rest.empty() && (rest.front() == ' ' || rest.front() == '\t'))
					rest.remove_prefix(1u);
				if (rest.empty())
				{
					LOG_WARN(Core, "[Engine] /announce : message vide");
					return true;
				}
				engine::network::admin::AdminCommandRequest req;
				req.command = "/announce";
				req.args.push_back(std::string(rest));
				std::vector<uint8_t> payload;
				engine::network::admin::BuildAdminCommandRequestPayload(req, payload);
				(void)m_authUi.SendGenericRequestAsync(
					engine::network::kOpcodeAdminCommandRequest, payload);
				LOG_INFO(Core, "[Engine] /announce sent to master");
				return true;
			}
			// CMANGOS.32 (Phase 5.32 step 3+4) — Slash command /ticket et /gmticket
			// pour ouvrir/fermer le panneau Support GM et synchroniser la liste
			// depuis le master au moment de l'ouverture.
			if (channel == static_cast<uint8_t>(engine::net::ChatChannel::Say)
				&& (text == "/ticket" || text == "/gmticket"
				    || text.starts_with("/ticket ") || text.starts_with("/ticket\t")
				    || text.starts_with("/gmticket ") || text.starts_with("/gmticket\t")))
			{
				m_gmTicketsVisible = !m_gmTicketsVisible;
				if (m_gmTicketsVisible)
				{
					m_gmTicketUi.RequestMyTickets();
				}
				LOG_INFO(Core, "[Engine] /ticket toggle (visible={})", m_gmTicketsVisible);
				sendAdminAudit("/ticket");
				return true;
			}
			// CMANGOS.25 (Phase 3.25 step 3+4) — Slash commands /ignore et /unignore.
			// Format V1 : "/ignore <account_id>" et "/unignore <account_id>" — la
			// resolution par character_name viendra avec PartySystem display.
			// "/ignore list" sans argument refresh la cache depuis le master.
			if (channel == static_cast<uint8_t>(engine::net::ChatChannel::Say)
				&& (text == "/ignore" || text.starts_with("/ignore ") || text.starts_with("/ignore\t")))
			{
				const auto spaceIdx = text.find_first_of(" \t");
				if (spaceIdx == std::string_view::npos)
				{
					// "/ignore" tout seul : refresh la liste.
					m_ignoreListUi.RequestIgnoreList();
					LOG_INFO(Core, "[Engine] /ignore (refresh list)");
					return true;
				}
				std::string_view arg = text.substr(spaceIdx + 1);
				while (!arg.empty() && (arg.front() == ' ' || arg.front() == '\t'))
					arg.remove_prefix(1u);
				if (arg == "list" || arg.empty())
				{
					m_ignoreListUi.RequestIgnoreList();
					LOG_INFO(Core, "[Engine] /ignore list");
					return true;
				}
				// Parse account_id en uint64 (base 10).
				uint64_t targetAccountId = 0u;
				bool parsed = false;
				try
				{
					size_t pos = 0;
					const std::string argStr(arg);
					targetAccountId = std::stoull(argStr, &pos, 10);
					parsed = (pos > 0u && targetAccountId != 0u);
				}
				catch (...)
				{
					parsed = false;
				}
				if (!parsed)
				{
					LOG_WARN(Core, "[Engine] /ignore : argument '{}' n'est pas un account_id valide",
						std::string(arg));
					return true;
				}
				m_ignoreListUi.IgnoreAccount(targetAccountId);
				LOG_INFO(Core, "[Engine] /ignore {}", targetAccountId);
				return true;
			}
			if (channel == static_cast<uint8_t>(engine::net::ChatChannel::Say)
				&& (text.starts_with("/unignore ") || text.starts_with("/unignore\t")))
			{
				const auto spaceIdx = text.find_first_of(" \t");
				std::string_view arg = text.substr(spaceIdx + 1);
				while (!arg.empty() && (arg.front() == ' ' || arg.front() == '\t'))
					arg.remove_prefix(1u);
				uint64_t targetAccountId = 0u;
				bool parsed = false;
				try
				{
					size_t pos = 0;
					const std::string argStr(arg);
					targetAccountId = std::stoull(argStr, &pos, 10);
					parsed = (pos > 0u && targetAccountId != 0u);
				}
				catch (...)
				{
					parsed = false;
				}
				if (!parsed)
				{
					LOG_WARN(Core, "[Engine] /unignore : argument '{}' n'est pas un account_id valide",
						std::string(arg));
					return true;
				}
				m_ignoreListUi.UnignoreAccount(targetAccountId);
				LOG_INFO(Core, "[Engine] /unignore {}", targetAccountId);
				return true;
			}
			return m_authUi.SendChatAsync(channel, targetToken, text);
		});
		m_authUi.SetMasterPushHandler([this](uint16_t opcode, const uint8_t* payload, size_t payloadSize) {
			using namespace engine::network;
			// CMANGOS.18 (Phase 3.18 step 4) — Dispatch des reponses Mail. Les
			// reponses (opcodes 50/52/54/56/58) ne sont pas des push purs mais
			// arrivent via le meme mecanisme PacketReceived puisque le client
			// n'utilise pas de RequestResponseDispatcher pour Mail (requestId=0
			// fire-and-forget cote envoi). On parse + dispatche ici.
			switch (opcode)
			{
			case kOpcodeMailListInboxResponse:
			{
				auto parsed = ParseMailListInboxResponsePayload(payload, payloadSize);
				if (!parsed)
				{
					LOG_WARN(Net, "[Engine] MAIL_LIST_INBOX_RESPONSE parse failed (size={})", payloadSize);
					return;
				}
				m_mailUi.OnInboxResponse(*parsed);
				return;
			}
			case kOpcodeMailReadResponse:
			{
				auto parsed = ParseMailReadResponsePayload(payload, payloadSize);
				if (!parsed)
				{
					LOG_WARN(Net, "[Engine] MAIL_READ_RESPONSE parse failed (size={})", payloadSize);
					return;
				}
				m_mailUi.OnReadResponse(*parsed);
				return;
			}
			case kOpcodeMailSendResponse:
			{
				auto parsed = ParseMailSendResponsePayload(payload, payloadSize);
				if (!parsed)
				{
					LOG_WARN(Net, "[Engine] MAIL_SEND_RESPONSE parse failed (size={})", payloadSize);
					return;
				}
				m_mailUi.OnSendResponse(*parsed);
				return;
			}
			case kOpcodeMailTakeAttachmentsResponse:
			{
				auto parsed = ParseMailTakeAttachmentsResponsePayload(payload, payloadSize);
				if (!parsed)
				{
					LOG_WARN(Net, "[Engine] MAIL_TAKE_ATTACHMENTS_RESPONSE parse failed (size={})", payloadSize);
					return;
				}
				m_mailUi.OnTakeAttachmentsResponse(*parsed);
				return;
			}
			case kOpcodeMailDeleteResponse:
			{
				auto parsed = ParseMailDeleteResponsePayload(payload, payloadSize);
				if (!parsed)
				{
					LOG_WARN(Net, "[Engine] MAIL_DELETE_RESPONSE parse failed (size={})", payloadSize);
					return;
				}
				m_mailUi.OnDeleteResponse(*parsed);
				return;
			}
			// CMANGOS.25 (Phase 3.25 step 3+4) — Dispatch des reponses IgnoreList
			// (69/71/73). La cache locale du presenter est mise a jour ici.
			case kOpcodeIgnoreAddResponse:
			{
				auto parsed = ParseIgnoreAddResponsePayload(payload, payloadSize);
				if (!parsed)
				{
					LOG_WARN(Net, "[Engine] IGNORE_ADD_RESPONSE parse failed (size={})", payloadSize);
					return;
				}
				m_ignoreListUi.OnIgnoreAddResponse(*parsed);
				return;
			}
			case kOpcodeIgnoreRemoveResponse:
			{
				auto parsed = ParseIgnoreRemoveResponsePayload(payload, payloadSize);
				if (!parsed)
				{
					LOG_WARN(Net, "[Engine] IGNORE_REMOVE_RESPONSE parse failed (size={})", payloadSize);
					return;
				}
				m_ignoreListUi.OnIgnoreRemoveResponse(*parsed);
				return;
			}
			case kOpcodeIgnoreListResponse:
			{
				auto parsed = ParseIgnoreListResponsePayload(payload, payloadSize);
				if (!parsed)
				{
					LOG_WARN(Net, "[Engine] IGNORE_LIST_RESPONSE parse failed (size={})", payloadSize);
					return;
				}
				m_ignoreListUi.OnIgnoreListResponse(*parsed);
				return;
			}
			// CMANGOS.32 (Phase 5.32 step 3+4) — Dispatch des reponses GmTickets
			// (77/79/81) + push GmTicketResolvedNotification (82).
			case kOpcodeGmTicketOpenResponse:
			{
				auto parsed = ParseGmTicketOpenResponsePayload(payload, payloadSize);
				if (!parsed)
				{
					LOG_WARN(Net, "[Engine] GMTICKET_OPEN_RESPONSE parse failed (size={})", payloadSize);
					return;
				}
				m_gmTicketUi.OnOpenResponse(*parsed);
				return;
			}
			case kOpcodeGmTicketListMineResponse:
			{
				auto parsed = ParseGmTicketListMineResponsePayload(payload, payloadSize);
				if (!parsed)
				{
					LOG_WARN(Net, "[Engine] GMTICKET_LIST_MINE_RESPONSE parse failed (size={})", payloadSize);
					return;
				}
				m_gmTicketUi.OnListMineResponse(*parsed);
				return;
			}
			case kOpcodeGmTicketCancelResponse:
			{
				auto parsed = ParseGmTicketCancelResponsePayload(payload, payloadSize);
				if (!parsed)
				{
					LOG_WARN(Net, "[Engine] GMTICKET_CANCEL_RESPONSE parse failed (size={})", payloadSize);
					return;
				}
				m_gmTicketUi.OnCancelResponse(*parsed);
				return;
			}
			case kOpcodeGmTicketResolvedNotification:
			{
				auto parsed = ParseGmTicketResolvedNotificationPayload(payload, payloadSize);
				if (!parsed)
				{
					LOG_WARN(Net, "[Engine] GMTICKET_RESOLVED_NOTIFICATION parse failed (size={})", payloadSize);
					return;
				}
				m_gmTicketUi.OnResolvedNotification(*parsed);
				return;
			}
			// CMANGOS.24 (Phase 3.24 step 3+4) — Dispatch des reponses Reputation
			// (96) + push UpdateNotification (97).
			case kOpcodeReputationListResponse:
			{
				auto parsed = ParseReputationListResponsePayload(payload, payloadSize);
				if (!parsed)
				{
					LOG_WARN(Net, "[Engine] REPUTATION_LIST_RESPONSE parse failed (size={})", payloadSize);
					return;
				}
				m_reputationUi.OnListResponse(*parsed);
				return;
			}
			case kOpcodeReputationUpdateNotification:
			{
				auto parsed = ParseReputationUpdateNotificationPayload(payload, payloadSize);
				if (!parsed)
				{
					LOG_WARN(Net, "[Engine] REPUTATION_UPDATE_NOTIFICATION parse failed (size={})", payloadSize);
					return;
				}
				m_reputationUi.OnUpdateNotification(*parsed);
				return;
			}
			// SP2 anniversaires (2026-07-18) — Dispatch de la réponse Exploits (207).
			case kOpcodeExploitListResponse:
			{
				auto parsed = ParseExploitListResponsePayload(payload, payloadSize);
				if (!parsed)
				{
					LOG_WARN(Net, "[Engine] EXPLOIT_LIST_RESPONSE parse failed (size={})", payloadSize);
					return;
				}
				m_exploitsUi.OnListResponse(*parsed);
				return;
			}
			// CMANGOS.39 (Phase 4.39 step 3+4) — Dispatch des reponses Skills
			// (114/116/118) + push UpgradeNotification (119).
			case kOpcodeSkillsListResponse:
			{
				auto parsed = ParseSkillsListResponsePayload(payload, payloadSize);
				if (!parsed)
				{
					LOG_WARN(Net, "[Engine] SKILLS_LIST_RESPONSE parse failed (size={})", payloadSize);
					return;
				}
				m_skillBookUi.OnListResponse(*parsed);
				return;
			}
			case kOpcodeSkillLearnResponse:
			{
				auto parsed = ParseSkillLearnResponsePayload(payload, payloadSize);
				if (!parsed)
				{
					LOG_WARN(Net, "[Engine] SKILL_LEARN_RESPONSE parse failed (size={})", payloadSize);
					return;
				}
				m_skillBookUi.OnLearnResponse(*parsed);
				return;
			}
			case kOpcodeSkillUseResponse:
			{
				auto parsed = ParseSkillUseResponsePayload(payload, payloadSize);
				if (!parsed)
				{
					LOG_WARN(Net, "[Engine] SKILL_USE_RESPONSE parse failed (size={})", payloadSize);
					return;
				}
				m_skillBookUi.OnUseResponse(*parsed);
				return;
			}
			case kOpcodeSkillUpgradeNotification:
			{
				auto parsed = ParseSkillUpgradeNotificationPayload(payload, payloadSize);
				if (!parsed)
				{
					LOG_WARN(Net, "[Engine] SKILL_UPGRADE_NOTIFICATION parse failed (size={})", payloadSize);
					return;
				}
				m_skillBookUi.OnUpgradeNotification(*parsed);
				return;
			}
			// CMANGOS.21 (Phase 5.21 step 3+4) — Dispatch des reponses Arena
			// (121/123/125/128) + push notifications (126/129).
			case kOpcodeArenaTeamListResponse:
			{
				auto parsed = ParseArenaTeamListResponsePayload(payload, payloadSize);
				if (!parsed)
				{
					LOG_WARN(Net, "[Engine] ARENA_TEAM_LIST_RESPONSE parse failed (size={})", payloadSize);
					return;
				}
				m_arenaUi.OnTeamListResponse(*parsed);
				return;
			}
			case kOpcodeArenaQueueResponse:
			{
				auto parsed = ParseArenaQueueResponsePayload(payload, payloadSize);
				if (!parsed)
				{
					LOG_WARN(Net, "[Engine] ARENA_QUEUE_RESPONSE parse failed (size={})", payloadSize);
					return;
				}
				m_arenaUi.OnQueueResponse(*parsed);
				return;
			}
			case kOpcodeArenaLeaveQueueResponse:
			{
				auto parsed = ParseArenaLeaveQueueResponsePayload(payload, payloadSize);
				if (!parsed)
				{
					LOG_WARN(Net, "[Engine] ARENA_LEAVE_QUEUE_RESPONSE parse failed (size={})", payloadSize);
					return;
				}
				m_arenaUi.OnLeaveQueueResponse(*parsed);
				return;
			}
			case kOpcodeArenaMatchProposalNotification:
			{
				auto parsed = ParseArenaMatchProposalNotificationPayload(payload, payloadSize);
				if (!parsed)
				{
					LOG_WARN(Net, "[Engine] ARENA_MATCH_PROPOSAL_NOTIFICATION parse failed (size={})", payloadSize);
					return;
				}
				m_arenaUi.OnMatchProposalNotification(*parsed);
				return;
			}
			case kOpcodeArenaMatchAcceptResponse:
			{
				auto parsed = ParseArenaMatchAcceptResponsePayload(payload, payloadSize);
				if (!parsed)
				{
					LOG_WARN(Net, "[Engine] ARENA_MATCH_ACCEPT_RESPONSE parse failed (size={})", payloadSize);
					return;
				}
				m_arenaUi.OnMatchAcceptResponse(*parsed);
				return;
			}
			case kOpcodeArenaMatchResultNotification:
			{
				auto parsed = ParseArenaMatchResultNotificationPayload(payload, payloadSize);
				if (!parsed)
				{
					LOG_WARN(Net, "[Engine] ARENA_MATCH_RESULT_NOTIFICATION parse failed (size={})", payloadSize);
					return;
				}
				m_arenaUi.OnMatchResultNotification(*parsed);
				return;
			}
			// CMANGOS.10 (Phase 5 step 3+4) — Dispatch des reponses BattleGround
			// (131/133/135) + push notifications (136/137/138).
			case kOpcodeBgListResponse:
			{
				auto parsed = ParseBgListResponsePayload(payload, payloadSize);
				if (!parsed)
				{
					LOG_WARN(Net, "[Engine] BG_LIST_RESPONSE parse failed (size={})", payloadSize);
					return;
				}
				m_battleGroundUi.OnListResponse(*parsed);
				return;
			}
			case kOpcodeBgQueueResponse:
			{
				auto parsed = ParseBgQueueResponsePayload(payload, payloadSize);
				if (!parsed)
				{
					LOG_WARN(Net, "[Engine] BG_QUEUE_RESPONSE parse failed (size={})", payloadSize);
					return;
				}
				m_battleGroundUi.OnQueueResponse(*parsed);
				return;
			}
			case kOpcodeBgLeaveQueueResponse:
			{
				auto parsed = ParseBgLeaveQueueResponsePayload(payload, payloadSize);
				if (!parsed)
				{
					LOG_WARN(Net, "[Engine] BG_LEAVE_QUEUE_RESPONSE parse failed (size={})", payloadSize);
					return;
				}
				m_battleGroundUi.OnLeaveQueueResponse(*parsed);
				return;
			}
			case kOpcodeBgMatchStartNotification:
			{
				auto parsed = ParseBgMatchStartNotificationPayload(payload, payloadSize);
				if (!parsed)
				{
					LOG_WARN(Net, "[Engine] BG_MATCH_START_NOTIFICATION parse failed (size={})", payloadSize);
					return;
				}
				m_battleGroundUi.OnMatchStartNotification(*parsed);
				return;
			}
			case kOpcodeBgScoreUpdateNotification:
			{
				auto parsed = ParseBgScoreUpdateNotificationPayload(payload, payloadSize);
				if (!parsed)
				{
					LOG_WARN(Net, "[Engine] BG_SCORE_UPDATE_NOTIFICATION parse failed (size={})", payloadSize);
					return;
				}
				m_battleGroundUi.OnScoreUpdateNotification(*parsed);
				return;
			}
			case kOpcodeBgMatchEndNotification:
			{
				auto parsed = ParseBgMatchEndNotificationPayload(payload, payloadSize);
				if (!parsed)
				{
					LOG_WARN(Net, "[Engine] BG_MATCH_END_NOTIFICATION parse failed (size={})", payloadSize);
					return;
				}
				m_battleGroundUi.OnMatchEndNotification(*parsed);
				return;
			}
			// CMANGOS.36 (Phase 5.36 step 3+4) — Dispatch des reponses OutdoorPvp
			// (141/143/145/147) + push notifications (148/149).
			case kOpcodeOutdoorPvpZoneListResponse:
			{
				auto parsed = ParseOutdoorPvpZoneListResponsePayload(payload, payloadSize);
				if (!parsed)
				{
					LOG_WARN(Net, "[Engine] OUTDOOR_PVP_ZONE_LIST_RESPONSE parse failed (size={})", payloadSize);
					return;
				}
				m_outdoorPvpUi.OnListResponse(*parsed);
				return;
			}
			case kOpcodeOutdoorPvpSubscribeResponse:
			{
				auto parsed = ParseOutdoorPvpSubscribeResponsePayload(payload, payloadSize);
				if (!parsed)
				{
					LOG_WARN(Net, "[Engine] OUTDOOR_PVP_SUBSCRIBE_RESPONSE parse failed (size={})", payloadSize);
					return;
				}
				m_outdoorPvpUi.OnSubscribeResponse(*parsed);
				return;
			}
			case kOpcodeOutdoorPvpUnsubscribeResponse:
			{
				auto parsed = ParseOutdoorPvpUnsubscribeResponsePayload(payload, payloadSize);
				if (!parsed)
				{
					LOG_WARN(Net, "[Engine] OUTDOOR_PVP_UNSUBSCRIBE_RESPONSE parse failed (size={})", payloadSize);
					return;
				}
				m_outdoorPvpUi.OnUnsubscribeResponse(*parsed);
				return;
			}
			case kOpcodeOutdoorPvpCaptureStartResponse:
			{
				auto parsed = ParseOutdoorPvpCaptureStartResponsePayload(payload, payloadSize);
				if (!parsed)
				{
					LOG_WARN(Net, "[Engine] OUTDOOR_PVP_CAPTURE_START_RESPONSE parse failed (size={})", payloadSize);
					return;
				}
				m_outdoorPvpUi.OnCaptureStartResponse(*parsed);
				return;
			}
			case kOpcodeOutdoorPvpCaptureProgressNotification:
			{
				auto parsed = ParseOutdoorPvpCaptureProgressNotificationPayload(payload, payloadSize);
				if (!parsed)
				{
					LOG_WARN(Net, "[Engine] OUTDOOR_PVP_CAPTURE_PROGRESS_NOTIFICATION parse failed (size={})", payloadSize);
					return;
				}
				m_outdoorPvpUi.OnCaptureProgressNotification(*parsed);
				return;
			}
			case kOpcodeOutdoorPvpCaptureCompletedNotification:
			{
				auto parsed = ParseOutdoorPvpCaptureCompletedNotificationPayload(payload, payloadSize);
				if (!parsed)
				{
					LOG_WARN(Net, "[Engine] OUTDOOR_PVP_CAPTURE_COMPLETED_NOTIFICATION parse failed (size={})", payloadSize);
					return;
				}
				m_outdoorPvpUi.OnCaptureCompletedNotification(*parsed);
				return;
			}
			// CMANGOS.42 (Phase 4.42 step 3+4) — Dispatch des reponses Weather
			// (151/153/155) + push notification (156).
			case kOpcodeWeatherListResponse:
			{
				auto parsed = ParseWeatherListResponsePayload(payload, payloadSize);
				if (!parsed)
				{
					LOG_WARN(Net, "[Engine] WEATHER_LIST_RESPONSE parse failed (size={})", payloadSize);
					return;
				}
				m_weatherUi.OnListResponse(*parsed);
				return;
			}
			case kOpcodeWeatherSubscribeResponse:
			{
				auto parsed = ParseWeatherSubscribeResponsePayload(payload, payloadSize);
				if (!parsed)
				{
					LOG_WARN(Net, "[Engine] WEATHER_SUBSCRIBE_RESPONSE parse failed (size={})", payloadSize);
					return;
				}
				m_weatherUi.OnSubscribeResponse(*parsed);
				return;
			}
			case kOpcodeWeatherUnsubscribeResponse:
			{
				auto parsed = ParseWeatherUnsubscribeResponsePayload(payload, payloadSize);
				if (!parsed)
				{
					LOG_WARN(Net, "[Engine] WEATHER_UNSUBSCRIBE_RESPONSE parse failed (size={})", payloadSize);
					return;
				}
				m_weatherUi.OnUnsubscribeResponse(*parsed);
				return;
			}
			case kOpcodeWeatherUpdateNotification:
			{
				auto parsed = ParseWeatherUpdateNotificationPayload(payload, payloadSize);
				if (!parsed)
				{
					LOG_WARN(Net, "[Engine] WEATHER_UPDATE_NOTIFICATION parse failed (size={})", payloadSize);
					return;
				}
				m_weatherUi.OnUpdateNotification(*parsed);
				// Signal autoritaire unique -> pilote le visuel (particules + nuages).
				// (Branchement absent jusqu'ici : la météo serveur n'affectait pas le rendu.)
				m_weatherSystem.SetWeather(
					engine::render::MapServerKindToWeatherState(parsed->kind));
				return;
			}
			// CMANGOS.31 (Phase 5.31 step 3+4) — Dispatch des reponses
			// GameEvents (158/160/162) + push notification (163).
			case kOpcodeGameEventListResponse:
			{
				auto parsed = ParseGameEventListResponsePayload(payload, payloadSize);
				if (!parsed)
				{
					LOG_WARN(Net, "[Engine] GAME_EVENT_LIST_RESPONSE parse failed (size={})", payloadSize);
					return;
				}
				m_gameEventUi.OnListResponse(*parsed);
				return;
			}
			case kOpcodeGameEventSubscribeResponse:
			{
				auto parsed = ParseGameEventSubscribeResponsePayload(payload, payloadSize);
				if (!parsed)
				{
					LOG_WARN(Net, "[Engine] GAME_EVENT_SUBSCRIBE_RESPONSE parse failed (size={})", payloadSize);
					return;
				}
				m_gameEventUi.OnSubscribeResponse(*parsed);
				return;
			}
			case kOpcodeGameEventUnsubscribeResponse:
			{
				auto parsed = ParseGameEventUnsubscribeResponsePayload(payload, payloadSize);
				if (!parsed)
				{
					LOG_WARN(Net, "[Engine] GAME_EVENT_UNSUBSCRIBE_RESPONSE parse failed (size={})", payloadSize);
					return;
				}
				m_gameEventUi.OnUnsubscribeResponse(*parsed);
				return;
			}
			case kOpcodeGameEventStateChangeNotification:
			{
				auto parsed = ParseGameEventStateChangeNotificationPayload(payload, payloadSize);
				if (!parsed)
				{
					LOG_WARN(Net, "[Engine] GAME_EVENT_STATE_CHANGE_NOTIFICATION parse failed (size={})", payloadSize);
					return;
				}
				m_gameEventUi.OnStateChangeNotification(*parsed);
				return;
			}
			// CMANGOS.21 (Phase 5.21 step 3+4 Guilds) — Dispatch des reponses
			// Guild (165/167/169/171) + push notification (172 MotdUpdate).
			case kOpcodeGuildListResponse:
			{
				auto parsed = ParseGuildListResponsePayload(payload, payloadSize);
				if (!parsed)
				{
					LOG_WARN(Net, "[Engine] GUILD_LIST_RESPONSE parse failed (size={})", payloadSize);
					return;
				}
				m_guildUi.OnListResponse(*parsed);
				return;
			}
			case kOpcodeGuildMembersResponse:
			{
				auto parsed = ParseGuildMembersResponsePayload(payload, payloadSize);
				if (!parsed)
				{
					LOG_WARN(Net, "[Engine] GUILD_MEMBERS_RESPONSE parse failed (size={})", payloadSize);
					return;
				}
				m_guildUi.OnMembersResponse(*parsed);
				return;
			}
			case kOpcodeGuildPermissionsResponse:
			{
				auto parsed = ParseGuildPermissionsResponsePayload(payload, payloadSize);
				if (!parsed)
				{
					LOG_WARN(Net, "[Engine] GUILD_PERMISSIONS_RESPONSE parse failed (size={})", payloadSize);
					return;
				}
				m_guildUi.OnPermissionsResponse(*parsed);
				return;
			}
			case kOpcodeGuildBankResponse:
			{
				auto parsed = ParseGuildBankResponsePayload(payload, payloadSize);
				if (!parsed)
				{
					LOG_WARN(Net, "[Engine] GUILD_BANK_RESPONSE parse failed (size={})", payloadSize);
					return;
				}
				m_guildUi.OnBankResponse(*parsed);
				return;
			}
			case kOpcodeGuildMotdUpdateNotification:
			{
				auto parsed = ParseGuildMotdUpdateNotificationPayload(payload, payloadSize);
				if (!parsed)
				{
					LOG_WARN(Net, "[Engine] GUILD_MOTD_UPDATE_NOTIFICATION parse failed (size={})", payloadSize);
					return;
				}
				m_guildUi.OnMotdUpdateNotification(*parsed);
				return;
			}
			// CMANGOS.09 (Phase 5.09 step 3+4 AuctionHouse) — Dispatch des
			// reponses Auction (174/176/178/180) + push notification 181
			// (AuctionExpired).
			case kOpcodeAuctionListResponse:
			{
				auto parsed = ParseAuctionListResponsePayload(payload, payloadSize);
				if (!parsed)
				{
					LOG_WARN(Net, "[Engine] AUCTION_LIST_RESPONSE parse failed (size={})", payloadSize);
					return;
				}
				m_auctionHouseUi.OnListResponse(*parsed);
				return;
			}
			case kOpcodeAuctionPostResponse:
			{
				auto parsed = ParseAuctionPostResponsePayload(payload, payloadSize);
				if (!parsed)
				{
					LOG_WARN(Net, "[Engine] AUCTION_POST_RESPONSE parse failed (size={})", payloadSize);
					return;
				}
				m_auctionHouseUi.OnPostResponse(*parsed);
				return;
			}
			case kOpcodeAuctionBidResponse:
			{
				auto parsed = ParseAuctionBidResponsePayload(payload, payloadSize);
				if (!parsed)
				{
					LOG_WARN(Net, "[Engine] AUCTION_BID_RESPONSE parse failed (size={})", payloadSize);
					return;
				}
				m_auctionHouseUi.OnBidResponse(*parsed);
				return;
			}
			case kOpcodeAuctionCancelResponse:
			{
				auto parsed = ParseAuctionCancelResponsePayload(payload, payloadSize);
				if (!parsed)
				{
					LOG_WARN(Net, "[Engine] AUCTION_CANCEL_RESPONSE parse failed (size={})", payloadSize);
					return;
				}
				m_auctionHouseUi.OnCancelResponse(*parsed);
				return;
			}
			case kOpcodeAuctionExpiredNotification:
			{
				auto parsed = ParseAuctionExpiredNotificationPayload(payload, payloadSize);
				if (!parsed)
				{
					LOG_WARN(Net, "[Engine] AUCTION_EXPIRED_NOTIFICATION parse failed (size={})", payloadSize);
					return;
				}
				m_auctionHouseUi.OnExpiredNotification(*parsed);
				return;
			}
			// CMANGOS.17 (Phase 3.17 step 3+4 Loot) — Dispatch des push Loot
			// (182 RollNotification + 185 RollResultNotification) + responses
			// (184 ChoiceResponse + 187 SimulateRollResponse).
			case kOpcodeLootRollNotification:
			{
				auto parsed = ParseLootRollNotificationPayload(payload, payloadSize);
				if (!parsed)
				{
					LOG_WARN(Net, "[Engine] LOOT_ROLL_NOTIFICATION parse failed (size={})", payloadSize);
					return;
				}
				m_lootRollUi.OnRollNotification(*parsed);
				return;
			}
			case kOpcodeLootRollChoiceResponse:
			{
				auto parsed = ParseLootRollChoiceResponsePayload(payload, payloadSize);
				if (!parsed)
				{
					LOG_WARN(Net, "[Engine] LOOT_ROLL_CHOICE_RESPONSE parse failed (size={})", payloadSize);
					return;
				}
				m_lootRollUi.OnChoiceResponse(*parsed);
				return;
			}
			case kOpcodeLootRollResultNotification:
			{
				auto parsed = ParseLootRollResultNotificationPayload(payload, payloadSize);
				if (!parsed)
				{
					LOG_WARN(Net, "[Engine] LOOT_ROLL_RESULT_NOTIFICATION parse failed (size={})", payloadSize);
					return;
				}
				m_lootRollUi.OnRollResultNotification(*parsed);
				return;
			}
			case kOpcodeLootSimulateRollResponse:
			{
				auto parsed = ParseLootSimulateRollResponsePayload(payload, payloadSize);
				if (!parsed)
				{
					LOG_WARN(Net, "[Engine] LOOT_SIMULATE_ROLL_RESPONSE parse failed (size={})", payloadSize);
					return;
				}
				m_lootRollUi.OnSimulateRollResponse(*parsed);
				return;
			}
			// Phase 5 step 3+4 Lunar — Dispatch des opcodes 193 (StateResponse)
			// et 194 (PhaseChangeNotification, push). Master autoritaire ; le
			// client recoit l'etat initial sur EnterWorld puis un push toutes
			// les ~21h sur changement de phase.
			case kOpcodeLunarStateResponse:
			{
				engine::network::lunar::LunarStateResponse parsed;
				if (!engine::network::lunar::ParseLunarStateResponsePayload(payload, payloadSize, parsed))
				{
					LOG_WARN(Net, "[Engine] LUNAR_STATE_RESPONSE parse failed (size={})", payloadSize);
					return;
				}
				if (parsed.status == engine::network::lunar::LunarStatus::Ok)
				{
					m_dayNight.OnLunarPhaseChange(parsed.phase, parsed.illumination);
					LOG_INFO(Render, "[Engine] LunarState received: phase={} illumination={:.3f}",
						static_cast<unsigned>(parsed.phase), parsed.illumination);
				}
				return;
			}
			case kOpcodeLunarPhaseChangeNotification:
			{
				engine::network::lunar::LunarPhaseChangeNotification parsed;
				if (!engine::network::lunar::ParseLunarPhaseChangeNotificationPayload(payload, payloadSize, parsed))
				{
					LOG_WARN(Net, "[Engine] LUNAR_PHASE_CHANGE parse failed (size={})", payloadSize);
					return;
				}
				m_dayNight.OnLunarPhaseChange(parsed.newPhase, parsed.newIllumination);
				LOG_INFO(Render, "[Engine] LunarPhaseChange: phase={} illumination={:.3f}",
					static_cast<unsigned>(parsed.newPhase), parsed.newIllumination);
				return;
			}
			// WorldClock sync (Task 6.2) — Dispatch des opcodes 204 (StateResponse)
			// et 205 (ChangeNotification, push admin). Les deux portent les memes
			// champs ; on construit les WorldClockParams et on les branche sur le
			// cycle jour/nuit via SetServerClock (bascule en mode driven). Le master
			// est autoritaire ; le client recoit l'etat initial sur EnterWorld, un
			// push 205 a chaque changement admin (/settime, /pausetime...), et une
			// re-sync periodique 204 (controle de derive) declenchee par Update().
			case kOpcodeWorldClockStateResponse:
			case kOpcodeWorldClockChangeNotification:
			{
				engine::network::worldclock::WorldClockStateResponse parsed;
				const bool ok = (opcode == kOpcodeWorldClockStateResponse)
					? engine::network::worldclock::ParseWorldClockStateResponsePayload(payload, payloadSize, parsed)
					: engine::network::worldclock::ParseWorldClockChangeNotificationPayload(payload, payloadSize, parsed);
				if (!ok || parsed.status != engine::network::worldclock::WorldClockStatus::Ok)
				{
					LOG_WARN(Net, "[Engine] WORLDCLOCK parse/status failed (opcode={}, size={})",
						opcode, payloadSize);
					return;
				}
				engine::world::WorldClockParams p;
				p.epochRefUnixMs          = parsed.epochRefUnixMs;
				p.timeScaleRealMinPerDay  = parsed.timeScaleRealMinPerDay;
				p.offsetGameSec           = parsed.offsetGameSec;
				p.paused                  = (parsed.paused != 0);
				p.pausedAtGameSec         = parsed.pausedAtGameSec;
				p.lunarPeriodGameSec      = parsed.lunarPeriodGameSec;
				const uint64_t clientNow = static_cast<uint64_t>(
					std::chrono::duration_cast<std::chrono::milliseconds>(
						std::chrono::system_clock::now().time_since_epoch()).count());
				m_dayNight.SetServerClock(p, parsed.serverTimeUnixMs, clientNow);
				LOG_INFO(Render, "[Engine] WorldClock sync: timeScale={:.1f} offset={:.0f} paused={}",
					p.timeScaleRealMinPerDay, p.offsetGameSec, static_cast<int>(p.paused));
				return;
			}
			// AdminCommand RBAC — reponse master apres validation du role +
			// log audit. Si status==Ok, on dispatch sur le nom de la commande
			// pour appliquer l'effet local (ex: /sky moon -> override visuel).
			// Sinon on log le refus.
			case kOpcodeAdminCommandResponse:
			{
				engine::network::admin::AdminCommandResponse parsed;
				if (!engine::network::admin::ParseAdminCommandResponsePayload(payload, payloadSize, parsed))
				{
					LOG_WARN(Net, "[Engine] ADMIN_COMMAND_RESPONSE parse failed (size={})", payloadSize);
					return;
				}
				// Helper inline pour pousser une ligne SRV dans le chat in-game
				// (cf. pattern pushSkyChatLine du send callback).
				auto pushAdminChatLine = [this](const char* sender, const char* fmt, auto... args) {
					char buf[256];
					std::snprintf(buf, sizeof(buf), fmt, args...);
					engine::net::ChatMessage chatMsg;
					chatMsg.timestampUnixMs = static_cast<uint64_t>(
						std::chrono::duration_cast<std::chrono::milliseconds>(
							std::chrono::system_clock::now().time_since_epoch()).count());
					chatMsg.channel = engine::net::ChatChannel::Server;
					chatMsg.sender  = sender;
					chatMsg.text    = buf;
					m_chatUi.PushNetworkLine(chatMsg);
				};
				if (parsed.status == engine::network::admin::AdminCommandStatus::Ok)
				{
					if (parsed.command == "/sky moon")
					{
						// Parse echoed args : ["phase=7", "illumination=1.000"]
						uint8_t phase = 0;
						float illumination = 0.0f;
						for (const auto& kv : parsed.result)
						{
							if (kv.starts_with("phase="))
							{
								try { phase = static_cast<uint8_t>(std::stoi(kv.substr(6))); }
								catch (...) { phase = 0; }
							}
							else if (kv.starts_with("illumination="))
							{
								try { illumination = std::stof(kv.substr(13)); }
								catch (...) { illumination = 0.0f; }
							}
						}
						m_dayNight.OnLunarPhaseChange(phase, illumination);
						LOG_INFO(Render, "[Sky] /sky moon ACK applied : phase={} illumination={:.3f}",
							static_cast<unsigned>(phase), illumination);
						pushAdminChatLine("[Sky]",
							"OK : moon phase %u illumination=%.0f%% (master autorise)",
							static_cast<unsigned>(phase),
							static_cast<double>(illumination * 100.0f));
					}
					else if (parsed.command == "/sky time")
					{
						float hours = 12.0f;
						for (const auto& kv : parsed.result)
						{
							if (kv.starts_with("hours="))
							{
								try { hours = std::stof(kv.substr(6)); }
								catch (...) { hours = 12.0f; }
							}
						}
						m_dayNight.SetTime(hours);
						LOG_INFO(Render, "[Sky] /sky time ACK applied : {:.2f}h",
							static_cast<double>(hours));
					}
					else if (parsed.command == "/sky info")
					{
						const auto& s = m_dayNight.GetState();
						static const char* kMoonName[16] = {
							"NewMoon", "WaxingCrescentEarly", "WaxingCrescentLate", "FirstQuarter",
							"WaxingGibbousEarly", "WaxingGibbousLate", "FullMoonRising", "FullMoon",
							"FullMoonSetting", "WaningGibbousEarly", "WaningGibbousLate", "LastQuarter",
							"WaningCrescentEarly", "WaningCrescentLate", "EarthshineEarly", "EarthshineLate"
						};
						LOG_INFO(Render, "[Sky] timeOfDay={:.2f}h isDaytime={}",
							s.timeOfDay, s.isDaytime);
						LOG_INFO(Render, "[Sky] sunDir=({:.2f},{:.2f},{:.2f})",
							s.lightDir[0], s.lightDir[1], s.lightDir[2]);
						LOG_INFO(Render, "[Sky] moonPhase={} ({}) illumination={:.0f}%",
							static_cast<unsigned>(s.moonPhase),
							kMoonName[s.moonPhase < 16 ? s.moonPhase : 0],
							s.moonIllumination * 100.0f);

						// Affichage chat in-game : sans ce push, l'utilisateur ne voit
						// l'info que dans les logs. Cf. pattern identique pour /sky moon
						// (pushAdminChatLine plus haut) et /who/report/kick/ban/announce.
						pushAdminChatLine("[Sky]", "timeOfDay=%.2fh isDaytime=%d",
							static_cast<double>(s.timeOfDay),
							static_cast<int>(s.isDaytime));
						pushAdminChatLine("[Sky]", "sunDir=(%.2f,%.2f,%.2f)",
							static_cast<double>(s.lightDir[0]),
							static_cast<double>(s.lightDir[1]),
							static_cast<double>(s.lightDir[2]));
						pushAdminChatLine("[Sky]", "moonPhase=%u (%s) illumination=%.0f%%",
							static_cast<unsigned>(s.moonPhase),
							kMoonName[s.moonPhase < 16 ? s.moonPhase : 0],
							static_cast<double>(s.moonIllumination * 100.0f));
					}
					else if (parsed.command == "/loot")
					{
						LOG_INFO(Core, "[Admin] /loot ACK : {}", parsed.message);
					}
					else if (parsed.command == "/promote")
					{
						LOG_INFO(Core, "[Admin] /promote ACK : {}", parsed.message);
					}
					else if (parsed.command == "/who"
					      || parsed.command == "/report"
					      || parsed.command == "/kick"
					      || parsed.command == "/mute"
					      || parsed.command == "/ban"
					      || parsed.command == "/announce")
					{
						// Wave 2 : reponses moderation. Affiche le resultat
						// dans le chat (canal Server) pour feedback joueur.
						std::string body;
						if (parsed.command == "/who")
						{
							std::string countStr;
							std::string loginsStr;
							for (const auto& kv : parsed.result)
							{
								if (kv.starts_with("count="))
									countStr = kv.substr(6);
								else if (kv.starts_with("logins="))
									loginsStr = kv.substr(7);
							}
							body = "[Who] " + countStr + " joueurs connectes";
							if (!loginsStr.empty())
								body += " : " + loginsStr;
						}
						else if (parsed.command == "/report")
						{
							body = "[Report] " + parsed.message;
						}
						else if (parsed.command == "/kick")
						{
							body = "[Kick] " + parsed.message;
						}
						else if (parsed.command == "/mute")
						{
							body = "[Mute] " + parsed.message;
						}
						else if (parsed.command == "/ban")
						{
							body = "[Ban] " + parsed.message;
						}
						else if (parsed.command == "/announce")
						{
							body = "[Announce] " + parsed.message;
						}

						if (!body.empty() && m_chatUi.IsInitialized())
						{
							engine::net::ChatMessage line;
							line.timestampUnixMs = 0;
							line.channel = engine::net::ChatChannel::Server;
							line.sender = "system";
							line.text = std::move(body);
							m_chatUi.PushNetworkLine(line);
						}
						LOG_INFO(Net, "[AdminCommand] OK command={} message={}",
							parsed.command, parsed.message);
					}
					else
					{
						LOG_INFO(Net, "[AdminCommand] OK : command={} (no client effect V1)", parsed.command);
						pushAdminChatLine("[AdminCommand]", "OK : %s", parsed.command.c_str());
					}
				}
				else
				{
					// Tout refus (Denied, InvalidArgs, Unauthorized, ServerError,
					// UnknownCommand) est trace cote client. L'audit cote master
					// a deja ete emis avec result=DENIED/INVALID_ARGS/etc.
					LOG_WARN(Net, "[AdminCommand] REFUSED command={} status={} message={}",
						parsed.command,
						static_cast<unsigned>(parsed.status),
						parsed.message);
					const char* statusName = "ERROR";
					switch (parsed.status)
					{
						case engine::network::admin::AdminCommandStatus::Unauthorized:
							statusName = "UNAUTHORIZED"; break;
						case engine::network::admin::AdminCommandStatus::Denied:
							statusName = "DENIED (role insuffisant)"; break;
						case engine::network::admin::AdminCommandStatus::UnknownCommand:
							statusName = "UNKNOWN_COMMAND"; break;
						case engine::network::admin::AdminCommandStatus::InvalidArgs:
							statusName = "INVALID_ARGS"; break;
						default: break;
					}
					pushAdminChatLine("[AdminCommand]",
						"REFUSE %s : %s -- %s",
						statusName, parsed.command.c_str(), parsed.message.c_str());
				}
				return;
			}
			// CMANGOS.33 (Phase 5.33 step 3+4) — Dispatch des reponses LFG
			// (101/103/105) + push MatchProposalNotification (106).
			case kOpcodeLfgQueueResponse:
			{
				auto parsed = ParseLfgQueueResponsePayload(payload, payloadSize);
				if (!parsed)
				{
					LOG_WARN(Net, "[Engine] LFG_QUEUE_RESPONSE parse failed (size={})", payloadSize);
					return;
				}
				m_lfgUi.OnQueueResponse(*parsed);
				return;
			}
			case kOpcodeLfgLeaveResponse:
			{
				auto parsed = ParseLfgLeaveResponsePayload(payload, payloadSize);
				if (!parsed)
				{
					LOG_WARN(Net, "[Engine] LFG_LEAVE_RESPONSE parse failed (size={})", payloadSize);
					return;
				}
				m_lfgUi.OnLeaveResponse(*parsed);
				return;
			}
			case kOpcodeLfgStatusResponse:
			{
				auto parsed = ParseLfgStatusResponsePayload(payload, payloadSize);
				if (!parsed)
				{
					LOG_WARN(Net, "[Engine] LFG_STATUS_RESPONSE parse failed (size={})", payloadSize);
					return;
				}
				m_lfgUi.OnStatusResponse(*parsed);
				return;
			}
			case kOpcodeLfgMatchProposalNotification:
			{
				auto parsed = ParseLfgMatchProposalNotificationPayload(payload, payloadSize);
				if (!parsed)
				{
					LOG_WARN(Net, "[Engine] LFG_MATCH_PROPOSAL_NOTIFICATION parse failed (size={})", payloadSize);
					return;
				}
				m_lfgUi.OnMatchProposal(*parsed);
				return;
			}
			// CMANGOS.30 (Phase 5.30 step 3+4) — Dispatch des opcodes Cinematics
			// (push 108 + responses 110/112). 109 (Ack) et 111 (SkipRequest)
			// sont sortants : pas de dispatch ici.
			case kOpcodeCinematicPlayNotification:
			{
				auto parsed = ParseCinematicPlayNotificationPayload(payload, payloadSize);
				if (!parsed)
				{
					LOG_WARN(Net, "[Engine] CINEMATIC_PLAY_NOTIFICATION parse failed (size={})", payloadSize);
					return;
				}
				m_cinematicUi.OnPlayNotification(*parsed);
				return;
			}
			case kOpcodeCinematicAckResponse:
			{
				auto parsed = ParseCinematicAckResponsePayload(payload, payloadSize);
				if (!parsed)
				{
					LOG_WARN(Net, "[Engine] CINEMATIC_ACK_RESPONSE parse failed (size={})", payloadSize);
					return;
				}
				m_cinematicUi.OnAckResponse(*parsed);
				return;
			}
			case kOpcodeCinematicSkipResponse:
			{
				auto parsed = ParseCinematicSkipResponsePayload(payload, payloadSize);
				if (!parsed)
				{
					LOG_WARN(Net, "[Engine] CINEMATIC_SKIP_RESPONSE parse failed (size={})", payloadSize);
					return;
				}
				m_cinematicUi.OnSkipResponse(*parsed);
				return;
			}
			// CMANGOS.27 (Phase 4.27 step 3+4) — Dispatch des reponses Trade
			// (84/87/89/92) + push notifications (85/90/94).
			case kOpcodeTradeBeginResponse:
			{
				auto parsed = ParseTradeBeginResponsePayload(payload, payloadSize);
				if (!parsed)
				{
					LOG_WARN(Net, "[Engine] TRADE_BEGIN_RESPONSE parse failed (size={})", payloadSize);
					return;
				}
				m_tradeWindowUi.OnTradeBeginResponse(*parsed);
				return;
			}
			case kOpcodeTradeBeginNotification:
			{
				auto parsed = ParseTradeBeginNotificationPayload(payload, payloadSize);
				if (!parsed)
				{
					LOG_WARN(Net, "[Engine] TRADE_BEGIN_NOTIFICATION parse failed (size={})", payloadSize);
					return;
				}
				m_tradeWindowUi.OnTradeBeginNotification(*parsed);
				return;
			}
			case kOpcodeTradeSetOfferResponse:
			{
				auto parsed = ParseTradeSetOfferResponsePayload(payload, payloadSize);
				if (!parsed)
				{
					LOG_WARN(Net, "[Engine] TRADE_SET_OFFER_RESPONSE parse failed (size={})", payloadSize);
					return;
				}
				m_tradeWindowUi.OnTradeSetOfferResponse(*parsed);
				return;
			}
			case kOpcodeTradeLockResponse:
			{
				auto parsed = ParseTradeLockResponsePayload(payload, payloadSize);
				if (!parsed)
				{
					LOG_WARN(Net, "[Engine] TRADE_LOCK_RESPONSE parse failed (size={})", payloadSize);
					return;
				}
				m_tradeWindowUi.OnTradeLockResponse(*parsed);
				return;
			}
			case kOpcodeTradeStateUpdateNotification:
			{
				auto parsed = ParseTradeStateUpdateNotificationPayload(payload, payloadSize);
				if (!parsed)
				{
					LOG_WARN(Net, "[Engine] TRADE_STATE_UPDATE_NOTIFICATION parse failed (size={})", payloadSize);
					return;
				}
				m_tradeWindowUi.OnTradeStateUpdate(*parsed);
				return;
			}
			case kOpcodeTradeCommitResponse:
			{
				auto parsed = ParseTradeCommitResponsePayload(payload, payloadSize);
				if (!parsed)
				{
					LOG_WARN(Net, "[Engine] TRADE_COMMIT_RESPONSE parse failed (size={})", payloadSize);
					return;
				}
				m_tradeWindowUi.OnTradeCommitResponse(*parsed);
				return;
			}
			case kOpcodeTradeCancelNotification:
			{
				auto parsed = ParseTradeCancelNotificationPayload(payload, payloadSize);
				if (!parsed)
				{
					LOG_WARN(Net, "[Engine] TRADE_CANCEL_NOTIFICATION parse failed (size={})", payloadSize);
					return;
				}
				m_tradeWindowUi.OnTradeCancelNotification(*parsed);
				return;
			}
			default:
				break;
			}
			if (opcode != kOpcodeChatRelay)
				return;
			auto parsed = ParseChatRelayPayload(payload, payloadSize);
			if (!parsed)
			{
				LOG_WARN(Net, "[Engine] CHAT_RELAY parse failed (size={})", payloadSize);
				return;
			}
			engine::net::ChatChannel ch = engine::net::ChatChannel::Say;
			(void)engine::net::TryDecodeChannelWire(parsed->channel, ch);
			engine::net::ChatMessage msg{};
			msg.timestampUnixMs = parsed->timestampUnixMs;
			msg.channel = ch;
			msg.sender = std::move(parsed->sender);
			msg.text = std::move(parsed->text);
			m_chatUi.PushNetworkLine(msg);
		});

		if (m_worldEditorExe && m_authUi.IsInitialized())
		{
			m_authUi.BypassAuthGateForWorldEditor();
			LOG_INFO(Core, "[Boot] World Editor : saut de l’écran d’authentification");
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
									// STAB.7 fix: VMA is disabled entirely. vmaCreateAllocator / vmaGetAllocatorInfo
									// corrupt the C++ heap on this MSVC build (ABI/CRT mismatch), which later
									// causes SEH 0xC0000005 in unrelated code (e.g. std::mutex::lock).
									// All GPU subsystems already use raw Vulkan allocations, so VMA is not needed.
									m_vmaAllocator = nullptr;
									LOG_INFO(Render, "[Boot] VMA allocator SKIPPED (STAB.7 — all subsystems use raw Vulkan)");

									{
										// M10.4: StagingAllocator with raw Vulkan (no VMA dependency).
										const size_t stagingBudget = m_gpuUploadQueue.GetBudgetBytes();
										if (!m_stagingAllocator.Init(m_vkDeviceContext.GetDevice(), m_vkDeviceContext.GetPhysicalDevice(), stagingBudget))
										{
											LOG_WARN(Render, "[Boot] StagingAllocator init FAILED (budget={} bytes) — streaming GPU uploads disabled", stagingBudget);
										}
										else
										{
											LOG_INFO(Render, "[Boot] StagingAllocator ready (budget={} bytes)", stagingBudget);
										}

										m_pipeline = std::make_unique<engine::render::DeferredPipeline>();

										m_assetRegistry.Init(m_vkDeviceContext.GetDevice(), m_vkDeviceContext.GetPhysicalDevice(), m_vmaAllocator, m_cfg);
										// Upload device-local des meshes de props (buffers GPU rapides via staging)
										// au lieu de buffers HOST_VISIBLE lus par PCIe chaque frame (lag de la foret).
										m_assetRegistry.SetUploadContext(m_vkDeviceContext.GetGraphicsQueue(),
											m_vkDeviceContext.GetGraphicsQueueFamilyIndex());

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
											std::string menuMusicRel = m_cfg.GetString("audio.menu_music_path", "");
											if (menuMusicRel.empty())
											{
												// Même piste que zone_audio.json (auth_music) si la clé n’est pas renseignée.
												menuMusicRel = "audio/Horns_of_the_Fallen_Bastion.mp3";
											}
											bool menuStarted = false;
											if (!m_worldEditorExe && !menuMusicRel.empty())
											{
												const std::filesystem::path menuResolved = engine::platform::FileSystem::ResolveContentPath(m_cfg, menuMusicRel);
												if (engine::platform::FileSystem::Exists(menuResolved))
												{
													menuStarted = m_audioEngine.StartMenuMusic(menuResolved);
													if (menuStarted)
													{
														LOG_INFO(Core, "[Boot] Menu music (miniaudio): {}", menuResolved.string());
														// Applique master × bus Music avant la première frame (Tick sinon ignoré si dt==0).
														(void)m_audioEngine.Tick(1.f / 60.f);
													}
												}
												else
												{
													LOG_WARN(Core, "[Boot] audio.menu_music_path missing on disk: {}", menuResolved.string());
												}
											}
											if (!menuStarted)
											{
												m_audioEngine.SetZone(0);
											}
											if (m_worldEditorExe)
											{
												m_audioEngine.StopMenuMusic();
												m_audioEngine.SetMasterVolume(0.0f);
												LOG_INFO(Core, "[Boot] World Editor: audio muet (pas de musique ni de lecture par défaut).");
											}
										}
										m_decalSystem.Init(m_cfg, m_assetRegistry);

										// Réticule de ciblage au sol (cercles concentriques + cône de
										// vision 120°) : decal orienté projeté sur le terrain — remplace
										// le rendu provisoire ImGui (« cercle de sélection enrichi »).
										// Hauteur sol = m_terrain (source de vérité client) ; yaw/position
										// = état lissé m_remoteSmoothed (repli : snapshot brut 10 Hz).
										{
											auto sampleGround = [this](float worldX, float worldZ) -> float
											{
												return m_terrain.SampleHeightAtWorldXZ(worldX, worldZ);
											};
											auto resolveSmoothed = [this](engine::server::EntityId entityId,
												float& outX, float& outZ, float& outYaw) -> bool
											{
												const auto it = m_remoteSmoothed.find(entityId);
												if (it == m_remoteSmoothed.end() || !it->second.valid)
													return false;
												outX = it->second.x;
												outZ = it->second.z;
												outYaw = it->second.yaw;
												return true;
											};
											if (!m_targetReticle.Init(m_cfg, m_decalSystem, m_assetRegistry,
												sampleGround, resolveSmoothed))
											{
												LOG_WARN(Render, "[Boot] TargetReticleSystem Init FAILED — réticule de ciblage désactivé");
											}
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
											m_authUiInfoLoginTexture = m_assetRegistry.LoadTexture("ui/login/info.png", true);
											if (!m_authUiInfoLoginTexture.IsValid())
											{
												LOG_WARN(Render, "[Boot] Auth UI field info icon not loaded: ui/login/info.png");
											}
											m_authUiInfoRegisterTexture = m_assetRegistry.LoadTexture("ui/register/info.png", true);
											if (!m_authUiInfoRegisterTexture.IsValid())
											{
												LOG_WARN(Render, "[Boot] Auth UI field info icon not loaded: ui/register/info.png");
											}
											m_authFlagFrTexture = m_assetRegistry.LoadTexture("localization/fr/images/drapeau.png", true);
											if (!m_authFlagFrTexture.IsValid())
											{
												LOG_WARN(Render, "[Boot] Auth flag FR not loaded: localization/fr/images/drapeau.png");
											}
											m_authFlagEnTexture = m_assetRegistry.LoadTexture("localization/en/images/drapeau.png", true);
											if (!m_authFlagEnTexture.IsValid())
											{
												LOG_WARN(Render, "[Boot] Auth flag EN not loaded: localization/en/images/drapeau.png");
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
										sceneColorHDRDesc.usage  = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT
										                         | VK_IMAGE_USAGE_SAMPLED_BIT
										                         | VK_IMAGE_USAGE_TRANSFER_SRC_BIT
										                         | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
										m_fgSceneColorHDRId = m_frameGraph.createImage("SceneColor_HDR", sceneColorHDRDesc);
										// M100.14 — Ping-pong target ecrit par WaterPass (ColorWrite) ou par
										// le fallback Water_Passthrough (vkCmdCopyImage TransferDst). Lu en
										// SampledRead par Bloom_Prefilter / Bloom_Combine.
										m_fgSceneColorHDRPostWaterId = m_frameGraph.createImage("SceneColor_HDR_PostWater", sceneColorHDRDesc);
										// M45.2 — SceneColor_HDR_Fogged : cible de la passe VolumetricFog
										// (brouillard volumique + god rays), ou copie passthrough si la
										// passe fog est invalide. Meme desc que PostWater (qui inclut deja
										// TRANSFER_DST pour le passthrough vkCmdCopyImage). Lu en aval par
										// Bloom_Prefilter / Bloom_Combine.
										m_fgSceneColorFoggedId = m_frameGraph.createImage("SceneColor_HDR_Fogged", sceneColorHDRDesc);
										// Nuages — SceneColor_HDR_Clouds : cible de la passe Clouds (composition
										// des nuages volumétriques sur la scène brouillardée). Même desc que
										// Fogged (R16G16B16A16_SFLOAT, extent swapchain). Lu en aval par Bloom
										// (Prefilter / Combine) quand la passe nuages est active.
										m_fgCloudsId = m_frameGraph.createImage("SceneColor_HDR_Clouds", sceneColorHDRDesc);
											// Lot 1 (2026-07-18) — Clouds_Half : cible RÉDUITE de la marche des
											// nuages (raymarch coûteux → 1/2 ou 1/4 de la swapchain, cf.
											// render.clouds.resolution_divider). rgb = couleur pré-multipliée,
											// a = visibilité scène ; upsamplée par Clouds_Composite.
											{
												int cloudDivider = m_cfg.GetInt("render.clouds.resolution_divider", 2);
												if (cloudDivider != 1 && cloudDivider != 2 && cloudDivider != 4)
												{
													LOG_WARN(Render, "render.clouds.resolution_divider={} invalide (1/2/4), repli sur 2", cloudDivider);
													cloudDivider = 2;
												}
												m_cloudsExtentPower = (cloudDivider == 4) ? 2u : (cloudDivider == 2 ? 1u : 0u);
												engine::render::ImageDesc cloudsHalfDesc{};
												cloudsHalfDesc.format           = VK_FORMAT_R16G16B16A16_SFLOAT;
												cloudsHalfDesc.usage            = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
												cloudsHalfDesc.extentScalePower = m_cloudsExtentPower;
												m_fgCloudsHalfId = m_frameGraph.createImage("Clouds_Half", cloudsHalfDesc);
											}
										// M45.3 — SceneColor_HDR_Dof : cible de la passe DepthOfField (bokeh),
										// ou copie passthrough si la passe DoF est invalide. Meme desc que
										// WithBloom/Fogged (qui inclut deja TRANSFER_DST pour le passthrough
										// vkCmdCopyImage). Lu en aval par Tonemap (a la place de WithBloom).
										m_fgSceneColorDofId = m_frameGraph.createImage("SceneColor_HDR_Dof", sceneColorHDRDesc);

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

										// IBL — état ciel figé au boot (statique, 1×). Rempli depuis
										// DayNightCycle::State (déjà Init au-dessus, avant ce point) :
										// gradient zénith/horizon + direction lumière (soleil/lune) +
										// paramètres lunaires. moonDir = -lightDir (convention Engine) ;
										// moonIntensity = 0 le jour, 1 la nuit (la lune n'éclaire que la nuit).
										engine::render::SkyCaptureParams iblSky{};
										{
											const engine::render::DayNightCycle::State& dnIbl = m_dayNight.GetState();
											for (int i = 0; i < 3; ++i)
											{
												iblSky.lightDir[i]     = dnIbl.sunDir[i];
												iblSky.zenithColor[i]  = dnIbl.skyZenith[i];
												iblSky.horizonColor[i] = dnIbl.skyHorizon[i];
												iblSky.moonDir[i]      = dnIbl.moonDir[i];
											}
											iblSky.moonIntensity    = dnIbl.isDaytime ? 0.0f : 1.0f;
											iblSky.moonPhase        = static_cast<float>(dnIbl.moonPhase);
											iblSky.moonIllumination = dnIbl.moonIllumination;
											// Lot 2 (2026-07-18) — la capture IBL suit le même
											// modèle de ciel que sky.frag (client.sky.analytic).
											iblSky.skyModel =
												m_cfg.GetBool("client.sky.analytic", true) ? 1.0f : 0.0f;
										}

										bool pipelineOk = m_pipeline->Init(
										    m_vkDeviceContext.GetDevice(),
										    m_vkDeviceContext.GetPhysicalDevice(),
										    m_vmaAllocator,
										    m_cfg,
										    static_cast<uint32_t>(m_cfg.GetInt("shadows.resolution", 1024)),
										    m_vkSwapchain.GetImageFormat(),
										    m_vkDeviceContext.GetGraphicsQueue(),
										    m_vkDeviceContext.GetGraphicsQueueFamilyIndex(),
										    loadSpirv,
										    iblSky
										);
									LOG_INFO(Core, "[Boot] DeferredPipeline init OK");

									// Suivi jour/nuit IBL : mémorise la direction soleil capturée au
									// boot pour que le 1er frame ne déclenche pas une regen immédiate
									// (la regen se fait uniquement quand le soleil a assez bougé).
									m_iblLastSunDir = { iblSky.lightDir[0], iblSky.lightDir[1], iblSky.lightDir[2] };

									// M45.2 — la passe VolumetricFog est active uniquement si son Init a
									// reussi (shaders presents). Sinon Engine enregistre un passthrough
									// (copie PostWater -> Fogged) pour que Bloom lise une image valide.
									m_volumetricFogReady = m_pipeline->GetVolumetricFogPass().IsValid();

									// M45.3 — la passe DepthOfField est active uniquement si son Init a
									// reussi (shaders presents). Sinon Engine enregistre un passthrough
									// (copie WithBloom -> Dof) pour que Tonemap lise une image valide.
									m_dofReady = m_pipeline->GetDepthOfFieldPass().IsValid();

									// M45.7/M45.8 — GI dynamique DDGI pilotée par NIVEAUX DE QUALITÉ.
									// DÉFAUT quality="off" => s.dynamic=false => aucune allocation, passe
									// DDGI_Update NON enregistrée, LightingPass useDdgi=0 => rendu
									// STRICTEMENT identique au chemin probes statiques. Seuls
									// dynamic-low/dynamic-high allouent le volume et activent la passe.
									//
									// Rétro-compat : si la clé gi.ddgi.quality est ABSENTE mais
									// gi.ddgi.enabled==true, on traite comme DynamicHigh (ancien
									// comportement « DDGI runtime pleine qualité »). Sinon, quality
									// prime (défaut "off").
									m_ddgiEnabled = false;
									engine::render::gi::DdgiQuality ddgiQuality;
									if (!m_cfg.Has("gi.ddgi.quality") && m_cfg.GetBool("gi.ddgi.enabled", false))
									{
										ddgiQuality = engine::render::gi::DdgiQuality::DynamicHigh;
									}
									else
									{
										const std::string q = m_cfg.GetString("gi.ddgi.quality", "off");
										ddgiQuality = engine::render::gi::ParseDdgiQuality(q);
									}
									const engine::render::gi::DdgiQualitySettings ddgiSettings =
										engine::render::gi::ResolveDdgiQuality(ddgiQuality);
									m_ddgiUpdateDivisor = (ddgiSettings.updateDivisor >= 1u) ? ddgiSettings.updateDivisor : 1u;
									m_ddgiIntensity = ddgiSettings.intensity;
									if (ddgiSettings.dynamic)
									{
										engine::render::gi::DdgiGridConfig gridCfg{};
										gridCfg.origin[0]  = static_cast<float>(m_cfg.GetDouble("gi.ddgi.origin_m[0]", 0.0));
										gridCfg.origin[1]  = static_cast<float>(m_cfg.GetDouble("gi.ddgi.origin_m[1]", 0.0));
										gridCfg.origin[2]  = static_cast<float>(m_cfg.GetDouble("gi.ddgi.origin_m[2]", 0.0));
										gridCfg.spacing[0] = static_cast<float>(m_cfg.GetDouble("gi.ddgi.spacing_m[0]", 2.0));
										gridCfg.spacing[1] = static_cast<float>(m_cfg.GetDouble("gi.ddgi.spacing_m[1]", 2.0));
										gridCfg.spacing[2] = static_cast<float>(m_cfg.GetDouble("gi.ddgi.spacing_m[2]", 2.0));
										gridCfg.counts[0]  = static_cast<uint32_t>(m_cfg.GetInt("gi.ddgi.counts[0]", 8));
										gridCfg.counts[1]  = static_cast<uint32_t>(m_cfg.GetInt("gi.ddgi.counts[1]", 8));
										gridCfg.counts[2]  = static_cast<uint32_t>(m_cfg.GetInt("gi.ddgi.counts[2]", 4));
										gridCfg.irradianceTexels = static_cast<uint32_t>(m_cfg.GetInt("gi.ddgi.irradiance_texels", 8));
										gridCfg.visibilityTexels = static_cast<uint32_t>(m_cfg.GetInt("gi.ddgi.visibility_texels", 16));
										m_ddgiVolume.SetConfig(gridCfg);

										std::string ddgiErr;
										if (!m_ddgiVolume.Allocate(m_vkDeviceContext.GetDevice(), m_vkDeviceContext.GetPhysicalDevice(), m_vmaAllocator, ddgiErr))
										{
											LOG_WARN(Render, "[Engine] DDGI desactive : allocation volume KO ({}) -> fallback probes statiques", ddgiErr);
										}
										else
										{
											std::vector<uint32_t> ddgiComp = loadSpirv("shaders/ddgi_update.comp.spv");
											if (ddgiComp.empty()
												|| !m_ddgiUpdatePass.Init(m_vkDeviceContext.GetDevice(), m_vkDeviceContext.GetPhysicalDevice(),
													ddgiComp.data(), ddgiComp.size(),
													VK_NULL_HANDLE))
											{
												LOG_WARN(Render, "[Engine] DDGI desactive : DdgiUpdatePass init KO (shader absent ?) -> fallback probes statiques");
												m_ddgiVolume.Destroy(m_vkDeviceContext.GetDevice(), m_vmaAllocator);
											}
											else
											{
												m_ddgiEnabled = true;
												LOG_INFO(Render, "[Engine] DDGI runtime ACTIF (quality={})",
													engine::render::gi::DdgiQualityName(ddgiQuality));
											}
										}
									}

									// M45.8 — log debug de l'état DDGI (gated par gi.ddgi.debug).
									// PÉRIMÈTRE : le « debug » se limite ici à ce log. La visualisation
									// 3D des sondes (sphères colorées en ImGui) est REPORTÉE — elle
									// nécessite un debug-draw 3D dédié (non couvert par ce ticket).
									if (m_cfg.GetBool("gi.ddgi.debug", false))
									{
										LOG_INFO(Render,
											"[Engine][DDGI debug] quality={} dynamic={} allouee={} probeCount={} divisor={} intensity={:.3f}",
											engine::render::gi::DdgiQualityName(ddgiQuality),
											ddgiSettings.dynamic ? 1 : 0,
											m_ddgiEnabled ? 1 : 0,
											m_ddgiVolume.ProbeCount(),
											m_ddgiUpdateDivisor,
											m_ddgiIntensity);
									}

									// M100 — Task 12 : Terrain Chunk Runtime — drawcall mesh-terrain par
									// chunk avec splat 8-layer. Co-existe avec le legacy TerrainRenderer
									// (skip strict si fichiers chunk absents). Cf.
									// docs/superpowers/specs/2026-05-07-terrain-chunk-runtime-design.md.
									if (pipelineOk)
									{
										std::string camErr;
										if (!CreateTerrainChunkCameraResources(camErr))
										{
											LOG_ERROR(Render, "[Engine] TerrainChunk camera resources init failed: {}", camErr);
										}
										else
										{
											m_terrainChunkRenderer = std::make_unique<engine::render::terrain_chunk::TerrainChunkRenderer>();
											std::string err;
											const std::string contentRoot = m_cfg.GetString("paths.content", "game/data");
											const std::string shaderRoot = contentRoot + "/shaders";
											// `staging` et `assetRegistry` sont passés mais non utilisés en M100
											// (uploads one-shot via VulkanBufferAllocator interne au renderer ;
											// PBR lookups directs via stb_image). Réservés pour évolutions futures.
											const bool ok = m_terrainChunkRenderer->Init(
												m_vkDeviceContext.GetDevice(),
												m_vkDeviceContext.GetPhysicalDevice(),
												m_pipeline->GetGeometryPass().GetRenderPassLoad(),
												m_terrainChunkCameraSetLayout,
												m_vkDeviceContext.GetGraphicsQueue(),
												m_vkDeviceContext.GetGraphicsQueueFamilyIndex(),
												&m_stagingAllocator,
												&m_assetRegistry,
												&m_streamCache,
												m_cfg,
												contentRoot,
												shaderRoot,
												err);
											if (!ok)
											{
												LOG_ERROR(Render, "[Engine] TerrainChunkRenderer init failed: {}", err);
												m_terrainChunkRenderer->Shutdown(m_vkDeviceContext.GetDevice());
												m_terrainChunkRenderer.reset();
												DestroyTerrainChunkCameraResources();
											}
											else
											{
												LOG_INFO(Core, "[Boot] TerrainChunkRenderer init OK");
											}
										}
									}

									// M100.14 — Water render pass FG-intégré.
									// Init WaterMeshGpu (buffer vide, prêt pour Rebuild). Sur les builds
									// STAB.7 (VMA disabled), m_vmaAllocator == nullptr → Init échoue par
									// design : la passe restera invalide et le fallback Water_Passthrough
									// (vkCmdCopyImage) prendra le relais en runtime.
									if (pipelineOk)
									{
										// Crée le command pool long-lived pour les uploads water (RESET entre Rebuilds).
										// Le pool est créé indépendamment du succès de WaterMeshGpu::Init : si VMA
										// est absent (STAB.7) Init échouera et le pool ne sera jamais utilisé,
										// mais il restera valide pour les builds qui réactivent VMA ultérieurement.
										{
											VkCommandPoolCreateInfo poolInfo{};
											poolInfo.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
											poolInfo.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT
											                          | VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
											poolInfo.queueFamilyIndex = m_vkDeviceContext.GetGraphicsQueueFamilyIndex();
											if (vkCreateCommandPool(m_vkDeviceContext.GetDevice(), &poolInfo, nullptr, &m_waterTransferPool) != VK_SUCCESS)
											{
												LOG_WARN(Render, "[Boot] WaterMeshGpu transfer pool creation failed — water rendering will be disabled");
												m_waterTransferPool = VK_NULL_HANDLE;
											}
										}

										if (!m_waterMeshGpu.Init(m_vkDeviceContext.GetDevice(), m_vkDeviceContext.GetPhysicalDevice()))
										{
											LOG_WARN(Render, "[Boot] WaterMeshGpu::Init failed — Water_Passthrough fallback");
										}

										// Init WaterPass : shaders + textures muettes 1×1.
										// normalMap  = (128,128,255,255) = normale plate vers le haut.
										// skybox cube = (15,30,80,255)   = bleu ciel sombre (fallback réfl.).
										std::vector<uint32_t> waterVert = loadSpirv("shaders/water.vert.spv");
										std::vector<uint32_t> waterFrag = loadSpirv("shaders/water.frag.spv");

										if (m_waterTransferPool != VK_NULL_HANDLE
											&& CreateSolidColorTexture(
												m_vkDeviceContext.GetDevice(),
												m_vkDeviceContext.GetPhysicalDevice(),
												m_waterTransferPool,
												m_vkDeviceContext.GetGraphicsQueue(),
												1u, VK_IMAGE_VIEW_TYPE_2D,
												128, 128, 255, 255,
												m_waterNormalMapImg, m_waterNormalMapMem,
												m_waterNormalMapView, m_waterNormalMapSampler))
										{
											LOG_INFO(Render, "[Boot] WaterPass: normalMap muette créée");
										}
										else
										{
											LOG_WARN(Render, "[Boot] WaterPass: création normalMap muette échouée");
										}

										if (m_waterTransferPool != VK_NULL_HANDLE
											&& CreateSolidColorTexture(
												m_vkDeviceContext.GetDevice(),
												m_vkDeviceContext.GetPhysicalDevice(),
												m_waterTransferPool,
												m_vkDeviceContext.GetGraphicsQueue(),
												6u, VK_IMAGE_VIEW_TYPE_CUBE,
												15, 30, 80, 255,
												m_waterSkyboxImg, m_waterSkyboxMem,
												m_waterSkyboxView, m_waterSkyboxSampler))
										{
											LOG_INFO(Render, "[Boot] WaterPass: skybox muette créée");
										}
										else
										{
											LOG_WARN(Render, "[Boot] WaterPass: création skybox muette échouée");
										}

										if (!waterVert.empty() && !waterFrag.empty()
											&& m_waterNormalMapView != VK_NULL_HANDLE
											&& m_waterNormalMapSampler != VK_NULL_HANDLE
											&& m_waterSkyboxView != VK_NULL_HANDLE
											&& m_waterSkyboxSampler != VK_NULL_HANDLE
											&& m_waterMeshGpu.IsInitialized())
										{
											if (m_waterPass.Init(
													m_vkDeviceContext.GetDevice(),
													m_vkDeviceContext.GetPhysicalDevice(),
													VK_FORMAT_R16G16B16A16_SFLOAT,
													waterVert.data(), waterVert.size(),
													waterFrag.data(), waterFrag.size(),
													m_waterNormalMapView, m_waterNormalMapSampler,
													m_waterSkyboxView,    m_waterSkyboxSampler,
													2u))
											{
												LOG_INFO(Render, "[Boot] WaterPass init OK");
											}
											else
											{
												LOG_WARN(Render, "[Boot] WaterPass::Init failed — Water_Passthrough fallback");
											}
										}
										else
										{
											LOG_WARN(Render,
												"[Boot] WaterPass : prerequisites missing (vert={} frag={} normalMap={} skybox={}) — Water_Passthrough fallback",
												!waterVert.empty(),
												!waterFrag.empty(),
												m_waterNormalMapView != VK_NULL_HANDLE,
												m_waterSkyboxView != VK_NULL_HANDLE);
										}
									}

									// Phase 5 Lunar + M38.1 Sky : init du SkyPass (charge sky.vert.spv +
									// sky.frag.spv depuis game/data/shaders, compile pipeline avec push
									// constants etendus pour la phase lunaire). Si l'init echoue
									// (shaders absents, push constants size mismatch, etc.), m_skyPassReady
									// reste false et le rendu fallback sur le clearColor existant.
									if (pipelineOk)
									{
										std::vector<uint32_t> skyVert = loadSpirv("shaders/sky.vert.spv");
										std::vector<uint32_t> skyFrag = loadSpirv("shaders/sky.frag.spv");
										if (!skyVert.empty() && !skyFrag.empty())
										{
											m_skyPassReady = m_skyPass.Init(
												m_vkDeviceContext.GetDevice(),
												m_pipeline->GetGeometryPass().GetRenderPassLoad(),
												/*subpass*/ 0u,
												skyVert.data(), skyVert.size(),
												skyFrag.data(), skyFrag.size());
											if (!m_skyPassReady)
												LOG_WARN(Render, "[Boot] SkyPass init failed -- fallback clearColor");
											else
												LOG_INFO(Render, "[Boot] SkyPass ready (Phase 5 Lunar + M38.1 Sky)");
										}
										else
										{
											LOG_WARN(Render, "[Boot] SkyPass : sky.vert.spv or sky.frag.spv missing -- fallback clearColor");
										}
									}

									// Client jeu : le terrain est toujours tenté (pas de drapeau « enabled ») ;
										// chemin par défaut conventionnel si la clé est absente ou vide.
										if (pipelineOk && !m_worldEditorExe)
										{
											static constexpr const char* kDefaultHeightmapRel = "terrain/heightmap.r16h";
											std::string hmGame = m_cfg.GetString("render.terrain.heightmap", kDefaultHeightmapRel);
											if (hmGame.empty())
												hmGame = kDefaultHeightmapRel;

											const std::string splat = m_cfg.GetString("render.terrain.splatmap", "");
											const std::string grass = m_cfg.GetString("render.terrain.grass_mask", "");
											const std::string hole = m_cfg.GetString("render.terrain.hole_mask", "");
											if (splat.empty())
											{
												LOG_INFO(Core,
													"[Boot] render.terrain.splatmap vide — splat CPU par défaut (herbe) ; "
													"voir docs/world_zone_demo_checklist.md §012.");
											}
											else
											{
												LOG_INFO(Core, "[Boot] render.terrain.splatmap = '{}'", splat);
											}
											if (grass.empty())
											{
												LOG_INFO(Core,
													"[Boot] render.terrain.grass_mask vide — masque herbe nul (ticket 010) ; "
													"aucun fichier GRMS requis.");
											}
											else
											{
												LOG_INFO(Core, "[Boot] render.terrain.grass_mask = '{}'", grass);
											}
											const std::vector<std::string> cliffPaths;
											auto loadFnTerrain = [this](const char* p) { return LoadTerrainSpirvWords(p); };
											if (m_terrain.Init(
													m_vkDeviceContext.GetDevice(),
													m_vkDeviceContext.GetPhysicalDevice(),
													m_cfg,
													hmGame,
													splat,
													grass,
													hole,
													cliffPaths,
													VK_FORMAT_R8G8B8A8_SRGB,
													VK_FORMAT_A2B10G10R10_UNORM_PACK32,
													VK_FORMAT_R8G8B8A8_UNORM,
													VK_FORMAT_R16G16_SFLOAT,
													VK_FORMAT_D32_SFLOAT,
													m_vkDeviceContext.GetGraphicsQueue(),
													m_vkDeviceContext.GetGraphicsQueueFamilyIndex(),
													loadFnTerrain))
											{
												LOG_INFO(Core, "[Boot] Terrain (jeu) initialisé: {}", hmGame);
											}
											else
											{
												LOG_WARN(Render, "[Boot] TerrainRenderer::Init (jeu) échec — vérifie le fichier sous paths.content : {}", hmGame);
											}
										}

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
													// --- Second atlas: valeurs de champs (ex. Morpheus.ttf) ---
													const std::string valueFontPath = m_cfg.GetString("render.auth_ui.value_font_path", "");
													if (!valueFontPath.empty() && ttfFragPtr != nullptr)
													{
														std::vector<uint8_t> valueFontBytes = engine::platform::FileSystem::ReadAllBytesContent(m_cfg, valueFontPath);
														if (!valueFontBytes.empty())
														{
															const float valueFontPx = static_cast<float>(std::clamp<int64_t>(
																m_cfg.GetInt("render.auth_ui.value_font_pixel_height", 24), 12, 96));
															if (m_authGlyphPass.UploadValueFontFromTtf(
																m_vkDeviceContext.GetDevice(),
																m_vkDeviceContext.GetPhysicalDevice(),
																m_vkDeviceContext.GetGraphicsQueue(),
																m_vkDeviceContext.GetGraphicsQueueFamilyIndex(),
																valueFontBytes.data(),
																valueFontBytes.size(),
																valueFontPx))
															{
																LOG_INFO(Render, "[Boot] Auth UI value font loaded: {}", valueFontPath);
															}
															else
															{
																LOG_WARN(Render, "[Boot] Auth UI value font upload failed: {}", valueFontPath);
															}
														}
														else
														{
															LOG_WARN(Render, "[Boot] Auth UI value font file missing or empty: {}", valueFontPath);
														}
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
												// C3 simplifie : clear color = couleur horizon ciel calculee par
												// DayNightCycle (au lieu d'un gris constant 0.15/0.15/0.18). Donne
												// un ciel dynamique qui change de couleur selon l'heure (bleu jour,
												// orange dawn/dusk, sombre nuit) sans avoir besoin d'une SkyboxPass
												// Vulkan complete. A remplacer par un vrai sky shader plus tard.
												// Log periodique pour verifier que la valeur change quand l'utilisateur
												// modifie le slider Heure dans le panneau Atmosphere.
												const engine::render::DayNightCycle::State& dn = m_dayNight.GetState();
												static float s_lastLoggedTime = -999.f;
												if (std::fabs(dn.timeOfDay - s_lastLoggedTime) > 0.05f)
												{
													s_lastLoggedTime = dn.timeOfDay;
													LOG_INFO(Render, "[Atmosphere] clear color update: time={:.2f}h skyHorizon=({:.2f},{:.2f},{:.2f}) lightColor=({:.2f},{:.2f},{:.2f}) isDay={}",
														dn.timeOfDay,
														dn.skyHorizon[0], dn.skyHorizon[1], dn.skyHorizon[2],
														dn.lightColor[0], dn.lightColor[1], dn.lightColor[2],
														dn.isDaytime ? 1 : 0);
												}
												VkClearColorValue clearColor = {
													{ dn.skyHorizon[0], dn.skyHorizon[1], dn.skyHorizon[2], 1.0f }
												};
												VkImageSubresourceRange range = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
												vkCmdClearColorImage(cmd, img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clearColor, 1, &range);
											});

										// Etape 2 vue 3eme personne : on charge directement le mesh placeholder
										// avatar (cube 0.5x1.8x0.5 m) au boot. Avant, on chargeait
										// 'meshes/test.mesh' (un simple triangle), utilise comme proxy de scene.
										// L'avatar est positionne a la cible orbitale via out.objectModelMatrix
										// (cf. branche !m_editorMode du Update).
										m_geometryMeshHandle = m_assetRegistry.LoadMesh("meshes/avatar_placeholder.mesh");

										// Materiaux PBR de l'avatar (#5 + multi-materiaux) : un personnage
										// modulaire (ex. Male_Ranger) possede DEUX materiaux glTF distincts :
										//   - l'habit  (materiau "MI_Ranger")      -> textures T_Ranger
										//   - la peau  (materiau "MI_Regular_Male") -> textures T_Regular_Male
										// Le mesh est fusionne en un seul buffer mais conserve ses sous-maillages
										// (SkinnedSubMesh : plage d'indices + nom de materiau). On cree donc ICI
										// deux materiaux bindless (habit + peau) et on memorise quels noms de
										// materiau glTF doivent recevoir la peau ; au draw, chaque sous-maillage
										// est dessine avec le bon index materiau (cf. SkinnedRenderer::Record).
										//
										// Chemins config-driven (client.character_creation.*) avec repli sur
										// l'ancien schema skin_* (#5) puis sur des chemins par defaut. BaseColor
										// en sRGB ; Normal/ORM lineaires. ORM peau optionnel : repli sur l'ORM
										// 1x1 par defaut (AO=1, rough=0.5, metal=0) du MaterialDescriptorCache.
										const std::string outfitBc  = m_cfg.GetString("client.character_creation.outfit_basecolor",
											m_cfg.GetString("client.character_creation.skin_basecolor", "textures/characters/humains/T_Ranger_BaseColor.png"));
										const std::string outfitNrm = m_cfg.GetString("client.character_creation.outfit_normal",
											m_cfg.GetString("client.character_creation.skin_normal", "textures/characters/humains/T_Ranger_Normal.png"));
										const std::string outfitOrm = m_cfg.GetString("client.character_creation.outfit_orm",
											m_cfg.GetString("client.character_creation.skin_orm", "textures/characters/humains/T_Ranger_ORM.png"));
										const std::string bodyBc  = m_cfg.GetString("client.character_creation.body_basecolor", "textures/characters/humains/T_Regular_Male_BaseColor.png");
										const std::string bodyNrm = m_cfg.GetString("client.character_creation.body_normal",    "textures/characters/humains/T_Regular_Male_Normal.png");
										const std::string bodyOrm = m_cfg.GetString("client.character_creation.body_orm",       "");
										// Sélecteur de genre : on cree DEUX materiaux de peau (male + femelle) au
										// boot pour la bascule live (apercu de creation) et le rendu in-world
										// immediat sans rechargement. Les chemins femelle derivent des chemins
										// male (Male_ -> Female_, ex. T_Regular_Male_* -> T_Regular_Female_*), en
										// miroir du swap de mesh (Male_Ranger -> Female_Ranger). Le mesh femelle
										// porte le materiau "MI_Regular_Female", le male "MI_Regular_Male" : les
										// DEUX sont listes par defaut dans body_material_names pour router la peau
										// quel que soit le genre.
										auto deriveFemalePath = [](std::string p) {
											std::string::size_type i = 0;
											while ((i = p.find("Male_", i)) != std::string::npos) { p.replace(i, 5, "Female_"); i += 7; }
											return p;
										};
										const std::string bodyBcF  = deriveFemalePath(bodyBc);
										const std::string bodyNrmF = deriveFemalePath(bodyNrm);
										const std::string bodyOrmF = bodyOrm.empty() ? std::string() : deriveFemalePath(bodyOrm);
										// Teinte foncee (skinColorIdx=1) : derive le chemin BaseColor en
										// inserant "_Dark" avant "_BaseColor" (ex. T_Regular_Male_BaseColor.png
										// -> T_Regular_Male_Dark_BaseColor.png). Normal/ORM partages avec la
										// teinte claire (la teinte ne change que l'albedo). Repli sur la teinte
										// claire au draw si la texture foncee est absente (id 0).
										auto deriveDarkPath = [](std::string p) {
											const std::string needle = "_BaseColor";
											const std::string::size_type i = p.rfind(needle);
											if (i != std::string::npos) p.replace(i, needle.size(), "_Dark_BaseColor");
											return p;
										};
										const std::string bodyBcDark  = deriveDarkPath(bodyBc);
										const std::string bodyBcDarkF = deriveDarkPath(bodyBcF);
										// Noms de materiaux glTF qui recoivent la PEAU (separes par des virgules) ;
										// tout autre nom recoit l'habit. Defaut : peau male ET femelle.
										m_avatarBodyMaterialNames = SplitCsv(
											m_cfg.GetString("client.character_creation.body_material_names", "MI_Regular_Male,MI_Regular_Female"));
										// Depth bias peau (anti z-fight peau/habit, « parait double ») : reglable
										// a chaud via config (pas de rebuild pour ajuster). 0 = desactive.
										m_avatarSkinDepthBiasConstant = static_cast<float>(
											m_cfg.GetDouble("client.character_creation.skin_depth_bias_constant", 4.0));
										m_avatarSkinDepthBiasSlope = static_cast<float>(
											m_cfg.GetDouble("client.character_creation.skin_depth_bias_slope", 4.0));

										m_avatarSkinTextureHandle = m_assetRegistry.LoadTexture(outfitBc, /*useSrgb*/ true);
										if (!m_avatarSkinTextureHandle.IsValid())
											m_avatarSkinTextureHandle = m_assetRegistry.LoadTexture("textures/avatar_skin.texr", true);
										if (m_avatarSkinTextureHandle.IsValid() && m_pipeline)
										{
											auto& materialCache = m_pipeline->GetMaterialDescriptorCache();
											if (materialCache.IsValid())
											{
												// Materiau HABIT (defaut pour tous les sous-maillages non-peau).
												engine::render::Material outfitMat{};
												outfitMat.baseColor = m_avatarSkinTextureHandle;
												const engine::render::TextureHandle outfitNrmTex = m_assetRegistry.LoadTexture(outfitNrm, /*useSrgb*/ false);
												const engine::render::TextureHandle outfitOrmTex = m_assetRegistry.LoadTexture(outfitOrm, /*useSrgb*/ false);
												if (outfitNrmTex.IsValid()) outfitMat.normal = outfitNrmTex;
												if (outfitOrmTex.IsValid()) outfitMat.orm = outfitOrmTex;
												m_avatarMaterialId = materialCache.CreateMaterial(m_vkDeviceContext.GetDevice(), outfitMat);

												// Fabrique un materiau de peau depuis un triplet bc/nrm/orm. Renvoie 0
												// (= retombe sur l'habit au draw) si la BaseColor est absente.
												auto makeBodyMaterial = [&](const std::string& bc, const std::string& nrm,
												                            const std::string& orm) -> uint32_t {
													const engine::render::TextureHandle bcTex = m_assetRegistry.LoadTexture(bc, /*useSrgb*/ true);
													if (!bcTex.IsValid()) return 0u;
													engine::render::Material mat{};
													mat.baseColor = bcTex;
													const engine::render::TextureHandle nrmTex = m_assetRegistry.LoadTexture(nrm, /*useSrgb*/ false);
													if (nrmTex.IsValid()) mat.normal = nrmTex;
													if (!orm.empty())
													{
														const engine::render::TextureHandle ormTex = m_assetRegistry.LoadTexture(orm, /*useSrgb*/ false);
														if (ormTex.IsValid()) mat.orm = ormTex;
													}
													return materialCache.CreateMaterial(m_vkDeviceContext.GetDevice(), mat);
												};
												m_avatarBodyMaterialIdMale   = makeBodyMaterial(bodyBc,  bodyNrm,  bodyOrm);
												m_avatarBodyMaterialIdFemale = makeBodyMaterial(bodyBcF, bodyNrmF, bodyOrmF);
												// Teinte foncee : 2 materiaux de peau supplementaires (male/femelle).
												// 0 si la texture _Dark est absente -> le draw retombe sur la teinte claire.
												m_avatarBodyMaterialIdMaleDark   = makeBodyMaterial(bodyBcDark,  bodyNrm,  bodyOrm);
												m_avatarBodyMaterialIdFemaleDark = makeBodyMaterial(bodyBcDarkF, bodyNrmF, bodyOrmF);
												LOG_INFO(Render, "[Avatar] Materiaux PBR habit(id={}) peau male(id={}) femelle(id={}) male_dark(id={}) femelle_dark(id={})",
													m_avatarMaterialId, m_avatarBodyMaterialIdMale, m_avatarBodyMaterialIdFemale,
													m_avatarBodyMaterialIdMaleDark, m_avatarBodyMaterialIdFemaleDark);
											}
										}

										// Sous-projet A (Task 15) -- Init runtime skinned avatar (Y Bot Mixamo).
										// Tente une seule fois au boot apres le materiel avatar. Si l'une
										// des etapes echoue (shaders absents, mat cache pas pret, .glb introuvable,
										// upload GPU KO), log warning + retombe sur le cube placeholder (cf. branche
										// per-frame dans la lambda FrameGraph "Geometry" plus bas).
										//
										// Effet de bord : alloue 1 VkRenderPass + 1 VkPipelineLayout + 1
										// VkPipeline + 1 VkDescriptorPool + 2 VkBuffer (bone SSBO + model
										// instance) cote SkinnedRenderer + 2 VkBuffer/VkDeviceMemory cote mesh
										// (vertex/index). Tout est libere au Shutdown (cf. bloc Destroy plus bas,
										// juste avant m_pipeline->Destroy).
										if (!m_skinnedAvatarReady && m_pipeline)
										{
											auto& materialCache = m_pipeline->GetMaterialDescriptorCache();
											const std::vector<uint32_t> skinnedVertSpv = loadSpirv("shaders/skinned_gbuffer.vert.spv");
											const std::vector<uint32_t> skinnedFragSpv = loadSpirv("shaders/gbuffer_geometry.frag.spv");
											if (!skinnedVertSpv.empty() && !skinnedFragSpv.empty() && materialCache.IsValid())
											{
												// Memes formats GBuffer / depth que DeferredPipeline.cpp:135-140 :
												// formatA = R8G8B8A8_SRGB (albedo), formatB = A2B10G10R10 (normal),
												// formatC = R8G8B8A8_UNORM (ORM), formatVelocity = R16G16_SFLOAT,
												// depthFormat = D32_SFLOAT. Necessaire pour que le pipeline skinne
												// soit compatible avec le render pass LOAD de GeometryPass (que nous
												// utiliserons via RecordTerrainChunkBatch pour eviter de nested-passer).
												const bool skinnedInitOk = m_skinnedRenderer.Init(
													m_vkDeviceContext.GetDevice(),
													m_vkDeviceContext.GetPhysicalDevice(),
													VK_FORMAT_R8G8B8A8_SRGB,
													VK_FORMAT_A2B10G10R10_UNORM_PACK32,
													VK_FORMAT_R8G8B8A8_UNORM,
													VK_FORMAT_R16G16_SFLOAT,
													VK_FORMAT_D32_SFLOAT,
													skinnedVertSpv.data(), skinnedVertSpv.size(),
													skinnedFragSpv.data(), skinnedFragSpv.size(),
													materialCache.GetLayout(),
													/*maxBonesPerSkeleton*/ 256u);
												if (skinnedInitOk)
												{
													// Sous-projet C MVP — Charge les 3 races MVP (humains, nains, orc)
													// dans m_raceMeshes. Chaque race partage le meme jeu de 7 clips
													// d'animation (Idle/StartWalking/WalkBack/Run/Jump/Fall/Land)
													// charges en animation-only depuis y_bot_* et retargetes par nom
													// de bone sur le squelette de la race.
													//
													// Lookup du meshPath via un CharacterCreationPresenter local
													// (instanciation legere : juste un parse de races.json, pas de
													// pipeline GPU). Cela evite d'ajouter une dependance permanente
													// dans Engine entre Engine et CharacterCreationPresenter.
													const std::string contentRoot = m_cfg.GetString("paths.content", "game/data");
													// IDs doivent matcher exactement les race_str de races.json :
													// "humains", "nains", "orcs" (au pluriel). Note : le dossier disk
													// game/data/models/avatars/orc/orc.glb est singulier (heritage de
													// l'upload utilisateur inbox/orc/), mais c'est OK car le meshPath
													// dans races.json pointe vers le bon chemin singulier — seule la
													// cle de map (race_str) doit etre pluriel pour matcher la DB.
													constexpr const char* kMvpRaces[] = { "humains", "nains", "orcs" };

													engine::client::CharacterCreationPresenter racesPresenter;
													racesPresenter.Init(m_cfg);

													// meshKey : clef de stockage dans m_raceMeshes ("raceId|gender",
													// ex. "humains|female"). raceId reste utilise pour les logs et la
													// detection de rig. Cf. RaceMeshKey() / GetRaceMesh().
													auto loadOneRace = [&](const std::string& meshKey, const std::string& raceId,
													                       const std::string& meshPath,
													                       float importScale, float importRotXDeg) -> bool {
														const std::string fullMeshPath = contentRoot + "/" + meshPath;
														auto loaded = engine::render::skinned::SkinnedMeshLoader::Load(
															m_vkDeviceContext.GetDevice(),
															m_vkDeviceContext.GetPhysicalDevice(),
															fullMeshPath);
														if (!loaded) {
															LOG_WARN(Render, "[Engine] Race '{}' : mesh load FAIL '{}'", raceId, fullMeshPath);
															return false;
														}
														// Renomme le clip de marche embarque dans le mesh (Mixamo
														// exporte tous ses clips sous le nom "mixamo.com") en "Walk".
														for (auto& c : loaded->clips) {
															if (c.name == "mixamo.com") { c.name = "Walk"; break; }
														}
														// Detection du rig : UE5 (pelvis, pas de mixamorig:*) vs Mixamo.
														// Bascule la source d'animations sans casser les races Mixamo (additif + repli).
														const bool isUe5Rig =
															loaded->skeleton.FindBoneIndex("pelvis") >= 0 &&
															loaded->skeleton.FindBoneIndex("mixamorig:Hips") < 0;
														if (isUe5Rig)
														{
															// Migration UE5 — clips depuis la library UE5 (45 takes "Armature|<Clip>"),
															// copies vers les noms de role attendus par la state machine. Retarget par
															// nom d'os (memes os que le corps UE5, cf. SkinnedMeshLoaderTests).
															const std::string ue5LibPath = contentRoot +
																"/models/animations/humanoid_base/Humanoid_Base_Standard/Humanoid_Base_Standard.glb";
															auto ue5Clips = engine::render::skinned::SkinnedMeshLoader::LoadClipsAnimOnly(
																ue5LibPath, loaded->skeleton);
															auto addRole = [&](const char* role, const char* ualName) {
																const std::string want = std::string("Armature|") + ualName;
																for (const auto& c : ue5Clips) {
																	if (c.name == want && c.duration > 0.0f) {
																		engine::render::skinned::AnimationClip copy = c;
																		copy.name = role;
																		loaded->clips.push_back(std::move(copy));
																		return;
																	}
																}
																LOG_WARN(Render, "[Engine] UE5 race '{}' : take '{}' introuvable (role '{}')", raceId, ualName, role);
															};
															addRole("Idle", "Idle_Loop");
															addRole("Walk", "Walk_Loop");
															addRole("StartWalking", "Walk_Loop");
															addRole("WalkBack", "Walk_Loop");
															addRole("Run", "Jog_Fwd_Loop");
															addRole("Sprint", "Sprint_Loop");
															addRole("CrouchIdle", "Crouch_Idle_Loop");
															addRole("CrouchWalk", "Crouch_Fwd_Loop");
															addRole("Roll", "Roll");
															addRole("Dance", "Dance_Loop");
															addRole("Sit", "Sitting_Idle_Loop");
															addRole("Talk", "Idle_Talking_Loop");
															addRole("Torch", "Idle_Torch_Loop");
															addRole("Kneel", "Fixing_Kneeling");
															addRole("SitTalk", "Sitting_Talking_Loop");
															addRole("Push", "Push_Loop");
															addRole("Attack", "Sword_Attack");
															addRole("Cast", "Spell_Simple_Enter");
															addRole("CastShoot", "Spell_Simple_Shoot");
															addRole("CastExit", "Spell_Simple_Exit");
															addRole("Interact", "Interact");
															addRole("Punch", "Punch_Jab");
															addRole("PunchCross", "Punch_Cross");
															addRole("Jump", "Jump_Start");
															addRole("Fall", "Jump_Loop");
															addRole("Land", "Jump_Land");
															addRole("SwimIdle", "Swim_Idle_Loop");
															addRole("SwimForward", "Swim_Fwd_Loop");

															// Expose AUSSI tous les autres clips UE5 par leur nom brut (sans
															// prefixe "Armature|"), disponibles pour les futurs systemes
															// (sprint, crouch, roll, combat, emotes, nage...). Exclus : armes
															// a feu (pas dans le jeu) + conduite (pas de vehicule) + A_TPose
															// (pose de reference, pas une anim de jeu). Aucun declencheur ici :
															// les clips sont seulement rendus accessibles via FindClip(<nom>).
															auto isExcludedUe5Clip = [](const std::string& n) {
																return n == "Pistol_Aim_Up" || n == "Pistol_Aim_Neutral"
																	|| n == "Pistol_Aim_Down" || n == "Pistol_Idle_Loop"
																	|| n == "Pistol_Reload" || n == "Pistol_Shoot"
																	|| n == "Driving_Loop" || n == "A_TPose";
															};
															int ue5Exposed = 0;
															for (const auto& c : ue5Clips) {
																if (c.duration <= 0.0f) continue;
																std::string name = c.name;
																const auto bar = name.rfind('|');
																if (bar != std::string::npos) name = name.substr(bar + 1);
																if (isExcludedUe5Clip(name)) continue;
																bool exists = false;
																for (const auto& existing : loaded->clips) {
																	if (existing.name == name) { exists = true; break; }
																}
																if (exists) continue;
																engine::render::skinned::AnimationClip copy = c;
																copy.name = name;
																loaded->clips.push_back(std::move(copy));
																++ue5Exposed;
															}
															LOG_INFO(Render, "[Engine] UE5 race '{}' : {} clips additionnels exposes (total {})",
																raceId, ue5Exposed, loaded->clips.size());
														}
														else
														{
														// Retarget les clips d'anim partages sur le squelette de cette
														// race. Convention herite de B.1 (LoadClipsAnimOnly matche par
														// nom de bone mixamorig:*). Si la race a un squelette
														// non-Mixamo, ces calls renvoient des clips vides -> animation
														// identite (la state machine fallback gracieusement).
														auto loadAnimOnly = [&](const std::string& path, const char* renameTo) -> bool {
															auto clipsLoaded = engine::render::skinned::SkinnedMeshLoader::LoadClipsAnimOnly(
																path, loaded->skeleton);
															for (auto& c : clipsLoaded) {
																if (c.duration > 0.0f && c.name == "mixamo.com") {
																	c.name = renameTo;
																	loaded->clips.push_back(std::move(c));
																	return true;
																}
															}
															return false;
														};
														const std::string idlePath      = contentRoot + "/models/avatars/y_bot_idle/y_bot_idle.glb";
														const std::string startWalkPath = contentRoot + "/models/avatars/y_bot_start_walking/y_bot_start_walking.glb";
														const std::string walkBackPath  = contentRoot + "/models/avatars/y_bot_walk_back/y_bot_walk_back.glb";
														const std::string runPath       = contentRoot + "/models/avatars/y_bot_run/y_bot_run.glb";
														const std::string jumpPath      = contentRoot + "/models/avatars/y_bot_jump/y_bot_jump.glb";
														const std::string fallPath      = contentRoot + "/models/avatars/y_bot_fall/y_bot_fall.glb";
														const std::string landPath      = contentRoot + "/models/avatars/y_bot_land/y_bot_land.glb";
														loadAnimOnly(idlePath, "Idle");
														loadAnimOnly(startWalkPath, "StartWalking");
														loadAnimOnly(walkBackPath, "WalkBack");
														loadAnimOnly(runPath, "Run");
														loadAnimOnly(jumpPath, "Jump");
														loadAnimOnly(fallPath, "Fall");
														loadAnimOnly(landPath, "Land");
														}
														// Migration UE5 — transform d'import (echelle/orientation) reglable via races.json.
														{
															const float th = importRotXDeg * 3.14159265358979f / 180.0f;
															const float cx = std::cos(th);
															const float sx = std::sin(th);
															engine::math::Mat4 imp;
															imp.m[0]  = importScale;
															imp.m[5]  = importScale * cx;
															imp.m[6]  = importScale * sx;
															imp.m[9]  = importScale * -sx;
															imp.m[10] = importScale * cx;
															loaded->importTransform = imp;
														}
														LOG_INFO(Render, "[Engine] Race '{}' loaded as '{}' ({} bones, {} clips) from '{}'",
															raceId, meshKey, loaded->skeleton.bones.size(), loaded->clips.size(), fullMeshPath);
														m_raceMeshes.emplace(meshKey, std::move(*loaded));
														return true;
													};

													// Charge les 3 races MVP, pour les DEUX genres (selecteur de genre).
													// Le male est toujours charge sous "raceId|male". Le femelle (chemin
													// derive Male_ -> Female_, ex. Female_Ranger) est charge sous
													// "raceId|female" UNIQUEMENT si une variante distincte existe ET se
													// charge ; sinon GetRaceMesh(race,"female") retombe sur le male.
													auto deriveFemaleMesh = [](std::string p) {
														std::string::size_type gp = 0;
														while ((gp = p.find("Male_", gp)) != std::string::npos) { p.replace(gp, 5, "Female_"); gp += 7; }
														return p;
													};
													for (const char* raceId : kMvpRaces) {
														const auto& races = racesPresenter.GetRaces();
														const engine::client::RaceDefinition* def = nullptr;
														for (const auto& r : races) if (r.id == raceId) { def = &r; break; }
														if (!def || def->meshPath.empty()) {
															LOG_WARN(Render, "[Engine] Race '{}' : RaceDefinition introuvable ou meshPath vide", raceId);
															continue;
														}
														const std::string maleMesh = def->meshPath;
														loadOneRace(RaceMeshKey(raceId, "male"), raceId, maleMesh, def->importScale, def->importRotXDeg);
														const std::string femaleMesh = deriveFemaleMesh(maleMesh);
														if (femaleMesh != maleMesh) {
															loadOneRace(RaceMeshKey(raceId, "female"), raceId, femaleMesh, def->importScale, def->importRotXDeg);
														}
													}
													// Genre courant depuis config (mergee avec le fichier de persistance au boot).
													m_avatarGender = m_cfg.GetString("client.character_creation.gender", "male");
													if (m_avatarGender != "male" && m_avatarGender != "female")
														m_avatarGender = "male";

													// Combat SP1 — catalogue d'apparences de creatures (non bloquant :
													// catalogue absent/corrompu = mobs rendus en fallback humains).
													m_creatureCatalog.Init(m_cfg);
													// Validation v12 (wire v13) — marqueurs cimetière/auberge.
													LoadRespawnMarkers();
													// Combat SP3 — catalogue d'affichage des kits de sorts (non
													// bloquant : absent = barre d'action masquée).
													m_spellCatalog.Init(m_cfg);
													// SP-D — catalogue des compétences par-classe (non bloquant :
													// catalogue vide si les fichiers sont absents).
													if (m_classSkillCatalog.Init(m_cfg))
													{
														LOG_INFO(Core, "[Boot] ClassSkillCatalog chargé ({} classe(s))",
															m_classSkillCatalog.ClassCount());
													}
													else
													{
														LOG_WARN(Core, "[Boot] ClassSkillCatalog init échoué — arbre de compétences désactivé");
													}

													// Pointe m_currentSkinnedMesh vers le mesh humain par defaut (le
													// perso n'est pas encore "in world" avant EnterWorld, mais Engine
													// peut etre questionne sur ce mesh pour des previews/tests).
													engine::render::skinned::SkinnedMesh* defaultMesh = GetRaceMesh("humains", m_avatarGender);
													if (defaultMesh != nullptr) {
														m_currentSkinnedMesh = defaultMesh;
														m_playerAnimStartTime = std::chrono::steady_clock::now();
														m_avatarLocoStateEnterTime = m_playerAnimStartTime;
														m_skinnedAvatarReady = true;
														LOG_INFO(Render, "[Engine] Default avatar = humains ({} bones, {} clips)",
															m_currentSkinnedMesh->skeleton.bones.size(),
															m_currentSkinnedMesh->clips.size());

														// Etat initial : Idle. On lance la premiere animation explicitement
														// pour eviter d'afficher la pose bind au tout premier frame avant
														// que la state machine n'enclenche un Play (cf. lambda Geometry
														// qui sample m_avatarCrossfade.Sample(skel, nowSec)).
														// Defensif : si Idle a echoue au load, on garde l'init existante
														// (m_avatarLocoStateEnterTime / m_avatarLocoState = Idle deja faits
														// plus haut), la state machine fera son fallback (Task 11).
														const engine::render::skinned::AnimationClip* idleClip = m_currentSkinnedMesh->FindClip("Idle");
														if (idleClip) {
															const float nowSec = EngineNowSec();
															m_avatarCrossfade.Play(*idleClip, /*loops=*/ true, nowSec);
															m_avatarLocoStateEnterTime = std::chrono::steady_clock::now();
															m_avatarLocoState = AvatarLocomotionState::Idle;
														}
													} else {
														LOG_WARN(Render, "[Engine] Race humains absente du m_raceMeshes -- avatar fallback cube");
														// Aucun mesh humain : libere le pipeline (Destroy idempotent).
														m_skinnedRenderer.Destroy(m_vkDeviceContext.GetDevice());
													}
												}
												else
												{
													LOG_WARN(Render, "[Engine] SkinnedRenderer::Init failed; fallback cube placeholder");
												}
											}
											else
											{
												LOG_WARN(Render, "[Engine] Skinned shaders not loaded or material cache not ready; fallback cube placeholder");
											}
										}

										// Sous-projet B.1 (Task 9) — Bind TerrainCollider sur le TerrainRenderer
										// et instancie le CharacterController du joueur. Doit imperativement
										// arriver APRES `m_terrain.Init` (cf. ~ligne 3538 plus haut) pour que
										// `GroundHeightAt` retourne une altitude reelle (sinon fallback 0.0,
										// le perso spawn enterre / suspend selon le sol).
										//
										// Le CharacterController est lui-meme cree avec une Config lue depuis
										// `config.json` (cle `player.movement.*`) avec des defaults sensibles
										// si les cles manquent (cas du config.json par defaut). L'initialisation
										// du membre se fait par move-assignment d'un temporaire — `CharacterController`
										// n'a pas d'operations move/copy explicites mais ses membres sont tous
										// trivialement assignables (Vec3 / float / enum / bool) donc l'operator=
										// implicite est suffisant.
										//
										// La position de spawn est posee au centre du terrain (0,0 en monde) en
										// echantillonnant le sol + half-capsule (0.9 m = height/2) pour eviter
										// que la sphere du bas de la capsule traverse le sol au premier sweep.
										{
											// Phase 2 (chantier C) — construire les IHeightField et les
											// injecter dans le collider. Chunk (terrain.bin residents,
											// grille 256) prioritaire si resident ; repli heightmap
											// (m_terrain) sinon. m_streamCache (Init ~ligne 1365) et
											// m_terrain (Init ~ligne 3538) deja initialises a ce point.
											m_heightmapField = std::make_unique<engine::world::terrain::HeightmapHeightField>(&m_terrain);
											m_chunkField     = std::make_unique<engine::world::terrain::ChunkHeightField>(&m_streamCache, &m_cfg);
											m_terrainCollider.BindHeightFields(m_chunkField.get(), m_heightmapField.get());

											// Nage (v1) : eau-test procedurale. La zone demo est plate et sans
											// eau ; on pose un BASSIN DE TEST au-dessus du sol pour valider la
											// mecanique de nage (a remplacer par une vraie etendue d'eau via le
											// pipeline water.bin / level-design). Desactivable : world.test_water.enabled=false.
											// Nage : tenter d'abord une VRAIE eau (instances/water.bin via StreamCache) ;
											// repli sur l'eau-test procedurale si absente (zone demo plate, sans water.bin).
											if (auto realWater = m_streamCache.LoadWater(m_cfg, ""))
											{
												m_clientWaterScene = realWater;
												m_waterClientSceneDirty = true;
												LOG_INFO(Render, "[Nage] water.bin charge ({} lac(s))", static_cast<int>(realWater->lakes.size()));
											}
											else if (m_cfg.GetBool("world.test_water.enabled", true))
											{
												const float cx = static_cast<float>(m_cfg.GetDouble("world.test_water.center_x", 25.0));
												const float cz = static_cast<float>(m_cfg.GetDouble("world.test_water.center_z", 0.0));
												const float half = static_cast<float>(m_cfg.GetDouble("world.test_water.half_size_m", 15.0));
												const float depth = static_cast<float>(m_cfg.GetDouble("world.test_water.depth_m", 2.5));
												const float lvl = m_terrainCollider.GroundHeightAt(cx, cz) + depth;
												auto waterScene = std::make_shared<engine::world::water::WaterScene>();
												engine::world::water::LakeInstance lake;
												lake.name = "test_pool";
												lake.waterLevelY = lvl;
												// Polygone XZ (SW->SE->NE->NW). Si la surface est cullee a l'affichage,
												// inverser l'ordre (winding) ; la nage (QueryWater) marche dans les 2 cas.
												lake.polygon = {
													engine::math::Vec3{ cx - half, lvl, cz - half },
													engine::math::Vec3{ cx + half, lvl, cz - half },
													engine::math::Vec3{ cx + half, lvl, cz + half },
													engine::math::Vec3{ cx - half, lvl, cz + half },
												};
												waterScene->lakes.push_back(std::move(lake));
												m_clientWaterScene = std::move(waterScene);
												m_waterClientSceneDirty = true;
												LOG_INFO(Render, "[Nage] Eau-test posee (centre=({:.1f},{:.1f}) half={:.1f} niveauY={:.2f})", cx, cz, half, lvl);
											}
											m_terrainCollider.BindWater(m_clientWaterScene.get());
											// REBUILD SYNCHRONE du mesh d'eau ICI (avant la 1ere construction du
											// FrameGraph). Sinon le gating de la passe water voit m_waterMeshGpu.IsValid()
											// == false (le rebuild paresseux n'arrive qu'APRES, plus tard dans la frame)
											// -> il grave la branche PASSTHROUGH et l'eau n'est JAMAIS dessinee
											// (diagnostic [WaterDiag] gating: meshValid=false). On force donc le rebuild
											// des maintenant pour que IsValid()==true quand le graphe choisit la branche.
											if (m_clientWaterScene && m_waterMeshGpu.IsInitialized()
												&& m_waterTransferPool != VK_NULL_HANDLE)
											{
												vkResetCommandPool(m_vkDeviceContext.GetDevice(), m_waterTransferPool, 0);
												if (m_waterMeshGpu.Rebuild(m_waterTransferPool,
														m_vkDeviceContext.GetGraphicsQueue(), *m_clientWaterScene))
												{
													m_waterClientSceneDirty = false;
													LOG_INFO(Render, "[Nage] Mesh d'eau reconstruit (synchrone, avant FrameGraph) -> IsValid={}",
														m_waterMeshGpu.IsValid());
												}
											}
											// Interaction : interactibles declares en config (world.interactables.N.*).
											// Repli sur 2 cibles de TEST pres du spawn si aucun n'est declare.
											m_interactables.clear();
											const int icount = static_cast<int>(m_cfg.GetInt("world.interactables.count", 0));
											for (int ii = 0; ii < icount; ++ii)
											{
												const std::string base = "world.interactables." + std::to_string(ii) + ".";
												InteractableEntity e;
												e.position = engine::math::Vec3{ static_cast<float>(m_cfg.GetDouble(base + "x", 0.0)), 0.0f, static_cast<float>(m_cfg.GetDouble(base + "z", 0.0)) };
												e.radius = static_cast<float>(m_cfg.GetDouble(base + "radius", 2.5));
												e.isNpc = m_cfg.GetBool(base + "npc", false);
												e.label = m_cfg.GetString(base + "label", "?");
												e.message = m_cfg.GetString(base + "message", "");
												e.meshPath = m_cfg.GetString(base + "mesh", "");
												e.meshScale = static_cast<float>(m_cfg.GetDouble(base + "scale", 1.0));
												e.meshYawDeg = static_cast<float>(m_cfg.GetDouble(base + "yaw_deg", 0.0));
												e.meshRotXDeg = static_cast<float>(m_cfg.GetDouble(base + "rot_x_deg", 0.0));
												const int dcount = static_cast<int>(m_cfg.GetInt(base + "dialogue.count", 0));
												for (int dj = 0; dj < dcount; ++dj)
													e.dialogue.push_back(m_cfg.GetString(base + "dialogue." + std::to_string(dj), ""));
												// Cellule de dialogue PNJ : sous-titre (role) + arbre moderne
												// (fallback legacy depuis e.dialogue si pas de dialogue_tree).
												e.role = m_cfg.GetString(base + "role", "");
												e.dialogueTree = engine::client::LoadDialogueTree(m_cfg, base, e.dialogue);
												// SP2 — id réseau du PNJ (ex. "npc:elder_marn"), utilisé pour envoyer un
												// Talk à l'ouverture du dialogue (\see OpenDialogue) puis comme cible
												// des QuestAccept/TurnInRequest. Vide = pas de PNJ quête (aucun Talk envoyé).
												e.npcTargetId = m_cfg.GetString(base + "npc_target_id", "");
												m_interactables.push_back(e);
											}
											if (m_interactables.empty())
											{
												InteractableEntity npc;
												npc.position = engine::math::Vec3{ 4.0f, 0.0f, 0.0f };
												npc.radius = 2.5f; npc.isNpc = true; npc.label = "Villageois";
												npc.message = "Villageois : Bienvenue ! L'eau de test est a l'est.";
												m_interactables.push_back(npc);
												InteractableEntity chest;
												chest.position = engine::math::Vec3{ -4.0f, 0.0f, 0.0f };
												chest.radius = 2.0f; chest.isNpc = false; chest.label = "Coffre";
												chest.message = "Vous fouillez le coffre... vide pour l'instant.";
												chest.meshPath = "meshes/props/Chest_Wood.gltf";
												m_interactables.push_back(chest);
											}
											m_interactableInRange = -1;
											// Coffre anime (Chest_Wood) : charge en skinne AVANT les props statiques
											// pour que LoadInteractableProps sache s'il doit sauter le rendu statique
											// du coffre (m_chestLoaded). Si le chargement skinne echoue, le coffre
											// retombe sur le rendu statique (pas de disparition).
											LoadAnimatedChest();
											// M45.5 — flag global impostors végétation (défaut OFF). Lu une fois ici ;
											// quand false, RecordPropsGeometry s'exécute exactement comme l'historique.
											m_impostorEnabled = m_cfg.GetBool("world.impostor.enabled", false);
											if (m_impostorEnabled)
												LOG_INFO(Render, "[Impostor] M45.5 activé (world.impostor.enabled=true)");
											// fix/hiz-gate-off — flag global Hi-Z occlusion (défaut OFF). Lu une fois
											// ici. Quand false : la passe HiZ_Build n'est pas enregistrée (plus de
											// réécriture de descriptor en vol => plus de violation de validation), et
											// le GpuDrivenCullingPass reçoit un view Hi-Z nul => fallback conservateur.
											m_hiZEnabled = m_cfg.GetBool("render.hiz.enabled", false);
											if (m_hiZEnabled)
												LOG_INFO(Render, "[Boot] Hi-Z occlusion enabled (render.hiz.enabled=true)");
											else
												LOG_INFO(Render, "[Boot] Hi-Z occlusion disabled (render.hiz.enabled=false)");
											// Chantier B : charge les meshes statiques des props (coffre si non anime + interactibles).
											LoadInteractableProps();
											// Décor solide (arbres, props nature) depuis world.scenery. Doit suivre
											// LoadInteractableProps (qui réinitialise le collisionneur composite).
											LoadScenery();
											// Bâtiments éditables (auberge…) depuis instances/.../buildings.bin.
											// Après LoadScenery (partage m_props + m_worldCollider).
											LoadBuildings();
											// Aperçu éditeur : mémorise le nombre de props « monde » (décor +
											// bâtiments posés) pour que SyncEditorBuildingPreview n'efface QUE
											// le brouillon par-dessus, sans toucher au monde chargé ici.
											m_editorBaselinePropCount = m_props.size();

											engine::gameplay::CharacterController::Config ccCfg{};
											ccCfg.walkSpeed     = static_cast<float>(m_cfg.GetDouble("player.movement.walk_speed",      5.0));
											ccCfg.runSpeed      = static_cast<float>(m_cfg.GetDouble("player.movement.run_speed",       9.0));
											ccCfg.gravity       = static_cast<float>(m_cfg.GetDouble("player.movement.gravity",       -20.0));
											ccCfg.jumpSpeed     = static_cast<float>(m_cfg.GetDouble("player.movement.jump_speed",      9.0));
											ccCfg.coyoteTimeSec = static_cast<float>(m_cfg.GetDouble("player.movement.coyote_time_s",   0.1));
											ccCfg.jumpBufferSec = static_cast<float>(m_cfg.GetDouble("player.movement.jump_buffer_s",   0.1));
											ccCfg.capsule.radius = 0.3f;
											ccCfg.capsule.height = 1.8f;
											m_characterController = engine::gameplay::CharacterController(ccCfg);

											// Spawn au centre map (XZ = 0,0), pose au-dessus du sol echantillonne
											// + halfHeight de capsule pour que le bas de la capsule effleure le sol.
											const float spawnX = 0.0f;
											const float spawnZ = 0.0f;
											const float spawnY = m_terrainCollider.GroundHeightAt(spawnX, spawnZ) + 0.9f;
											m_characterController.Init(engine::math::Vec3{ spawnX, spawnY, spawnZ });
											LOG_INFO(Render,
												"[Engine] B.1 CharacterController spawn=({:.2f}, {:.2f}, {:.2f}) capsule r={:.2f} h={:.2f}",
												spawnX, spawnY, spawnZ, ccCfg.capsule.radius, ccCfg.capsule.height);
										}

										m_frameGraph.addPass("GPU_Cull",
											[](engine::render::PassBuilder&) {},
											[this](VkCommandBuffer cmd, engine::render::Registry&) {
												auto& cullingPass = m_pipeline->GetGpuDrivenCullingPass();
												if (!cullingPass.IsValid())
													return;

												const uint32_t readIdx = m_renderReadIndex.load(std::memory_order_acquire);
												const engine::RenderState& rs = m_renderStates[readIdx];
												// Sous-projet A (Task 15) : si le skinned avatar Y Bot est pret, on
												// supprime le cube placeholder du GpuCulling pipeline (drawItemCount=0
												// -> RecordIndirect ne dessinera pas le cube). L'avatar skinne sera
												// dessine apres la Geometry par un RecordTerrainChunkBatch dedie.
												engine::render::MeshAsset* mesh = (rs.objectVisible && !m_skinnedAvatarReady)
													? m_geometryMeshHandle.Get() : nullptr;
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

												// fix/hiz-gate-off — quand la Hi-Z est désactivée (m_hiZEnabled=false,
												// défaut), on passe un view nul + extent {0,0} + mipCount 0 : dans
												// GpuDrivenCullingPass::Record, occlusionEnabled devient false et le cull
												// retombe sur son fallback Hi-Z conservateur. On NE lit PAS les accesseurs
												// HiZPyramidPass (la passe HiZ_Build n'est alors pas enregistrée).
												if (m_hiZEnabled)
												{
													const auto& hiZPass = m_pipeline->GetHiZPyramidPass();
													cullingPass.Record(
														m_vkDeviceContext.GetDevice(), cmd, rs.viewProjMatrix.m, m_currentFrame,
														hiZPass.GetImageView(m_currentFrame),
														hiZPass.GetExtent(),
														hiZPass.GetMipCount());
												}
												else
												{
													cullingPass.Record(
														m_vkDeviceContext.GetDevice(), cmd, rs.viewProjMatrix.m, m_currentFrame,
														VK_NULL_HANDLE,
														VkExtent2D{ 0, 0 },
														0u);
												}
											});

										// Terrain prepass must not be a separate FrameGraph pass: it targets the same
										// G-buffer attachments as Geometry, and the MVP graph forbids multi-writer.
										// Record terrain inside the Geometry pass before GeometryPass::Record* (LOAD path).

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
												// Phase 0 (chantier C) : rendu exclusif. On ne dessine le terrain
												// heightmap legacy que si les chunks n'ont pas couvert la scène à la
												// frame précédente -> supprime le z-fighting du double terrain.
												const bool terrainBeforeGeometry = engine::render::ShouldDrawLegacyTerrain(
													m_terrain.IsValid(),
													m_pipeline->GetGeometryPass().HasLoadPass(),
													m_lastFrameChunkDrawCount);
												if (terrainBeforeGeometry)
												{
													m_terrain.Record(
														m_vkDeviceContext.GetDevice(), cmd, reg,
														m_vkSwapchain.GetExtent(),
														m_fgGBufferAId, m_fgGBufferBId, m_fgGBufferCId,
														m_fgGBufferVelocityId, m_fgDepthId,
														rs.prevViewProjMatrix.m, rs.viewProjMatrix.m,
														rs.camera.position, rs.frustum);
												}
												// Sous-projet A (Task 15) : meme regle que GPU_Cull -- si l'avatar skinne
												// est pret, on passe mesh = nullptr pour que GeometryPass::Record/Indirect
												// ouvre/ferme le render pass GBuffer (necessaire pour les layouts) sans
												// dessiner le cube. L'avatar skinne est ajoute juste apres via
												// RecordTerrainChunkBatch (meme render pass LOAD compatible).
												engine::render::MeshAsset* mesh = (rs.objectVisible && !m_skinnedAvatarReady)
													? m_geometryMeshHandle.Get() : nullptr;
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
														(m_avatarMaterialId != 0u ? m_avatarMaterialId : materialCache.GetDefaultMaterialIndex()),
														terrainBeforeGeometry);
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
														(m_avatarMaterialId != 0u ? m_avatarMaterialId : materialCache.GetDefaultMaterialIndex()),
														terrainBeforeGeometry);
												}

												// Sous-projet A (Task 15) : draw call skinned Y Bot.
												// Apres l'ouverture/fermeture de la passe Geometry par Record/RecordIndirect,
												// on superpose la draw skinned via RecordTerrainChunkBatch qui (re)ouvre le
												// render pass LOAD du GeometryPass (formats compatibles -- meme GBuffer A/B/C
												// + Velocity + Depth). Idempotent : si m_skinnedAvatarReady reste false (init
												// echouee ou .glb manquant), on saute et le cube placeholder a deja ete
												// dessine par Record/RecordIndirect ci-dessus.
												if (m_skinnedAvatarReady && m_currentSkinnedMesh && m_skinnedRenderer.IsValid())
												{
													// B.1 / Task 11 : la state machine 7 etats + le Play du crossfade
													// vivent desormais dans Engine::Update (driven par signaux gameplay
													// propres : grounded, jumpPressed, run, moveDirXZ). Le lambda
													// Geometry ne fait plus que sampler la pose du crossfade au temps
													// courant et l'envoyer au SkinnedRenderer.
													//
													// nowSec doit utiliser la meme horloge que celle passee a
													// `m_avatarCrossfade.Play(...)` dans Update : meme reference de temps
													// via `EngineNowSec()` (normalise par le boot du jeu, evite la perte
													// de precision float qui faisait trembler le mesh — anim.startTime
													// stocke par Play etait < now mais la difference etait noyee dans la
													// precision float 32, donc l'echantillonnage progressait par sauts).
													const float nowSec = EngineNowSec();

													auto locals  = m_avatarCrossfade.Sample(
														m_currentSkinnedMesh->skeleton, nowSec);
													auto globals = engine::render::skinned::AnimationSampler::ComputeGlobalMatrices(
														m_currentSkinnedMesh->skeleton, locals);
													auto finals  = engine::render::skinned::AnimationSampler::ComputeFinalMatrices(
														m_currentSkinnedMesh->skeleton, globals);

													// Chantier 2 SP1 — le corps de base de l'avatar modulaire = le mesh
													// courant (idempotent chaque frame ; suit un éventuel changement de
													// race). Les parties équipées (placeholder) persistent via /modular.
													m_modularAvatar.SetBody(m_currentSkinnedMesh);

													// --- B.1 / Task 9 : model matrix depuis CharacterController + m_avatarYaw ---
													// On lit la position monde directement depuis le CC (deja appele
													// dans Update *avant* OrbitalCameraController, cf. ligne ~6580),
													// et on compose la matrice T(feetPos) * R_y(m_avatarYaw).
													//   - `feetPos.y = ccPos.y - 0.9` : la position du CC est le CENTRE de la
													//     capsule ; le mesh Y Bot va de Y=0 (pieds) a Y=1.8 (tete) -> on
													//     redescend de halfHeight (0.9 m) pour poser les pieds au sol.
													//   - `m_avatarYaw` : aligne le mesh sur la direction de mouvement.
													//     Init a pi pour que le spawn (avant tout input) ait le perso dos
													//     a la camera (Mixamo Y Bot face +Z intrinseque ; cam yaw=0 forward
													//     = -Z ; R_y(pi) tourne le mesh face -> -Z = loin de la camera).
													//     Avant le fix d'orientation : on appliquait R_y(yaw + pi) avec un
													//     `+ pi` redondant qui s'additionnait au pi deja contenu dans
													//     atan2(forward.x, forward.z) -> inversion W/S et Q/D en jeu.
													//
													// La matrice rs.objectModelMatrix reste utilisee par le cube fallback
													// (RecordIndirect plus haut) ; chaque chemin a desormais sa propre source.
													const engine::math::Vec3 ccPos = m_characterController.GetPosition();
													const engine::math::Vec3 feetPos{ ccPos.x, ccPos.y - 0.9f, ccPos.z };
													const engine::math::Mat4 finalModelMat =
														engine::math::Mat4::Translate(feetPos) *
														engine::math::Mat4::RotateY(m_avatarYaw) *
														(m_currentSkinnedMesh ? m_currentSkinnedMesh->importTransform : engine::math::Mat4::Identity());

													// La matrice modele est desormais calculee a partir de cc.GetPosition()
													// + m_avatarYaw ; le cube placeholder garde sa matrice d'origine (cf.
													// rs.objectModelMatrix construit dans Update).
													// materialDescriptorSet + materialIndex : meme set bindless que le
													// cube draw. skinnedMaterialIndex = HABIT (defaut).
													const uint32_t skinnedMaterialIndex = (m_avatarMaterialId != 0u)
														? m_avatarMaterialId
														: materialCache.GetDefaultMaterialIndex();

													// Multi-materiaux : un index par sous-maillage. Les sous-maillages
													// dont le nom de materiau glTF figure dans m_avatarBodyMaterialNames
													// recoivent la PEAU (selon genre x teinte), les autres l'habit.
													// Si aucune texture de peau n'est chargee (id 0), tout retombe sur
													// l'habit -> comportement identique a l'ancien mono-draw.
													// Teinte foncee (m_avatarSkinTone==1) avec repli sur claire si absente.
													uint32_t bodyMaterialId;
													if (m_avatarGender == "female")
														bodyMaterialId = (m_avatarSkinTone == 1 && m_avatarBodyMaterialIdFemaleDark != 0u)
															? m_avatarBodyMaterialIdFemaleDark : m_avatarBodyMaterialIdFemale;
													else
														bodyMaterialId = (m_avatarSkinTone == 1 && m_avatarBodyMaterialIdMaleDark != 0u)
															? m_avatarBodyMaterialIdMaleDark : m_avatarBodyMaterialIdMale;
													// Routage peau/habit par sous-maillage (fonction pure partagee
													// avec l'apercu de creation). Vide si pas de materiau peau ou pas
													// de sous-maillages -> mono-draw habit (comportement identique
													// a l'ancien code).
													std::vector<uint32_t> submeshMaterialIndices =
														engine::render::skinned::BuildSubmeshMaterialIndices(
															m_currentSkinnedMesh->submeshes,
															m_avatarBodyMaterialNames,
															bodyMaterialId,
															skinnedMaterialIndex);
													// Diagnostic peau/genre (#1/#2) : logge une seule fois par changement de
													// genre. Revele en clair l'etat runtime (ids materiaux, nb de sous-maillages
													// peau vs habit, noms de materiaux reellement charges).
													if (m_avatarSkinDiagLoggedGender != m_avatarGender)
													{
														m_avatarSkinDiagLoggedGender = m_avatarGender;
														std::size_t bodyCount = 0;
														for (uint32_t matId : submeshMaterialIndices)
															if (matId == bodyMaterialId) ++bodyCount;
														std::string noms;
														for (const auto& sub : m_currentSkinnedMesh->submeshes)
														{
															if (!noms.empty()) noms += ", ";
															noms += sub.materialName.empty() ? "<vide>" : sub.materialName;
														}
														LOG_INFO(Render,
															"[AvatarSkinDiag] genre={} idMale={} idFemale={} bodyId={} habitId={} submeshes={} peau={} noms=[{}]",
															m_avatarGender, m_avatarBodyMaterialIdMale, m_avatarBodyMaterialIdFemale,
															bodyMaterialId, skinnedMaterialIndex, m_currentSkinnedMesh->submeshes.size(),
															bodyCount, noms);
														if (bodyMaterialId == 0u)
															LOG_ERROR(Render, "[AvatarSkinDiag] bodyMaterialId=0 -> peau NON routee (texture peau non chargee ?)");
														else if (!submeshMaterialIndices.empty() && bodyCount == 0)
															LOG_WARN(Render, "[AvatarSkinDiag] 0 sous-maillage peau (noms ne matchent pas body_material_names)");
													}

													m_pipeline->GetGeometryPass().RecordTerrainChunkBatch(
														m_vkDeviceContext.GetDevice(), cmd, reg,
														m_vkSwapchain.GetExtent(),
														m_fgGBufferAId, m_fgGBufferBId, m_fgGBufferCId,
														m_fgGBufferVelocityId, m_fgDepthId,
														[this, &rs, &finals, skinnedMaterialIndex, &materialCache, finalModelMat, bodyMaterialId,
														 submeshMaterialIndices = std::move(submeshMaterialIndices)](VkCommandBuffer innerCmd) {
															// Chantier 2 SP1 — avatar modulaire : dessine chaque partie active
															// avec les MÊMES matrices d'os (squelette partagé). Le Body garde son
															// routage matériau (habit/peau) ; les parties placeholder utilisent le
															// chemin mono-draw (materialIndex 0). ActiveParts() = Body seul tant
															// qu'aucune partie n'est équipée -> rendu identique à avant.
															// Matrices d'os IDENTITÉ pour les parties placeholder : le rig UE5
															// a des os inexploitables (bind-globales à l'origine) -> peser une
															// boîte a un os la projette n'importe ou. Avec une pose identité, la
															// partie n'est plus deformee par le squelette : posee en espace
															// MODELE (~1.6 m = tete) puis placee par finalModelMat (la meme
															// matrice qui positionne tout l'avatar), elle se pose sur la tete et
															// suit position + orientation du perso. Suivi fin par os -> rig propre.
															std::vector<engine::math::Mat4> identityFinals(
																finals.size(), engine::math::Mat4::Identity());
															for (const engine::render::skinned::SkinnedMesh* part : m_modularAvatar.ActiveParts())
															{
																const bool isBody = (part == m_currentSkinnedMesh);
																m_skinnedRenderer.Record(
																	m_vkDeviceContext.GetDevice(), innerCmd,
																	m_vkSwapchain.GetExtent(),
																	m_pipeline->GetGeometryPass().GetRenderPassLoad(),
																	VK_NULL_HANDLE,
																	rs.prevViewProjMatrix.m, rs.viewProjMatrix.m,
																	*part,
																	isBody ? finals : identityFinals,
																	materialCache.GetDescriptorSet(),
																	finalModelMat.m,
																	isBody ? skinnedMaterialIndex : 0u,
																	isBody ? submeshMaterialIndices : std::vector<uint32_t>{},
																	isBody ? bodyMaterialId : 0u,
																	m_avatarSkinDepthBiasConstant,
																	m_avatarSkinDepthBiasSlope);
															}
														});
												}

												// Chantier B : dessine les props statiques (coffre, etc.) par-dessus le GBuffer.
												RecordPropsGeometry(cmd, reg, rs);

												// TD.2 : dessine les avatars des joueurs distants (réplication AoI).
												RecordRemoteAvatars(cmd, reg, rs);
												// Coffre anime (ouverture/fermeture a l'interaction).
												RecordAnimatedChest(cmd, reg, rs);

												// M100 — Task 12 : drawcall terrain chunk (post-Phase-3a).
												// Dessine les chunks visibles qui ont terrain.bin + splat.bin
												// dans une passe de chargement (loadOp=LOAD) après la geometry
												// principale. Skip strict si fichiers absents (legacy
												// TerrainRenderer continue à les dessiner). PAS de branche
												// m_editorEnabled — parité jeu/éditeur garantie par le format
												// binaire identique (cf. critère M100.5/.9).
												std::uint32_t chunksDrawnThisFrame = 0u;
												if (m_terrainChunkRenderer && m_terrainChunkRenderer->IsValid())
												{
													UpdateTerrainChunkCameraUbo(rs.viewProjMatrix.m);
													// Phase 1 (chantier C) : les chunks terrain sont sur la grille 256 m,
													// pas sur la grille 500 m de m_world (instances/zone). On dérive donc
													// l'ensemble visible directement de la position caméra sur la grille
													// terrain, pour charger/placer les bons terrain.bin (alignés éditeur).
													const std::vector<engine::world::GlobalChunkCoord> visibleChunks =
														engine::world::ComputeVisibleTerrainChunks(
															rs.camera.position.x, rs.camera.position.z);
													if (!visibleChunks.empty())
													{
														m_pipeline->GetGeometryPass().RecordTerrainChunkBatch(
															m_vkDeviceContext.GetDevice(), cmd, reg,
															m_vkSwapchain.GetExtent(),
															m_fgGBufferAId, m_fgGBufferBId, m_fgGBufferCId,
															m_fgGBufferVelocityId, m_fgDepthId,
															[this, &visibleChunks](VkCommandBuffer innerCmd) {
																m_terrainChunkRenderer->RenderVisibleChunks(
																	innerCmd,
																	m_terrainChunkCameraSet,
																	m_world,
																	visibleChunks);
															});
														chunksDrawnThisFrame = m_terrainChunkRenderer->GetLastDrawnChunkCount();
													}
												}
												// Phase 0 : mémorise pour le gating legacy de la frame SUIVANTE.
												// Remis à 0 chaque frame (carte heightmap-only / chunks non visibles
												// -> le legacy redevient visible sans rester "collé" éteint).
												m_lastFrameChunkDrawCount = chunksDrawnThisFrame;

												// Phase 5 Lunar + M38.1 Sky : enregistre le draw fullscreen-quad
												// du SkyPass (ciel + disque lunaire procedural) dans le render
												// pass loadOp=LOAD du GeometryPass. SkyPass a ete Init contre
												// `GetRenderPassLoad()` au boot, donc on doit etre dans ce
												// render pass actif pour que vkCmdDraw soit valide. On reuse
												// `RecordTerrainChunkBatch` (qui ouvre exactement ce render
												// pass + framebuffer GBuffer + viewport/scissor) comme
												// wrapper. La pass est emise en fin de lambda Geometry pour
												// que le clear initial du GeometryPass ne l'ecrase pas. Avec
												// le pipeline SkyPass (depthTest=FALSE, depthWrite=FALSE), le
												// fullscreen-quad couvre tout le champ de vision ; les pixels
												// reels de geometrie ont deja ete ecrits dans GBuffer
												// avant. La consommation finale par LightingPass se fait via
												// les autres attachments (depth, normal, ORM) — Sky ne
												// renseigne que GBuffer A (albedo).
												if (m_skyPassReady)
												{
													engine::render::SkyPass::PushConstants skyPc{};

													// Calcul invViewProj inline (meme pattern que Decals/Lighting plus bas
													// dans le fichier — pas de helper Mat4::Inverse global pour l'instant).
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
														const float invDet = 1.0f / det;
														skyPc.invViewProj[0]  = ( a11*b11-a21*b10+a31*b09)*invDet;
														skyPc.invViewProj[1]  = (-a10*b11+a20*b10-a30*b09)*invDet;
														skyPc.invViewProj[2]  = ( a13*b05-a23*b04+a33*b03)*invDet;
														skyPc.invViewProj[3]  = (-a12*b05+a22*b04-a32*b03)*invDet;
														skyPc.invViewProj[4]  = (-a01*b11+a21*b08-a31*b07)*invDet;
														skyPc.invViewProj[5]  = ( a00*b11-a20*b08+a30*b07)*invDet;
														skyPc.invViewProj[6]  = (-a03*b05+a23*b02-a33*b01)*invDet;
														skyPc.invViewProj[7]  = ( a02*b05-a22*b02+a32*b01)*invDet;
														skyPc.invViewProj[8]  = ( a01*b10-a11*b08+a31*b06)*invDet;
														skyPc.invViewProj[9]  = (-a00*b10+a10*b08-a30*b06)*invDet;
														skyPc.invViewProj[10] = ( a03*b04-a13*b02+a33*b00)*invDet;
														skyPc.invViewProj[11] = (-a02*b04+a12*b02-a32*b00)*invDet;
														skyPc.invViewProj[12] = (-a01*b09+a11*b07-a21*b06)*invDet;
														skyPc.invViewProj[13] = ( a00*b09-a10*b07+a20*b06)*invDet;
														skyPc.invViewProj[14] = (-a03*b03+a13*b01-a23*b00)*invDet;
														skyPc.invViewProj[15] = ( a02*b03-a12*b01+a22*b00)*invDet;
													}
													else
													{
														// Identity fallback — l'invViewProj sera approximatif mais le
														// shader continuera a tourner sans NaN.
														skyPc.invViewProj[0] = skyPc.invViewProj[5] =
															skyPc.invViewProj[10] = skyPc.invViewProj[15] = 1.0f;
													}

													const auto& dn = m_dayNight.GetState();
													skyPc.lightDir[0]      = dn.sunDir[0];
													skyPc.lightDir[1]      = dn.sunDir[1];
													skyPc.lightDir[2]      = dn.sunDir[2];
													skyPc.zenithColor[0]   = dn.skyZenith[0];
													skyPc.zenithColor[1]   = dn.skyZenith[1];
													skyPc.zenithColor[2]   = dn.skyZenith[2];
													skyPc.horizonColor[0]  = dn.skyHorizon[0];
													skyPc.horizonColor[1]  = dn.skyHorizon[1];
													skyPc.horizonColor[2]  = dn.skyHorizon[2];
													// Lune : direction réelle de la lune (jour comme nuit).
													skyPc.moonDir[0]       = dn.moonDir[0];
													skyPc.moonDir[1]       = dn.moonDir[1];
													skyPc.moonDir[2]       = dn.moonDir[2];
													skyPc.moonIntensity    = dn.isDaytime ? 0.0f : 1.0f;
													skyPc.moonPhase        = static_cast<float>(dn.moonPhase);
													skyPc.moonIllumination = dn.moonIllumination;
													// Chantier ciel 2026-07-17 — modèle de ciel :
													// analytique (Rayleigh+Mie) par défaut ; la clé
													// client.sky.analytic=false rebascule sur le
													// dégradé legacy sans rebuild (filet visuel).
													skyPc.skyModel =
														m_cfg.GetBool("client.sky.analytic", true) ? 1.0f : 0.0f;
													// Position caméra : sert au sky.frag à reconstruire un vrai
													// rayon de vue (sinon ciel uniforme, soleil/lune invisibles).
													skyPc.cameraPos[0]     = rs.camera.position.x;
													skyPc.cameraPos[1]     = rs.camera.position.y;
													skyPc.cameraPos[2]     = rs.camera.position.z;
													skyPc.cameraPos[3]     = 0.0f;

													m_pipeline->GetGeometryPass().RecordTerrainChunkBatch(
														m_vkDeviceContext.GetDevice(), cmd, reg,
														m_vkSwapchain.GetExtent(),
														m_fgGBufferAId, m_fgGBufferBId, m_fgGBufferCId,
														m_fgGBufferVelocityId, m_fgDepthId,
														[this, &skyPc](VkCommandBuffer innerCmd) {
															m_skyPass.Record(innerCmd, skyPc);
														});
												}
											});

										// fix/hiz-gate-off — la passe HiZ_Build n'est enregistrée QUE si la Hi-Z
										// est explicitement activée (render.hiz.enabled=true). Par défaut (false)
										// elle est éteinte du chemin frame : HiZPyramidPass réécrivait son
										// descriptor set unique pendant qu'il était en vol (violation Vulkan,
										// erreur de validation chaque frame) alors que sa sortie était inerte
										// (le GpuDrivenCullingPass ne traitait que le cube placeholder désactivé).
										// La classe HiZPyramidPass est conservée, seulement débranchée ici.
										if (m_hiZEnabled)
										{
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
										}

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

										// M45.7 — DDGI_Update : passe COMPUTE qui rafraîchit l'atlas
										// d'irradiance du volume DDGI AVANT le LightingPass. Enregistrée
										// UNIQUEMENT si m_ddgiEnabled (gi.ddgi.enabled=true + alloc/init OK) ;
										// sinon le LightingPass garde useDdgi=0 et le rendu est inchangé.
										// L'écriture de l'atlas (image persistante hors frame graph) est
										// gérée par les barrières internes de la passe ; on déclare en READ
										// la shadow cascade 0 pour que le FG la rende lisible (SAMPLED).
										if (m_ddgiEnabled && m_ddgiUpdatePass.IsValid())
										{
											m_frameGraph.addPass("DDGI_Update",
												[this](engine::render::PassBuilder& b) {
													b.read(m_fgShadowMapIds[0], engine::render::ImageUsage::SampledRead);
												},
												[this](VkCommandBuffer cmd, engine::render::Registry& reg) {
													if (!m_ddgiEnabled || !m_ddgiUpdatePass.IsValid() || !m_ddgiVolume.IsAllocated())
														return;
													const uint32_t readIdx = m_renderReadIndex.load(std::memory_order_acquire);
													const engine::RenderState& rs = m_renderStates[readIdx];

													engine::render::gi::DdgiUpdatePass::DdgiUpdateParams up{};
													const engine::render::gi::DdgiGridConfig& gc = m_ddgiVolume.Config();
													up.gridOrigin[0] = gc.origin[0]; up.gridOrigin[1] = gc.origin[1]; up.gridOrigin[2] = gc.origin[2];
													up.gridSpacing[0] = gc.spacing[0]; up.gridSpacing[1] = gc.spacing[1]; up.gridSpacing[2] = gc.spacing[2];
													// M45.8 — slot .w (sinon inutilisé) = diviseur d'amortissement,
													// lu par ddgi_update.comp (1 sonde sur N mise à jour par frame).
													up.gridSpacing[3] = static_cast<float>(m_ddgiUpdateDivisor);
													up.counts[0] = gc.counts[0]; up.counts[1] = gc.counts[1]; up.counts[2] = gc.counts[2];
													up.counts[3] = gc.irradianceTexels;
													// Direction VERS le soleil (normalisée), couleur soleil per-zone.
													{
														float sx = m_zoneAtmosphere.sunDirection[0];
														float sy = m_zoneAtmosphere.sunDirection[1];
														float sz = m_zoneAtmosphere.sunDirection[2];
														float sl = std::sqrt(sx*sx + sy*sy + sz*sz);
														if (sl > 1e-6f) { sx /= sl; sy /= sl; sz /= sl; }
														up.sunDir[0] = sx; up.sunDir[1] = sy; up.sunDir[2] = sz;
													}
													up.sunColor[0] = m_zoneAtmosphere.sunColor[0];
													up.sunColor[1] = m_zoneAtmosphere.sunColor[1];
													up.sunColor[2] = m_zoneAtmosphere.sunColor[2];
													// Horizon du ciel piloté par DayNightCycle.
													{
														const engine::render::DayNightCycle::State& dn = m_dayNight.GetState();
														up.skyColor[0] = dn.skyHorizon[0];
														up.skyColor[1] = dn.skyHorizon[1];
														up.skyColor[2] = dn.skyHorizon[2];
													}
													up.params[0] = static_cast<float>(m_cfg.GetDouble("gi.ddgi.hysteresis", 0.95));
													up.params[1] = static_cast<float>(m_ddgiVolume.AtlasCols());
													up.params[2] = static_cast<float>(m_ddgiVolume.IrradianceTileSize());
													// M45.7b — indice de frame courant : le compute fait le modulo
													// kUpdateDivisor pour l'amortissement (1 sonde sur 4 par frame).
													up.params[3] = static_cast<float>(m_currentFrame);
													(void)rs; // rs réservé (cascade 0 lue via le registry ci-dessous).

													// Shadow cascade 0 : on lit sa vue via le registry (comme VolumetricFog).
													VkImageView shadowView = reg.getImageView(m_fgShadowMapIds[0]);
													m_ddgiUpdatePass.Record(
														m_vkDeviceContext.GetDevice(), cmd, m_ddgiVolume,
														shadowView, m_ddgiUpdatePass.GetShadowSampler(), up);
												});
										}

										m_frameGraph.addPass("Lighting",
											[this](engine::render::PassBuilder& b) {
												b.read(m_fgGBufferAId,       engine::render::ImageUsage::SampledRead);
												b.read(m_fgGBufferBId,       engine::render::ImageUsage::SampledRead);
												b.read(m_fgGBufferCId,       engine::render::ImageUsage::SampledRead);
												b.read(m_fgDepthId,          engine::render::ImageUsage::SampledRead);
												b.read(m_fgSsaoBlurId,       engine::render::ImageUsage::SampledRead);
												b.read(m_fgDecalOverlayId,   engine::render::ImageUsage::SampledRead);
												// CSM — les 4 shadow maps cascades (rendues lisibles SAMPLED par le FG).
												for (uint32_t i = 0; i < engine::render::kCascadeCount; ++i)
													b.read(m_fgShadowMapIds[i], engine::render::ImageUsage::SampledRead);
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
												// Brouillard atmospherique LINEAIRE a distance. fogStart (cameraPos.w) :
												// rayon CLAIR autour du joueur ; fogEnd (lightDir.w) : distance ou tout
												// est fondu vers l'horizon. Le joueur n'est plus noye dans le brouillard.
												// Regler fogEnd ~ world.props.cull_distance_m pour que les arbres se
												// fondent dans le ciel juste avant d'etre cull. Desactive si end<=start.
												// Slots .w libres -> pas de redimensionnement de LightParams. Lu chaque
												// frame (reglage a chaud via config.json).
												lp.cameraPos[3] = static_cast<float>(m_cfg.GetDouble("world.fog.start_m", 35.0));
												lp.lightDir[0] = m_zoneAtmosphere.sunDirection[0]; lp.lightDir[1] = m_zoneAtmosphere.sunDirection[1]; lp.lightDir[2] = m_zoneAtmosphere.sunDirection[2];
												lp.lightDir[3] = static_cast<float>(m_cfg.GetDouble("world.fog.end_m", 85.0)); // brouillard : distance de fondu total (cf. cameraPos.w = start)
												lp.lightColor[0] = m_zoneAtmosphere.sunColor[0]; lp.lightColor[1] = m_zoneAtmosphere.sunColor[1]; lp.lightColor[2] = m_zoneAtmosphere.sunColor[2];
												// M45.1 — densité d'extinction de la perspective aérienne (slot lightColor.w).
												// Override config.json (world.fog.density) sinon valeur per-zone (atmosphere.json).
												lp.lightColor[3] = static_cast<float>(m_cfg.GetDouble("world.fog.density", static_cast<double>(m_zoneAtmosphere.aerialDensity)));
												const float probeIntensity = GetGlobalProbeIntensity(m_zoneProbes);
												lp.ambientColor[0] = m_zoneAtmosphere.ambientColor[0] * probeIntensity;
												lp.ambientColor[1] = m_zoneAtmosphere.ambientColor[1] * probeIntensity;
												lp.ambientColor[2] = m_zoneAtmosphere.ambientColor[2] * probeIntensity;
												// M45.1 — force d'inscattering directionnel (slot ambientColor.w).
												// Override config.json (world.fog.inscatter) sinon valeur per-zone.
												lp.ambientColor[3] = static_cast<float>(m_cfg.GetDouble("world.fog.inscatter", static_cast<double>(m_zoneAtmosphere.aerialInscatter)));

												// Nuages (Phase 3) — assombrissement "ciel couvert" : module le
												// soleil directionnel / l'ambiant selon la couverture nuageuse
												// courante (dérivée du WeatherSystem, même source que CloudPass).
												// Modulation scalaire pure (aucun changement de pipeline/descriptor) ;
												// couplée au flag nuages, désactivable via overcast_strength = 0.
												if (m_cfg.GetBool("render.clouds.enabled", true))
												{
													const float overcastStrength = static_cast<float>(m_cfg.GetDouble("render.clouds.overcast_strength", 0.6));
													engine::render::CloudParams ocCur = engine::render::CloudWeatherMapper::ParamsFor(m_weatherSystem.GetCurrentState());
													engine::render::CloudParams ocTgt = engine::render::CloudWeatherMapper::ParamsFor(m_weatherSystem.GetTargetState());
													engine::render::CloudParams ocp   = engine::render::CloudParams::Lerp(ocCur, ocTgt, m_weatherSystem.GetIntensity());
													float overcast = ocp.coverage * (ocp.density * 0.5f) * overcastStrength;
													overcast = overcast < 0.0f ? 0.0f : (overcast > 1.0f ? 1.0f : overcast);
													const float sunFactor = 1.0f - 0.7f * overcast; // soleil atténué (jusqu'à -70%)
													const float ambFactor = 1.0f + 0.4f * overcast; // ambiant diffus renforcé
													for (int i = 0; i < 3; ++i)
													{
														lp.lightColor[i]   *= sunFactor;
														lp.ambientColor[i] *= ambFactor;
													}
												}
												// Couleur du ciel utilisée par lighting.frag pour les pixels
												// sans géométrie (depth==1.0). Pilotée par DayNightCycle pour
												// rendre le cycle jour/nuit visible sans skybox dédié.
												// Phase 5 Lunar (PR #561 fix Concern 3) : skyColor.w sert de
												// flag "SkyPass ready". Quand m_skyPassReady=true, le SkyPass
												// a ecrit la sky procedurale (gradient + sun glow + moon disk
												// avec phase) dans GBufferA pour les pixels depth==1.0 ;
												// lighting.frag lit alors GBufferA. Sinon (SkyPass.Init failed
												// ou shaders absents) on tombe sur la flat skyColor pour
												// preserver le rendu jour/nuit.
												{
													const engine::render::DayNightCycle::State& dnLight = m_dayNight.GetState();
													lp.skyColor[0] = dnLight.skyHorizon[0];
													lp.skyColor[1] = dnLight.skyHorizon[1];
													lp.skyColor[2] = dnLight.skyHorizon[2];
													lp.skyColor[3] = m_skyPassReady ? 1.0f : 0.0f;
												}
												VkImageView irrView       = m_pipeline->GetIrradiancePass().IsValid() ? m_pipeline->GetIrradiancePass().GetImageView() : VK_NULL_HANDLE;
												VkSampler   irrSamp       = m_pipeline->GetIrradiancePass().IsValid() ? m_pipeline->GetIrradiancePass().GetSampler()   : VK_NULL_HANDLE;
												VkImageView prefilterView = m_pipeline->GetSpecularPrefilterPass().IsValid() ? m_pipeline->GetSpecularPrefilterPass().GetImageView() : VK_NULL_HANDLE;
												VkSampler   prefilterSamp = m_pipeline->GetSpecularPrefilterPass().IsValid() ? m_pipeline->GetSpecularPrefilterPass().GetSampler()    : VK_NULL_HANDLE;
												VkImageView brdfView      = m_pipeline->GetBrdfLutPass().GetImageView();
												VkSampler   brdfSamp      = m_pipeline->GetBrdfLutPass().GetSampler();
												lp.useIBL = (irrView != VK_NULL_HANDLE && prefilterView != VK_NULL_HANDLE && brdfView != VK_NULL_HANDLE) ? 1.0f : 0.0f;

												// M45.7 — DDGI dynamique. Par défaut (m_ddgiEnabled=false ou volume
												// non alloué) : useDdgi reste 0 et la vue DDGI reste VK_NULL_HANDLE
												// (LightingPass lie alors un fallback valide mais non lu) => rendu
												// strictement identique au chemin probes statiques.
												VkImageView ddgiView = VK_NULL_HANDLE;
												VkSampler   ddgiSamp = VK_NULL_HANDLE;
												if (m_ddgiEnabled && m_ddgiVolume.IsAllocated())
												{
													lp.useDdgi = 1.0f;
													const engine::render::gi::DdgiGridConfig& gc = m_ddgiVolume.Config();
													lp.ddgiOrigin[0] = gc.origin[0]; lp.ddgiOrigin[1] = gc.origin[1]; lp.ddgiOrigin[2] = gc.origin[2];
													lp.ddgiSpacing[0] = gc.spacing[0]; lp.ddgiSpacing[1] = gc.spacing[1]; lp.ddgiSpacing[2] = gc.spacing[2];
													lp.ddgiCounts[0] = static_cast<float>(gc.counts[0]);
													lp.ddgiCounts[1] = static_cast<float>(gc.counts[1]);
													lp.ddgiCounts[2] = static_cast<float>(gc.counts[2]);
													lp.ddgiCounts[3] = static_cast<float>(gc.irradianceTexels);
													lp.ddgiAtlas[0] = static_cast<float>(m_ddgiVolume.AtlasCols());
													lp.ddgiAtlas[1] = static_cast<float>(m_ddgiVolume.AtlasRows());
													lp.ddgiAtlas[2] = static_cast<float>(m_ddgiVolume.IrradianceTileSize());
													// M45.8 — intensité pilotée par le niveau de qualité (DynamicLow=0.5,
													// DynamicHigh=1.0). Remplace l'ancienne lecture de gi.ddgi.intensity.
													lp.ddgiAtlas[3] = m_ddgiIntensity;
													ddgiView = m_ddgiVolume.IrradianceView();
													ddgiSamp = m_ddgiUpdatePass.GetShadowSampler(); // sampler linéaire-équivalent (NEAREST clamp) interne
												}

												// ---- CSM — Ombres cascades --------------------------------------
												// Récupère les 4 vues shadow maps et remplit l'UBO cascades
												// (binding 11). shadowParams : x=useShadows(0/1), y=texelSize
												// (1/résolution), z=biasConstant, w=biasSlopeMax. Si une vue est
												// invalide, useShadows tombe à 0 (le shader ignore alors le fallback).
												engine::render::LightingPass::ShadowUbo shadowUbo{};
												VkImageView shadowViews[engine::render::kCascadeCount] = {};
												bool shadowViewsOk = true;
												for (uint32_t i = 0; i < engine::render::kCascadeCount; ++i)
												{
													shadowViews[i] = reg.getImageView(m_fgShadowMapIds[i]);
													if (shadowViews[i] == VK_NULL_HANDLE) shadowViewsOk = false;
													std::memcpy(&shadowUbo.lightViewProj[i * 16],
														rs.cascades.lightViewProj[i].m, sizeof(float) * 16);
												}
												const bool shadowsEnabled = m_cfg.GetBool("shadows.enabled", true);
												const uint32_t shadowRes  = static_cast<uint32_t>(m_cfg.GetInt("shadows.resolution", 1024));
												shadowUbo.shadowParams[0] = (shadowsEnabled && shadowViewsOk) ? 1.0f : 0.0f;
												shadowUbo.shadowParams[1] = (shadowRes > 0) ? (1.0f / static_cast<float>(shadowRes)) : 0.0f;
												shadowUbo.shadowParams[2] = static_cast<float>(m_cfg.GetDouble("shadows.bias_constant", 0.0015));
												shadowUbo.shadowParams[3] = static_cast<float>(m_cfg.GetDouble("shadows.bias_slope_max", 0.005));

												// UBO point lights (binding 12). Snapshot rs.pointLights (anti
												// data-race) ; clamp 64 ; intensité globale world.point_lights.intensity_scale
												// (défaut 1.0, garde-fou anti sur-exposition HDR).
												engine::render::LightingPass::PointLightUbo pointLightUbo{};
												const float intensityScale =
													static_cast<float>(m_cfg.GetDouble("world.point_lights.intensity_scale", 1.0));
												const size_t activeCount = std::min<size_t>(rs.pointLights.size(), 64);
												pointLightUbo.count[0] = static_cast<uint32_t>(activeCount);
												for (size_t i = 0; i < activeCount; ++i)
												{
													const engine::render::ActivePointLight& pl = rs.pointLights[i];
													pointLightUbo.lights[i].posRadius[0] = pl.position[0];
													pointLightUbo.lights[i].posRadius[1] = pl.position[1];
													pointLightUbo.lights[i].posRadius[2] = pl.position[2];
													pointLightUbo.lights[i].posRadius[3] = pl.radius;
													pointLightUbo.lights[i].colorIntensity[0] = pl.color[0];
													pointLightUbo.lights[i].colorIntensity[1] = pl.color[1];
													pointLightUbo.lights[i].colorIntensity[2] = pl.color[2];
													pointLightUbo.lights[i].colorIntensity[3] = pl.intensity * intensityScale;
												}

												const uint32_t frameIdx = m_currentFrame % 2;
												m_pipeline->GetLightingPass().Record(
													m_vkDeviceContext.GetDevice(), cmd, reg,
													m_vkSwapchain.GetExtent(),
													m_fgGBufferAId, m_fgGBufferBId, m_fgGBufferCId, m_fgDepthId,
													m_fgSceneColorHDRId, m_fgSsaoBlurId, m_fgDecalOverlayId,
													irrView, irrSamp, prefilterView, prefilterSamp, brdfView, brdfSamp,
													ddgiView, ddgiSamp,
													shadowViews, shadowUbo,
													pointLightUbo,
													lp, frameIdx);
											});

										// M100.14 — Water render pass FG-intégré (ping-pong SceneColor_HDR → SceneColor_HDR_PostWater).
										// Si WaterPass::Init a échoué (shaders, normal map ou skybox absents),
										// on enregistre un passthrough vkCmdCopyImage à la place pour garantir
										// que le ping-pong PostWater est toujours renseigné — sinon Bloom_Prefilter
										// (qui lit PostWater) lirait du contenu indéfini.
										//
										// IMPORTANT : on n'enregistre la passe « Water » (ColorWrite sur PostWater)
										// QUE si un mesh d'eau valide existe (m_waterMeshGpu.IsValid()). Sinon le record
										// sortirait tôt (scene/mesh absent) et PostWater resterait NON ÉCRIT → bloom lit
										// du garbage → écran blanc délavé. Sans eau réelle, on prend le passthrough qui
										// copie SceneColor → PostWater et garantit un contenu valide.
										// DIAG one-shot : revele quelle branche eau est prise (et pourquoi).
										{
											static bool s_waterBranchLogged = false;
											if (!s_waterBranchLogged)
											{
												s_waterBranchLogged = true;
												LOG_INFO(Render, "[WaterDiag] gating: waterPassValid={} meshValid={} -> branche={}",
													m_waterPass.IsValid(), m_waterMeshGpu.IsValid(),
													(m_waterPass.IsValid() && m_waterMeshGpu.IsValid()) ? "WATER(dessin quad)" : "PASSTHROUGH(pas d'eau)");
											}
										}
										// Le FrameGraph MVP INTERDIT deux writers sur la meme ressource
										// (PostWater). On ne peut donc PAS faire "passe copie" + "passe eau"
										// separees. On fait donc UNE SEULE passe, writer unique de PostWater,
										// qui fait les deux choses a la suite :
										//   (1) copie SceneColor_HDR -> PostWater (toute la scene),
										//   (2) dessine le quad d'eau PAR-DESSUS (renderpass loadOp=LOAD).
										// On declare read(SceneColor)+read(depth)+write(PostWater) : le FG met
										// SceneColor en SHADER_READ et PostWater en COLOR_ATTACHMENT avant le
										// lambda. On bascule manuellement en layouts transfer pour la copie,
										// puis on restaure pour que (2) trouve les bons layouts.
										if (m_waterPass.IsValid() && m_waterMeshGpu.IsValid())
										{
											m_frameGraph.addPass("Water",
												[this](engine::render::PassBuilder& b) {
													b.read(m_fgSceneColorHDRId,           engine::render::ImageUsage::SampledRead);
													b.read(m_fgDepthId,                   engine::render::ImageUsage::SampledRead);
													b.write(m_fgSceneColorHDRPostWaterId, engine::render::ImageUsage::ColorWrite);
												},
												[this](VkCommandBuffer cmd, engine::render::Registry& reg) {
													const engine::world::water::WaterScene* scene = nullptr;
													if (m_worldEditorExe && m_worldEditorShell)
														scene = &m_worldEditorShell->GetWaterDocument().Get();
													else if (m_clientWaterScene)
														scene = m_clientWaterScene.get();
													if (!scene || !m_waterMeshGpu.IsValid()) return;

													VkImage src = reg.getImage(m_fgSceneColorHDRId);
													VkImage dst = reg.getImage(m_fgSceneColorHDRPostWaterId);
													if (src == VK_NULL_HANDLE || dst == VK_NULL_HANDLE) return;

													// (1) Copie SceneColor (SHADER_READ) -> PostWater (COLOR_ATTACHMENT).
													//     On passe les deux en layouts transfer, on copie, on restaure.
													auto barrier = [&](VkImage img, VkImageLayout oldL, VkImageLayout newL,
													                   VkAccessFlags srcA, VkAccessFlags dstA,
													                   VkPipelineStageFlags srcS, VkPipelineStageFlags dstS) {
														VkImageMemoryBarrier bar{};
														bar.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
														bar.srcAccessMask = srcA;
														bar.dstAccessMask = dstA;
														bar.oldLayout = oldL;
														bar.newLayout = newL;
														bar.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
														bar.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
														bar.image = img;
														bar.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
														vkCmdPipelineBarrier(cmd, srcS, dstS, 0, 0, nullptr, 0, nullptr, 1, &bar);
													};

													barrier(src, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
													        VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_READ_BIT,
													        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
													barrier(dst, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
													        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
													        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

													VkImageCopy copy{};
													copy.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
													copy.srcSubresource.layerCount = 1;
													copy.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
													copy.dstSubresource.layerCount = 1;
													VkExtent2D ext = m_vkSwapchain.GetExtent();
													copy.extent = { ext.width, ext.height, 1 };
													vkCmdCopyImage(cmd, src, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
														dst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);

													// Restaure : SceneColor -> SHADER_READ (lu par le shader eau),
													//            PostWater  -> COLOR_ATTACHMENT (cible du renderpass LOAD).
													barrier(src, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
													        VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_SHADER_READ_BIT,
													        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
													barrier(dst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
													        VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
													        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);

													// (2) Dessine l'eau PAR-DESSUS la scene copiee (loadOp=LOAD).
													const uint32_t readIdx = m_renderReadIndex.load(std::memory_order_acquire);
													const engine::RenderState& rs = m_renderStates[readIdx];

													engine::render::WaterPassPushConstants base{};
													std::memcpy(base.viewProj, rs.viewProjMatrix.m, sizeof(float) * 16);
													base.cameraPos[0] = rs.camera.position.x;
													base.cameraPos[1] = rs.camera.position.y;
													base.cameraPos[2] = rs.camera.position.z;
													// TODO(M100.x): vraie source de temps wall-clock. Note : perte de précision
													// float après ~78h (uint32_t > 2^24 = 16.7 M frames @ 60Hz).
													base.timeSeconds   = static_cast<float>(m_currentFrame) / 60.0f;
													base.screenSize[0] = static_cast<float>(m_vkSwapchain.GetExtent().width);
													base.screenSize[1] = static_cast<float>(m_vkSwapchain.GetExtent().height);

													const uint32_t frameIdx = m_currentFrame % 2;
													m_waterPass.Record(m_vkDeviceContext.GetDevice(), cmd, reg, m_vkSwapchain.GetExtent(),
														m_fgSceneColorHDRId, m_fgDepthId, m_fgSceneColorHDRPostWaterId,
														m_waterMeshGpu, base, *scene, frameIdx);
												});
										}
										else
										{
											// Pas d'eau prete -> simple copie SceneColor -> PostWater (writer
											// unique) pour que les passes aval (Bloom_*) lisent une image valide.
											m_frameGraph.addPass("Water_Passthrough",
												[this](engine::render::PassBuilder& b) {
													b.read(m_fgSceneColorHDRId,           engine::render::ImageUsage::TransferSrc);
													b.write(m_fgSceneColorHDRPostWaterId, engine::render::ImageUsage::TransferDst);
												},
												[this](VkCommandBuffer cmd, engine::render::Registry& reg) {
													VkImage src = reg.getImage(m_fgSceneColorHDRId);
													VkImage dst = reg.getImage(m_fgSceneColorHDRPostWaterId);
													if (src == VK_NULL_HANDLE || dst == VK_NULL_HANDLE) return;
													VkImageCopy copy{};
													copy.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
													copy.srcSubresource.layerCount = 1;
													copy.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
													copy.dstSubresource.layerCount = 1;
													VkExtent2D ext = m_vkSwapchain.GetExtent();
													copy.extent = { ext.width, ext.height, 1 };
													vkCmdCopyImage(cmd, src, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
														dst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
												});
										}

										// M45.2 — Brouillard volumique + god rays. Lit SceneColor_HDR_PostWater,
										// le depth et la shadow map cascade 0, ray-marche par pixel et ecrit
										// SceneColor_HDR_Fogged. Si la passe fog est invalide (shaders absents /
										// Init KO), on enregistre a la place un passthrough vkCmdCopyImage qui
										// recopie PostWater -> Fogged, exactement comme Water_Passthrough, pour
										// garantir que Bloom_Prefilter (qui lit desormais Fogged) trouve une
										// image valide. Le gating runtime (density<=0) est gere dans le shader.
										if (m_volumetricFogReady)
										{
											m_frameGraph.addPass("VolumetricFog",
												[this](engine::render::PassBuilder& b) {
													b.read(m_fgSceneColorHDRPostWaterId, engine::render::ImageUsage::SampledRead);
													b.read(m_fgDepthId,                  engine::render::ImageUsage::SampledRead);
													b.read(m_fgShadowMapIds[0],          engine::render::ImageUsage::SampledRead);
													b.write(m_fgSceneColorFoggedId,      engine::render::ImageUsage::ColorWrite);
												},
												[this](VkCommandBuffer cmd, engine::render::Registry& reg) {
													if (!m_pipeline->GetVolumetricFogPass().IsValid()) return;
													const uint32_t readIdx = m_renderReadIndex.load(std::memory_order_acquire);
													const engine::RenderState& rs = m_renderStates[readIdx];
													engine::render::VolumetricFogPass::FogParams fp{};
													// invViewProj = inverse de rs.viewProjMatrix (meme routine que Lighting).
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
														fp.invViewProj[0]  = ( a11*b11-a21*b10+a31*b09)*inv;
														fp.invViewProj[1]  = (-a10*b11+a20*b10-a30*b09)*inv;
														fp.invViewProj[2]  = ( a13*b05-a23*b04+a33*b03)*inv;
														fp.invViewProj[3]  = (-a12*b05+a22*b04-a32*b03)*inv;
														fp.invViewProj[4]  = (-a01*b11+a21*b08-a31*b07)*inv;
														fp.invViewProj[5]  = ( a00*b11-a20*b08+a30*b07)*inv;
														fp.invViewProj[6]  = (-a03*b05+a23*b02-a33*b01)*inv;
														fp.invViewProj[7]  = ( a02*b05-a22*b02+a32*b01)*inv;
														fp.invViewProj[8]  = ( a01*b10-a11*b08+a31*b06)*inv;
														fp.invViewProj[9]  = (-a00*b10+a10*b08-a30*b06)*inv;
														fp.invViewProj[10] = ( a03*b04-a13*b02+a33*b00)*inv;
														fp.invViewProj[11] = (-a02*b04+a12*b02-a32*b00)*inv;
														fp.invViewProj[12] = (-a01*b09+a11*b07-a21*b06)*inv;
														fp.invViewProj[13] = ( a00*b09-a10*b07+a20*b06)*inv;
														fp.invViewProj[14] = (-a03*b03+a13*b01-a23*b00)*inv;
														fp.invViewProj[15] = ( a02*b03-a12*b01+a22*b00)*inv;
													}
													// sunViewProj = lightViewProj de la cascade 0.
													std::memcpy(fp.sunViewProj, rs.cascades.lightViewProj[0].m, sizeof(float) * 16);
													fp.cameraPos[0] = rs.camera.position.x;
													fp.cameraPos[1] = rs.camera.position.y;
													fp.cameraPos[2] = rs.camera.position.z;
													// densite fog (defaut 0 = desactive ; gating runtime dans le shader).
													fp.cameraPos[3] = static_cast<float>(m_cfg.GetDouble("world.volfog.density", 0.0));
													// direction VERS le soleil (normalisee).
													{
														float sx = m_zoneAtmosphere.sunDirection[0];
														float sy = m_zoneAtmosphere.sunDirection[1];
														float sz = m_zoneAtmosphere.sunDirection[2];
														float sl = std::sqrt(sx*sx + sy*sy + sz*sz);
														if (sl > 1e-6f) { sx /= sl; sy /= sl; sz /= sl; }
														fp.sunDir[0] = sx; fp.sunDir[1] = sy; fp.sunDir[2] = sz;
													}
													fp.sunDir[3]   = static_cast<float>(m_cfg.GetDouble("world.volfog.anisotropy", 0.6));
													fp.sunColor[0] = m_zoneAtmosphere.sunColor[0];
													fp.sunColor[1] = m_zoneAtmosphere.sunColor[1];
													fp.sunColor[2] = m_zoneAtmosphere.sunColor[2];
													fp.sunColor[3] = static_cast<float>(m_cfg.GetDouble("world.volfog.inscatter", 1.0));
													fp.fogParams[0] = static_cast<float>(m_cfg.GetDouble("world.volfog.steps", 32.0));
													fp.fogParams[1] = static_cast<float>(m_cfg.GetDouble("world.volfog.max_dist_m", 120.0));
													fp.fogParams[2] = static_cast<float>(m_cfg.GetDouble("world.volfog.height_m", 10.0));
													fp.fogParams[3] = static_cast<float>(m_cfg.GetDouble("world.volfog.height_falloff", 0.1));
													m_pipeline->GetVolumetricFogPass().Record(
														m_vkDeviceContext.GetDevice(), cmd, reg,
														m_vkSwapchain.GetExtent(),
														m_fgSceneColorHDRPostWaterId, m_fgDepthId, m_fgShadowMapIds[0],
														m_fgSceneColorFoggedId, fp, m_currentFrame % 2);
												});
										}
										else
										{
											// Fog indisponible -> copie PostWater -> Fogged (writer unique) pour
											// que Bloom_Prefilter lise une image valide. Meme pattern que
											// Water_Passthrough (les barrieres FG posent TransferSrc/TransferDst).
											m_frameGraph.addPass("VolumetricFog_Passthrough",
												[this](engine::render::PassBuilder& b) {
													b.read(m_fgSceneColorHDRPostWaterId, engine::render::ImageUsage::TransferSrc);
													b.write(m_fgSceneColorFoggedId,      engine::render::ImageUsage::TransferDst);
												},
												[this](VkCommandBuffer cmd, engine::render::Registry& reg) {
													VkImage src = reg.getImage(m_fgSceneColorHDRPostWaterId);
													VkImage dst = reg.getImage(m_fgSceneColorFoggedId);
													if (src == VK_NULL_HANDLE || dst == VK_NULL_HANDLE) return;
													VkImageCopy copy{};
													copy.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
													copy.srcSubresource.layerCount = 1;
													copy.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
													copy.dstSubresource.layerCount = 1;
													VkExtent2D ext = m_vkSwapchain.GetExtent();
													copy.extent = { ext.width, ext.height, 1 };
													vkCmdCopyImage(cmd, src, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
														dst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
												});
										}

										// Nuages volumétriques — passe insérée entre VolumetricFog et Bloom.
										// Lit la scène brouillardée + le depth, compose les nuages ray-marchés
										// et écrit SceneColor_HDR_Clouds. Les matrices caméra (invViewProj +
										// cameraPos) sont recalculées EXACTEMENT comme la passe fog ci-dessus
										// (même rs.viewProjMatrix / rs.camera.position) ; l'apparence vient de
										// l'état jour/nuit (m_dayNight) et de la météo (m_weatherSystem via
										// CloudWeatherMapper). Si la passe nuages n'est pas prête, on garde
										// SceneColor_HDR_Fogged comme source du bloom (cf sceneAfterClouds).
										const bool cloudsEnabled = m_cfg.GetBool("render.clouds.enabled", true);
										{
											// Diagnostic one-shot : confirme si la passe nuages est active
											// (enabled + SPIR-V chargé + pipeline OK). Tranche "build/spv" vs "réglage".
											static bool s_cloudDiagLogged = false;
											if (!s_cloudDiagLogged)
											{
												s_cloudDiagLogged = true;
												LOG_INFO(Render, "[Clouds][diag] enabled={} cloudPassReady={}",
													cloudsEnabled,
													(m_pipeline && m_pipeline->IsCloudPassReady()));
											}
										}
										if (cloudsEnabled && m_pipeline && m_pipeline->IsCloudPassReady())
										{
											// Lot 1 (2026-07-18) — la marche des nuages (raymarch coûteux) tourne
											// en RÉSOLUTION RÉDUITE (Clouds_Half) et ne lit plus la scène : le
											// shader écrit couleur pré-multipliée (rgb) + visibilité (a). La
											// composition pleine résolution est une 2e passe (Clouds_Composite).
											m_frameGraph.addPass("Clouds_March",
												[this](engine::render::PassBuilder& b) {
													b.read(m_fgDepthId,       engine::render::ImageUsage::SampledRead);
													b.write(m_fgCloudsHalfId, engine::render::ImageUsage::ColorWrite);
												},
												[this](VkCommandBuffer cmd, engine::render::Registry& reg) {
													if (!m_pipeline->GetCloudPass().IsValid()) return;
													const engine::render::DayNightCycle::State& dn = m_dayNight.GetState();
													engine::render::CloudParams target =
														engine::render::CloudWeatherMapper::ParamsFor(m_weatherSystem.GetTargetState());
													engine::render::CloudParams current =
														engine::render::CloudWeatherMapper::ParamsFor(m_weatherSystem.GetCurrentState());
													engine::render::CloudParams cp =
														engine::render::CloudParams::Lerp(current, target, m_weatherSystem.GetIntensity());

													// Surcharges LIVE (config.json, sans rebuild) pour le réglage visuel :
													// échelle de couverture/densité + altitude/épaisseur de la couche.
													float cloudCoverage = cp.coverage * static_cast<float>(m_cfg.GetDouble("render.clouds.coverage_scale", 1.0));
													cloudCoverage = cloudCoverage < 0.0f ? 0.0f : (cloudCoverage > 1.0f ? 1.0f : cloudCoverage);
													float cloudDensityV = cp.density * static_cast<float>(m_cfg.GetDouble("render.clouds.density_scale", 1.0));
													if (cloudDensityV < 0.0f) cloudDensityV = 0.0f;
													const double baseAltCfg = m_cfg.GetDouble("render.clouds.base_altitude_m", 0.0); // 0 = auto (météo)
													const double thickCfg   = m_cfg.GetDouble("render.clouds.thickness_m", 0.0);     // 0 = auto
													float cloudBaseAlt = baseAltCfg > 0.0 ? static_cast<float>(baseAltCfg) : cp.baseAltMeters;
													float cloudTopAlt  = thickCfg   > 0.0 ? (cloudBaseAlt + static_cast<float>(thickCfg)) : cp.topAltMeters;
													if (cloudTopAlt <= cloudBaseAlt) cloudTopAlt = cloudBaseAlt + 100.0f;

													engine::render::CloudPass::CloudPushConstants pc{};
													// invViewProj + cameraPos. On utilise la matrice NON-jitterée
													// (viewProjMatrixUnjittered) : le raymarch nuages doit être stable
													// temporellement, sinon le jitter TAA fait scintiller les nuages
													// (haute fréquence, non reprojetables faute de vecteurs de mouvement).
													const uint32_t readIdx = m_renderReadIndex.load(std::memory_order_acquire);
													const engine::RenderState& rs = m_renderStates[readIdx];
													const float* vp = rs.viewProjMatrixUnjittered.m;
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
														pc.invViewProj[0]  = ( a11*b11-a21*b10+a31*b09)*inv;
														pc.invViewProj[1]  = (-a10*b11+a20*b10-a30*b09)*inv;
														pc.invViewProj[2]  = ( a13*b05-a23*b04+a33*b03)*inv;
														pc.invViewProj[3]  = (-a12*b05+a22*b04-a32*b03)*inv;
														pc.invViewProj[4]  = (-a01*b11+a21*b08-a31*b07)*inv;
														pc.invViewProj[5]  = ( a00*b11-a20*b08+a30*b07)*inv;
														pc.invViewProj[6]  = (-a03*b05+a23*b02-a33*b01)*inv;
														pc.invViewProj[7]  = ( a02*b05-a22*b02+a32*b01)*inv;
														pc.invViewProj[8]  = ( a01*b10-a11*b08+a31*b06)*inv;
														pc.invViewProj[9]  = (-a00*b10+a10*b08-a30*b06)*inv;
														pc.invViewProj[10] = ( a03*b04-a13*b02+a33*b00)*inv;
														pc.invViewProj[11] = (-a02*b04+a12*b02-a32*b00)*inv;
														pc.invViewProj[12] = (-a01*b09+a11*b07-a21*b06)*inv;
														pc.invViewProj[13] = ( a00*b09-a10*b07+a20*b06)*inv;
														pc.invViewProj[14] = (-a03*b03+a13*b01-a23*b00)*inv;
														pc.invViewProj[15] = ( a02*b03-a12*b01+a22*b00)*inv;
													}
													pc.cameraPos[0] = rs.camera.position.x;
													pc.cameraPos[1] = rs.camera.position.y;
													pc.cameraPos[2] = rs.camera.position.z;
													// w = temps réel cumulé (s) : advection des nuages par le vent.
													pc.cameraPos[3] = m_cloudTimeSeconds;

													for (int i = 0; i < 3; ++i)
													{
														pc.sunDir[i]       = dn.lightDir[i];
														pc.sunColor[i]     = dn.lightColor[i];
														const float tint   = (i == 0 ? cp.tintR : (i == 1 ? cp.tintG : cp.tintB));
														pc.zenithColor[i]  = dn.skyZenith[i]  * tint;
														pc.horizonColor[i] = dn.skyHorizon[i] * tint;
													}
													pc.sunDir[3]       = cloudCoverage;
													pc.sunColor[3]     = cloudDensityV;
													pc.zenithColor[3]  = cloudBaseAlt;
													pc.horizonColor[3] = cloudTopAlt;

													pc.windParams[0] = 1.0f;
													pc.windParams[1] = 0.3f;
													pc.windParams[2] = static_cast<float>(m_cfg.GetDouble("render.clouds.wind_scale", 6.0));
													pc.windParams[3] = 0.2f;

													pc.stepParams[0] = static_cast<float>(m_cfg.GetInt("render.clouds.raymarch_steps", 64));
													pc.stepParams[1] = static_cast<float>(m_cfg.GetInt("render.clouds.light_steps", 6));
													pc.stepParams[2] = static_cast<float>(m_cfg.GetDouble("render.clouds.max_distance_meters", 60000.0));
													pc.stepParams[3] = static_cast<float>(m_cfg.GetDouble("render.clouds.ambient_strength", 0.4));

													// Phase 2 — force des ombres de nuages au sol (0 = désactivé).
													pc.shadowParams[0] = static_cast<float>(m_cfg.GetDouble("render.clouds.shadow_strength", 0.5));
													// y = plafond de luminosité des nuages (anti-blowout vers le soleil) ; 0 = off.
													pc.shadowParams[1] = static_cast<float>(m_cfg.GetDouble("render.clouds.max_luminance", 1.3));
													// z = distance (m) de début d'estompage des nuages lointains (perspective
													// aérienne) ; évite le mur blanc à l'horizon. 0 = désactivé.
													pc.shadowParams[2] = static_cast<float>(m_cfg.GetDouble("render.clouds.fade_distance_m", 2500.0));
													pc.shadowParams[3] = 0.0f;

													// Extent réduit — même calcul que FrameGraph pour Clouds_Half
													// (shift + clamp à 1) afin que viewport == framebuffer.
													const VkExtent2D fullExt = m_vkSwapchain.GetExtent();
													VkExtent2D scaledExt{
														fullExt.width  >> m_cloudsExtentPower,
														fullExt.height >> m_cloudsExtentPower };
													if (scaledExt.width  < 1u) scaledExt.width  = 1u;
													if (scaledExt.height < 1u) scaledExt.height = 1u;
													m_pipeline->GetCloudPass().RecordMarch(
														m_vkDeviceContext.GetDevice(), cmd, reg, scaledExt,
														m_fgDepthId, m_fgCloudsHalfId, pc, m_currentFrame % 2);
												});

											// Composition pleine résolution : upsample bilinéaire de Clouds_Half
											// sur la scène brouillardée → SceneColor_HDR_Clouds (consommée par
											// Bloom via sceneAfterClouds, inchangé).
											m_frameGraph.addPass("Clouds_Composite",
												[this](engine::render::PassBuilder& b) {
													b.read(m_fgSceneColorFoggedId, engine::render::ImageUsage::SampledRead);
													b.read(m_fgCloudsHalfId,       engine::render::ImageUsage::SampledRead);
													b.write(m_fgCloudsId,          engine::render::ImageUsage::ColorWrite);
												},
												[this](VkCommandBuffer cmd, engine::render::Registry& reg) {
													if (!m_pipeline->GetCloudPass().IsValid()) return;
													m_pipeline->GetCloudPass().RecordComposite(
														m_vkDeviceContext.GetDevice(), cmd, reg, m_vkSwapchain.GetExtent(),
														m_fgSceneColorFoggedId, m_fgCloudsHalfId, m_fgCloudsId, m_currentFrame % 2);
												});
										}

										// Source effective consommée par le bloom : sortie nuages si la passe
										// Clouds est active, sinon la scène brouillardée. Les DEUX consommateurs
										// aval de Fogged (Bloom_Prefilter ET Bloom_Combine) basculent dessus
										// pour rester cohérents (sinon Combine recomposerait sur une base sans
										// nuages).
										const engine::render::ResourceId sceneAfterClouds =
											(cloudsEnabled && m_pipeline && m_pipeline->IsCloudPassReady())
												? m_fgCloudsId : m_fgSceneColorFoggedId;

										m_frameGraph.addPass("Bloom_Prefilter",
											[this, sceneAfterClouds](engine::render::PassBuilder& b) {
												b.read(sceneAfterClouds, engine::render::ImageUsage::SampledRead);
												b.write(m_fgBloomDownMipIds[0], engine::render::ImageUsage::ColorWrite);
											},
											[this, sceneAfterClouds](VkCommandBuffer cmd, engine::render::Registry& reg) {
												if (!m_pipeline->GetBloomPrefilterPass().IsValid()) return;
												engine::render::BloomPrefilterPass::PrefilterParams pp{};
												pp.threshold = static_cast<float>(m_cfg.GetDouble("bloom.threshold", 1.0));
												pp.knee      = static_cast<float>(m_cfg.GetDouble("bloom.knee", 0.5));
												const uint32_t frameIdx = m_currentFrame % 2;
												m_pipeline->GetBloomPrefilterPass().Record(
													m_vkDeviceContext.GetDevice(), cmd, reg,
													m_vkSwapchain.GetExtent(),
													sceneAfterClouds, m_fgBloomDownMipIds[0], pp, frameIdx);
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
											[this, sceneAfterClouds](engine::render::PassBuilder& b) {
												// M45.2 — base de composition = scene + fog volumique (et nuages si
												// la passe Clouds est active : sceneAfterClouds), pas PostWater,
												// sinon le fog/les nuages seraient bypasses dans l'image finale
												// (Combine produit l'image post-bloom consommee par
												// AutoExposure/Tonemap).
												b.read(sceneAfterClouds,             engine::render::ImageUsage::SampledRead);
												b.read(m_fgBloomUpMipIds[0],         engine::render::ImageUsage::SampledRead);
												b.write(m_fgSceneColorHDRWithBloomId, engine::render::ImageUsage::ColorWrite);
											},
											[this, sceneAfterClouds](VkCommandBuffer cmd, engine::render::Registry& reg) {
												if (!m_pipeline->GetBloomCombinePass().IsValid()) return;
												engine::render::BloomCombinePass::CombineParams cp{};
												cp.intensity = static_cast<float>(m_cfg.GetDouble("bloom.intensity", 1.0));
												const uint32_t frameIdx = m_currentFrame % 2;
												m_pipeline->GetBloomCombinePass().Record(m_vkDeviceContext.GetDevice(), cmd, reg, m_vkSwapchain.GetExtent(), sceneAfterClouds, m_fgBloomUpMipIds[0], m_fgSceneColorHDRWithBloomId, cp, frameIdx);
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

										// M45.3 — Profondeur de champ / bokeh. Lit SceneColor_HDR_WithBloom
										// (HDR post-bloom) et le depth, calcule un CoC par pixel et ecrit
										// SceneColor_HDR_Dof. Si la passe DoF est invalide (shaders absents /
										// Init KO), on enregistre a la place un passthrough vkCmdCopyImage qui
										// recopie WithBloom -> Dof, exactement comme VolumetricFog_Passthrough,
										// pour garantir que Tonemap (qui lit desormais Dof) trouve une image
										// valide. Le gating runtime (focus_range_m<=0) est gere dans le shader.
										if (m_dofReady)
										{
											m_frameGraph.addPass("DepthOfField",
												[this](engine::render::PassBuilder& b) {
													b.read(m_fgSceneColorHDRWithBloomId, engine::render::ImageUsage::SampledRead);
													b.read(m_fgDepthId,                  engine::render::ImageUsage::SampledRead);
													b.write(m_fgSceneColorDofId,         engine::render::ImageUsage::ColorWrite);
												},
												[this](VkCommandBuffer cmd, engine::render::Registry& reg) {
													if (!m_pipeline->GetDepthOfFieldPass().IsValid()) return;
													const uint32_t readIdx = m_renderReadIndex.load(std::memory_order_acquire);
													const engine::RenderState& rs = m_renderStates[readIdx];
													engine::render::DepthOfFieldPass::DofParams dp{};
													// invViewProj = inverse de rs.viewProjMatrix (meme routine que VolumetricFog).
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
														dp.invViewProj[0]  = ( a11*b11-a21*b10+a31*b09)*inv;
														dp.invViewProj[1]  = (-a10*b11+a20*b10-a30*b09)*inv;
														dp.invViewProj[2]  = ( a13*b05-a23*b04+a33*b03)*inv;
														dp.invViewProj[3]  = (-a12*b05+a22*b04-a32*b03)*inv;
														dp.invViewProj[4]  = (-a01*b11+a21*b08-a31*b07)*inv;
														dp.invViewProj[5]  = ( a00*b11-a20*b08+a30*b07)*inv;
														dp.invViewProj[6]  = (-a03*b05+a23*b02-a33*b01)*inv;
														dp.invViewProj[7]  = ( a02*b05-a22*b02+a32*b01)*inv;
														dp.invViewProj[8]  = ( a01*b10-a11*b08+a31*b06)*inv;
														dp.invViewProj[9]  = (-a00*b10+a10*b08-a30*b06)*inv;
														dp.invViewProj[10] = ( a03*b04-a13*b02+a33*b00)*inv;
														dp.invViewProj[11] = (-a02*b04+a12*b02-a32*b00)*inv;
														dp.invViewProj[12] = (-a01*b09+a11*b07-a21*b06)*inv;
														dp.invViewProj[13] = ( a00*b09-a10*b07+a20*b06)*inv;
														dp.invViewProj[14] = (-a03*b03+a13*b01-a23*b00)*inv;
														dp.invViewProj[15] = ( a02*b03-a12*b01+a22*b00)*inv;
													}
													dp.cameraPos[0] = rs.camera.position.x;
													dp.cameraPos[1] = rs.camera.position.y;
													dp.cameraPos[2] = rs.camera.position.z;
													// w = distance focale (m).
													dp.cameraPos[3] = static_cast<float>(m_cfg.GetDouble("world.dof.focus_distance_m", 12.0));
													// x = plage de nettete (m ; defaut 0 = desactive ; gating runtime dans le shader).
													dp.dofParams[0] = static_cast<float>(m_cfg.GetDouble("world.dof.focus_range_m", 0.0));
													dp.dofParams[1] = static_cast<float>(m_cfg.GetDouble("world.dof.max_blur_px", 8.0));
													dp.dofParams[2] = static_cast<float>(m_cfg.GetDouble("world.dof.near_scale", 1.0));
													dp.dofParams[3] = static_cast<float>(m_cfg.GetDouble("world.dof.far_scale", 1.0));
													VkExtent2D ext = m_vkSwapchain.GetExtent();
													dp.texelSize[0] = (ext.width  > 0) ? (1.0f / static_cast<float>(ext.width))  : 0.0f;
													dp.texelSize[1] = (ext.height > 0) ? (1.0f / static_cast<float>(ext.height)) : 0.0f;
													dp.texelSize[2] = 0.0f;
													dp.texelSize[3] = 0.0f;
													m_pipeline->GetDepthOfFieldPass().Record(
														m_vkDeviceContext.GetDevice(), cmd, reg,
														ext,
														m_fgSceneColorHDRWithBloomId, m_fgDepthId,
														m_fgSceneColorDofId, dp, m_currentFrame % 2);
												});
										}
										else
										{
											// DoF indisponible -> copie WithBloom -> Dof (writer unique) pour que
											// Tonemap lise une image valide. Meme pattern que VolumetricFog_Passthrough.
											m_frameGraph.addPass("DepthOfField_Passthrough",
												[this](engine::render::PassBuilder& b) {
													b.read(m_fgSceneColorHDRWithBloomId, engine::render::ImageUsage::TransferSrc);
													b.write(m_fgSceneColorDofId,         engine::render::ImageUsage::TransferDst);
												},
												[this](VkCommandBuffer cmd, engine::render::Registry& reg) {
													VkImage src = reg.getImage(m_fgSceneColorHDRWithBloomId);
													VkImage dst = reg.getImage(m_fgSceneColorDofId);
													if (src == VK_NULL_HANDLE || dst == VK_NULL_HANDLE) return;
													VkImageCopy copy{};
													copy.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
													copy.srcSubresource.layerCount = 1;
													copy.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
													copy.dstSubresource.layerCount = 1;
													VkExtent2D ext = m_vkSwapchain.GetExtent();
													copy.extent = { ext.width, ext.height, 1 };
													vkCmdCopyImage(cmd, src, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
														dst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
												});
										}

										m_frameGraph.addPass("Tonemap",
											[this](engine::render::PassBuilder& b) {
												b.read(m_fgSceneColorDofId,          engine::render::ImageUsage::SampledRead);
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
												m_pipeline->GetTonemapPass().Record(m_vkDeviceContext.GetDevice(), cmd, reg, m_vkSwapchain.GetExtent(), m_fgSceneColorDofId, m_fgSceneColorLDRId, tp, lutView, frameIdx);
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
													LOG_DEBUG(Render, "[CopyPresent] debug solid-color present enabled; skipping scene copy");
													const VkClearColorValue debugColor = { { 0.9f, 0.0f, 0.9f, 1.0f } };
													VkImageSubresourceRange clearRange{};
													clearRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
													clearRange.baseMipLevel = 0;
													clearRange.levelCount = 1;
													clearRange.baseArrayLayer = 0;
													clearRange.layerCount = 1;
													vkCmdClearColorImage(cmd, dstImg, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &debugColor, 1, &clearRange);
													LOG_DEBUG(Render, "[CopyPresent] debug clear color applied");
												}
												else
												{
													LOG_DEBUG(Render, "[CopyPresent] vkCmdCopyImage begin");
													// Use a direct copy for presentation. Some Intel/swapchain combinations are fragile
													// with vkCmdBlitImage here even when source and destination extents match.
													VkImageCopy region{};
													region.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
													region.srcOffset = { 0, 0, 0 };
													region.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
													region.dstOffset = { 0, 0, 0 };
													region.extent = { ext.width, ext.height, 1 };
													vkCmdCopyImage(cmd, srcImg, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dstImg, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
													LOG_DEBUG(Render, "[CopyPresent] vkCmdCopyImage done");
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
																"[CopyPresent] auth fond: blit {}x{} -> {}x{} fit={} (log unique; blit chaque frame)",
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
												const bool authImguiSkipsVkOverlay = m_authImGui != nullptr
													&& m_cfg.GetBool("render.auth_ui.imgui.enabled", false);
												if (authVisualState.active
													&& authUiDynamicRenderingEnabled
													&& !authImguiSkipsVkOverlay
													&& backbufferView != VK_NULL_HANDLE
													&& m_vkDeviceContext.SupportsDynamicRendering())
												{
													LOG_DEBUG(Render, "[CopyPresent] auth overlay enabled; building model");
													const engine::client::AuthUiPresenter::RenderModel authRenderModel = m_authUi.BuildRenderModel();
													LOG_DEBUG(Render, "[CopyPresent] auth render model built; loading theme");
													const engine::render::AuthUiTheme authTheme = engine::render::LoadAuthUiTheme(m_cfg);
													LOG_DEBUG(Render, "[CopyPresent] auth theme loaded; issuing barriers");
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

													LOG_DEBUG(Render, "[CopyPresent] begin rendering attachment");
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
													LOG_DEBUG(Render, "[CopyPresent] attachment info ready (view={})", (void*)backbufferView);

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
													LOG_DEBUG(Render, "[CopyPresent] vkCmdBeginRendering call (proc lookup)");

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
														LOG_DEBUG(Render, "[CopyPresent] vkCmdBeginRendering returned");

														LOG_DEBUG(Render, "[CopyPresent] building UI layers");
														const bool authCalibOverlay = m_cfg.GetBool("render.auth_ui_calibration_overlay.enabled", false);
														const std::vector<engine::render::AuthUiLayer> layers =
															engine::render::BuildAuthUiLayers(ext, authVisualState, authRenderModel, authTheme, authCalibOverlay, authPhotoBackdrop);
														LOG_DEBUG(Render, "[CopyPresent] UI layers built; clearing attachments");
														for (const engine::render::AuthUiLayer& layer : layers)
														{
															VkClearAttachment clearAttachment{};
															clearAttachment.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
															clearAttachment.colorAttachment = 0;
															clearAttachment.clearValue.color = layer.color;
															vkCmdClearAttachments(cmd, 1, &clearAttachment, 1, &layer.rect);
														}
														LOG_DEBUG(Render, "[CopyPresent] UI layers cleared; recording glyphs (if valid)");
														// Dessiner le logo AVANT le texte pour éviter qu’un PNG opaque ne recouvre les glyphes.
														// Logo statut : affiche pendant la requete HTTP (spin) ET ensuite, des qu'un
														// resultat de sonde est connu, pour montrer success/error. Sans cette seconde
														// condition, l'utilisateur perd tout retour visuel quand le master est down.
														const bool showAuthStatusLogo = authVisualState.login
															&& (authVisualState.authLogoSpin || authVisualState.authStatusKnown);
														if (m_authLogoPass.IsValid() && showAuthStatusLogo)
														{
															engine::render::TextureAsset* logoTex = nullptr;
															if (authVisualState.authLogoSpin && m_authLogoTexture.IsValid())
															{
																logoTex = m_authLogoTexture.Get();
															}
															else if (authVisualState.authStatusKnown && authVisualState.authStatusOk
																&& m_authLogoSuccessTexture.IsValid())
															{
																logoTex = m_authLogoSuccessTexture.Get();
															}
															else if (authVisualState.authStatusKnown && !authVisualState.authStatusOk
																&& m_authLogoErrorTexture.IsValid())
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
																const float margin =
																	static_cast<float>(engine::render::kAuthUiStatusLogoCornerMarginPx);
																const float cx = margin + half;
																// Ajustement repère vertical : le shader du logo attend un centre "haut-gauche"
																// alors que le rendu actuel le place en bas-gauche.
																const float cy = static_cast<float>(ext.height) - (margin + half);
																// L’orientation 180° est appliquée dans AuthLogoPass ; ici uniquement l’angle de spin chargement.
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
																	spin);
															}
														}
														{
															const std::vector<engine::render::AuthFieldInfoIconLayout> infoLayouts =
																engine::render::BuildAuthFieldInfoIconLayouts(ext, authVisualState, authRenderModel);
															for (const engine::render::AuthFieldInfoIconLayout& iconLay : infoLayouts)
															{
																if (!iconLay.valid)
																{
																	continue;
																}
																engine::render::TextureHandle& infoTex = authVisualState.registerMode
																	? m_authUiInfoRegisterTexture
																	: m_authUiInfoLoginTexture;
																bool& infoLayoutReady = authVisualState.registerMode
																	? m_authUiInfoRegisterLayoutReady
																	: m_authUiInfoLoginLayoutReady;
																engine::render::TextureAsset* it = infoTex.Get();
																if (!it || it->image == VK_NULL_HANDLE || it->view == VK_NULL_HANDLE)
																{
																	continue;
																}
																m_authLogoPass.Record(
																	m_vkDeviceContext.GetDevice(),
																	cmd,
																	ext,
																	it->image,
																	it->view,
																	infoLayoutReady,
																	iconLay.centerXPx,
																	iconLay.centerYPx,
																	iconLay.halfExtentPx,
																	0.f,
																	0.f);
															}
														}
														{
															if (authRenderModel.languageFirstRunLayout && m_authLogoPass.IsValid())
															{
																const engine::render::AuthUiLayoutMetrics layLang =
																	engine::render::BuildAuthUiLayoutMetrics(ext, authVisualState, authRenderModel);
																for (int32_t ci = 0; ci < layLang.languageCardCount; ++ci)
																{
																	if (static_cast<size_t>(ci) >= authRenderModel.languageFirstRunCards.size())
																	{
																		break;
																	}
																	const std::string& tag =
																		authRenderModel.languageFirstRunCards[static_cast<size_t>(ci)].localeTag;
																	engine::render::TextureAsset* flagTex = nullptr;
																	bool* flagLayoutReady = nullptr;
																	if (tag == "fr")
																	{
																		flagTex = m_authFlagFrTexture.Get();
																		flagLayoutReady = &m_authFlagFrLayoutReady;
																	}
																	else if (tag == "en")
																	{
																		flagTex = m_authFlagEnTexture.Get();
																		flagLayoutReady = &m_authFlagEnLayoutReady;
																	}
																	if (flagTex == nullptr || flagTex->image == VK_NULL_HANDLE || flagTex->view == VK_NULL_HANDLE
																		|| flagLayoutReady == nullptr)
																	{
																		continue;
																	}
																	m_authLogoPass.Record(
																		m_vkDeviceContext.GetDevice(),
																		cmd,
																		ext,
																		flagTex->image,
																		flagTex->view,
																		*flagLayoutReady,
																		static_cast<float>(layLang.languageFlagCenterX[ci]),
																		static_cast<float>(layLang.languageFlagCenterY[ci]),
																		static_cast<float>(layLang.languageFlagHalfExtentPx[ci]),
																		0.f,
																		0.f);
																}
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
														LOG_DEBUG(Render, "[CopyPresent] vkCmdEndRendering done; barrier to present");

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
														LOG_DEBUG(Render, "[CopyPresent] auth dynamic rendering disabled by config; using present-only path");
													if (authVisualState.active && backbufferView == VK_NULL_HANDLE)
														LOG_DEBUG(Render, "[CopyPresent] backbuffer imageView is null; skipping auth UI overlay");

													bool worldEditorUiToPresent = false;
#if defined(_WIN32)
													// Phase 3.11.1 — RecordToBackbuffer fire aussi quand le panneau chat est actif post-auth
													// (le draw list ImGui contient alors la fenêtre chat émise par ChatImGuiRenderer).
													// Responsabilite : chat HUD VISIBLE uniquement si auth INITIALISEE *et* COMPLETE.
													// Sans `IsInitialized()`, un echec d'init de localisation fait passer authGateActive
													// a false (m_initialized=false) et le chat apparaissait alors par-dessus un ecran noir.
													const bool chatImguiActive = m_chatImGui && m_chatUi.IsInitialized()
														&& m_authUi.IsInitialized() && m_authUi.IsMasterAuthenticated()
														&& !m_worldEditorExe
														&& (m_cfg.GetBool("render.chat_imgui.enabled", true) || m_inGamePauseMenuVisible || m_inGameOptionsPanelVisible);
													// M43.4 — RecordToBackbuffer également quand --editor (sans world-editor).
													const bool editorHubActive = m_editorHubImGui && m_editorEnabled && !m_worldEditorExe;
													if (m_worldEditorImGui && m_worldEditorImGui->IsReady()
														&& (m_worldEditorExe
															|| (m_authImGui && authVisualState.active
																&& m_cfg.GetBool("render.auth_ui.imgui.enabled", false))
															|| chatImguiActive
															|| editorHubActive)
														&& m_vkDeviceContext.SupportsDynamicRendering() && backbufferView != VK_NULL_HANDLE
														&& !presentSolidColorDebug)
													{
														worldEditorUiToPresent = m_worldEditorImGui->RecordToBackbuffer(
															cmd, dstImg, backbufferView, ext, m_vkDeviceContext);
													}
#endif
													if (!worldEditorUiToPresent)
													{
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
			// Sous-projet C MVP (Task 12) — Detruit le viewport offscreen
			// race AVANT m_editorViewportTarget (idem motif que celui-ci :
			// ordre LIFO de liberation des descriptors ImGui).
			m_racePreviewViewport.Shutdown(m_vkDeviceContext.GetDevice());
			// M100.34 incrément 1 — détruit l'image offscreen viewport
			// AVANT TexturePreviewCache (qui possède aussi des descriptors
			// ImGui), pour respecter l'ordre LIFO de désallocation.
			m_editorViewportTarget.Shutdown(m_vkDeviceContext.GetDevice());
#if defined(_WIN32)
			if (m_texturePreviewCache) m_texturePreviewCache->Shutdown();
			m_texturePreviewCache.reset();
			// M100.1 — Persiste le layout du shell (ImGui::SaveIniSettingsToDisk)
			// AVANT WorldEditorImGui::Shutdown, qui detruit le contexte ImGui.
			// Sinon le Shell::Shutdown appele plus bas (apres destruction du
			// contexte + du device Vulkan) ecrirait via un contexte ImGui nul
			// -> access violation a la fermeture (SEH 0xC0000005). Idempotent :
			// le 2e Shutdown plus bas est neutralise par la garde m_initialized.
			if (m_worldEditorShell) m_worldEditorShell->Shutdown();
			// SP-E — Libère les descripteurs d'icônes (ImGui_ImplVulkan_RemoveTexture)
			// AVANT la destruction du backend ImGui Vulkan ci-dessous.
			m_skillIconCache.Shutdown();
			if (m_worldEditorImGui)
			{
				m_worldEditorImGui->DetachPlatformWindow(m_window);
				m_worldEditorImGui->Shutdown(m_vkDeviceContext.GetDevice());
				m_worldEditorImGui.reset();
			}
			m_authImGui.reset();
			m_worldEditorTerrainTools.Shutdown();
#endif
		if (m_terrain.IsValid())
			m_terrain.Destroy(m_vkDeviceContext.GetDevice());
		// M100 — Task 12 : shutdown terrain chunk runtime AVANT le DeferredPipeline
		// (le renderer dépend du renderPass `m_pipeline->GetGeometryPass`).
		if (m_terrainChunkRenderer)
		{
			m_terrainChunkRenderer->Shutdown(m_vkDeviceContext.GetDevice());
			m_terrainChunkRenderer.reset();
		}
		DestroyTerrainChunkCameraResources();
		m_authGlyphPass.Destroy(m_vkDeviceContext.GetDevice());
			m_authLogoPass.Destroy(m_vkDeviceContext.GetDevice());
			// M100.14 — Détruit la passe water + ses buffers GPU avant le DeferredPipeline
			// (les ressources sont indépendantes mais l'ordre symétrique d'Init est plus
			// lisible côté boot ↔ shutdown).
			m_waterPass.Destroy(m_vkDeviceContext.GetDevice());
			// Textures muettes WaterPass (normalMap + skybox cube).
			{
				VkDevice dev = m_vkDeviceContext.GetDevice();
				if (m_waterNormalMapSampler != VK_NULL_HANDLE)
				{ vkDestroySampler(dev, m_waterNormalMapSampler, nullptr); m_waterNormalMapSampler = VK_NULL_HANDLE; }
				if (m_waterNormalMapView != VK_NULL_HANDLE)
				{ vkDestroyImageView(dev, m_waterNormalMapView, nullptr);  m_waterNormalMapView = VK_NULL_HANDLE; }
				if (m_waterNormalMapImg != VK_NULL_HANDLE)
				{ vkDestroyImage(dev, m_waterNormalMapImg, nullptr);       m_waterNormalMapImg = VK_NULL_HANDLE; }
				if (m_waterNormalMapMem != VK_NULL_HANDLE)
				{ vkFreeMemory(dev, m_waterNormalMapMem, nullptr);         m_waterNormalMapMem = VK_NULL_HANDLE; }
				if (m_waterSkyboxSampler != VK_NULL_HANDLE)
				{ vkDestroySampler(dev, m_waterSkyboxSampler, nullptr);    m_waterSkyboxSampler = VK_NULL_HANDLE; }
				if (m_waterSkyboxView != VK_NULL_HANDLE)
				{ vkDestroyImageView(dev, m_waterSkyboxView, nullptr);     m_waterSkyboxView = VK_NULL_HANDLE; }
				if (m_waterSkyboxImg != VK_NULL_HANDLE)
				{ vkDestroyImage(dev, m_waterSkyboxImg, nullptr);          m_waterSkyboxImg = VK_NULL_HANDLE; }
				if (m_waterSkyboxMem != VK_NULL_HANDLE)
				{ vkFreeMemory(dev, m_waterSkyboxMem, nullptr);            m_waterSkyboxMem = VK_NULL_HANDLE; }
			}
			// Phase 5 Lunar + M38.1 Sky : detruit le SkyPass (pipeline + layout)
			// avant le DeferredPipeline (m_skyPass depend du renderPass de
			// GeometryPass, donc on libere le pipeline d'abord).
			if (m_skyPassReady)
			{
				m_skyPass.Shutdown(m_vkDeviceContext.GetDevice());
				m_skyPassReady = false;
			}
			m_waterMeshGpu.Destroy();
			if (m_waterTransferPool != VK_NULL_HANDLE)
			{
				vkDestroyCommandPool(m_vkDeviceContext.GetDevice(), m_waterTransferPool, nullptr);
				m_waterTransferPool = VK_NULL_HANDLE;
			}
			// Sous-projet A (Task 15) + Sous-projet C MVP : libere les meshes
			// skinnes (ownership = m_raceMeshes depuis C MVP, plus
			// m_currentSkinnedMesh qui n'est plus qu'un pointeur dans la map),
			// puis le renderer. Doit etre fait apres vkDeviceWaitIdle (line ~5060)
			// et AVANT m_pipeline->Destroy (le SkinnedRenderer reutilise le
			// materialLayout du MaterialDescriptorCache mais possede son propre
			// pipeline/render pass -- l'ordre n'est pas critique entre eux, mais
			// on respecte l'ordre LIFO).
			// Idempotent : safe si Init a echoue (map vide -> boucle skippee,
			// handles VK_NULL_HANDLE skippes par SkinnedMesh::Destroy).
			for (auto& kv : m_raceMeshes)
			{
				kv.second.Destroy(m_vkDeviceContext.GetDevice());
			}
			m_raceMeshes.clear();
			m_chestMesh.Destroy(m_vkDeviceContext.GetDevice());
			m_currentSkinnedMesh = nullptr;
			m_skinnedRenderer.Destroy(m_vkDeviceContext.GetDevice());
			m_skinnedAvatarReady = false;
			// M45.7 — libère le volume DDGI + sa passe de mise à jour (idempotent :
			// sûr même si DDGI n'a jamais été activé/alloué).
			m_ddgiUpdatePass.Destroy(m_vkDeviceContext.GetDevice());
			m_ddgiVolume.Destroy(m_vkDeviceContext.GetDevice(), m_vmaAllocator);
			m_ddgiEnabled = false;
			if (m_pipeline)
			{
				m_pipeline->Destroy(m_vkDeviceContext.GetDevice());
				m_pipeline.reset();
			}
			m_profilerHud.Shutdown();
			m_profiler.Shutdown(m_vkDeviceContext.GetDevice());
			m_audioEngine.Shutdown();
			// Le réticule détient un handle de decal persistant : à retirer AVANT
			// le Shutdown du DecalSystem (sa texture appartient à l'AssetRegistry).
			m_targetReticle.Shutdown();
			m_decalSystem.Shutdown();
			m_assetRegistry.Destroy();
			m_frameGraph.destroy(m_vkDeviceContext.GetDevice(), m_vmaAllocator);
			m_stagingAllocator.Destroy(m_vkDeviceContext.GetDevice());
			engine::render::DestroyFrameResources(m_vkDeviceContext.GetDevice(), m_frameResources);
			// VMA is disabled (STAB.7); m_vmaAllocator is always nullptr.
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
		// M100.1 — Persiste le layout ImGui du nouvel éditeur monde puis
		// libère ses panneaux. Idempotent si Init() n'a pas réussi.
		if (m_worldEditorShell)
		{
			m_worldEditorShell->Shutdown();
			m_worldEditorShell.reset();
		}
		// Phase 3.6.6 — Sauvegarde finale de la position avant de fermer la connexion master.
		// Fire-and-forget : on n'attend pas l'ack (ce serait risqué dans un chemin de shutdown
		// avec timers et ordre de destruction) — le serveur logge en cas d'échec d'UPDATE.
		// La connexion m_authUi.m_masterClient est encore vivante ici (Shutdown des UI vient juste après).
		if (!m_shutdownPositionSaved && m_currentCharacterId != 0u)
		{
			const uint32_t shutdownReadIdx = m_renderReadIndex.load(std::memory_order_acquire) & 1u;
			const engine::render::Camera& shutdownCam = m_renderStates[shutdownReadIdx].camera;
			constexpr float kRad2Deg = 180.f / 3.14159265f;
			const float yawDeg   = shutdownCam.yaw   * kRad2Deg;
			const float pitchDeg = shutdownCam.pitch * kRad2Deg;
			// Vue 3eme personne : on persiste la position cible (joueur), pas la camera.
			const engine::math::Vec3& playerPos = m_orbitalCameraController.GetTargetPosition();
			const bool sent = m_authUi.SavePositionAsync(m_currentCharacterId,
				playerPos.x, playerPos.y, playerPos.z,
				yawDeg, pitchDeg);
			LOG_INFO(Core, "[SavePosition] shutdown save (character_id={}, pos=({:.1f},{:.1f},{:.1f}), sent={})",
				m_currentCharacterId, playerPos.x, playerPos.y, playerPos.z, sent);
			m_shutdownPositionSaved = true;
		}

		ShutdownGameplayNet();
		m_authUi.Shutdown();
		m_chatUi.Shutdown();
		m_mailUi.Shutdown();
		m_gmTicketUi.Shutdown();
		m_reputationUi.Shutdown();
		m_lfgUi.Shutdown();
		m_cinematicUi.Shutdown();
		m_skillBookUi.Shutdown();
		m_grimoireUi.Shutdown(); // Grimoire (Task 13)
		m_classSkillTreeUi.Shutdown(); // SP-D
		m_arenaUi.Shutdown();
		m_battleGroundUi.Shutdown();
		m_outdoorPvpUi.Shutdown();
		m_weatherUi.Shutdown();
		m_gameEventUi.Shutdown();
		m_guildUi.Shutdown();
		m_auctionHouseUi.Shutdown();
		m_lootRollUi.Shutdown();
		m_window.Destroy();
		LOG_INFO(Core, "[Engine] Shutdown complete");
		return 0;
	}

	void Engine::BeginFrame()
	{
		// PROFILE_FUNCTION();
		LOG_DEBUG(Render, "[DIAG] BeginFrame enter frame={}", m_currentFrame);
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
		else if (m_cinematicUi.IsInitialized() && m_cinematicUi.GetState().isPlaying)
		{
			// CMANGOS.30 (Phase 5.30 step 3+4) — Pendant une cinematique, Esc
			// envoie une demande de skip au master. La reponse asynchrone via
			// OnSkipResponse termine effectivement la lecture si allowed=true.
			if (m_input.WasPressed(engine::platform::Key::Escape))
			{
				m_cinematicUi.RequestSkip();
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
			// Echap in-game (post-auth, pas dans un menu chat/auction/shop) :
			// toggle le menu pause au lieu de quitter directement le client.
			// Demande utilisateur explicite : 'on quitte automatiquement le jeu,
			// il ne faut surtout pas. Nous devons toujours passer par un menu
			// intermediaire, qui propose de Quitter / Options / Se deconnecter'.
			ToggleInGamePauseMenu();
		}

		if (m_input.WasPressed(engine::platform::Key::F_11))
    		m_window.ToggleFullscreen();

		// Chantier 1 — touche F2 (Skill Book) LIBÉRÉE : le livre de compétences est
		// désormais l'onglet Compétences de la fenêtre Personnage unifiée (F1). Le
		// RequestList() est déclenché à l'ouverture de la fenêtre côté conteneur au
		// besoin ; l'ancien toggle F2 autonome est retiré.

		// Chantier 1 — touches F3 (Grimoire) et F4 (arbre) LIBÉRÉES : ces panneaux
		// sont désormais des onglets de la fenêtre Personnage unifiée ouverte par F1
		// (cf. bloc F1 plus bas). Leurs anciens toggles autonomes sont retirés.

		// Bascule HUD carte — Touche U remappable post-auth masque/affiche le
		// « cluster carte » : boussole + radar minimap + tracker de quêtes (retour
		// joueur : « pouvoir boucher la boussole, le radar et le suivi de quête »).
		// Le journal et le panneau donneur ne sont PAS concernés (interactifs).
		// Mêmes guards que les autres toggles (chat focus + pause + editor + auth).
		{
			const bool chatBlocks = m_chatUi.IsInitialized() && m_chatUi.IsChatFocusActive();
			const bool inGameNoMenu = !m_inGamePauseMenuVisible
				&& !m_inGameOptionsPanelVisible
				&& !m_editorEnabled
				&& m_authUi.IsInitialized()
				&& m_authUi.IsFlowComplete();
			const engine::platform::Key hudKey =
				KeyFromName(m_cfg.GetString("controls.keybind.hud_toggle", "U"), engine::platform::Key::U);
			if (inGameNoMenu && !chatBlocks && m_input.WasPressed(hudKey))
			{
				m_hudMapClusterHidden = !m_hudMapClusterHidden;
				LOG_INFO(Core, "[Engine] HUD map cluster toggle (hidden={})", m_hudMapClusterHidden);
			}
		}

		// CMANGOS.21 (Phase 5.21 step 3+4) — Touche A post-auth toggle le
		// panneau Arena (equivalent a la slash command /arena). Memes guards
		// que la touche B (chat focus + pause + editor + auth flow).
		{
			const bool chatBlocks = m_chatUi.IsInitialized() && m_chatUi.IsChatFocusActive();
			const bool inGameNoMenu = !m_inGamePauseMenuVisible
				&& !m_inGameOptionsPanelVisible
				&& !m_editorEnabled
				&& m_authUi.IsInitialized()
				&& m_authUi.IsFlowComplete();
			if (inGameNoMenu && !chatBlocks
				&& m_input.WasPressed(KeyFromName(m_cfg.GetString("controls.keybind.arena", "F8"), engine::platform::Key::F_8)))
			{
				m_arenaVisible = !m_arenaVisible;
				if (m_arenaVisible)
				{
					m_arenaUi.RequestTeams();
				}
				LOG_INFO(Core, "[Engine] F8 toggle arena (visible={})", m_arenaVisible);
			}
		}

		// CMANGOS.10 (Phase 5 step 3+4) — Touche G post-auth toggle le panneau
		// BattleGround (equivalent a la slash command /bg). Memes guards que
		// la touche A (chat focus + pause + editor + auth flow).
		{
			const bool chatBlocks = m_chatUi.IsInitialized() && m_chatUi.IsChatFocusActive();
			const bool inGameNoMenu = !m_inGamePauseMenuVisible
				&& !m_inGameOptionsPanelVisible
				&& !m_editorEnabled
				&& m_authUi.IsInitialized()
				&& m_authUi.IsFlowComplete();
			if (inGameNoMenu && !chatBlocks
				&& m_input.WasPressed(KeyFromName(m_cfg.GetString("controls.keybind.battleground", "F9"), engine::platform::Key::F_9)))
			{
				m_battleGroundVisible = !m_battleGroundVisible;
				if (m_battleGroundVisible)
				{
					m_battleGroundUi.RequestList();
				}
				LOG_INFO(Core, "[Engine] G toggle battleground (visible={})", m_battleGroundVisible);
			}
			// CMANGOS.36 (Phase 5.36 step 3+4) — Touche P : toggle panneau
			// OutdoorPvp + RequestList si on l'ouvre. Memes guards que A/G.
			if (inGameNoMenu && !chatBlocks
				&& m_input.WasPressed(KeyFromName(m_cfg.GetString("controls.keybind.outdoorpvp", "F10"), engine::platform::Key::F_10)))
			{
				m_outdoorPvpVisible = !m_outdoorPvpVisible;
				if (m_outdoorPvpVisible)
				{
					m_outdoorPvpUi.RequestList();
				}
				LOG_INFO(Core, "[Engine] F10 toggle outdoorpvp (visible={})", m_outdoorPvpVisible);
			}
			// CMANGOS.42 (Phase 4.42 step 3+4) — Touche Y : toggle panneau
			// Weather + RequestList si on l'ouvre. Memes guards que A/G/P.
			// Note : la touche Y est aussi utilisee comme Ctrl+Y pour le
			// redo dans WorldEditorShell, mais le guard inGameNoMenu inclut
			// !m_editorEnabled donc pas de conflit. WorldEditorShell traite
			// Ctrl+Y / Ctrl+Shift+Y dans son propre bloc en aval.
			if (inGameNoMenu && !chatBlocks
				&& m_input.WasPressed(KeyFromName(m_cfg.GetString("controls.keybind.weather", "F12"), engine::platform::Key::F_12)))
			{
				m_weatherVisible = !m_weatherVisible;
				if (m_weatherVisible)
				{
					m_weatherUi.RequestList();
				}
				LOG_INFO(Core, "[Engine] F12 toggle weather (visible={})", m_weatherVisible);
			}
			// Touche E desormais LIBRE (reservee a une future action "interagir"
			// hors combat). Le panneau GameEvents s'ouvre via la barre de menus
			// "Panneaux" (rendu ImGui in-game), plus par une touche dediee.
			// CMANGOS.21 (Phase 5.21 step 3+4 Guilds) — Touche U : toggle
			// panneau Guildes + RequestList si on l'ouvre. Memes guards que
			// E (chat focus, pause, editor). Pas de conflit Ctrl+U.
			if (inGameNoMenu && !chatBlocks
				&& !m_input.IsDown(engine::platform::Key::Control)
				&& m_input.WasPressed(KeyFromName(m_cfg.GetString("controls.keybind.guild", "F5"), engine::platform::Key::F_5)))
			{
				m_guildVisible = !m_guildVisible;
				if (m_guildVisible)
				{
					m_guildUi.RequestList();
				}
				LOG_INFO(Core, "[Engine] F5 toggle guilds (visible={})", m_guildVisible);
			}
			// CMANGOS.09 (Phase 5.09 step 3+4 AuctionHouse) — Touche H : toggle
			// panneau Hotel des Ventes + RequestList si on l'ouvre. Memes
			// guards que U. Pas de conflit Ctrl+H.
			if (inGameNoMenu && !chatBlocks
				&& m_input.WasPressed(KeyFromName(m_cfg.GetString("controls.keybind.auctionhouse", "F6"), engine::platform::Key::F_6)))
			{
				m_auctionHouseVisible = !m_auctionHouseVisible;
				if (m_auctionHouseVisible)
				{
					m_auctionHouseUi.RequestList(0u);
				}
				LOG_INFO(Core, "[Engine] F6 toggle auction house (visible={})", m_auctionHouseVisible);
			}
			// CMANGOS.17 (Phase 3.17 step 3+4 Loot) — Touche L : toggle
			// fenetre Loot Roll. Memes guards que U. Pas de conflit Ctrl+L.
			// Pas de fetch a l'ouverture : les pending rolls arrivent via
			// push, le bouton Simulate sert pour la demo V1.
			if (inGameNoMenu && !chatBlocks
				&& !m_input.IsDown(engine::platform::Key::Control)
				&& m_input.WasPressed(KeyFromName(m_cfg.GetString("controls.keybind.lootroll", "F7"), engine::platform::Key::F_7)))
			{
				m_lootRollVisible = !m_lootRollVisible;
				LOG_INFO(Core, "[Engine] F7 toggle loot roll (visible={})", m_lootRollVisible);
			}
			// Chantier 1 — Touche F1 : ouvre/ferme la fenêtre Personnage unifiée à
			// onglets (perso 3D + inventaire + caractéristiques + argent, plus les
			// onglets Compétences/Techniques/Arbre). Remplace l'ancien toggle de la
			// fiche autonome ; F2/F3/F4/I sont désormais libres. Clé de config
			// conservée (controls.keybind.charactersheet, défaut F1). Mêmes guards.
			if (inGameNoMenu && !chatBlocks
				&& m_input.WasPressed(KeyFromName(m_cfg.GetString("controls.keybind.charactersheet", "F1"), engine::platform::Key::F_1)))
			{
				if (m_characterWindowImGui)
				{
					m_characterWindowImGui->ToggleVisible();
					if (m_characterWindowImGui->IsVisible())
					{
						// Peupler l'onglet Compétences (l'ancien toggle F2 le faisait ;
						// le Skill Book se remplit via RequestList côté serveur).
						m_skillBookUi.RequestList();
						// SP2 anniversaires (2026-07-18) — peupler l'onglet Exploits
						// (opcode 206 fire-and-forget, réponse 207 au push handler).
						m_exploitsUi.RequestExploitList();
						// Task 4 — pose l'avatar du joueur dans le viewport 3D À
						// L'OUVERTURE seulement : SetMesh réinitialise l'animation, il ne
						// doit PAS être rappelé chaque frame (sinon l'anim reste figée à
						// t=0). Le Tick/RenderOffscreen se font ensuite tant que ouvert.
						m_racePreviewViewport.SetGender(m_avatarGender);
						m_racePreviewViewport.SetSkinTone(m_avatarSkinTone);
						m_racePreviewViewport.SetMesh(m_currentSkinnedMesh);
						// Vue de face + rotation manuelle (pas d'auto-orbit) : retour
						// joueur 2026-07-09. Le drag dans la fenêtre pilote ensuite l'angle.
						m_racePreviewViewport.SetAutoOrbit(false);
						m_racePreviewViewport.SetOrbitYaw(0.0f);
						m_characterWindowImGui->ResetPreviewOrientation();
					}
				}
				LOG_INFO(Core, "[Engine] F1 toggle fenetre Personnage");
			}
		}

		// M100.2 — Dispatch des raccourcis éditeur monde vers le shell. Ctrl+Z
		// / Ctrl+Shift+Z / Ctrl+Y branchent la pile undo/redo ; F1..F12
		// (déjà gérés en M100.1) restent supportés. On ne dispatche que si
		// le shell est initialisé (CLI --editor-world ou editor.world.enabled).
		if (m_worldEditorShell && m_worldEditorShell->IsInitialized())
		{
			const bool ctrl  = m_input.IsDown(engine::platform::Key::Control);
			const bool shift = m_input.IsDown(engine::platform::Key::Shift);
			// En « Mode édition bâtiment », Ctrl+Z/Ctrl+Y (et Ctrl+Shift+Z)
			// pilotent l'undo/redo du BROUILLON de bâtiment plutôt que la pile
			// undo globale (terrain/scène) — évite tout conflit : l'utilisateur
			// active cette case quand il travaille la composition.
			engine::editor::world::panels::BuildingEditorPanel* bp =
				m_worldEditorShell->GetBuildingEditorPanel();
			const bool buildMode = bp && bp->EditModeActive();
			if (m_input.WasPressed(engine::platform::Key::Z))
			{
				if (ctrl && buildMode) { if (shift) bp->Redo(); else bp->Undo(); }
				else m_worldEditorShell->HandleShortcut('Z', ctrl, shift);
			}
			if (m_input.WasPressed(engine::platform::Key::Y))
			{
				if (ctrl && buildMode) bp->Redo();
				else m_worldEditorShell->HandleShortcut('Y', ctrl, shift);
			}
			// Lot 5 (2026-07-18) — Ctrl+D (dupliquer la sélection) et Suppr
			// (supprimer la sélection). Ignorés quand ImGui édite du texte
			// (champ de saisie actif) : Suppr/Ctrl+D y ont leur sens d'édition
			// de texte et ne doivent pas toucher la scène. ImGui n'existe que
			// côté Windows (comme partout dans Engine.cpp) — sur les autres
			// plateformes l'éditeur n'a pas d'UI, pas de saisie à protéger.
#if defined(_WIN32)
			const bool imguiTextInput =
				(ImGui::GetCurrentContext() != nullptr) && ImGui::GetIO().WantTextInput;
#else
			const bool imguiTextInput = false;
#endif
			if (!imguiTextInput)
			{
				if (ctrl && m_input.WasPressed(engine::platform::Key::D))
				{
					// shift transmis tel quel : Ctrl+Shift+D reste l'outil
					// Dungeon Portal (géré par le shell).
					m_worldEditorShell->HandleShortcut('D', true, shift);
				}
				if (!ctrl && !shift && m_input.WasPressed(engine::platform::Key::Delete))
				{
					m_worldEditorShell->HandleShortcut(0x2E, false, false);
				}
				// Roadmap-6 (2026-07-19) — E / T / C : mode du gizmo de scène
				// (dÉplacer / Tourner / éChelle). Sans modifiers, hors saisie
				// de texte (garde imguiTextInput ci-dessus).
				if (!ctrl && !shift)
				{
					if (m_input.WasPressed(engine::platform::Key::E))
						m_worldEditorShell->HandleShortcut('E', false, false);
					if (m_input.WasPressed(engine::platform::Key::T))
						m_worldEditorShell->HandleShortcut('T', false, false);
					if (m_input.WasPressed(engine::platform::Key::C))
						m_worldEditorShell->HandleShortcut('C', false, false);
				}
			}
		}

		m_shaderHotReload.Poll(m_cfg);
		m_shaderHotReload.ApplyPending(m_shaderCache);

		if (m_swapchainResizeRequested)
		{
    		LOG_INFO(Platform, "[Resize] Swapchain recreate requested");

			if (m_vkDeviceContext.IsValid() && m_vkSwapchain.IsValid() && m_width > 0 && m_height > 0)
			{
				vkDeviceWaitIdle(m_vkDeviceContext.GetDevice());
				if (m_pipeline)
					m_pipeline->InvalidateFramebufferCaches(m_vkDeviceContext.GetDevice());
				if (m_terrain.IsValid())
					m_terrain.InvalidateFramebufferCache(m_vkDeviceContext.GetDevice());
				// M100.14 — Le cache framebuffer de la passe water est indexé par
				// VkImageView source : un resize détruit toutes les vues FG → cache
				// stale. Invalidation au même endroit que terrain/pipeline.
				m_waterPass.InvalidateFramebufferCache(m_vkDeviceContext.GetDevice());
				// Audit 2026-06-10 (Lot B2) — UnderwaterPass cache désormais son
				// framebuffer (pattern WaterPass) ; pas membre de DeferredPipeline,
				// donc invalidation ici comme m_waterPass.
				m_underwaterPass.InvalidateFramebufferCache(m_vkDeviceContext.GetDevice());

				m_frameGraph.destroy(m_vkDeviceContext.GetDevice(), m_vmaAllocator);
				// All frame-graph images are recreated after a resize/out-of-date event, so the
				// TAA history must be rebuilt from scratch on the next frame.
				m_taaHistoryInvalid = true;
				m_taaHistoryEverFilled = false;

				bool ok = m_vkSwapchain.Recreate(static_cast<uint32_t>(m_width), static_cast<uint32_t>(m_height));
				if (ok)
				{
					// Hot-fix : on ne clear le flag qu'apres succes complet. Si Recreate echoue
					// (resize trop rapide, surface lost momentanee, etc.) le flag reste true et
					// la frame suivante retente -> evite l'ecran noir permanent en cas d'echec
					// transitoire.
					m_swapchainResizeRequested = false;
					m_suboptimalStreak = 0;
					m_suboptimalWidth = m_width;
					m_suboptimalHeight = m_height;
					LOG_INFO(Platform, "[Resize] Swapchain recreated OK ({}x{})", m_width, m_height);
					if (m_worldEditorImGui)
					{
						m_worldEditorImGui->OnSwapchainRecreate(m_vkSwapchain.GetImageCount());
					}
				}
				else
				{
					LOG_WARN(Platform, "[Resize] Swapchain recreate FAILED ({}x{}) - retry next frame", m_width, m_height);
					// flag deliberement laisse a true : retry au prochain BeginFrame.
				}
			}
			else
			{
				// Cas typique : fenetre minimisee (m_width/m_height a 0). On clear le flag, il
				// sera reposte automatiquement quand WM_SIZE refire au restore (cf. Window.cpp).
				m_swapchainResizeRequested = false;
				LOG_WARN(Platform, "[Resize] Swapchain recreate skipped - device/swapchain not ready or invalid size ({}x{})", m_width, m_height);
			}
		}

		m_time.BeginFrame();
		if (m_profiler.IsInitialized())
		{
			m_profiler.BeginFrame(m_currentFrame);
		}
		m_frameArena.BeginFrame(m_time.FrameIndex());
		m_chunkStats.ResetPerFrame();
		// M100 — Task 12 : maintenance entre frames du runtime terrain chunk
		// (eviction LRU des chunks Far + reset descriptor pool). Doit être
		// appelée AVANT l'enregistrement de la frame courante.
		if (m_terrainChunkRenderer)
			m_terrainChunkRenderer->Tick(m_vkDeviceContext.GetDevice());
		PumpGameplayPackets();

		// CMANGOS.30 (Phase 5.30 step 3+4) — Tick le presenter cinematique pour
		// faire avancer l'interpolation camera + la detection de fin de
		// sequence. No-op si aucune cinematique active. Le timestamp est en
		// ms epoch (meme reference utilisee par le presenter pour startTimeMs).
		if (m_cinematicUi.IsInitialized() && m_cinematicUi.GetState().isPlaying)
		{
			using namespace std::chrono;
			const uint64_t nowMs = static_cast<uint64_t>(
				duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count());
			m_cinematicUi.Tick(nowMs);
		}
	}

	void Engine::Update()
	{
		// PROFILE_FUNCTION();
		LOG_DEBUG(Render, "[DIAG] Update enter frame={}", m_currentFrame);
		const uint32_t readIdx  = m_renderReadIndex.load(std::memory_order_acquire);
		const uint32_t writeIdx = 1u - (readIdx & 1u);
		const auto& readState   = m_renderStates[readIdx];
		auto& out               = m_renderStates[writeIdx];

#if defined(_WIN32)
		if (!m_worldEditorImGui && m_vkDeviceContext.IsValid() && m_vkSwapchain.IsValid()
			&& m_window.GetNativeHandle() != nullptr && m_vkDeviceContext.SupportsDynamicRendering())
		{
			m_worldEditorImGui = std::make_unique<engine::editor::WorldEditorImGui>();
			if (m_worldEditorImGui->Init(
				m_vkInstance.GetHandle(),
				m_vkDeviceContext,
				m_vkSwapchain.GetImageFormat(),
				m_vkSwapchain.GetImageCount(),
				VK_API_VERSION_1_1,
				m_window.GetNativeHandle(),
				&m_cfg,
				m_worldEditorExe))
			{
				if (m_worldEditorExe)
				{
					m_worldEditorImGui->SetEditorContext(m_worldEditorSession.get(), &m_cfg);
					// M100.46 incrément 3 — branche le Shell pour que le
					// dialog Zone Presets puisse résoudre documents et
					// catalogs au moment de l'exécution.
					m_worldEditorImGui->SetWorldEditorShell(m_worldEditorShell.get());
					// Réorganisation UI 2026-07-17 — « Fichier > Quitter » :
					// demande de fermeture par le même chemin que la croix
					// fenêtre (sortie propre de la boucle principale).
					m_worldEditorImGui->SetQuitCallback([this] { OnQuit(); });

					// Cache de vignettes pour les textures de splatting.
					m_texturePreviewCache = std::make_unique<engine::editor::TexturePreviewCache>();
					const std::string contentDir = m_cfg.GetString("paths.content", "game/data");
					const std::filesystem::path absContent = std::filesystem::absolute(contentDir);
					if (!m_texturePreviewCache->Init(m_vkDeviceContext.GetDevice(),
					                                 m_vkDeviceContext.GetPhysicalDevice(),
					                                 m_vkDeviceContext.GetGraphicsQueue(),
					                                 m_vkDeviceContext.GetGraphicsQueueFamilyIndex(),
					                                 absContent.string()))
					{
						LOG_WARN(Render, "[Engine] TexturePreviewCache init failed -- vignettes editeur indisponibles");
						m_texturePreviewCache.reset();
					}
					if (m_worldEditorImGui && m_texturePreviewCache)
					{
						m_worldEditorImGui->SetTexturePreviewCache(m_texturePreviewCache.get());
					}

					// M100.34 incrément 1 — Cible offscreen viewport éditeur.
					// Taille initiale alignée sur la swapchain ; la PR 2 ajoutera
					// la passe FrameGraph qui y copie SceneColor_LDR + la
					// logique de resize sur la taille du ScenePanel.
					const uint32_t initW = m_vkSwapchain.GetExtent().width;
					const uint32_t initH = m_vkSwapchain.GetExtent().height;
					if (!m_editorViewportTarget.Init(
						m_vkDeviceContext.GetDevice(),
						m_vkDeviceContext.GetPhysicalDevice(),
						m_vkDeviceContext.GetGraphicsQueue(),
						m_vkDeviceContext.GetGraphicsQueueFamilyIndex(),
						initW, initH))
					{
						LOG_WARN(Render, "[Engine] EditorViewportRenderTarget init failed -- ScenePanel restera en mode placeholder");
					}
				}
				// Sous-projet C MVP (Task 12) — Initialise le viewport
				// offscreen 512x512 dedie a l'apercu race dans l'ecran
				// ImGui AuthImGuiCharacterCreate. Doit etre fait apres
				// ImGui_ImplVulkan_Init (couvert par m_worldEditorImGui->Init
				// plus haut, qui appelle ImGui_ImplVulkan_Init dans tous les
				// cas — meme en mode client/jeu, le contexte ImGui partage
				// est utilise par AuthImGui). En cas d'echec, AuthImGui
				// recevra nullptr -> l'ecran de creation perso retombera
				// sur le fallback texte (cf. AuthImGuiCharacterCreate).
				if (!m_racePreviewViewport.Init(
					m_vkDeviceContext.GetDevice(),
					m_vkDeviceContext.GetPhysicalDevice(),
					m_vkDeviceContext.GetGraphicsQueue(),
					m_vkDeviceContext.GetGraphicsQueueFamilyIndex(),
					/*width*/ 512u, /*height*/ 512u))
				{
					LOG_WARN(Render, "[Engine] RacePreviewViewport init failed -- ecran creation perso restera sans apercu 3D");
				}
				else
				{
					// Sous-chantier A phase 2 — pipeline forward 3D de l'apercu +
					// cablage des materiaux avatar (set bindless + ids peau/habit).
					// loadSpirv (defini plus haut) n'est pas dans la portee ici :
					// on relit le SPIR-V directement depuis le content root.
					auto loadPreviewSpv = [&](const char* p) -> std::vector<uint32_t> {
						std::vector<uint8_t> bytes = engine::platform::FileSystem::ReadAllBytesContent(m_cfg, p);
						std::vector<uint32_t> out;
						if (!bytes.empty() && bytes.size() % 4 == 0)
						{
							out.resize(bytes.size() / 4);
							std::memcpy(out.data(), bytes.data(), bytes.size());
						}
						return out;
					};
					const std::vector<uint32_t> previewVert = loadPreviewSpv("shaders/skinned_preview.vert.spv");
					const std::vector<uint32_t> previewFrag = loadPreviewSpv("shaders/skinned_preview.frag.spv");
					if (m_pipeline && !previewVert.empty() && !previewFrag.empty())
					{
						auto& matCache = m_pipeline->GetMaterialDescriptorCache();
						if (matCache.IsValid()
							&& m_racePreviewViewport.InitForwardPipeline(
								matCache.GetLayout(),
								previewVert.data(), previewVert.size(),
								previewFrag.data(), previewFrag.size()))
						{
							const uint32_t previewOutfitId = (m_avatarMaterialId != 0u)
								? m_avatarMaterialId : matCache.GetDefaultMaterialIndex();
							m_racePreviewViewport.SetAvatarMaterials(
								matCache.GetDescriptorSet(),
								previewOutfitId,
								m_avatarBodyMaterialIdMale,
								m_avatarBodyMaterialIdFemale,
								m_avatarBodyMaterialIdMaleDark,
								m_avatarBodyMaterialIdFemaleDark,
								m_avatarBodyMaterialNames,
								m_avatarSkinDepthBiasConstant,
								m_avatarSkinDepthBiasSlope);
							LOG_INFO(Render, "[Engine] RacePreviewViewport forward 3D pret (outfitId={})", previewOutfitId);
						}
						else
						{
							LOG_WARN(Render, "[Engine] RacePreviewViewport InitForwardPipeline echoue -- apercu en clear couleur");
						}
					}
					else
					{
						LOG_WARN(Render, "[Engine] shaders skinned_preview absents -- apercu 3D en clear couleur");
					}
				}
				// Branche le DayNightCycle au panneau "Atmosphere" pour que l'utilisateur
				// puisse regler time-of-day et timeScale en live depuis l'editeur monde.
				m_worldEditorImGui->SetDayNightCycle(&m_dayNight);
				m_worldEditorImGui->AttachPlatformWindow(m_window.GetNativeHandle(), m_window);
				m_authImGui = std::make_unique<engine::render::AuthImGuiRenderer>();
				m_authImGui->BindAuthUiBridge(&m_authUi, &m_cfg, &m_window);
				// Sous-projet C MVP (Task 12) — passe le viewport offscreen
				// race au renderer ImGui de l'ecran de creation perso. Le
				// renderer accepte nullptr (IsValid() == false -> fallback
				// texte) ; on transmet quand meme la reference pour que
				// l'edit dynamique reste possible si on veut recreer le
				// viewport apres un resize (pas le cas en MVP).
				m_authImGui->SetRacePreview(&m_racePreviewViewport);
				// Sous-projet C MVP (Task 12) — Donne au AuthUiPresenter une
				// reference vers Engine pour la resolution race_str ->
				// SkinnedMesh* (deleguee a Engine::GetRaceMesh) consommee
				// par AuthImGuiCharacterCreate quand l'utilisateur change
				// la race selectionnee dans le combo.
				m_authUi.SetEngineForRaceMeshLookup(this);
				// Phase 3.11.1 — partage du même contexte ImGui (NewFrame/Render gérés par m_worldEditorImGui).
				m_chatImGui = std::make_unique<engine::render::ChatImGuiRenderer>();
				m_chatImGui->BindChatUi(&m_chatUi, &m_cfg);
				// Cellule de dialogue PNJ : renderer ImGui dédié (fenêtre centrale).
				// Partage le contexte ImGui avec auth/chat ; rendu Windows uniquement
				// (le .cpp est gardé par #if _WIN32). Constructeur par défaut.
				m_dialogueImGui = std::make_unique<engine::render::DialogueImGuiRenderer>();
				// PR-B — Acceptation/rendu de quête DANS la conversation : le renderer
				// de dialogue injecte les boutons Accepter/Rendre depuis UIModel.giverList
				// (titres via le catalogue). Le callback d'action est câblé plus bas
				// (même bloc GameplayNet que le panneau donneur, m_gameplayUdp requis).
				m_dialogueImGui->BindQuestGiver(&m_uiModelBinding, &m_questTextCatalog);
				// SP2 Task 5 — Renderer ImGui journal/tracker/panneau donneur.
				// Partage le contexte ImGui avec auth/chat/dialogue. Le callback
				// d'action panneau donneur est cable plus bas (apres m_gameplayUdp
				// disponible), dans le meme bloc que le wiring GameplayNet.
				m_questImGui = std::make_unique<engine::render::QuestImGuiRenderer>();
				m_questImGui->BindQuestUi(&m_questUi, &m_questTextCatalog, &m_uiModelBinding, &m_cfg);
				// CMANGOS.18 (Phase 3.18 step 4) — Renderer ImGui de la boite mail.
				// Partage le contexte ImGui avec auth/chat. Visible uniquement quand
				// m_mailVisible (toggle via /mail). La taille viewport est mise a
				// jour dans le boucle Render plus bas.
				m_mailImGui = std::make_unique<engine::render::MailImGuiRenderer>();
				m_mailImGui->SetPresenter(&m_mailUi);
				// CMANGOS.32 (Phase 5.32 step 3+4) — Renderer ImGui du panneau
				// Support GM. Partage le contexte ImGui avec auth/chat/mail. Visible
				// uniquement quand m_gmTicketsVisible (toggle via /ticket). La
				// taille viewport est mise a jour dans la boucle Render plus bas.
				m_gmTicketImGui = std::make_unique<engine::render::GmTicketImGuiRenderer>();
				m_gmTicketImGui->SetPresenter(&m_gmTicketUi);
				// CMANGOS.24 (Phase 3.24 step 3+4) — Renderer ImGui du panneau
				// Reputation. Partage le contexte ImGui avec auth/chat/mail/gmtickets.
				// Visible uniquement quand m_reputationVisible (toggle via /rep).
				// La taille viewport est mise a jour dans la boucle Render plus bas.
				m_reputationImGui = std::make_unique<engine::render::ReputationImGuiRenderer>();
				m_reputationImGui->SetPresenter(&m_reputationUi);
				// R1-B (Task 4) — Renderer ImGui de la feuille de personnage. Lit
				// directement m_uiModelBinding.GetModel().playerStats (pas de
				// presenter). Visible quand m_characterSheetVisible (toggle touche C
				// hors combat). Taille viewport mise a jour dans la boucle Render.
				m_characterSheetImGui = std::make_unique<engine::render::CharacterSheetImGuiRenderer>();
				// CMANGOS.33 (Phase 5.33 step 3+4) — Renderer ImGui du panneau LFG.
				// Partage le contexte ImGui avec auth/chat/mail/gmtickets/reputation.
				// Visible uniquement quand m_lfgVisible (toggle via /lfg).
				m_lfgImGui = std::make_unique<engine::render::LfgImGuiRenderer>();
				m_lfgImGui->SetPresenter(&m_lfgUi);
				// CMANGOS.30 (Phase 5.30 step 3+4) — Renderer overlay cinematique
				// (black bars + skip hint). Visible uniquement quand une cinematique
				// est en cours (state.isPlaying == true). Pas de toggle slash command :
				// le declenchement est server-pushed.
				m_cinematicImGui = std::make_unique<engine::render::CinematicImGuiRenderer>();
				m_cinematicImGui->SetPresenter(&m_cinematicUi);
				// CMANGOS.39 (Phase 4.39 step 3+4) — Renderer ImGui du panneau
				// Skill Book. Partage le contexte ImGui avec auth/chat/mail/gmtickets/
				// reputation/lfg. Visible uniquement quand m_skillBookVisible
				// (toggle via /skills ou touche B).
				m_skillBookImGui = std::make_unique<engine::render::SkillBookImGuiRenderer>();
				m_skillBookImGui->SetPresenter(&m_skillBookUi);
				// Grimoire (Task 13) — Renderer ImGui du panneau Grimoire / Carnet de
				// techniques. Visible quand m_grimoireVisible (touche V ou /grimoire /
				// /sorts). Partage le contexte ImGui post-auth.
				m_grimoireImGui = std::make_unique<engine::render::GrimoireImGuiRenderer>();
				m_grimoireImGui->SetPresenter(&m_grimoireUi);
				// SP-D — Renderer ImGui du panneau arbre de compétences par-classe.
				// Visible quand m_classSkillTreeVisible (touche Y ou /arbre / /competences).
				m_classSkillTreeImGui = std::make_unique<engine::render::ClassSkillTreeImGuiRenderer>();
				m_classSkillTreeImGui->SetPresenter(&m_classSkillTreeUi);
				// SP-E — Cache d'icônes de compétences (PNG icons/skills/<classId>/...).
				// Init APRÈS ImGui_ImplVulkan_Init (assuré par m_worldEditorImGui->Init
				// plus haut). Idempotent. Partagé avec le Grimoire / la barre d'action.
				if (!m_skillIconCache.IsInitialized())
				{
					(void)m_skillIconCache.Init(m_vkDeviceContext.GetDevice(), &m_assetRegistry);
				}
				m_classSkillTreeImGui->SetIconCache(&m_skillIconCache);
				if (m_grimoireImGui) { m_grimoireImGui->SetIconCache(&m_skillIconCache); }
				// Chantier 1 — fenêtre Personnage unifiée à onglets (F1). Regroupe les 3
				// panneaux (pilotés en mode embarqué) + inventaire + argent + perso 3D
				// (viewport branché en Task 4). Bind après création des sous-renderers et
				// init du cache d'icônes.
				m_characterWindowImGui = std::make_unique<engine::render::CharacterWindowImGuiRenderer>();
				m_characterWindowImGui->Bind(&m_cfg, &m_uiModelBinding, &m_invUi, &m_skillIconCache,
					m_skillBookImGui.get(), m_grimoireImGui.get(), m_classSkillTreeImGui.get());
				// SP2 anniversaires (2026-07-18) — onglet Exploits (renderer lié
				// au presenter m_exploitsUi ; la requête part au toggle F1).
				m_exploitsImGui = std::make_unique<engine::render::ExploitsImGuiRenderer>();
				m_exploitsImGui->Bind(&m_exploitsUi);
				m_characterWindowImGui->SetExploitsRenderer(m_exploitsImGui.get());
				// Chantier 2 SP-A — catalogue d'objets client (noms/slots/bonus pour le
				// panneau équipement + tooltips). Source d'affichage ; le serveur reste
				// autoritaire. Non fatal : catalogue absent -> noms bruts (#itemId).
				{
					const std::string itemsText =
						engine::platform::FileSystem::ReadAllTextContent(m_cfg, "items/items.json");
					if (itemsText.empty() || !m_itemCatalog.LoadFromJson(itemsText))
						LOG_WARN(Net, "[Engine] catalogue d'objets client indisponible (items/items.json)");
					else
						LOG_INFO(Net, "[Engine] catalogue d'objets client charge : {} objet(s)", m_itemCatalog.Count());
					m_characterWindowImGui->SetItemCatalog(&m_itemCatalog);
				}
				// Task 4 — aperçu 3D : le conteneur affiche la texture offscreen du
				// viewport perso (alimenté quand la fenêtre est ouverte, cf. Update).
				m_characterWindowImGui->SetRaceViewport(&m_racePreviewViewport);
				// CMANGOS.21 (Phase 5.21 step 3+4) — Renderer ImGui du panneau
				// Arena. Visible uniquement quand m_arenaVisible (toggle via
				// /arena ou touche A). Le popup proposal s'affiche aussi quand
				// pendingProposalId.has_value() == true (meme si le panneau
				// principal est masque), pour que le joueur ne rate pas la
				// formation de match.
				m_arenaImGui = std::make_unique<engine::render::ArenaImGuiRenderer>();
				m_arenaImGui->SetPresenter(&m_arenaUi);
				// CMANGOS.10 (Phase 5 step 3+4) — Renderer ImGui du panneau
				// BattleGround. Visible quand m_battleGroundVisible (toggle
				// /bg ou touche G), ou quand un match BG est actif (le
				// scoreboard s'auto-affiche apres le push 136 MatchStart).
				m_battleGroundImGui = std::make_unique<engine::render::BattleGroundImGuiRenderer>();
				m_battleGroundImGui->SetPresenter(&m_battleGroundUi);
				// CMANGOS.36 (Phase 5.36 step 3+4) — Renderer ImGui du panneau
				// OutdoorPvp. Visible uniquement quand m_outdoorPvpVisible
				// (toggle via /pvp ou touche P).
				m_outdoorPvpImGui = std::make_unique<engine::render::OutdoorPvpImGuiRenderer>();
				m_outdoorPvpImGui->SetPresenter(&m_outdoorPvpUi);
				// CMANGOS.42 (Phase 4.42 step 3+4) — Renderer ImGui Weather.
				// Le panel principal n'est visible que quand m_weatherVisible
				// (toggle via /weather ou touche Y). Le HUD top-right est
				// rendu independamment des que activeZoneId est set sur le
				// presenter (selectionne via le bouton "Set Active" du panel).
				m_weatherImGui = std::make_unique<engine::render::WeatherImGuiRenderer>();
				m_weatherImGui->SetPresenter(&m_weatherUi);
				// CMANGOS.31 (Phase 5.31 step 3+4) — Renderer ImGui GameEvents.
				// Le panel principal n'est visible que quand m_gameEventVisible
				// (toggle via /events ou touche E). Le toast 5s sur dernier
				// StateChange reçu est rendu independamment du flag (peut
				// arriver panneau ferme).
				m_gameEventImGui = std::make_unique<engine::render::GameEventImGuiRenderer>();
				m_gameEventImGui->SetPresenter(&m_gameEventUi);
				// CMANGOS.21 (Phase 5.21 step 3+4 Guilds) — Renderer ImGui
				// Guildes. Le panel principal n'est visible que quand
				// m_guildVisible (toggle via /guild ou touche U). Le toast 5s
				// sur dernier MotdUpdate reçu est rendu independamment du flag.
				m_guildImGui = std::make_unique<engine::render::GuildImGuiRenderer>();
				m_guildImGui->SetPresenter(&m_guildUi);
				// CMANGOS.09 (Phase 5.09 step 3+4 AuctionHouse) — Renderer
				// ImGui Hotel des Ventes. Le panel principal n'est visible
				// que quand m_auctionHouseVisible (toggle via /ah ou touche
				// H). Les toasts 5s sur derniere bid + dernier
				// AuctionExpired sont rendus independamment du flag.
				m_auctionHouseImGui = std::make_unique<engine::render::AuctionImGuiRenderer>();
				m_auctionHouseImGui->SetPresenter(&m_auctionHouseUi);

				// CMANGOS.17 (Phase 3.17 step 3+4 Loot) — Renderer ImGui Loot
				// Roll. Le panel principal n'est visible que quand
				// m_lootRollVisible (toggle via /loot ou touche L). Le toast
				// 5s sur dernier RollResult reçu est rendu independamment.
				m_lootRollImGui = std::make_unique<engine::render::LootRollImGuiRenderer>();
				m_lootRollImGui->SetPresenter(&m_lootRollUi);
				// M43.4 — Editor Hub overlay : créé inconditionnellement, ne s'affiche que
				// si --editor est actif (cf. condition Render branch plus bas).
				m_editorHubImGui = std::make_unique<engine::render::EditorHubImGuiRenderer>();
				if (m_editorMode)
					m_editorHubImGui->BindEditorMode(m_editorMode.get());
			}
			else
			{
				m_worldEditorImGui.reset();
			}
		}
		if (m_worldEditorExe && m_worldEditorSession)
		{
			// Sous-projet 1 (boucle d'edition d'une zone) — Zone NEUVE : initialise
			// les chunks plats (source de verite) AVANT le rebuild GPU. L'empreinte
			// N (chunks par axe) se deduit de la resolution r16h : apres l'alignement
			// A2, 1 texel = 1 m, et un chunk fait 256 m, donc N = max(1, res/256).
			if (m_worldEditorSession->ConsumeNewZoneChunkInitRequest()
				&& m_worldEditorShell && m_worldEditorShell->IsInitialized())
			{
				const uint32_t hmRes = m_worldEditorSession->Doc().heightmapResolution;
				const int chunksPerAxis = static_cast<int>(std::max<uint32_t>(1u, hmRes / 256u));
				// Accord de hauteur chunk <-> heightmap (corrige le "terrain qui
				// chute hors champ a la 1ere edition"). ActionNewMap ecrit la
				// heightmap r16h a la valeur 32768 (milieu). Le chunk plat
				// (source de verite) doit demarrer a la MEME hauteur physique,
				// sinon la 1ere SyncWorldEditorHeightmapFromDocument reecrit
				// toute la heightmap a la hauteur du chunk et fait chuter le sol.
				// height_scale est lue avec la meme cle/le meme defaut que
				// TerrainRenderer::Init (terrain.height_scale, defaut 200).
				const float heightScale =
					static_cast<float>(m_cfg.GetDouble("terrain.height_scale", 200.0));
				const float flatHeightMeters = (32768.0f / 65535.0f) * heightScale;
				// Lot B3 (audit 2026-06-10 §4.2, correctifs 1-2-4) — Nouvelle carte
				// = changement de monde : vide la pile undo, reinitialise les 4
				// documents (terrain/eau/mesh/portails) et propage le zoneId
				// (namespacing disque chunks/zone_<id>/...) AVANT l'init des
				// chunks plats. Pas de LoadZoneDocuments ici : une zone neuve
				// demarre avec des documents vides.
				m_worldEditorShell->ResetForZoneChange(
					engine::editor::SanitizeZoneId(m_worldEditorSession->Doc().zoneId));
				m_worldEditorShell->InitNewZoneTerrain(chunksPerAxis, flatHeightMeters);
				// Réorganisation UI 2026-07-17 — une zone fraîchement créée
				// (fichiers déjà écrits par ActionNewMap) repart d'un état
				// « tout enregistré » : sans ce NoteSaved, le Clear de la
				// pile undo (ResetForZoneChange) marquerait la carte neuve
				// « non sauvegardée » dans la barre de statut.
				m_worldEditorShell->NoteSaved();
				// Nouvelle carte = la caméra est replacée sur la nouvelle zone :
				// l'origine STABLE du brouillon (mémorisée pour le gizmo/picking)
				// pointe encore sur l'ANCIENNE carte. On l'invalide pour que le
				// brouillon (auberge en cours) se recentre sur la vue de la
				// nouvelle carte au prochain rebuild — sinon il reste hors-champ
				// et « ne se charge plus ». On force aussi le rebuild.
				m_editorPreviewValid = false;
				if (auto* bp = m_worldEditorShell->GetBuildingEditorPanel())
					bp->MarkPreviewDirty();
			}
			// Lot B3 (correctifs 1+3) — Chargement d'une carte EXISTANTE :
			// reinitialise les documents (les chunks de la carte precedente
			// restaient en RAM et reecrivaient la heightmap de la nouvelle via
			// SyncWorldEditorHeightmapFromDocument), vide l'undo, propage le
			// zoneId, puis recharge eau / mesh inserts / portails depuis les
			// chemins namespaces (fallback legacy plat en lecture). Place AVANT
			// la consommation du reload GPU pour que le rebuild parte d'un
			// etat document coherent.
			if (m_worldEditorSession->ConsumeZoneDocumentsReloadRequest()
				&& m_worldEditorShell && m_worldEditorShell->IsInitialized())
			{
				m_worldEditorShell->ResetForZoneChange(
					engine::editor::SanitizeZoneId(m_worldEditorSession->Doc().zoneId));
				m_worldEditorShell->LoadZoneDocuments(m_cfg);
				// Réorganisation UI 2026-07-17 — une carte fraîchement
				// chargée repart d'un état « tout enregistré » (cf. barre de
				// statut / modale Quitter) : le Clear de la pile undo fait
				// par ResetForZoneChange incrémente le sériel du CommandStack.
				m_worldEditorShell->NoteSaved();
				// Force la reconstruction de l'aperçu 3D des bâtiments de la
				// nouvelle zone (placements chargés depuis buildings.bin).
				if (auto* bp = m_worldEditorShell->GetBuildingEditorPanel())
					bp->MarkPreviewDirty();
				// Changement de zone : l'origine STABLE du brouillon (mémorisée pour
				// le picking) n'est plus pertinente — la prochaine apparition se
				// recentre sur la caméra de la nouvelle carte.
				m_editorPreviewValid = false;
			}
			if (m_worldEditorSession->ConsumeTerrainGpuReloadRequest())
			{
				RebuildWorldEditorTerrainGpu();
			}
			// Sous-projet 1 — Sauvegarde des chunks (source de verite) demandee par
			// ActionSaveCurrentMap / ActionSaveEditJson. Le r16h est ecrit par le
			// terrainSaveHook ; ici on persiste en plus les chunks terrain/splat.
			if (m_worldEditorSession->ConsumeTerrainChunksSaveRequest()
				&& m_worldEditorShell && m_worldEditorShell->IsInitialized())
			{
				// Lot B3 (correctif 4) — re-propage le zoneId courant avant la
				// persistance : couvre le « save sous un autre nom de zone »
				// (ActionSaveCurrentMap re-sanitize m_doc.zoneId depuis le
				// buffer UI juste avant d'armer la requete de save).
				m_worldEditorShell->PropagateZoneIdToDocuments(
					engine::editor::SanitizeZoneId(m_worldEditorSession->Doc().zoneId));
				const size_t nWritten = m_worldEditorShell->SaveTerrainChunks(m_cfg);
				// Lot B3 (correctif 3) — persiste aussi les documents annexes
				// (eau, mesh inserts, portails de donjon) : avant ce fix, aucun
				// SaveToDisk n'etait appele hors tests -> lacs/rivieres/grottes/
				// arches/portails places etaient perdus a la fermeture.
				const size_t nDocs = m_worldEditorShell->SaveZoneDocuments(m_cfg);
				LOG_INFO(EditorWorld,
					"[Engine] Persisted {} terrain chunk file(s) + {} zone document(s)",
					nWritten, nDocs);
			}
		}
		// M100.46+ — Pont TerrainDocument → HeightmapData GPU. Consomme le
		// flag set par le callback OnChunkChanged et déclenche la copie
		// chunks → heightmap CPU + FlushHeightmap GPU. Coût ~10-30 ms par
		// tick concerné (acceptable pour des ops one-shot ; pas atteint par
		// les brushstrokes interactifs qui ont leur propre path direct).
		if (m_worldEditorTerrainNeedsSync.exchange(false, std::memory_order_acq_rel))
		{
			SyncWorldEditorHeightmapFromDocument();
		}
		if (m_worldEditorExe && m_worldEditorSession && m_texturePreviewCache)
		{
			for (const std::string& rel : m_worldEditorSession->RecentlyImportedTextures())
			{
				m_texturePreviewCache->Invalidate(rel);
			}
			m_worldEditorSession->ClearRecentlyImportedTextures();
		}
		ProcessSplatRefsDirty();
		if (m_texturePreviewCache)
		{
			m_texturePreviewCache->Tick(static_cast<uint64_t>(m_currentFrame),
			                             kEditorTexCacheFramesInFlight);
		}
#endif

	const double dt               = (m_fixedDt > 0.0) ? m_fixedDt : m_time.DeltaSeconds();

	// Temps cumulé pour l'advection des nuages (vent). Continu, non cyclique.
	m_cloudTimeSeconds += static_cast<float>(dt);

	// M38.1 — Advance day/night cycle and propagate results into m_zoneAtmosphere
	// so that the existing lighting path picks them up without further changes.
	{
		m_dayNight.Advance(static_cast<float>(dt));
		const engine::render::DayNightCycle::State& dnState = m_dayNight.GetState();
		m_zoneAtmosphere.sunDirection[0] = dnState.lightDir[0];
		m_zoneAtmosphere.sunDirection[1] = dnState.lightDir[1];
		m_zoneAtmosphere.sunDirection[2] = dnState.lightDir[2];
		m_zoneAtmosphere.sunColor[0]     = dnState.lightColor[0];
		m_zoneAtmosphere.sunColor[1]     = dnState.lightColor[1];
		m_zoneAtmosphere.sunColor[2]     = dnState.lightColor[2];
		m_zoneAtmosphere.ambientColor[0] = dnState.ambientColor[0];
		m_zoneAtmosphere.ambientColor[1] = dnState.ambientColor[1];
		m_zoneAtmosphere.ambientColor[2] = dnState.ambientColor[2];
	}

	// WorldClock sync (Task 6.2) — controle de derive. Une fois l'horloge
	// serveur branchee (mode driven), on renvoie periodiquement un
	// WorldClockStateRequest (203) au master pour rafraichir m_clockOffsetMs
	// (la reponse 204 rappelle SetServerClock). La correction est sub-seconde
	// donc invisible — pas besoin de lerp. Fire-and-forget : no-op si la
	// connexion master n'est pas vivante.
	if (m_dayNight.IsServerDriven() && m_worldClockDriftCheckSec > 0.0f)
	{
		m_worldClockResyncTimer += static_cast<float>(dt);
		if (m_worldClockResyncTimer >= m_worldClockDriftCheckSec)
		{
			m_worldClockResyncTimer = 0.0f;
			std::vector<uint8_t> wcReq;
			engine::network::worldclock::BuildWorldClockStateRequestPayload(wcReq);
			(void)m_authUi.SendGenericRequestAsync(
				engine::network::kOpcodeWorldClockStateRequest, wcReq);
			LOG_DEBUG(Render, "[Engine] WorldClock re-sync request envoye (drift check)");
		}
	}

	// --- Suivi jour/nuit de l'IBL : re-capture du ciel quand le soleil a
	// assez bouge, throttle pour eviter un stall trop frequent (vkQueueWaitIdle).
	// Garde sur IsValid() (IBL active). Point sur : fin du bloc day/night,
	// hors enregistrement de command buffer de frame.
	if (m_pipeline && m_pipeline->GetIrradiancePass().IsValid())
	{
		constexpr float kIblMinRegenInterval = 2.5f;   // s
		constexpr float kIblSunCosThreshold  = 0.9962f; // cos(~5deg)
		m_iblRegenTimer += static_cast<float>(dt);
		const engine::render::DayNightCycle::State& dn = m_dayNight.GetState();
		const float dotSun = dn.lightDir[0]*m_iblLastSunDir[0]
		                   + dn.lightDir[1]*m_iblLastSunDir[1]
		                   + dn.lightDir[2]*m_iblLastSunDir[2];
		if (m_iblRegenTimer >= kIblMinRegenInterval && dotSun < kIblSunCosThreshold)
		{
			engine::render::SkyCaptureParams iblSky{};
			for (int i = 0; i < 3; ++i)
			{
				iblSky.lightDir[i]     =  dn.sunDir[i];
				iblSky.zenithColor[i]  =  dn.skyZenith[i];
				iblSky.horizonColor[i] =  dn.skyHorizon[i];
				iblSky.moonDir[i]      =  dn.moonDir[i];
			}
			iblSky.moonIntensity    = dn.isDaytime ? 0.0f : 1.0f;
			iblSky.moonPhase        = static_cast<float>(dn.moonPhase);
			iblSky.moonIllumination = dn.moonIllumination;
			// Lot 2 (2026-07-18) — même modèle de ciel que sky.frag.
			iblSky.skyModel = m_cfg.GetBool("client.sky.analytic", true) ? 1.0f : 0.0f;
			m_pipeline->RegenerateIbl(m_vkDeviceContext.GetDevice(),
			                          m_vkDeviceContext.GetGraphicsQueue(), iblSky);
			m_iblRegenTimer = 0.0f;
			m_iblLastSunDir = { dn.lightDir[0], dn.lightDir[1], dn.lightDir[2] };
		}
	}

	// M38.2 — Advance weather system and propagate audio volume.
	if (m_weatherSystem.IsInitialized())
	{
		const engine::render::Camera& cam = readState.camera;
		m_weatherSystem.Tick(static_cast<float>(dt),
		                     cam.position.x, cam.position.y, cam.position.z);

		// Drive the "Weather" audio bus volume from rain intensity (spec step 6).
		// Graceful no-op if the bus is not defined in the zone audio JSON.
		m_audioEngine.SetBusVolume("Weather", m_weatherSystem.GetAudioVolume());
	}

	// M38.3 — Advance dynamic point-light system (streetlamps, torches, windows).
	// Reads the current time-of-day from the day/night cycle to auto-trigger lights.
	if (m_dynamicLights.IsInitialized())
	{
		const float timeOfDay = m_dayNight.GetState().timeOfDay;
		m_dynamicLights.Tick(timeOfDay, static_cast<float>(dt));
	}

	const float  mouseSensitivity = static_cast<float>(m_cfg.GetDouble("camera.mouse_sensitivity", 0.002));
		const bool invertY = m_cfg.GetBool("controls.invert_y", false);
		const std::string moveLayoutStr = m_cfg.GetString("controls.movement_layout", "wasd");
		const engine::render::MovementLayout movementLayout =
			(moveLayoutStr == "zqsd") ? engine::render::MovementLayout::ZQSD : engine::render::MovementLayout::WASD;

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
			LOG_DEBUG(Render, "[DIAG] authUi.Update begin frame={}", m_currentFrame);
			m_authUi.Update(m_input, static_cast<float>(dt), m_window, m_cfg);
			LOG_DEBUG(Render, "[DIAG] authUi.Update done frame={}", m_currentFrame);
		}

		// Refactor B2 (ST1) — Le consume/application des réglages d'options doit tourner
		// CHAQUE FRAME (auth comme in-game) pour pouvoir réutiliser l'écran d'options en
		// jeu. Les ConsumePending*Settings() sont idempotents (commande vide tant qu'aucun
		// apply n'est demandé), donc l'appel inconditionnel est sûr et ne ré-applique rien
		// hors frame d'apply. Placé APRÈS m_authUi.Update pour consommer les commandes
		// stagées par l'UI au cours de la frame courante (pas un cran de retard). La garde
		// réseau (Init/ShutdownGameplayNet) reste conditionnée à authGateActive
		// (cf. ApplyConsumedSettingsCommands).
		ApplyConsumedSettingsCommands(authGateActive);

		if (authGateActive)
		{
			if (m_chatUi.IsInitialized())
			{
				// PAS d'update du chat pendant les ecrans d'auth : sinon le Enter
				// que l'utilisateur tape pour valider le login active aussi le chat
				// focus (cf. ChatUiPresenter::Update qui toggle sur Slash/Enter).
				// Resultat : in-game, m_chatFocus reste true -> orbital camera Update
				// est skip -> camera fige a la position spawn = position avatar
				// (utilisateur voyait l'interieur du mesh humanoide). On garde
				// uniquement la mise a jour viewport pour que la geometrie chat
				// soit prete au moment d'EnterWorld.
				(void)m_chatUi;
			}
			out.authHudText = m_authUi.BuildPanelText();
			out.chatDebugText.clear();
			const bool authUiDynamicRenderingEnabled = m_cfg.GetBool("render.auth_ui_dynamic_rendering.enabled", true);
			const bool authImguiClearsHud = m_authImGui && m_cfg.GetBool("render.auth_ui.imgui.enabled", false);
			if (m_authUi.GetVisualState().active
				&& m_vkDeviceContext.SupportsDynamicRendering()
				&& (authUiDynamicRenderingEnabled || authImguiClearsHud))
				m_window.SetOverlayText({});
			else
				m_window.SetOverlayText(out.authHudText);
		}
		else
		{
			if (m_audioEngine.GetCurrentZoneId() == 9999)
			{
				m_audioEngine.SetZone(0);
			}

			// Phase 3 — Première frame post-auth : consommer la EnterWorldCommand émise par
			// AuthScreenCharacterSelect ("Jouer") pour câbler la connexion gameplay UDP au shard
			// choisi par l'utilisateur. La commande est one-shot : ConsumePendingEnterWorldCommand
			// la remet à zéro après lecture, donc cette branche n'agit qu'une seule fois par session.
			const engine::client::AuthUiPresenter::EnterWorldCommand enterCmd
				= m_authUi.ConsumePendingEnterWorldCommand();
			if (enterCmd.applyRequested)
			{
				LOG_INFO(Core, "[EnterWorld] character_id={}, name='{}', shard_id={}, endpoint='{}'",
					enterCmd.characterId, enterCmd.characterName, enterCmd.shardId, enterCmd.shardEndpoint);

				// Niveau du perso (depuis CHARACTER_LIST) : sert a l'arbre de
				// competences (verrouillage paliers + affichage « Niveau joueur »).
				m_activeCharacterLevel = (enterCmd.level > 0u) ? enterCmd.level : 1u;

				// Coupe la musique des ecrans d'auth/menu (Horns_of_the_Fallen_Bastion.mp3)
				// au moment d'entrer dans le monde : le joueur entend desormais l'ambiance
				// du shard (zone audio) et eventuellement de futures musiques in-game.
				m_audioEngine.StopMenuMusic();

				// Phase 3.5/3.6 — Téléportation de la caméra à la position de spawn.
				// Priorité 1 (Phase 3.6) : spawn par-personnage, lu depuis characters.spawn_*
				// via la payload CHARACTER_LIST puis posé dans EnterWorldCommand par
				// AuthScreenCharacterSelect. enterCmd.hasSpawn vaut false si tous les champs
				// spawn de la DB étaient à zéro (pré-migration, ou défaut DB tel quel).
				// Priorité 2 (Phase 3.5, fallback) : défaut config `client.world.default_spawn.*`.
				{
					float spawnX, spawnY, spawnZ, yawDeg, pitchDeg;
					if (enterCmd.hasSpawn)
					{
						spawnX   = enterCmd.spawnX;
						spawnY   = enterCmd.spawnY;
						spawnZ   = enterCmd.spawnZ;
						yawDeg   = enterCmd.spawnYawDeg;
						pitchDeg = enterCmd.spawnPitchDeg;
						LOG_INFO(Core, "[EnterWorld] using per-character spawn from DB");
					}
					else
					{
						spawnX   = static_cast<float>(m_cfg.GetDouble("client.world.default_spawn.x", 0.0));
						spawnY   = static_cast<float>(m_cfg.GetDouble("client.world.default_spawn.y", 100.0));
						spawnZ   = static_cast<float>(m_cfg.GetDouble("client.world.default_spawn.z", 0.0));
						yawDeg   = static_cast<float>(m_cfg.GetDouble("client.world.default_spawn.yaw_deg", 0.0));
						pitchDeg = static_cast<float>(m_cfg.GetDouble("client.world.default_spawn.pitch_deg", -10.0));
						LOG_INFO(Core, "[EnterWorld] using config default spawn (no per-character data)");
					}
					constexpr float kDeg2Rad = 3.14159265f / 180.f;
					out.camera.position.x = spawnX;
					out.camera.position.y = spawnY;
					out.camera.position.z = spawnZ;
					out.camera.yaw = yawDeg * kDeg2Rad;
					out.camera.pitch = pitchDeg * kDeg2Rad;
					// Aligne la cible orbitale sur le spawn DB. Le controleur
					// repositionnera ensuite la camera derriere la cible (par defaut
					// 6 m d'orbite arriere) au prochain Update.
					m_orbitalCameraController.SetTargetPosition(out.camera.position);
					// Etape 6 : initialise la derniere position synchronisee au spawn
					// pour que la 1ere detection de mouvement soit correcte (sans cela
					// le perso etait "deplace" depuis (0,0,0) au tout 1er tick).
					m_lastSyncedPosition = out.camera.position;

					// Sous-projet B.1 (post-review fix) : repositionne le CharacterController
					// au spawn DB. Sans ca, le boot Init laisse le CC a (0, ground+0.9, 0)
					// et a la 1ere frame post-EnterWorld, cc.Update tourne AVANT le code de
					// teleport, donc cc.GetPosition() = (0,0,0) overwrite la position spawn
					// via SetTargetPosition(cc.GetPosition()). SavePositionAsync persiste
					// ensuite (0,0,0) en DB -> perte de la position spawn de l'utilisateur.
					// Anti-embedded : max(spawnY, GroundHeightAt+0.9) au cas ou la DB ait
					// une altitude trop basse pour la capsule (height/2 = 0.9 m).
					{
						const float groundY = m_terrainCollider.GroundHeightAt(spawnX, spawnZ);
						const float ccY = std::max(spawnY, groundY + 0.9f);
						m_characterController.Init(engine::math::Vec3{ spawnX, ccY, spawnZ });
						LOG_INFO(Core, "[EnterWorld] CharacterController repositioned to ({:.2f}, {:.2f}, {:.2f}) (groundY={:.2f}, halfHeight=0.9)",
							spawnX, ccY, spawnZ, groundY);
					}

					// #1 — genre du perso applique AVANT la selection du mesh/peau.
					// Source autoritative : enterCmd.gender (DB serveur via CHARACTER_LIST,
					// migration 0067). Repli : characters.<nom>.gender (fix client interim)
					// si le serveur ne fournit pas encore le genre (master pas redeploye).
					{
						std::string gender = enterCmd.gender;
						const char* src = "serveur";
						if (gender != "male" && gender != "female")
						{
							gender = m_cfg.GetString("characters." + enterCmd.characterName + ".gender", "");
							src = "client (repli)";
						}
						if (gender == "male" || gender == "female")
						{
							if (gender != m_avatarGender)
							{
								m_avatarGender = gender;
								m_avatarSkinDiagLoggedGender.clear();
							}
							LOG_INFO(Core, "[EnterWorld] genre perso '{}' = {} ({})",
								enterCmd.characterName, gender, src);
						}
						else
						{
							LOG_INFO(Core, "[EnterWorld] pas de genre (serveur/client) pour '{}' -> genre courant '{}'",
								enterCmd.characterName, m_avatarGender);
						}
					}
					// Teinte de peau du perso (DB serveur via CHARACTER_LIST, migration 0068).
					// 0 = claire (defaut), 1 = foncee. Applique le materiau de peau au draw.
					m_avatarSkinTone = (enterCmd.skinColorIdx == 1u) ? 1 : 0;
					LOG_INFO(Core, "[EnterWorld] teinte peau '{}' = {}",
						enterCmd.characterName, m_avatarSkinTone == 1 ? "foncee" : "claire");
					// Sous-projet C MVP — Resout le mesh de la race du perso depuis le
					// payload EnterWorld (race_str persistee en DB depuis migration 0033).
					// Fallback humains si la race n'est pas chargee dans m_raceMeshes.
					{
						const std::string& raceId = enterCmd.raceId;
						engine::render::skinned::SkinnedMesh* mesh = GetRaceMesh(raceId);
						if (mesh) {
							m_currentSkinnedMesh = mesh;
							LOG_INFO(Core, "[EnterWorld] Avatar mesh selected for race '{}' ({} bones, {} clips)",
								raceId, mesh->skeleton.bones.size(), mesh->clips.size());
						} else {
							m_skinnedAvatarReady = false;
							LOG_WARN(Core, "[EnterWorld] No mesh available for race '{}' (humains also absent) -- cube fallback",
								raceId);
						}
					}

					LOG_INFO(Core, "[EnterWorld] camera teleport ({}, {}, {}) yaw={}deg pitch={}deg",
						spawnX, spawnY, spawnZ, yawDeg, pitchDeg);
				}

				// Phase 3.5 — Bannière "Bienvenue, <perso>" affichée 5 s.
				{
					std::string personName = enterCmd.characterName.empty()
						? std::string("aventurier") : enterCmd.characterName;
					const std::string tpl = m_authUi.UiTranslate("auth.enter_world.welcome");
					if (!tpl.empty())
					{
						const std::string token{"{name}"};
						const size_t pos = tpl.find(token);
						m_enterWorldBannerText = (pos == std::string::npos)
							? tpl
							: tpl.substr(0, pos) + personName + tpl.substr(pos + token.size());
					}
					else
					{
						m_enterWorldBannerText = "Bienvenue, " + personName + " !";
					}
					m_enterWorldBannerExpiry = std::chrono::steady_clock::now() + std::chrono::seconds(5);
				}

				// Phase 3.6.6 — Memorise l'identité du perso et arme la sauvegarde périodique.
				// La connexion master (m_authUi.m_masterClient) reste vivante grâce au fix
				// Phase 2/3 ; SavePositionAsync l'utilise en fire-and-forget.
				m_currentCharacterId = enterCmd.characterId;
				// Cellule de dialogue PNJ : journal local de conversation de quête pour ce
				// personnage + branchement du presenter (sink de journalisation + callback
				// d'action quête). Accept/turn-in partent au shard (SP2 Task 7, voir
				// callback ci-dessous).
				m_dialogueJournal = std::make_unique<engine::client::QuestConversationJournal>(m_cfg, m_currentCharacterId);
				m_dialogue.SetJournalSink(m_dialogueJournal.get());
				m_dialogue.SetQuestActionCallback(
					[this](engine::client::DialogueAction action, int questId, const std::string& questKey)
					{
						// SP2 Task 7 — accept/turn-in passent EXCLUSIVEMENT par le shard
						// (opcodes 93/94 ci-dessous, Tasks 4/5). Le système A maître (retiré,
						// Cleanup 2026-07-02) n'existe plus.
						(void)questId;
						// SP2 — wire quêtes UDP (shard) : le choix de dialogue porte la clé
						// texte de quête ; on envoie QuestAccept/TurnInRequest au shard avec
						// le PNJ dont le dialogue est actuellement ouvert (Task 4b —
						// m_currentDialogueNpcTargetId, mémorisé par OpenDialogue depuis
						// InteractableEntity::npcTargetId ; PAS UIModel.giverList.npcTargetId,
						// qui n'est mis à jour que par une réponse QuestGiverList et resterait
						// vide/périmé car OpenDialogue est purement local hors Talk explicite).
						// Ignoré si questKey vide (dialogue non relié au catalogue quêtes SP1/SP2).
						if (!questKey.empty() && m_gameplayNetInitialized)
						{
							const uint32_t gameplayClientId = m_gameplayUdp.ServerClientId();
							const std::string& npcTargetId = m_currentDialogueNpcTargetId;
							if (action == engine::client::DialogueAction::AcceptQuest)
								(void)m_gameplayUdp.SendQuestAcceptRequest(gameplayClientId, questKey, npcTargetId);
							else if (action == engine::client::DialogueAction::CompleteQuest)
								(void)m_gameplayUdp.SendQuestTurnInRequest(gameplayClientId, questKey, npcTargetId);
						}
					});
				const int64_t intervalCfg = m_cfg.GetInt("client.save_position.interval_sec", 30);
				m_savePositionIntervalSec = std::chrono::seconds(std::max<int64_t>(5, intervalCfg));
				m_nextSavePositionTime = std::chrono::steady_clock::now() + m_savePositionIntervalSec;
				m_shutdownPositionSaved = false;
				// Reset defensif du chat focus : si l'utilisateur a tape Enter pendant
				// la saisie du login, ChatUiPresenter::Update aurait toggle m_chatFocus=true.
				// In-game ce flag bloque l'orbital camera Update (cf. line 3275).
				if (m_chatUi.IsInitialized() && m_chatUi.IsChatFocusActive())
				{
					m_chatUi.SetChatFocus(false);
					LOG_INFO(Core, "[EnterWorld] chat focus reset OFF (etait active depuis l'auth)");
				}
				LOG_INFO(Core, "[EnterWorld] periodic position save armed (character_id={}, interval={}s)",
					m_currentCharacterId, m_savePositionIntervalSec.count());

				// Phase 4 chat — Annonce le perso actif au master pour le sender display +
				// la résolution de cible /whisper. Fire-and-forget : la réponse arrive via
				// PumpPostAuthEvents et est juste loggée en debug (pas de blocage UI).
				if (m_currentCharacterId != 0u && !enterCmd.characterName.empty())
				{
					(void)m_authUi.SendEnterWorldAsync(m_currentCharacterId, enterCmd.characterName);
					// Phase 5 reconnect — mémorise l'identité pour pouvoir ré-envoyer
					// CHARACTER_ENTER_WORLD à la reconnexion sans repasser par tout le flow.
					m_authUi.RememberPostEnterWorldCharacter(m_currentCharacterId, enterCmd.characterName);
				}

				// Phase 5 Lunar — fetch initial lunar state (master-authoritative).
				// Le master repondra via opcode 193 (LunarStateResponse) qui est
				// dispatche dans le push handler ci-dessus pour appeler
				// m_dayNight.OnLunarPhaseChange. Le push 194 arrive ensuite a
				// chaque changement de phase (~21h).
				{
					std::vector<uint8_t> lunarPayload;
					engine::network::lunar::BuildLunarStateRequestPayload(lunarPayload);
					(void)m_authUi.SendGenericRequestAsync(
						engine::network::kOpcodeLunarStateRequest, lunarPayload);
				}

				// WorldClock sync — l'horloge est désormais reçue en piggyback de
				// la réponse liste des personnages (opcode 40) et stockée dans
				// l'AuthUi. On l'applique ici (main thread) au lieu d'envoyer une
				// requête 203 séparée. Repli : si aucune horloge piggyback (vieux
				// master / requête liste échouée), on garde l'ancien comportement
				// (requête 203) pour ne pas régresser le solo/compat.
				if (m_authUi.HasWorldClock())
				{
					const auto& wc = m_authUi.WorldClock();
					engine::world::WorldClockParams p;
					p.epochRefUnixMs         = wc.epochRefUnixMs;
					p.timeScaleRealMinPerDay = wc.timeScaleRealMinPerDay;
					p.offsetGameSec          = wc.offsetGameSec;
					p.paused                 = (wc.paused != 0u);
					p.pausedAtGameSec        = wc.pausedAtGameSec;
					p.lunarPeriodGameSec     = wc.lunarPeriodGameSec;
					m_dayNight.SetServerClock(p, wc.serverTimeUnixMs, m_authUi.WorldClockClientRecvMs());
					m_authUi.ClearWorldClock();
					LOG_INFO(Render, "[Engine] WorldClock applique depuis le piggyback liste perso");
				}
				else
				{
					std::vector<uint8_t> wcReq;
					engine::network::worldclock::BuildWorldClockStateRequestPayload(wcReq);
					(void)m_authUi.SendGenericRequestAsync(
						engine::network::kOpcodeWorldClockStateRequest, wcReq);
				}

				// Override runtime du host:port gameplay UDP par l'endpoint du shard accepté.
				// InitGameplayNet relit ces clés à l'appel (cf. ligne ~3552) ; les écraser avant
				// l'init est suffisant pour cibler le bon shard.
				if (!enterCmd.shardEndpoint.empty())
				{
					const size_t colon = enterCmd.shardEndpoint.rfind(':');
					if (colon != std::string::npos)
					{
						const std::string host = enterCmd.shardEndpoint.substr(0, colon);
						const int64_t port = std::strtoll(enterCmd.shardEndpoint.substr(colon + 1).c_str(), nullptr, 10);
						if (!host.empty() && port > 0 && port < 65536)
						{
							m_cfg.SetValue("client.gameplay_udp.host", host);
							m_cfg.SetValue("client.gameplay_udp.port", port);
							m_cfg.SetValue("client.gameplay_udp.enabled", true);
							// Phase 3.7 — Le shard utilise déjà clientNonce comme tentativeCharacterKey
							// (cf. ServerApp::HandleHello). On override la clé config pour propager le
							// character_id réel sélectionné par l'utilisateur.
							// Phase 3.7.5 — clientNonce est désormais uint64, plus de troncation des bits hauts.
							// La config porte un int64_t (signé) ; on reinterpret-cast bit-à-bit pour préserver
							// la valeur uint64 (negative-looking si bit 63 set, mais réinterprété en uint64
							// côté lecture).
							if (enterCmd.characterId != 0u)
							{
								m_cfg.SetValue("client.gameplay_udp.character_key",
									static_cast<int64_t>(enterCmd.characterId));
								LOG_INFO(Core, "[EnterWorld] propagating character_id={} as gameplay UDP character_key (uint64)",
									enterCmd.characterId);
							}
							else
							{
								// Correctif visibilité 1ère connexion — diagnostic : un characterId==0
								// ici signifie que le Hello UDP partirait avec la clé de config
								// résiduelle (souvent 1) → le shard associerait ce client au mauvais
								// personnage → invisibilité mutuelle (corrigé en amont côté
								// AuthUiPresenter, cf. CODEBASE_MAP §59). Ce log doit rester muet en
								// fonctionnement nominal ; s'il apparaît, le fallback d'id amont a
								// échoué et il faut investiguer le flux post-création.
								LOG_ERROR(Core, "[EnterWorld] character_id == 0 a l'entree en jeu : le character_key "
									"gameplay UDP n'est PAS surcharge (risque d'invisibilite mutuelle). "
									"Verifier la CHARACTER_LIST / le fallback de creation.");
							}
							// Si la session UDP a été ouverte au boot avec un host différent
							// (config par défaut), on la coupe avant de la rouvrir sur le bon shard.
							if (m_gameplayNetInitialized)
							{
								ShutdownGameplayNet();
							}
							InitGameplayNet();
						}
						else
						{
							LOG_WARN(Core, "[EnterWorld] endpoint invalide host='{}' port={} : connexion gameplay non démarrée",
								host, port);
						}
					}
					else
					{
						LOG_WARN(Core, "[EnterWorld] endpoint sans ':' ('{}') : connexion gameplay non démarrée",
							enterCmd.shardEndpoint);
					}
				}
				else
				{
					LOG_WARN(Core, "[EnterWorld] endpoint vide : connexion gameplay non démarrée (la scène 3D s'affichera quand même mais sans réseau)");
				}
			}

			// Phase 3.6.6 — Drain des événements de la connexion master encore vivante post-auth.
			// AuthUiPresenter conserve m_masterClient + m_masterSessionId après EnterWorld grâce
			// au fix Phase 2/3 (suppression des ResetMasterSession() avant MasterFlow). Ici on
			// pompe pour récupérer les réponses CHARACTER_SAVE_POSITION_RESPONSE (loggées en debug)
			// et détecter une déconnexion master inattendue (auquel cas SavePositionAsync échouera
			// proprement aux prochains ticks).
			m_authUi.PumpPostAuthEvents();

			// Phase 5 reconnect — Si une déconnexion master a été détectée par PumpPostAuthEvents,
			// AuthUi est passé en mode reconnect. TickReconnect lance la tentative auto au bon moment.
			m_authUi.TickReconnect(m_cfg);

			// Phase 3.6.6 — Tick périodique de sauvegarde de position. Démarré à la consommation
			// de EnterWorldCommand (m_currentCharacterId != 0). Intervalle borné à >= 5 s côté
			// AuthUiPresenter::SavePositionAsync via la config `client.save_position.interval_sec`.
			//
			// Etape 6 vue 3eme personne : ajout d'une heuristique mouvement -> save plus
			// frequente. Quand le perso a bouge de plus de 0.5 m depuis la derniere
			// synchro, on declenche immediatement (rate-limite a 1.0 s entre 2 saves).
			// Si statique, on revient a l'intervalle long (m_savePositionIntervalSec).
			if (m_currentCharacterId != 0u)
			{
				const engine::math::Vec3& playerPos = m_orbitalCameraController.GetTargetPosition();
				const float dx = playerPos.x - m_lastSyncedPosition.x;
				const float dy = playerPos.y - m_lastSyncedPosition.y;
				const float dz = playerPos.z - m_lastSyncedPosition.z;
				const float dist2 = dx * dx + dy * dy + dz * dz;
				constexpr float kMoveThresholdM   = 0.5f;
				constexpr float kMoveThresholdSqr = kMoveThresholdM * kMoveThresholdM;
				const auto now = std::chrono::steady_clock::now();
				const bool intervalElapsed = now >= m_nextSavePositionTime;
				const bool movedSignificantly = dist2 >= kMoveThresholdSqr;
				if (intervalElapsed || movedSignificantly)
				{
					constexpr float kRad2Deg = 180.f / 3.14159265f;
					const float yawDeg   = out.camera.yaw   * kRad2Deg;
					const float pitchDeg = out.camera.pitch * kRad2Deg;
					if (m_authUi.SavePositionAsync(m_currentCharacterId,
						playerPos.x, playerPos.y, playerPos.z,
						yawDeg, pitchDeg))
					{
						LOG_DEBUG(Core, "[SavePosition] sync sent (character_id={}, pos=({:.1f},{:.1f},{:.1f}), reason={})",
							m_currentCharacterId, playerPos.x, playerPos.y, playerPos.z,
							movedSignificantly ? "moved" : "tick");
					}
					m_lastSyncedPosition = playerPos;
					// Si on est ici parce qu'on a bouge (et pas a cause de l'intervalle
					// long), on rate-limite la prochaine sync a 1.0 s minimum pour ne
					// pas spammer le serveur tant que le joueur enchaine les pas.
					// Sinon on retombe sur l'intervalle long (config-driven).
					const auto nextDelay = movedSignificantly ? std::chrono::milliseconds(1000)
					                                          : std::chrono::duration_cast<std::chrono::milliseconds>(m_savePositionIntervalSec);
					m_nextSavePositionTime = now + nextDelay;
				}
			}

			// Phase 5 reconnect — Si une tentative de reconnexion master est en cours,
			// la bannière de statut prend la priorité (elle remplace même la bannière welcome).
			// Le texte est court et localisé côté AuthUi (Tr("auth.info.reconnect_in_progress")).
			// Phase 3.5 — Affichage de la bannière "Bienvenue" tant qu'elle n'a pas expiré.
			// Phase 3.11 — Quand la bannière a expiré, on affiche le panneau chat à la place
			// (c'est la première surface visuelle pour le système de chat post-auth).
			// Priorité : reconnect > banner > chat > vide.
			if (m_authUi.IsReconnecting() && !m_authUi.ReconnectStatusText().empty())
			{
				m_window.SetOverlayText(m_authUi.ReconnectStatusText());
			}
			else if (!m_enterWorldBannerText.empty()
				&& std::chrono::steady_clock::now() < m_enterWorldBannerExpiry)
			{
				m_window.SetOverlayText(m_enterWorldBannerText);
			}
			else
			{
				if (!m_enterWorldBannerText.empty())
				{
					// Premier frame d'expiration : on libère explicitement le texte de la bannière.
					m_enterWorldBannerText.clear();
				}
				// Phase 3.11.1 — Si le panneau ImGui chat est actif (Windows + render.chat_imgui.enabled),
				// on n'écrit PAS le texte overlay Win32 pour éviter le double affichage.
				// Sinon (Linux build, ou flag désactivé), on retombe sur l'overlay texte legacy.
				bool chatImguiOwnsDisplay = false;
#if defined(_WIN32)
				chatImguiOwnsDisplay = m_chatImGui && m_chatUi.IsInitialized()
					&& m_cfg.GetBool("render.chat_imgui.enabled", true);
#endif
				if (chatImguiOwnsDisplay)
				{
					m_window.SetOverlayText({});
				}
				else if (m_chatUi.IsInitialized())
				{
					std::string chatHud = m_chatUi.BuildHudPanelText();
					if (!chatHud.empty())
					{
						m_window.SetOverlayText(chatHud);
					}
					else
					{
						m_window.SetOverlayText({});
					}
				}
				else
				{
					m_window.SetOverlayText({});
				}
			}
		}

#if defined(_WIN32)
		// Dear ImGui : lire io.WantCapture* après NewFrame() pour la frame courante (sinon la caméra reste figée).
		const bool authImguiOverlayNewFrame = m_authImGui && m_authUi.GetVisualState().active
			&& m_cfg.GetBool("render.auth_ui.imgui.enabled", false);
		// Phase 3.11.1 — NewFrame également quand le panneau chat doit s'afficher (post-auth, pas en éditeur).
		// Responsabilite : chat HUD VISIBLE des que le master a accepte l'AUTH
		// (Global + Friends) ; la liste s'enrichit du canal Zone une fois le shard
		// rejoint. Cf. retour utilisateur : 'une fois authentifie, il doit y avoir
		// le chat global + amis ; une fois le royaume choisi, + zone'.
		const bool postAuthMaster = m_authUi.IsInitialized() && m_authUi.IsMasterAuthenticated()
			&& !m_worldEditorExe;
		// Chat HUD : on garde l'appel a ImGui::NewFrame en post-master-auth (meme
		// pre-EnterWorld) car la branche de rendu in-game ligne 3739+ fait
		// m_chatImGui->Render + ImGui::Render() en supposant qu'un NewFrame a deja ete
		// appele plus haut. Sans cet appel, ImGui::Render() utilise des draw data stale
		// -> swapchain presente le meme framebuffer en boucle, ecran fige.
		// Le RENDU du panneau chat pre-EnterWorld reste desactive (cf. branche
		// auth-rendering qui n'appelle plus m_chatImGui->Render).
		const bool chatImguiOverlayNewFrame = m_chatImGui && m_chatUi.IsInitialized()
			&& postAuthMaster
			&& (m_cfg.GetBool("render.chat_imgui.enabled", true) || m_inGamePauseMenuVisible || m_inGameOptionsPanelVisible);
		// M43.4 — NewFrame également quand --editor (sans world-editor exe) actif.
		const bool editorHubOverlayNewFrame = m_editorHubImGui && m_editorEnabled && !m_worldEditorExe;
		if (m_worldEditorImGui && m_worldEditorImGui->IsReady()
			&& (m_worldEditorExe || authImguiOverlayNewFrame || chatImguiOverlayNewFrame || editorHubOverlayNewFrame))
		{
			float imguiDw = static_cast<float>(std::max(1, m_width));
			float imguiDh = static_cast<float>(std::max(1, m_height));
			if (m_vkSwapchain.IsValid())
			{
				const VkExtent2D extImg = m_vkSwapchain.GetExtent();
				if (extImg.width > 0 && extImg.height > 0)
				{
					imguiDw = static_cast<float>(extImg.width);
					imguiDh = static_cast<float>(extImg.height);
				}
			}
			m_worldEditorImGui->NewFrame(static_cast<float>(dt), imguiDw, imguiDh);
		}

		// M100.34 incrément 3 — Resize de la cible offscreen viewport sur la
		// taille réelle du ScenePanel. Sans ça, la target reste fixée à la
		// taille de la swapchain au boot et l'image est étirée par
		// `ImGui::Image` dans le panneau (aspect ratio cassé). La mesure
		// `ScenePanel::GetViewportWidth/Height` vient du frame précédent
		// (set dans `ScenePanel::Render` post-`ImGui::GetContentRegionAvail`).
		// Si elle diffère de la target actuelle, on attend que le device
		// soit idle (sinon destroy d'une image en cours d'utilisation) puis
		// on recrée à la nouvelle taille. Pattern rare (resize panel = action
		// utilisateur sporadique), donc waitIdle acceptable côté perf.
		if (m_worldEditorExe && m_worldEditorShell
			&& m_worldEditorShell->IsInitialized()
			&& m_editorViewportTarget.IsValid()
			&& !m_worldEditorShell->Panels().empty()
			&& m_worldEditorShell->Panels()[0])
		{
			auto* scenePanel = dynamic_cast<engine::editor::world::panels::ScenePanel*>(
				m_worldEditorShell->MutablePanels()[0].get());
			if (scenePanel != nullptr)
			{
				const int wRaw = scenePanel->GetViewportWidth();
				const int hRaw = scenePanel->GetViewportHeight();
				const uint32_t w = (wRaw > 0) ? static_cast<uint32_t>(wRaw) : 0u;
				const uint32_t h = (hRaw > 0) ? static_cast<uint32_t>(hRaw) : 0u;
				if (w > 0u && h > 0u
					&& (w != m_editorViewportTarget.GetWidth()
					 || h != m_editorViewportTarget.GetHeight()))
				{
					vkDeviceWaitIdle(m_vkDeviceContext.GetDevice());
					if (!m_editorViewportTarget.Resize(
						m_vkDeviceContext.GetDevice(),
						m_vkDeviceContext.GetPhysicalDevice(),
						m_vkDeviceContext.GetGraphicsQueue(),
						m_vkDeviceContext.GetGraphicsQueueFamilyIndex(),
						w, h))
					{
						LOG_WARN(Render,
							"[Engine] EditorViewportRenderTarget::Resize({}x{}) failed",
							w, h);
					}
				}
			}
		}

		// M100.1 — Rendu de la coquille du nouvel éditeur monde. Doit être
		// appelée après ImGui::NewFrame (fait par WorldEditorImGui::NewFrame
		// ci-dessus, qui partage le même contexte ImGui) et avant
		// ImGui::Render. RenderFrame est no-op si Init() n'a pas été appelé
		// avec succès. Pas thread : main thread uniquement.
		if (m_worldEditorShell && m_worldEditorShell->IsInitialized()
			&& m_worldEditorImGui && m_worldEditorImGui->IsReady())
		{
			// M100.34 incrément 1 — pousse le texture ID de l'image offscreen
			// du viewport au ScenePanel (premier panel du Shell, ordre stable
			// garanti par WorldEditorShell::Init). PR 1 : image noire, pas
			// encore branchée au FrameGraph. PR 2 : copie SceneColor_LDR
			// dedans et la cible devient visible.
			if (m_editorViewportTarget.IsValid()
				&& !m_worldEditorShell->Panels().empty()
				&& m_worldEditorShell->Panels()[0])
			{
				auto* scenePanel = dynamic_cast<engine::editor::world::panels::ScenePanel*>(
					m_worldEditorShell->MutablePanels()[0].get());
				if (scenePanel != nullptr)
				{
					scenePanel->SetEditorViewportTextureId(
						m_editorViewportTarget.GetImguiTextureId());
				}
			}
			// Sous-projet 1, bloc B/C — alimente l'Outliner / l'Inspector : lie le
			// modèle de scène aux documents sources (layout = session, mesh/donjon
			// = shell) puis le reconstruit. Coût négligeable (quelques clears +
			// push de vecteurs) ; fait chaque frame pour refléter les placements/
			// suppressions immédiatement.
			if (m_worldEditorSession)
			{
				m_worldEditorShell->MutableSceneModel().Bind(
					&m_worldEditorSession->Doc(),
					&m_worldEditorShell->GetMeshInsertDocument(),
					&m_worldEditorShell->GetDungeonPortalDocument());
				m_worldEditorShell->MutableSceneModel().Rebuild();

				// Sous-projet 1, bloc D — foncteur d'écriture de transform consommé
				// par l'Inspector (SetEntityTransformCommand). Écrit dans le
				// document concret par EntityId.index (= position dans la liste
				// source au moment du Rebuild de cette frame). Capture [this] :
				// l'Engine survit aux commandes (undo/redo différé OK).
				m_worldEditorShell->SetTransformWriter(
					[this](engine::editor::scene::EntityId id,
						const engine::editor::scene::EntityTransform& t)
					{
						using K = engine::editor::scene::EntityKind;
						if (id.kind == K::LayoutInstance && m_worldEditorSession)
						{
							auto& insts = m_worldEditorSession->MutableDoc().layoutInstances;
							if (id.index < insts.size())
							{
								auto& inst = insts[id.index];
								inst.worldX = static_cast<double>(t.position.x);
								inst.worldY = static_cast<double>(t.position.y);
								inst.worldZ = static_cast<double>(t.position.z);
								inst.yawDegrees = static_cast<double>(t.eulerDeg.y);
								inst.uniformScale = static_cast<double>(t.uniformScale);
							}
						}
						else if (id.kind == K::MeshInsert && m_worldEditorShell)
						{
							auto& doc = m_worldEditorShell->MutableMeshInsertDocument();
							const auto& all = doc.All();
							if (id.index < all.size())
							{
								auto updated = all[id.index];
								updated.worldPosition = t.position;
								updated.eulerRotationDeg = t.eulerDeg;
								updated.uniformScale = t.uniformScale;
								doc.Update(updated.guid, updated);
							}
						}
						else if (id.kind == K::DungeonPortal && m_worldEditorShell)
						{
							auto& doc = m_worldEditorShell->MutableDungeonPortalDocument();
							const auto& all = doc.All();
							if (id.index < all.size())
							{
								auto updated = all[id.index];
								updated.worldPosition = t.position;
								updated.eulerRotationDeg = t.eulerDeg;
								doc.Update(updated.guid, updated);
							}
						}
					});

				// Lot 5 (2026-07-18) — Foncteurs d'édition STRUCTURELLE des
				// entités (Dupliquer/Supprimer undoables). Même pattern que le
				// TransformWriter ci-dessus : l'Engine capture [this] pour
				// atteindre le doc layout (session) que le shell ne possède
				// pas. Réinstallés chaque frame (comme le writer) — les
				// commandes gardent leur PROPRE copie des foncteurs au Push.
				{
					using engine::editor::scene::EntityId;
					using engine::editor::scene::EntityKind;
					using engine::editor::scene::EntitySnapshot;
					engine::editor::scene::EntityEditOps ops;

					// Copie l'entité (kind+index scène) en snapshot à guid
					// stable, sans la retirer. false si index hors bornes ou
					// kind non éditable.
					ops.capture = [this](EntityId id, EntitySnapshot& out) -> bool
					{
						if (id.kind == EntityKind::LayoutInstance && m_worldEditorSession)
						{
							const auto& insts = m_worldEditorSession->Doc().layoutInstances;
							if (id.index >= insts.size()) return false;
							out.kind = id.kind;
							out.layout = insts[id.index];
							out.layoutIndex = id.index;
							return true;
						}
						if (id.kind == EntityKind::MeshInsert && m_worldEditorShell)
						{
							const auto& all = m_worldEditorShell->GetMeshInsertDocument().All();
							if (id.index >= all.size()) return false;
							out.kind = id.kind;
							out.meshInsert = all[id.index];
							return true;
						}
						if (id.kind == EntityKind::DungeonPortal && m_worldEditorShell)
						{
							const auto& all = m_worldEditorShell->GetDungeonPortalDocument().All();
							if (id.index >= all.size()) return false;
							out.kind = id.kind;
							out.portal = all[id.index];
							return true;
						}
						return false;
					};

					// Retire l'entité du snapshot — par guid (stable), jamais
					// par index (instable après édition structurelle).
					ops.remove = [this](const EntitySnapshot& s) -> bool
					{
						if (s.kind == EntityKind::LayoutInstance && m_worldEditorSession)
						{
							auto& insts = m_worldEditorSession->MutableDoc().layoutInstances;
							for (size_t i = 0; i < insts.size(); ++i)
							{
								if (insts[i].guid == s.layout.guid)
								{
									insts.erase(insts.begin() + static_cast<ptrdiff_t>(i));
									return true;
								}
							}
							return false;
						}
						if (s.kind == EntityKind::MeshInsert && m_worldEditorShell)
							return m_worldEditorShell->MutableMeshInsertDocument().Remove(s.meshInsert.guid);
						if (s.kind == EntityKind::DungeonPortal && m_worldEditorShell)
							return m_worldEditorShell->MutableDungeonPortalDocument().Remove(s.portal.guid);
						return false;
					};

					// Réinsère le snapshot : au rang d'origine (borné) pour le
					// layout, par Add (guid conservé, compteur resynchronisé)
					// pour les volumes.
					ops.restore = [this](const EntitySnapshot& s) -> bool
					{
						if (s.kind == EntityKind::LayoutInstance && m_worldEditorSession)
						{
							auto& insts = m_worldEditorSession->MutableDoc().layoutInstances;
							const size_t at = std::min<size_t>(s.layoutIndex, insts.size());
							insts.insert(insts.begin() + static_cast<ptrdiff_t>(at), s.layout);
							return true;
						}
						if (s.kind == EntityKind::MeshInsert && m_worldEditorShell)
						{
							(void)m_worldEditorShell->MutableMeshInsertDocument().Add(s.meshInsert);
							return true;
						}
						if (s.kind == EntityKind::DungeonPortal && m_worldEditorShell)
						{
							(void)m_worldEditorShell->MutableDungeonPortalDocument().Add(s.portal);
							return true;
						}
						return false;
					};

					// Ajoute une COPIE de src (nouveau guid + décalage +1,5 m
					// en X pour que la copie soit visible à côté de
					// l'original) et renvoie son snapshot dans outCopy.
					ops.duplicate = [this](const EntitySnapshot& src, EntitySnapshot& outCopy) -> bool
					{
						constexpr double kDupOffsetM = 1.5;
						outCopy = src;
						if (src.kind == EntityKind::LayoutInstance && m_worldEditorSession)
						{
							auto& insts = m_worldEditorSession->MutableDoc().layoutInstances;
							// Préfixe "we_dup_" distinct du "we_inst_" de la
							// session : aucun risque de collision de guid.
							static uint64_t s_dupSeq = 1u;
							char buf[48];
							std::snprintf(buf, sizeof(buf), "we_dup_%llu",
								static_cast<unsigned long long>(s_dupSeq++));
							outCopy.layout.guid = buf;
							outCopy.layout.worldX += kDupOffsetM;
							outCopy.layoutIndex = static_cast<uint32_t>(insts.size());
							insts.push_back(outCopy.layout);
							return true;
						}
						if (src.kind == EntityKind::MeshInsert && m_worldEditorShell)
						{
							auto& doc = m_worldEditorShell->MutableMeshInsertDocument();
							outCopy.meshInsert.guid = 0u; // Add() assigne un nouveau guid
							outCopy.meshInsert.worldPosition.x += static_cast<float>(kDupOffsetM);
							outCopy.meshInsert.guid = doc.Add(outCopy.meshInsert);
							return true;
						}
						if (src.kind == EntityKind::DungeonPortal && m_worldEditorShell)
						{
							auto& doc = m_worldEditorShell->MutableDungeonPortalDocument();
							outCopy.portal.guid = 0u; // Add() assigne un nouveau guid
							outCopy.portal.worldPosition.x += static_cast<float>(kDupOffsetM);
							outCopy.portal.guid = doc.Add(outCopy.portal);
							return true;
						}
						return false;
					};

					m_worldEditorShell->SetEntityEditOps(std::move(ops));
				}
			}
			// Aperçu 3D live des bâtiments (brouillon + placements) dans la vue
			// éditeur. Reconstruit m_props seulement si l'aperçu est « sale ».
			SyncEditorBuildingPreview();
			m_worldEditorShell->RenderFrame();
			// Gizmo (axes/anneaux X=rouge/Y=vert/Z=bleu) sur la pièce active,
			// en overlay sur la 3D (sous les panneaux). Visuel seul pour l'instant.
			DrawEditorBuildingGizmo();
			// Roadmap-6 — gizmo des entités de scène sélectionnées (E/T/C) +
			// marqueurs de multi-sélection. No-op en mode édition bâtiment.
			DrawEditorSceneGizmo();
		}
#endif

		if (!m_editorEnabled)
		{
			if (!authGateActive && !m_chatUi.IsChatFocusActive() && !m_inGameOptionsPanelVisible
					// Menu Pause ouvert => gèle tout l'input gameplay (déplacement,
					// caméra, attaque, sorts, interaction), comme le panneau Options :
					// le joueur ne doit ni bouger ni attaquer tant que la pause est active.
					&& !m_inGamePauseMenuVisible)
			{
				// Touches d'action remappables (controls.keybind.*), resolues chaque
				// frame depuis la config pour refleter immediatement un rebind fait
				// dans le panneau Options. Defauts : sprint=Alt, crouch=Ctrl, sort=R.
				const engine::platform::Key sprintKey =
					KeyFromName(m_cfg.GetString("controls.keybind.sprint", "Alt"), engine::platform::Key::Alt);
				const engine::platform::Key crouchKey =
					KeyFromName(m_cfg.GetString("controls.keybind.crouch", "Ctrl"), engine::platform::Key::Control);
				const engine::platform::Key castKey =
					KeyFromName(m_cfg.GetString("controls.keybind.cast", "R"), engine::platform::Key::R);
				const engine::platform::Key interactKey =
					KeyFromName(m_cfg.GetString("controls.keybind.interact", "E"), engine::platform::Key::E);
				const engine::platform::Key punchKey =
					KeyFromName(m_cfg.GetString("controls.keybind.punch", "X"), engine::platform::Key::X);
				// Vue 3eme personne : controleur orbital pur (camera derriere la
				// cible). Souris libre par defaut ; clic droit maintenu = rotate
				// camera autour de la cible (yaw/pitch) ; molette = zoom.
				//
				// B.1 / Task 8 : OrbitalCameraController est devenu camera pure.
				// B.1 / Task 9 : on tick d'abord le CharacterController (physics +
				// collision sol) PUIS la camera suit la position resultante via
				// `SetTargetPosition`. Ordre important : si la camera tournait
				// avant le CC, son repere (Forward/Right XZ) servirait a projeter
				// l'input du frame courant mais sa position cible utiliserait la
				// position de la frame precedente -> 1-frame lag visible.
				auto moveInput = BuildMoveInput(m_input, m_orbitalCameraController, movementLayout, sprintKey, crouchKey);
				// Verrou de déplacement : on neutralise toutes les entrées de
				// déplacement (direction + saut) pour garder l'avatar immobile, dans
				// deux cas — (1) dialogue PNJ actif (cellule dédiée) ; (2) geste
				// verrouillant en cours (ex. ouverture de coffre via
				// m_avatarMoveLockUntilSec). La caméra reste libre dans les deux cas
				// (m_orbitalCameraController non touché). nowSec est recalculé ici (le
				// nowSec de la state machine n'est pas encore en portée à ce point) ;
				// l'écart sub-ms avec la garde roulade plus bas est sans effet visible.
				const bool moveLocked = EngineNowSec() < m_avatarMoveLockUntilSec;
				// Combat SP2 — un joueur mort ne bouge plus (overlay « Vous êtes
				// mort » affiché ; le respawn rend la main). kEntityStateDead = bit 0.
				const bool localPlayerDead = m_gameplayNetInitialized
					&& (m_uiModelBinding.GetModel().playerStats.stateFlags & 1u) != 0u;
				if (m_dialogueActive || moveLocked || localPlayerDead)
					moveInput = engine::gameplay::MoveInput{};
				// Roadmap-2 (2026-07-19) — vitesses AUTORITAIRES du serveur
				// (PlayerStats kind 79 : classe + équipement + buffs d'auras).
				// 0 = pas encore reçu → la config locale reste en vigueur.
				{
					const auto& ps79 = m_uiModelBinding.GetModel().playerStats;
					if (ps79.speedRun > 0.1f)
						m_characterController.SetMoveSpeeds(ps79.speedWalk, ps79.speedRun, ps79.speedSprint);
				}
				// Collisionneur composite : terrain (sol + eau) + cylindres des props/décor.
				m_characterController.Update(static_cast<float>(dt), moveInput, m_worldCollider);
				const engine::math::Vec3 ccPos = m_characterController.GetPosition();
				m_orbitalCameraController.SetTargetPosition(ccPos);

				// Yaw avatar : convention "back-step" simple. On declenche le
				// mode WalkBack (mesh inchange + anim Walking Backwards)
				// UNIQUEMENT si la touche back (S) est enfoncee seule, sans
				// aucune autre direction. Tout autre input (forward, strafe,
				// diagonale, S+autre direction) repasse en free-mover : le
				// mesh pivote pour aligner sa face sur la direction de
				// mouvement, et la state machine joue Walk/Run.
				//
				// L'ancienne heuristique basee sur dot(moveDir, mesh_forward)
				// donnait Q/D = strafe pur (no pivot) quand le perso etait
				// deja aligne (apres avoir avance avec W). Resultat : le
				// mesh ne pivotait plus en Q/D et l'utilisateur ne voyait
				// aucun mouvement visible (la camera suit, mesh reste centre).
				// Le simple "pure S" est plus previsible et conforme a la
				// convention MMO classique (Q/D = strafe avec pivot, S = back).
				const bool movingBack = IsPureBackInput(m_input);
				const bool hasMove = (moveInput.moveDirXZ.x != 0.0f || moveInput.moveDirXZ.z != 0.0f);
				if (hasMove && !movingBack)
				{
					// Free-mover : aligne le mesh sur la direction de marche.
					m_avatarYaw = std::atan2(moveInput.moveDirXZ.x, moveInput.moveDirXZ.z);
				}
				// Si movingBack : on garde m_avatarYaw, le mesh reste dos cam
				// et la state machine bascule en WalkBack ci-dessous.

				// Memorise l'input pour la state machine de locomotion (Task 11
				// l'etendra a 7 etats — Idle / Walk / Run / Jump / Fall / Land /
				// Crouch — et lira `m_lastMoveInput` pour decider).
				m_lastMoveInput = moveInput;

				// --- B.1 / Task 11 : state machine 7 etats + crossfade ---
				//
				// Driven par CharacterController.IsGrounded + moveInput. Remplace la
				// state machine A (qui vivait dans le lambda Geometry, basee sur le
				// delta de la model matrix entre frames). On dispose ici de signaux
				// gameplay propres : `grounded` (sol vs air), `moving` (intention
				// d'input non-nulle), `jumpPressed` (edge sur Space), `run` (Shift).
				//
				// Transitions a portee de main :
				//   - Idle <-> Walk/Run via StartWalking (transition lift-off one-shot).
				//   - Jump -> Fall apres 40% du clip Jump (= takeoff termine).
				//   - Fall -> Land au moment ou le CC retouche le sol.
				//   - Land -> Idle/Walk/Run en fonction de l'input apres fin de Land.
				//
				// Trigger crossfade : a chaque transition, on appelle
				// `m_avatarCrossfade.Play(clip, loops, nowSec)`. Le clip vit dans
				// `m_currentSkinnedMesh->clips` (storage stable -> pointeur safe
				// jusqu'au shutdown). Le sampling pleine pose se fait dans le
				// lambda Geometry via `m_avatarCrossfade.Sample(skel, nowSec)`.
				if (m_skinnedAvatarReady && m_currentSkinnedMesh)
				{
					const bool grounded = m_characterController.IsGrounded();
					const bool moving = (moveInput.moveDirXZ.x != 0.0f || moveInput.moveDirXZ.z != 0.0f);

					const auto now = std::chrono::steady_clock::now();
					// nowSec via EngineNowSec : meme reference de temps que les sites de
					// Sample/Play, evite la perte de precision float 32 quand on utilise
					// time_since_epoch (~10^9 secondes depuis boot machine).
					const float nowSec = EngineNowSec();
					const float stateElapsed = std::chrono::duration<float>(now - m_avatarLocoStateEnterTime).count();

					const engine::render::skinned::AnimationClip* startWalkClip =
						m_currentSkinnedMesh->FindClip("StartWalking");
					const engine::render::skinned::AnimationClip* jumpClip =
						m_currentSkinnedMesh->FindClip("Jump");
					const engine::render::skinned::AnimationClip* landClip =
						m_currentSkinnedMesh->FindClip("Land");
					const engine::render::skinned::AnimationClip* rollClip =
						m_currentSkinnedMesh->FindClip("Roll");
					const engine::render::skinned::AnimationClip* attackClip =
						m_currentSkinnedMesh->FindClip("Attack");
					const engine::render::skinned::AnimationClip* castClip =
						m_currentSkinnedMesh->FindClip("Cast");
					const engine::render::skinned::AnimationClip* castShootClip =
						m_currentSkinnedMesh->FindClip("CastShoot");
					const engine::render::skinned::AnimationClip* castExitClip =
						m_currentSkinnedMesh->FindClip("CastExit");
					// Clip dynamique : "Interact" (geste générique) ou "PickUp_Table"
					// (près d'un coffre). m_currentInteractRole est fixé au déclenchement.
					const engine::render::skinned::AnimationClip* interactClip =
						m_currentSkinnedMesh->FindClip(m_currentInteractRole.c_str());
					const engine::render::skinned::AnimationClip* punchClip =
						m_currentSkinnedMesh->FindClip(m_currentPunchRole.c_str());

					// Attaque melee : clic gauche (edge). Le bloc gameplay est deja garde
					// contre le focus chat / l'auth (cf. ligne ~6969) ; on exclut en plus
					// le drag inventaire pour ne pas frapper en relachant un objet.
					// Touche d'attaque ALTERNATIVE (controls.keybind.attack, vide = clic gauche
					// seul). Permet de remapper l'attaque sur une touche, en plus du clic gauche.
					// Round-trip KeyName(KeyFromName(...)) pour ignorer une valeur invalide.
					const std::string attackKeyName = m_cfg.GetString("controls.keybind.attack", "");
					const engine::platform::Key attackKey = KeyFromName(attackKeyName, engine::platform::Key::Escape);
					const bool attackKeyBound = !attackKeyName.empty() && std::string(KeyName(attackKey)) == attackKeyName;
					// Pendant un dialogue PNJ : aucune attaque. Le clic gauche sert à
					// sélectionner une réponse (sinon le perso frappe en même temps).
					const bool attackPressed = !m_dialogueActive &&
						((m_input.WasMousePressed(engine::platform::MouseButton::Left) && !m_invUi.IsDragging())
						|| (attackKeyBound && m_input.WasPressed(attackKey)));

					// Sort : touche R (edge). Meme bloc gameplay garde contre le focus
					// chat / l'auth (cf. ligne ~6961). Geste cosmetique one-shot (pas de
					// cible ni d'aller-retour serveur), pendant clavier de l'attaque souris.
					const bool castPressed =
						m_input.WasPressed(castKey) && !m_dialogueActive;

					// Interagir : touche remappable (controls.keybind.interact, def. E),
					// edge. Action non-combat (la touche E reservee au §32 trouve ici son
					// usage). Geste cosmetique one-shot ; cible/objet a brancher plus tard.
					const bool interactPressed =
						m_input.WasPressed(interactKey);

					// Coup de poing : 2e attaque melee, touche remappable (controls.keybind.punch, def. X).
					const bool punchPressed =
						m_input.WasPressed(punchKey) && !m_dialogueActive;

					// Esquive/roulade : double-appui (fenetre 0.30s) sur la touche Crouch
					// (remappable). Touche maintenue = crouch ; deux appuis = Roll (one-shot).
					bool dodgePressed = false;
					if (m_input.WasPressed(crouchKey))
					{
						if (nowSec - m_lastCtrlTapSec <= 0.30f)
							dodgePressed = true;
						m_lastCtrlTapSec = nowSec;
					}
					// Emote demandee par slash command, consommee ici (vide = aucune).
					const std::string emoteRole = m_pendingEmoteRole;
					m_pendingEmoteRole.clear();

					AvatarLocomotionState newState = m_avatarLocoState;
					if (grounded)
					{
						switch (m_avatarLocoState)
						{
							case AvatarLocomotionState::Idle:
								if (moveInput.jumpPressed)        newState = AvatarLocomotionState::Jump;
								else if (movingBack)              newState = AvatarLocomotionState::WalkBack;
								else if (moving)                  newState = AvatarLocomotionState::StartWalking;
								break;
							case AvatarLocomotionState::StartWalking:
								if (!moving)                      newState = AvatarLocomotionState::Idle;
								else if (moveInput.jumpPressed)   newState = AvatarLocomotionState::Jump;
								else if (movingBack)              newState = AvatarLocomotionState::WalkBack;
								else if (startWalkClip && stateElapsed >= startWalkClip->duration)
									newState = (moveInput.sprint ? AvatarLocomotionState::Sprint
									            : moveInput.run ? AvatarLocomotionState::Run
									            : AvatarLocomotionState::Walk);
								break;
							case AvatarLocomotionState::Walk:
								if (!moving)                      newState = AvatarLocomotionState::Idle;
								else if (moveInput.jumpPressed)   newState = AvatarLocomotionState::Jump;
								else if (movingBack)              newState = AvatarLocomotionState::WalkBack;
								else if (moveInput.sprint)        newState = AvatarLocomotionState::Sprint;
								else if (moveInput.run)           newState = AvatarLocomotionState::Run;
								break;
							case AvatarLocomotionState::WalkBack:
								if (!moving)                      newState = AvatarLocomotionState::Idle;
								else if (moveInput.jumpPressed)   newState = AvatarLocomotionState::Jump;
								else if (!movingBack)             newState = AvatarLocomotionState::Walk;
								break;
							case AvatarLocomotionState::Run:
								if (!moving)                      newState = AvatarLocomotionState::Idle;
								else if (moveInput.jumpPressed)   newState = AvatarLocomotionState::Jump;
								else if (movingBack)              newState = AvatarLocomotionState::WalkBack;
								else if (moveInput.sprint)        newState = AvatarLocomotionState::Sprint;
								else if (!moveInput.run)          newState = AvatarLocomotionState::Walk;
								break;
							case AvatarLocomotionState::Sprint:
								if (!moving)                      newState = AvatarLocomotionState::Idle;
								else if (moveInput.jumpPressed)   newState = AvatarLocomotionState::Jump;
								else if (movingBack)              newState = AvatarLocomotionState::WalkBack;
								else if (!moveInput.sprint)
									newState = (moveInput.run ? AvatarLocomotionState::Run : AvatarLocomotionState::Walk);
								break;
							case AvatarLocomotionState::Jump:
								// Takeoff = first 40% of Jump clip ; then Fall (until grounded).
								if (jumpClip && stateElapsed >= jumpClip->duration * 0.4f)
									newState = AvatarLocomotionState::Fall;
								break;
							case AvatarLocomotionState::Fall:
								// Touched ground -> Land.
								newState = AvatarLocomotionState::Land;
								break;
							case AvatarLocomotionState::Land:
								if (landClip && stateElapsed >= landClip->duration)
								{
									if (!moving)                  newState = AvatarLocomotionState::Idle;
									else if (movingBack)          newState = AvatarLocomotionState::WalkBack;
									else if (moveInput.sprint)    newState = AvatarLocomotionState::Sprint;
									else if (moveInput.run)       newState = AvatarLocomotionState::Run;
									else                          newState = AvatarLocomotionState::Walk;
								}
								break;
							case AvatarLocomotionState::CrouchIdle:
							case AvatarLocomotionState::CrouchWalk:
								// Base de sortie vers la station debout ; l'override crouch ci-dessous
								// re-force le crouch tant que Ctrl est tenu.
								if (moveInput.jumpPressed)        newState = AvatarLocomotionState::Jump;
								else if (!moving)                 newState = AvatarLocomotionState::Idle;
								else if (movingBack)              newState = AvatarLocomotionState::WalkBack;
								else if (moveInput.sprint)        newState = AvatarLocomotionState::Sprint;
								else if (moveInput.run)           newState = AvatarLocomotionState::Run;
								else                              newState = AvatarLocomotionState::Walk;
								break;
							case AvatarLocomotionState::Roll:
								// One-shot : retour locomotion debout quand le clip Roll est fini.
								if (!rollClip || stateElapsed >= rollClip->duration)
								{
									if (!moving)                  newState = AvatarLocomotionState::Idle;
									else if (movingBack)          newState = AvatarLocomotionState::WalkBack;
									else if (moveInput.sprint)    newState = AvatarLocomotionState::Sprint;
									else if (moveInput.run)       newState = AvatarLocomotionState::Run;
									else                          newState = AvatarLocomotionState::Walk;
								}
								break;
							case AvatarLocomotionState::Emote:
								// Emote en boucle : interrompue par tout mouvement / saut.
								if (moving || movingBack || moveInput.jumpPressed)
								{
									if (moveInput.jumpPressed)    newState = AvatarLocomotionState::Jump;
									else if (movingBack)          newState = AvatarLocomotionState::WalkBack;
									else if (moveInput.sprint)    newState = AvatarLocomotionState::Sprint;
									else if (moveInput.run)       newState = AvatarLocomotionState::Run;
									else                          newState = AvatarLocomotionState::Walk;
								}
								break;
							case AvatarLocomotionState::Attack:
								// One-shot : retour locomotion quand le clip d'attaque est fini.
								// Le deplacement reste pilote par le CharacterController pendant
								// l'attaque (geste plein corps, pas de root motion).
								if (!attackClip || stateElapsed >= attackClip->duration)
								{
									if (!moving)                  newState = AvatarLocomotionState::Idle;
									else if (movingBack)          newState = AvatarLocomotionState::WalkBack;
									else if (moveInput.sprint)    newState = AvatarLocomotionState::Sprint;
									else if (moveInput.run)       newState = AvatarLocomotionState::Run;
									else                          newState = AvatarLocomotionState::Walk;
								}
								break;
							case AvatarLocomotionState::Cast:
							{
								// Sequence : Enter (clip "Cast") -> Shoot -> Exit, les 2 derniers rejoues
								// via m_avatarPendingClipRole (sans changer d'etat). Garde-fou 3s.
								const float enterDur = castClip ? castClip->duration : 0.0f;
								const float shootDur = castShootClip ? castShootClip->duration : 0.0f;
								const float exitDur  = castExitClip ? castExitClip->duration : 0.0f;
								if (m_castPhase == 0 && stateElapsed >= enterDur)
								{
									m_avatarPendingClipRole = "CastShoot";
									m_castPhase = 1;
								}
								else if (m_castPhase == 1 && stateElapsed >= enterDur + shootDur)
								{
									m_avatarPendingClipRole = "CastExit";
									m_castPhase = 2;
								}
								if (stateElapsed >= enterDur + shootDur + exitDur || stateElapsed >= 3.0f)
								{
									if (!moving)                  newState = AvatarLocomotionState::Idle;
									else if (movingBack)          newState = AvatarLocomotionState::WalkBack;
									else if (moveInput.sprint)    newState = AvatarLocomotionState::Sprint;
									else if (moveInput.run)       newState = AvatarLocomotionState::Run;
									else                          newState = AvatarLocomotionState::Walk;
								}
								break;
							}
							case AvatarLocomotionState::Interact:
								// One-shot : retour locomotion quand le geste d'interaction est fini.
								// Action non-combat (la touche E reservee au menu Options trouve ici son usage).
								if (!interactClip || stateElapsed >= interactClip->duration)
								{
									if (!moving)                  newState = AvatarLocomotionState::Idle;
									else if (movingBack)          newState = AvatarLocomotionState::WalkBack;
									else if (moveInput.sprint)    newState = AvatarLocomotionState::Sprint;
									else if (moveInput.run)       newState = AvatarLocomotionState::Run;
									else                          newState = AvatarLocomotionState::Walk;
								}
								break;
							case AvatarLocomotionState::Punch:
								// One-shot : retour locomotion quand le coup de poing est fini.
								if (!punchClip || stateElapsed >= punchClip->duration)
								{
									if (!moving)                  newState = AvatarLocomotionState::Idle;
									else if (movingBack)          newState = AvatarLocomotionState::WalkBack;
									else if (moveInput.sprint)    newState = AvatarLocomotionState::Sprint;
									else if (moveInput.run)       newState = AvatarLocomotionState::Run;
									else                          newState = AvatarLocomotionState::Walk;
								}
								break;
						}

						// Une action "one-shot" en cours (roulade ou geste combat/interaction)
						// bloque le declenchement d'une autre. Centralise ici pour eviter une
						// longue liste d'exclusions par override et faciliter l'ajout d'actions.
						auto busyOneShot = [&]()
						{
							return m_avatarLocoState == AvatarLocomotionState::Roll
								|| m_avatarLocoState == AvatarLocomotionState::Attack
								|| m_avatarLocoState == AvatarLocomotionState::Cast
								|| m_avatarLocoState == AvatarLocomotionState::Interact
								|| m_avatarLocoState == AvatarLocomotionState::Punch;
						};

						// Crouch (Ctrl) : etat accroupi prioritaire tant que la touche est tenue
						// (sauf amorce de saut, emote, ou action one-shot en cours). CrouchWalk si deplacement.
						if (moveInput.crouch && newState != AvatarLocomotionState::Jump
							&& newState != AvatarLocomotionState::Roll
							&& newState != AvatarLocomotionState::Emote
							&& newState != AvatarLocomotionState::Attack
							&& newState != AvatarLocomotionState::Cast
							&& newState != AvatarLocomotionState::Interact
							&& newState != AvatarLocomotionState::Punch)
							newState = moving ? AvatarLocomotionState::CrouchWalk : AvatarLocomotionState::CrouchIdle;

						// Esquive/roulade (double-appui Crouch) : Roll one-shot, prioritaire sur crouch.
						// Bloquée pendant le verrou de geste (ouverture de coffre).
						if (dodgePressed && m_avatarLocoState != AvatarLocomotionState::Roll
							&& nowSec >= m_avatarMoveLockUntilSec)
						{
							newState = AvatarLocomotionState::Roll;
							// Impulsion d'esquive : direction = mouvement si actif, sinon l'avant camera.
							// Le CharacterController force la vitesse horizontale pendant dodgeDurationSec.
							engine::math::Vec3 dodgeDir = moveInput.moveDirXZ;
							if (dodgeDir.x * dodgeDir.x + dodgeDir.z * dodgeDir.z < 1e-6f)
								dodgeDir = m_orbitalCameraController.GetForwardXZ();
							m_characterController.ApplyDodgeImpulse(dodgeDir);
						}

						// Emote (slash) : uniquement a l'arret (immobile, sans saut ni roll en cours).
						if (!emoteRole.empty() && !moving && !movingBack && !moveInput.jumpPressed
							&& m_avatarLocoState != AvatarLocomotionState::Roll)
						{
							m_currentEmoteRole = emoteRole;
							newState = AvatarLocomotionState::Emote;
						}

						// Attaque melee (clic gauche) : Sword_Attack one-shot. Pas pendant un saut
						// ni une autre action one-shot ; prioritaire sur le crouch.
						if (attackPressed && !moveInput.jumpPressed && !busyOneShot())
							newState = AvatarLocomotionState::Attack;

						// Coup de poing (touche C par defaut) : Punch_Jab one-shot, 2e attaque melee.
						if (punchPressed && !moveInput.jumpPressed && !busyOneShot())
						{
							// Alterne Jab <-> Cross a chaque coup (variete ; clip dynamique).
							m_currentPunchRole = m_punchAlt ? "PunchCross" : "Punch";
							m_punchAlt = !m_punchAlt;
							newState = AvatarLocomotionState::Punch;
						}

						// Sort (touche R par defaut) : Spell_Simple_Shoot one-shot.
						if (castPressed && !moveInput.jumpPressed && !busyOneShot())
							newState = AvatarLocomotionState::Cast;

						// Interagir (touche E par defaut) : geste Interact one-shot, action non-combat.
						// Près d'un coffre : joue "PickUp_Table" (se pencher → saisir → se redresser)
						// et verrouille le déplacement le temps du clip. Ailleurs : geste "Interact"
						// générique, sans verrou (comportement historique inchangé).
						if (interactPressed && !moveInput.jumpPressed && !busyOneShot())
						{
							const bool nearChest =
								m_chestLoaded && m_interactableInRange == m_chestInteractableIndex;
							if (nearChest)
							{
								m_currentInteractRole = "PickUp_Table";
								const engine::render::skinned::AnimationClip* puClip =
									m_currentSkinnedMesh->FindClip("PickUp_Table");
								// Clip absent (race non-UE5 / fallback) -> durée 0 -> verrou expire
								// immédiatement, pas de gel permanent ; le repli sur "Interact"
								// est géré juste en dessous.
								m_avatarMoveLockUntilSec = nowSec + (puClip ? puClip->duration : 0.0f);
								if (!puClip)
									m_currentInteractRole = "Interact";
							}
							else
							{
								m_currentInteractRole = "Interact";
							}
							newState = AvatarLocomotionState::Interact;
						}
					}
					else
					{
						// Not grounded : Jump (takeoff phase) -> Fall after takeoff duration ;
						// sinon (Walk/Run/Land/Idle qui perdent le sol) -> Fall direct.
						if (m_avatarLocoState == AvatarLocomotionState::Jump)
						{
							if (jumpClip && stateElapsed >= jumpClip->duration * 0.4f)
								newState = AvatarLocomotionState::Fall;
						}
						else if (m_avatarLocoState != AvatarLocomotionState::Fall)
						{
							newState = AvatarLocomotionState::Fall;
						}
					}

					// Nage : si le controller est en mode eau (immersion > bassin), l'avatar
					// nage -> surclasse locomotion / saut / actions one-shot ci-dessus. A la
					// sortie d'eau, on quitte explicitement Swim* (le switch n'a pas de case
					// Swim* -> sinon l'avatar resterait fige en nage au sol).
					if (m_characterController.IsInWater())
					{
						newState = (moving || movingBack) ? AvatarLocomotionState::SwimForward : AvatarLocomotionState::SwimIdle;
					}
					else if (m_avatarLocoState == AvatarLocomotionState::SwimIdle
						|| m_avatarLocoState == AvatarLocomotionState::SwimForward)
					{
						if (!grounded)        newState = AvatarLocomotionState::Fall;
						else if (movingBack)  newState = AvatarLocomotionState::WalkBack;
						else if (moving)      newState = moveInput.sprint ? AvatarLocomotionState::Sprint : (moveInput.run ? AvatarLocomotionState::Run : AvatarLocomotionState::Walk);
						else                  newState = AvatarLocomotionState::Idle;
					}

					// Validation v12 — avatar MORT : il restait DEBOUT (la SM n'a pas
					// d'état Dead). On joue le clip « Death » du mesh s'il existe
					// (une fois, clampé sur la dernière frame), et on GÈLE la machine
					// d'états tant que le joueur est mort (sinon une transition vers
					// Idle écraserait la pose). Au respawn : retour Idle immédiat.
					const bool avatarDead =
						(m_uiModelBinding.GetModel().playerStats.stateFlags & 1u) != 0u;
					if (avatarDead && !m_avatarDeathClipPlaying)
					{
						// Validation v12 — recherche TOLÉRANTE du clip de mort : le mesh
						// humain a 63 clips mais aucun nommé exactement Death/Die/Dead
						// (constat log client). On cherche par sous-chaîne insensible à
						// la casse ; sans candidat, on liste les clips disponibles UNE
						// fois pour identifier l'asset à compléter.
						const engine::render::skinned::AnimationClip* deathClip = nullptr;
						for (const engine::render::skinned::AnimationClip& candidateClip : m_currentSkinnedMesh->clips)
						{
							std::string lowered = candidateClip.name;
							std::transform(lowered.begin(), lowered.end(), lowered.begin(),
								[](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
							if (lowered.find("death") != std::string::npos
								|| lowered.find("die") != std::string::npos
								|| lowered.find("dead") != std::string::npos
								|| lowered.find("knock") != std::string::npos)
							{
								deathClip = &candidateClip;
								break;
							}
						}
						if (deathClip != nullptr)
						{
							m_avatarCrossfade.Play(*deathClip, /*loops=*/false, nowSec);
							LOG_INFO(Render, "[Avatar SM] Mort : clip '{}' joue (clamp)", deathClip->name);
						}
						else
						{
							std::string clipList;
							for (const engine::render::skinned::AnimationClip& availableClip : m_currentSkinnedMesh->clips)
							{
								if (!clipList.empty()) clipList += ", ";
								clipList += availableClip.name;
							}
							LOG_WARN(Render,
								"[Avatar SM] Mort : aucun clip de mort dans le mesh — pose Idle conservee. Clips disponibles : {}",
								clipList);
						}
						m_avatarDeathClipPlaying = true;
					}
					else if (!avatarDead && m_avatarDeathClipPlaying)
					{
						m_avatarDeathClipPlaying = false;
						if (const engine::render::skinned::AnimationClip* idleClip = m_currentSkinnedMesh->FindClip("Idle"))
						{
							m_avatarCrossfade.Play(*idleClip, /*loops=*/true, nowSec);
						}
						m_avatarLocoState = AvatarLocomotionState::Idle;
					}

					if (!avatarDead && newState != m_avatarLocoState)
					{
						// DEBUG B.1 : log chaque transition d'etat pour diagnostiquer
						// "modèle qui saute toujours". Une fois le bug compris, retirer.
						LOG_INFO(Render, "[Avatar SM] {} -> {} (grounded={} moving={} jumpPressed={} run={} stateElapsed={:.3f}s)",
							StateToClipName(m_avatarLocoState),
							StateToClipName(newState),
							grounded ? 1 : 0,
							moving ? 1 : 0,
							moveInput.jumpPressed ? 1 : 0,
							moveInput.run ? 1 : 0,
							stateElapsed);

						m_avatarLocoState = newState;
						m_avatarLocoStateEnterTime = now;
						if (newState == AvatarLocomotionState::Cast) m_castPhase = 0;

						// Trigger crossfade vers le clip du nouvel etat. Si le clip est
						// introuvable (asset manquant -> FindClip == nullptr), on log un
						// warn une seule fois et on laisse l'animation precedente continuer
						// (Sample retombera dessus jusqu'a la prochaine transition reussie).
						const char* clipName =
							(newState == AvatarLocomotionState::Emote && !m_currentEmoteRole.empty()) ? m_currentEmoteRole.c_str()
							: (newState == AvatarLocomotionState::Punch) ? m_currentPunchRole.c_str()
							: (newState == AvatarLocomotionState::Interact) ? m_currentInteractRole.c_str()
							: StateToClipName(newState);
						const engine::render::skinned::AnimationClip* newClip = m_currentSkinnedMesh->FindClip(clipName);
						if (newClip)
						{
							LOG_INFO(Render, "[Avatar SM] Play('{}') duration={:.3f}s loops={}",
								clipName, newClip->duration, ClipLoops(newState) ? 1 : 0);
							m_avatarCrossfade.Play(*newClip, ClipLoops(newState), nowSec);
						}
						else
						{
							LOG_WARN(Render, "[Avatar SM] FindClip('{}') returned nullptr — animation precedente continue", clipName);
						}
					}
					// Replay d'un clip en cours d'etat (sequence de sort) sans transition d'etat.
					if (!m_avatarPendingClipRole.empty())
					{
						const engine::render::skinned::AnimationClip* rc = m_currentSkinnedMesh->FindClip(m_avatarPendingClipRole.c_str());
						if (rc)
							m_avatarCrossfade.Play(*rc, /*loops=*/false, nowSec);
						m_avatarPendingClipRole.clear();
					}

					// Interaction (touche E) : interactible le plus proche (distance XZ) a portee.
					// Hint chat a l'entree de portee ; sur E -> effet (objet) ou dialogue (PNJ).
					// Le geste Interact joue par ailleurs (SM). v1 sans rendu des cibles.
					{
						const engine::math::Vec3 ppos = m_characterController.GetPosition();
						int nearestI = -1;
						float bestD2 = 0.0f;
						for (std::size_t i = 0; i < m_interactables.size(); ++i)
						{
							const InteractableEntity& e = m_interactables[i];
							const float dx = e.position.x - ppos.x;
							const float dz = e.position.z - ppos.z;
							const float d2 = dx * dx + dz * dz;
							if (d2 <= e.radius * e.radius && (nearestI < 0 || d2 < bestD2))
							{
								nearestI = static_cast<int>(i);
								bestD2 = d2;
							}
						}
						auto pushInteractChat = [&](const std::string& sender, const std::string& text)
						{
							engine::net::ChatMessage cm;
							cm.timestampUnixMs = static_cast<uint64_t>(
								std::chrono::duration_cast<std::chrono::milliseconds>(
									std::chrono::system_clock::now().time_since_epoch()).count());
							cm.channel = engine::net::ChatChannel::Server;
							cm.sender = sender;
							cm.text = text;
							m_chatUi.PushNetworkLine(cm);
						};
						if (nearestI != m_interactableInRange)
						{
							m_interactableInRange = nearestI;
							if (nearestI >= 0)
								pushInteractChat("[Interaction]", "Pres de " + m_interactables[nearestI].label + " - appuyez sur E.");
						}
						// Refermeture automatique du coffre 2 s apres la derniere ouverture
						// (verifie chaque frame, independamment de l'appui sur E).
						if (m_chestLoaded && m_chestOpen && EngineNowSec() >= m_chestAutoCloseAtSec)
						{
							m_chestOpen = false;
							if (const auto* clip = m_chestMesh.FindClip("Chest_Close"))
								m_chestCrossfade.Play(*clip, /*loops*/ false, EngineNowSec());
							pushInteractChat("[Objet]", "Le coffre se referme.");
						}
						if (interactPressed && nearestI >= 0)
						{
							InteractableEntity& e = m_interactables[nearestI];
							if (m_chestLoaded && e.meshPath.find("Chest_Wood") != std::string::npos)
							{
								// E ouvre le coffre (s'il ne l'est pas deja) puis (re)arme une
								// refermeture automatique 2 s plus tard (cf. check par frame).
								if (!m_chestOpen)
								{
									m_chestOpen = true;
									if (const auto* clip = m_chestMesh.FindClip("Chest_Open"))
										m_chestCrossfade.Play(*clip, /*loops*/ false, EngineNowSec());
									pushInteractChat("[Objet]", "Vous ouvrez le coffre.");
								}
								m_chestAutoCloseAtSec = EngineNowSec() + 2.0f;
							}
							else if (e.isNpc && !e.dialogueTree.nodes.empty())
							{
								// Cellule de dialogue PNJ : ouvre la fenêtre centrale dédiée
								// (plus de poussée dans le chat). Ignoré si un dialogue est
								// déjà actif (E ne ré-ouvre pas par-dessus).
								if (!m_dialogueActive)
								{
									engine::client::DialogueNpcRef npc;
									npc.label       = e.label;
									npc.role        = e.role;
									npc.entityIndex = nearestI; // index de l'interactable courant
									m_dialogue.OpenDialogue(e.dialogueTree, npc);
									m_dialogueActive = true;
									// SP2 (Task 4b) — mémorise le PNJ courant pour l'accept/turn-in
									// (Task 4) ET envoie un Talk au shard : OpenDialogue est purement
									// local (pas de round-trip serveur), donc sans ce Talk,
									// UIModel.giverList.npcTargetId reste vide/périmé (dernier PNJ
									// parlé, pas forcément celui-ci) et le shard rejetterait l'accept/
									// turn-in. Ignoré si le PNJ n'a pas d'id réseau ou si le réseau
									// gameplay n'est pas encore prêt (dialogue reste utilisable en
									// mode solo/hors-ligne, juste sans wire quête).
									m_currentDialogueNpcTargetId = e.npcTargetId;
									if (!e.npcTargetId.empty() && m_gameplayNetInitialized)
									{
										(void)m_gameplayUdp.SendTalkRequest(m_gameplayUdp.ServerClientId(), e.npcTargetId);
									}
								}
							}
							else
							{
								pushInteractChat(e.isNpc ? "[PNJ]" : "[Objet]", e.message);
							}
						}

						// Cellule de dialogue PNJ : tick par frame (auto-scroll + rupture de
						// distance à 1,50 m) et fermeture sur Échap. La distance est recalculée
						// vers le PNJ courant (entityIndex mémorisé à l'ouverture).
						if (m_dialogueActive)
						{
							float dist = 999.0f;
							const int idx = m_dialogue.Npc().entityIndex;
							if (idx >= 0 && idx < static_cast<int>(m_interactables.size()))
							{
								const engine::math::Vec3 pp = m_characterController.GetPosition();
								const engine::math::Vec3 np = m_interactables[idx].position;
								const float dx = np.x - pp.x;
								const float dz = np.z - pp.z;
								dist = std::sqrt(dx * dx + dz * dz);
							}
							m_dialogue.Tick(static_cast<float>(dt), dist);

							// Échap ferme le dialogue (edge-triggered).
							if (m_input.WasPressed(engine::platform::Key::Escape))
								m_dialogue.Close(engine::client::DialogueCloseReason::UserClose);

							// Synchronise le flag : toute fermeture (distance, Échap, choix End)
							// libère le déplacement.
							if (!m_dialogue.IsActive())
							{
								m_dialogueActive = false;
								// SP2 (Task 4b) — le PNJ mémorisé n'est plus valide une fois le
								// dialogue fermé (évite qu'un futur accept/turn-in hors dialogue
								// réutilise par erreur la dernière cible).
								m_currentDialogueNpcTargetId.clear();
								// Le panneau « Quêtes du PNJ » (giver-list, rempli au Talk) ne doit
								// vivre que le temps de la conversation : on le vide à la fermeture,
								// sinon il resterait affiché en permanence après avoir parlé une fois.
								m_uiModelBinding.ClearGiverList();
							}
						}
					}
				}

				// --- Zoom radar minimap (molette + clic sur l'arc). Si le curseur
				// survole le radar (disque + anneau des repères), la molette pilote le
				// zoom du radar (5 crans 200..1000 m) au lieu du zoom caméra, et le clic
				// sur un repère saute à ce cran. Géométrie via ComputeRadarScreenRect
				// (m_width/m_height = taille framebuffer = io.DisplaySize du radar, donc
				// même géométrie qu'au rendu). Cran persisté dans minimap_settings.json.
				bool mouseOverRadar = false;
				{
					const engine::client::RadarScreenRect radarRect =
						engine::client::ComputeRadarScreenRect(m_cfg,
							static_cast<float>(m_width), static_cast<float>(m_height));
					if (radarRect.enabled)
					{
						const float rcx = radarRect.x0 + radarRect.size * 0.5f;
						const float rcy = radarRect.y0 + radarRect.size * 0.5f;
						// Zone d'interaction = disque + marge couvrant l'anneau des repères
						// (tracés à R+6). Sans cette marge, un clic sur un repère (hors du
						// disque) ne serait jamais détecté.
						const float interactR = radarRect.size * 0.5f + 16.0f;
						const float mdx = static_cast<float>(m_input.MouseX()) - rcx;
						const float mdy = static_cast<float>(m_input.MouseY()) - rcy;
						mouseOverRadar = (mdx * mdx + mdy * mdy) <= (interactR * interactR);
						if (mouseOverRadar)
						{
							int newZoom = m_minimapZoomIndex;
							const int wheel = m_input.MouseScrollDelta();
							if (wheel != 0)
								newZoom = engine::client::StepZoomIndex(newZoom, wheel);
							if (m_input.WasMousePressed(engine::platform::MouseButton::Left))
							{
								constexpr float kTickHitRadiusPx = 12.0f;
								for (int t = 0; t < engine::client::kMinimapZoomLevelCount; ++t)
								{
									const engine::client::ScreenPoint tp =
										engine::client::RadarZoomTickPos(radarRect, t);
									const float tdx = static_cast<float>(m_input.MouseX()) - tp.x;
									const float tdy = static_cast<float>(m_input.MouseY()) - tp.y;
									if ((tdx * tdx + tdy * tdy) <= (kTickHitRadiusPx * kTickHitRadiusPx))
									{
										newZoom = t;
										break;
									}
								}
							}
							if (newZoom != m_minimapZoomIndex)
							{
								m_minimapZoomIndex = newZoom;
								m_questUi.SetMinimapRadius(engine::client::RadiusForZoomIndex(newZoom));
								engine::core::Config zoomCfg;
								zoomCfg.SetValue("client.quest.minimap.zoom_level", static_cast<int64_t>(newZoom));
								if (!zoomCfg.SaveToFile("minimap_settings.json"))
									LOG_WARN(Core, "[Minimap] echec persistance zoom (minimap_settings.json)");
								else
									LOG_INFO(Core, "[Minimap] zoom radar -> cran {} ({} m)",
										newZoom, engine::client::RadiusForZoomIndex(newZoom));
							}
						}
					}
				}

				const bool rmbLook = m_input.IsMouseDown(engine::platform::MouseButton::Right);
				// applyZoom coupé aussi quand ImGui capte la souris : sinon la molette
				// utilisée pour défiler dans une fenêtre (ex. onglet Arbre de la fenêtre
				// Personnage) zoomait AUSSI la caméra du jeu (retour joueur 2026-07-09).
				// Cette section n'est PAS sous #if defined(_WIN32) et ImGui n'est inclus
				// que sur Windows -> on garde la lecture WantCaptureMouse.
				bool imguiWantsMouse = false;
#if defined(_WIN32)
				imguiWantsMouse = ImGui::GetIO().WantCaptureMouse;
#endif
				m_orbitalCameraController.Update(m_input, dt, mouseSensitivity, invertY, rmbLook,
					!mouseOverRadar && !imguiWantsMouse, out.camera);

				// --- Clamp anti-sous-sol : la caméra ne descend pas sous le terrain. ---
				const bool occEnabled = m_cfg.GetBool("client.camera.occlusion_fade.enabled", true);
				const float clampMargin = static_cast<float>(
					m_cfg.GetDouble("client.camera.terrain_clamp_margin", 0.5));
				const float groundY = m_terrain.SampleHeightAtWorldXZ(
					out.camera.position.x, out.camera.position.z);
				const float minCamY = groundY + clampMargin;
				if (out.camera.position.y < minCamY)
					out.camera.position.y = minCamY;

				// --- Fondu d'occlusion : collecte des occulteurs (props + bâtiments). ---
				// L'index `i` ci-dessous est l'identifiant d'occulteur ; il DOIT
				// correspondre à l'index utilisé pour FadeFor dans RecordPropsGeometry
				// (même conteneur m_props, même ordre dans la même frame).
				if (occEnabled)
				{
					std::vector<engine::render::OccluderSphere> occluders;
					occluders.reserve(m_props.size());
					for (std::size_t i = 0; i < m_props.size(); ++i)
					{
						const auto& prop = m_props[i];
						if (prop.occluderRadius <= 0.0f) continue;
						occluders.push_back({ static_cast<std::uint32_t>(i),
							prop.occluderCenter, prop.occluderRadius });
					}
					const engine::math::Vec3 focus = m_orbitalCameraController.GetTargetPosition();
					m_cameraOcclusionFade.Update(out.camera.position, focus,
						occluders, static_cast<float>(dt));
				}
			}

			// Chat update : uniquement post-EnterWorld. Le rendu pre-game est desactive
			// (cf. chatImguiOverlayNewFrame=false plus haut), donc pas besoin d'Update
			// le presenter avant. Si on reactive le chat pre-game un jour, etendre cette
			// gate avec un OR sur IsMasterAuthenticated().
			if (!authGateActive && m_chatUi.IsInitialized())
			{
				// Phase 3.11.3 — Indique au presenter si un ImGui::InputText pilote la saisie
				// (panneau chat ImGui actif). Coupe la branche keyboard-typing/Enter dans Update
				// pour éviter une double insertion ; Escape et scroll restent actifs.
#if defined(_WIN32)
				const bool chatImguiInputActive = m_chatImGui && !m_worldEditorExe
					&& m_cfg.GetBool("render.chat_imgui.enabled", true);
				m_chatUi.SetImGuiInputActive(chatImguiInputActive);
#else
				m_chatUi.SetImGuiInputActive(false);
#endif
				m_chatUi.Update(m_input, static_cast<float>(dt));
			}
		}
		else if (m_worldEditorExe && m_worldEditorImGui && m_worldEditorImGui->IsReady())
		{
			// Ne pas lier souris et clavier : un InputText actif mettait WantCaptureKeyboard à true et bloquait
			// aussi la souris (orientation), ce qui figeait la caméra dans l’éditeur.
			[[maybe_unused]] const bool capMouse = m_worldEditorImGui->WantsCaptureMouse();
			const bool capKb = m_worldEditorImGui->WantsCaptureKeyboard();
			// Convention UX standard (Unity/Unreal/Blender) : la rotation de la
			// caméra free-fly de l'éditeur n'est active QUE pendant que le clic
			// droit est maintenu. Sinon le moindre déplacement de la souris fait
			// dériver la caméra et l'utilisateur perd le terrain. Avant ce fix,
			// `applyLook = !capMouse || rmbLook` faisait tourner la caméra dès
			// que la souris survolait la zone 3D — UX cassée.
			const bool rmbLook = m_input.IsMouseDown(engine::platform::MouseButton::Right);
			const bool applyLook = rmbLook;
			const bool applyKb = !capKb;
			float terrainWorldM = 0.f;
			if (m_terrain.IsValid())
			{
				terrainWorldM = m_terrain.GetTerrainWorldSize();
			}
			const float editorSpeedMult = static_cast<float>(
				m_cfg.GetDouble("controls.editor_camera_speed_multiplier", 1.0));
			m_fpsCameraController.Update(m_input, dt, mouseSensitivity, invertY, movementLayout, true, applyLook, applyKb,
				terrainWorldM, out.camera, editorSpeedMult);
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
		// Réticule de ciblage : suit la cible (position/yaw lissés) et pilote le
		// decal persistant (fade in/out). APRÈS UpdateGameplayNet (m_remoteSmoothed
		// et UIModel à jour), AVANT l'enregistrement de la frame (BuildVisibleList).
		if (m_gameplayNetInitialized)
		{
			m_targetReticle.Update(m_uiModelBinding.GetModel(), out.camera, static_cast<float>(dt));
		}

		out.viewMatrix = out.camera.ComputeViewMatrix();
		{
			// La direction gaze est stockee en row 2 du view matrix corrige :
			// (m[2], m[6], m[10]) = (forward.x, forward.y, forward.z). Cf.
			// commentaire dans Camera::ComputeViewMatrix.
			const engine::math::Vec3 forward(out.viewMatrix.m[2], out.viewMatrix.m[6], out.viewMatrix.m[10]);
			m_streamingScheduler.PushRequests(m_world.GetPendingChunkRequests(), out.camera.position, forward);
			m_streamingScheduler.DropStaleFromAllQueues();
		}

		if (m_width > 0 && m_height > 0 && std::abs(out.camera.fovYDeg - readState.camera.fovYDeg) > 0.0001f)
			m_taaHistoryInvalid = true;

		out.projMatrix = out.camera.ComputeProjectionMatrix();

		// ViewProj non-jitterée (avant l'ajout du jitter TAA) : sert au raymarch des
		// nuages pour qu'ils soient stables temporellement (pas de tremblement
		// sous-pixel -> pas de scintillement via le TAA).
		out.viewProjMatrixUnjittered = out.projMatrix * out.viewMatrix;

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
			// Demande utilisateur : afficher un humanoide de reference au centre du
			// terrain pour juger des reliefs. On override la matrice pour positionner
			// l'avatar (1.8m) au centre XZ du terrain courant a la hauteur du sol
			// echantillonnee. Si pas de terrain, fallback (0, 0, 0).
			out.objectVisible = true;
			float refX = 0.0f, refZ = 0.0f, refY = 0.0f;
			if (m_terrain.IsValid())
			{
				const float ox = m_terrain.GetTerrainOriginX();
				const float oz = m_terrain.GetTerrainOriginZ();
				const float ws = m_terrain.GetTerrainWorldSize();
				refX = ox + ws * 0.5f;
				refZ = oz + ws * 0.5f;
				refY = m_terrain.SampleHeightAtWorldXZ(refX, refZ);
			}
			// Matrice column-major : identite (rotation 0) + translation (refX, refY, refZ).
			// Avatar mesh y va de 0 (pieds) a 1.8 (tete) -> pieds au sol echantillonne.
			out.objectModelMatrix[0]  = 1.0f; out.objectModelMatrix[1]  = 0.0f; out.objectModelMatrix[2]  = 0.0f; out.objectModelMatrix[3]  = 0.0f;
			out.objectModelMatrix[4]  = 0.0f; out.objectModelMatrix[5]  = 1.0f; out.objectModelMatrix[6]  = 0.0f; out.objectModelMatrix[7]  = 0.0f;
			out.objectModelMatrix[8]  = 0.0f; out.objectModelMatrix[9]  = 0.0f; out.objectModelMatrix[10] = 1.0f; out.objectModelMatrix[11] = 0.0f;
			out.objectModelMatrix[12] = refX; out.objectModelMatrix[13] = refY; out.objectModelMatrix[14] = refZ; out.objectModelMatrix[15] = 1.0f;
		}
		else
		{
			out.objectVisible = true;
			// Etape 2 + 4 vue 3eme personne : position + orientation de l'avatar.
			//
			// Position : translation a la cible orbitale (= position joueur).
			// Le mesh 'avatar_placeholder.mesh' (chantier 3) est un humanoide
			// composite : torse + tete + 2 bras + 2 jambes (6 boites soudees,
			// 144 verts, 216 indices). Pieds a mesh-Y=0, sommet du crane a
			// mesh-Y=1.8 ; translation(target) sans offset.
			//
			// Orientation : rotation Y selon le yaw camera (etape 4). Standard MMO :
			// le perso fait face dans la meme direction horizontale que la camera,
			// ce qui revient a montrer son dos a la camera (3eme personne classique).
			// Le cube placeholder etant symetrique, ca n'a pas d'effet visuel pour
			// le moment ; le cablage est en place pour les futurs meshes textures.
			//
			// Matrice row-major M = T(target) * R_y(yaw) :
			//   | cos(y)   0  sin(y)  tx |
			//   | 0        1  0       ty |
			//   | -sin(y)  0  cos(y)  tz |
			//   | 0        0  0       1  |
			// On rend l'avatar systematiquement (meme si character_id == 0u, ex.
			// scenarios de test sans EnterWorld complet) pour que la 3eme personne
			// soit visible des l'entree dans la scene 3D.
			{
				const engine::math::Vec3& target = m_orbitalCameraController.GetTargetPosition();
				const float yaw = out.camera.yaw;
				const float c = std::cos(yaw);
				const float s = std::sin(yaw);
				// B.1 / Task 8 : le bob synthetique (placeholder anim) est supprime
				// avec la retrogradation d'OrbitalCameraController en camera pure.
				// L'animation visuelle (walk-bob, vraies anims squelettiques) sera
				// rebranchee plus tard depuis la state machine de locomotion.
				//
				// Plus de kTargetEyeHeight non plus : la cible orbitale n'est plus
				// systematiquement "yeux a 1.7 m du sol". CharacterController
				// (Task 9) decidera ou poser la cible (yeux, hanche, ou pieds) et
				// poussera la position correspondante via SetTargetPosition. Pour
				// l'instant target reste a (0,0,0) -> avatar pose les pieds en 0.
				const float feetY = target.y;
				// IMPORTANT : layout COLUMN-MAJOR. Le shader gbuffer_geometry.vert
				// reconstruit la mat4 via 4 vec4 (instanceRow0..3), chacun lu en sequence
				// dans le buffer d'instance ; GLSL mat4(c0,c1,c2,c3) place chaque vec4
				// comme COLONNE. Donc nos indices [0..3] = colonne 0, [4..7] = colonne 1,
				// etc. La translation va dans la 4eme COLONNE (indices 12, 13, 14).
				// Avant ce fix, le code etait ecrit en row-major, ce qui mettait la
				// translation dans la composante w des positions monde -> avatar
				// invisible (worldPos.w != 1, et translation nulle dans xyz).
				//
				// Matrice mathematique (colonne-major M = T * R_y(yaw)) :
				//   | cos(y)   0   sin(y)    tx |
				//   | 0        1   0         ty |
				//   | -sin(y)  0   cos(y)    tz |
				//   | 0        0   0         1  |
				// Stockage colonne-par-colonne :
				// Avatar a echelle 1:1 (1.8 m). Le scale x3 introduit pour "garantir
				// la visibilite" placait la camera A L'INTERIEUR du corps de l'avatar
				// (5.4 m de haut, camera a 1.81 m au-dessus du sol = inside the model)
				// -> ecran fige perceptuel. La camera 3eme personne (dist=8m, height=1.5m)
				// est largement assez genereuse a echelle reelle.
				// Avatar a echelle reelle 1.8m (pas de scale). Cible image 2 utilisateur :
				// avec camera a 5m de distance (cf. Camera.h kDistanceDefault), l'avatar
				// occupe 26% de hauteur ecran -> visible et identifiable comme humanoide.
				out.objectModelMatrix[0]  = c;     out.objectModelMatrix[1]  = 0.0f; out.objectModelMatrix[2]  = -s;   out.objectModelMatrix[3]  = 0.0f; // col0 : axe X local
				out.objectModelMatrix[4]  = 0.0f;  out.objectModelMatrix[5]  = 1.0f; out.objectModelMatrix[6]  = 0.0f; out.objectModelMatrix[7]  = 0.0f; // col1 : axe Y local
				out.objectModelMatrix[8]  = s;     out.objectModelMatrix[9]  = 0.0f; out.objectModelMatrix[10] = c;    out.objectModelMatrix[11] = 0.0f; // col2 : axe Z local
				out.objectModelMatrix[12] = target.x; out.objectModelMatrix[13] = feetY; out.objectModelMatrix[14] = target.z; out.objectModelMatrix[15] = 1.0f; // col3 : translation
			}
		}

#if defined(_WIN32)
		if (m_worldEditorExe && m_worldEditorImGui && m_worldEditorImGui->IsReady())
		{
			float dw = static_cast<float>(std::max(1, m_width));
			float dh = static_cast<float>(std::max(1, m_height));
			int vw = std::max(1, m_width);
			int vh = std::max(1, m_height);
			if (m_vkSwapchain.IsValid())
			{
				const VkExtent2D extUi = m_vkSwapchain.GetExtent();
				if (extUi.width > 0 && extUi.height > 0)
				{
					dw = static_cast<float>(extUi.width);
					dh = static_cast<float>(extUi.height);
					vw = static_cast<int>(extUi.width);
					vh = static_cast<int>(extUi.height);
				}
			}

			engine::editor::WorldEditorViewportOverlayDesc overlay{};
			overlay.viewProjColMajor = out.viewProjMatrix.m;
			overlay.cameraWorldX = out.camera.position.x;
			overlay.cameraWorldY = out.camera.position.y;
			overlay.cameraWorldZ = out.camera.position.z;
			overlay.cameraYawDeg = out.camera.yaw * (180.0f / 3.14159265f);
			overlay.cameraPitchDeg = out.camera.pitch * (180.0f / 3.14159265f);
			overlay.viewportWidth = vw;
			overlay.viewportHeight = vh;

			bool terrainPick = false;
			float pickX = 0.f;
			float pickZ = 0.f;
			if (m_terrain.IsValid() && m_worldEditorSession)
			{
				const engine::render::terrain::HeightmapData& hm = m_terrain.GetMutableHeightmapData();
				if (hm.width > 0u && hm.height > 0u)
				{
					overlay.heightmap = &hm;
					overlay.terrainOriginX = m_terrain.GetTerrainOriginX();
					overlay.terrainOriginZ = m_terrain.GetTerrainOriginZ();
					overlay.terrainWorldSize = m_terrain.GetTerrainWorldSize();
					overlay.heightScale = m_terrain.GetHeightScale();
					overlay.showGrid = m_worldEditorSession->ShowGrid();
					overlay.gridCellMeters = m_worldEditorSession->GridCellMeters();
					overlay.brushRadiusMeters = m_worldEditorSession->BrushRadius();
					const bool capBeforeUi = m_worldEditorImGui->WantsCaptureMouse();
					terrainPick = RaycastTerrainFromCamera(out.camera, vw, vh, m_input.MouseX(), m_input.MouseY(), hm,
						overlay.terrainOriginX, overlay.terrainOriginZ, overlay.terrainWorldSize, overlay.heightScale,
						pickX, pickZ);
					overlay.showBrushPreview =
						terrainPick && !capBeforeUi && m_worldEditorSession->TerrainEditMode() != 3
						&& m_worldEditorSession->TerrainEditMode() != 4;
					if (terrainPick)
					{
						overlay.brushWorldX = pickX;
						overlay.brushWorldZ = pickZ;
					}
				}
				overlay.layoutInstancesOverlay = &m_worldEditorSession->MutableDoc().layoutInstances;
				overlay.selectedLayoutInstanceOverlay = m_worldEditorSession->SelectedLayoutInstanceIndex();
			}

			m_worldEditorImGui->BuildUi(&overlay);

			if (m_worldEditorExe && m_worldEditorSession && m_worldEditorTerrainTools.IsValid() && m_vkDeviceContext.IsValid()
				&& m_worldEditorSession->ConsumeRouteApplyDraftRequest())
			{
				engine::editor::WorldEditorSession& ws = *m_worldEditorSession;
				if (ws.RouteDraftPoints().size() < 2u)
				{
					ws.SetStatus("Routes: au moins 2 points dans le brouillon (clics gauche sur le sol).");
				}
				else
				{
					engine::editor::WorldMapRoutePolyline rp{};
					rp.pointsXz = ws.RouteDraftPoints();
					rp.widthM = static_cast<double>(ws.RouteStripWidthM());
					rp.splatLayer = static_cast<uint32_t>(std::clamp(ws.RouteSplatLayer(), 0, 3));
					engine::render::terrain::BrushParams bp{};
					bp.radius = ws.BrushRadius();
					bp.strength = ws.BrushStrength();
					bp.falloff = 1.f;
					bp.flattenTarget = 0.5f;
					if (m_worldEditorTerrainTools.PaintSplatAlongPolyline(rp.pointsXz, rp.widthM, rp.splatLayer, bp))
					{
						(void)m_worldEditorTerrainTools.FlushSplatMap(m_vkDeviceContext.GetDevice(),
							m_vkDeviceContext.GetPhysicalDevice(),
							m_vkDeviceContext.GetGraphicsQueue(),
							m_vkDeviceContext.GetGraphicsQueueFamilyIndex());
						ws.MutableDoc().routes.push_back(std::move(rp));
						ws.ClearRouteDraft();
						ws.SetStatus("Routes: bande splat appliquée — sauvegardez l’édition pour persister SLAP + JSON.");
					}
					else
					{
						ws.SetStatus("Routes: peinture splat impossible (vérifier le terrain / la splat).");
					}
				}
			}

			// Sous-projet 1, bloc G — pipeline d'edition unifie : quand un outil
			// MODERNE du Shell est actif (Sculpt ou Stamp), on le pilote depuis
			// l'input viewport (il edite les CHUNKS -> sync 1:1 vers le GPU via
			// A2/A3 + undo/redo via CommandStack) et on NEUTRALISE le pinceau
			// legacy (garde `!modernEditActive` sur le bloc legacy plus bas). Sans
			// outil moderne d'edition actif, le legacy reste le chemin par defaut.
			// Ctrl+clic gauche reste reserve au picking B3 (donc exclu ici).
			// Mode « édition bâtiment » : quand actif (case à cocher du Building
			// Editor), le clic gauche dans la vue NE modifie PAS le terrain (ni
			// outils modernes ni brush legacy) — il est réservé à la
			// sélection/édition des pièces de bâtiment. Évite de sculpter le
			// terrain par réflexe en voulant choisir une pièce.
			const bool buildingEditMode =
				(m_worldEditorShell && m_worldEditorShell->GetBuildingEditorPanel())
					? m_worldEditorShell->GetBuildingEditorPanel()->EditModeActive()
					: false;

			bool modernEditActive = false;
			if (m_worldEditorShell && m_worldEditorShell->IsInitialized())
			{
				const engine::editor::world::ActiveTool tool = m_worldEditorShell->GetActiveTool();
				const bool freeClick = !m_worldEditorImGui->WantsCaptureMouse()
					&& !m_input.IsDown(engine::platform::Key::Control)
					&& !buildingEditMode;
				if (tool == engine::editor::world::ActiveTool::TerrainSculpt)
				{
					modernEditActive = true;
					if (freeClick)
					{
						engine::editor::world::TerrainSculptTool& sculpt =
							m_worldEditorShell->MutableSculptTool();
						const int mx = m_input.MouseX();
						const int my = m_input.MouseY();
						if (m_input.WasMousePressed(engine::platform::MouseButton::Left))
							sculpt.OnMouseDown(out.camera, mx, my, vw, vh, m_cfg);
						else if (m_input.IsMouseDown(engine::platform::MouseButton::Left))
							sculpt.OnMouseMove(out.camera, mx, my, vw, vh, m_cfg);
						if (m_input.WasMouseReleased(engine::platform::MouseButton::Left))
							sculpt.OnMouseUp();
					}
				}
				else if (tool == engine::editor::world::ActiveTool::TerrainStamp)
				{
					modernEditActive = true;
					// Stamp one-shot : clic gauche au point de sol cliqué (pickX,pickZ)
					// -> calcule la preview puis l'applique (push command -> sync +
					// persistance + undo). terrainPick garantit un point valide.
					if (freeClick && terrainPick
						&& m_input.WasMousePressed(engine::platform::MouseButton::Left))
					{
						engine::editor::world::TerrainStampTool& stamp =
							m_worldEditorShell->MutableStampTool();
						stamp.OnClickAt(m_cfg, pickX, pickZ);
						stamp.Apply();
					}
				}
				else if (tool == engine::editor::world::ActiveTool::MountainRange)
				{
					modernEditActive = true;
					// Macro polyline : chaque clic gauche ajoute un sommet ; la
					// generation (rasterisation -> command -> sync validee) se fait
					// via le bouton "Apply" du ToolPropertiesPanel.
					if (freeClick && terrainPick
						&& m_input.WasMousePressed(engine::platform::MouseButton::Left))
					{
						m_worldEditorShell->MutableMountainRangeTool().AddVertex(pickX, pickZ);
					}
				}
				else if (tool == engine::editor::world::ActiveTool::ValleyChain)
				{
					modernEditActive = true;
					// Jumeau soustractif de MountainRange (vallees). Meme entree :
					// clic = ajout de sommet, "Apply" (panneau) genere.
					if (freeClick && terrainPick
						&& m_input.WasMousePressed(engine::platform::MouseButton::Left))
					{
						m_worldEditorShell->MutableValleyChainTool().AddVertex(pickX, pickZ);
					}
				}
				else if (tool == engine::editor::world::ActiveTool::Spline)
				{
					modernEditActive = true;
					// Roadmap-8 — spline (routes/chemins) : chaque clic ajoute un
					// noeud au trace en cours (largeur = defaut du panneau) ; le
					// bouton « Ajouter la spline » du ToolPropertiesPanel pousse
					// l'AddSplineCommand undoable.
					if (freeClick && terrainPick
						&& m_input.WasMousePressed(engine::platform::MouseButton::Left))
					{
						engine::editor::world::SplineTool& spline =
							m_worldEditorShell->MutableSplineTool();
						const float wy = m_terrainCollider.GroundHeightAt(pickX, pickZ);
						spline.AddNode(engine::math::Vec3{ pickX, wy, pickZ },
							spline.DefaultWidthMeters());
					}
				}
				else if (tool == engine::editor::world::ActiveTool::GameplayZone)
				{
					modernEditActive = true;
					// Roadmap-8 — zone de gameplay : chaque clic ajoute un sommet
					// au polygone en cours ; « Ajouter la zone » (panneau) pousse
					// l'AddGameplayZoneCommand undoable.
					if (freeClick && terrainPick
						&& m_input.WasMousePressed(engine::platform::MouseButton::Left))
					{
						const float wy = m_terrainCollider.GroundHeightAt(pickX, pickZ);
						m_worldEditorShell->MutableZoneTool().AddPoint(
							engine::math::Vec3{ pickX, wy, pickZ });
					}
				}
				else if (tool == engine::editor::world::ActiveTool::Hazard)
				{
					modernEditActive = true;
					// Roadmap-8 — danger : pose one-shot au point clique, undoable
					// immediatement (AddHazardCommand aux parametres courants du
					// panneau — type, forme, profondeur...).
					if (freeClick && terrainPick
						&& m_input.WasMousePressed(engine::platform::MouseButton::Left))
					{
						const float wy = m_terrainCollider.GroundHeightAt(pickX, pickZ);
						m_worldEditorShell->MutableCommandStack().Push(
							std::make_unique<engine::editor::world::AddHazardCommand>(
								m_worldEditorShell->MutableHazardDocument(),
								m_worldEditorShell->GetHazardTool().CreateAt(
									engine::math::Vec3{ pickX, wy, pickZ })));
					}
				}
			}

			// Gizmo étape 3.2 — cliquer-glisser : en mode « édition bâtiment », on
			// tente d'abord de saisir/glisser un handle du gizmo (axe = translation,
			// anneau = rotation) sur la pièce sélectionnée. S'il capture la souris,
			// on N'enchaîne PAS sur la sélection de pièce ci-dessous.
			bool gizmoGrabbed = false;
			if (buildingEditMode && !m_worldEditorImGui->WantsCaptureMouse()
				&& !m_input.IsDown(engine::platform::Key::Control))
			{
				gizmoGrabbed = UpdateEditorBuildingGizmoDrag(m_input.MouseX(), m_input.MouseY());
			}

			// Gizmo étape 3.1 — picking de pièce de bâtiment : en mode « édition
			// bâtiment », un clic gauche dans la vue (hors ImGui, sans Ctrl)
			// sélectionne la pièce du brouillon dont la position monde (XZ) est la
			// plus proche du point de sol cliqué. Réutilise la transform du groupe
			// mémorisée au dernier rendu (m_editorPreview*), pour que le picking
			// corresponde à ce qui est affiché. Remplace la saisie de valeurs « à
			// l'aveugle » : on clique la pièce, le gizmo s'y replace.
			if (!gizmoGrabbed && buildingEditMode && m_editorPreviewValid && terrainPick
				&& m_worldEditorShell && m_worldEditorShell->GetBuildingEditorPanel()
				&& !m_worldEditorImGui->WantsCaptureMouse()
				&& !m_input.IsDown(engine::platform::Key::Control)
				&& m_input.WasMousePressed(engine::platform::MouseButton::Left))
			{
				engine::editor::world::panels::BuildingEditorPanel* bpanel =
					m_worldEditorShell->GetBuildingEditorPanel();
				const auto& parts = bpanel->DraftParts();
				constexpr float kDeg2Rad = 3.14159265f / 180.f;
				const float c = std::cos(m_editorPreviewYaw * kDeg2Rad);
				const float s = std::sin(m_editorPreviewYaw * kDeg2Rad);
				int best = -1;
				float bestDist2 = 36.0f; // (6 m)^2 : seuil de tolérance autour de la pièce
				for (size_t i = 0; i < parts.size(); ++i)
				{
					const float sx = m_editorPreviewScale * parts[i].localPosition.x;
					const float sz = m_editorPreviewScale * parts[i].localPosition.z;
					const float wx = m_editorPreviewOriginX + (c * sx + s * sz);
					const float wz = m_editorPreviewOriginZ + (-s * sx + c * sz);
					const float dx = wx - pickX;
					const float dz = wz - pickZ;
					const float d2 = dx * dx + dz * dz;
					if (d2 < bestDist2) { bestDist2 = d2; best = static_cast<int>(i); }
				}
				if (best >= 0)
				{
					bpanel->SetSelectedDraft(best);
					LOG_INFO(EditorWorld, "[Buildings][editeur] piece selectionnee par clic: #{} (dist={:.1f} m)",
						best, std::sqrt(bestDist2));
				}
			}

			// Roadmap-6 (2026-07-19) — gizmo de scène : cliquer-glisser sur les
			// handles (axes / anneau yaw / poignée d'échelle) de la sélection.
			// Hors mode bâtiment (le gizmo bâtiment garde la main) et sans Ctrl
			// (Ctrl = picking d'entité ci-dessous). S'il capture la souris, le
			// clic ne doit alimenter NI le picking NI les outils terrain.
			bool sceneGizmoGrabbed = false;
			if (!buildingEditMode && m_worldEditorShell && m_worldEditorShell->IsInitialized()
				&& !m_worldEditorImGui->WantsCaptureMouse()
				&& !m_input.IsDown(engine::platform::Key::Control))
			{
				sceneGizmoGrabbed = UpdateEditorSceneGizmoDrag(m_input.MouseX(), m_input.MouseY());
			}

			// Sous-projet 1, bloc B3 — picking d'entite : Ctrl+clic gauche dans la
			// vue 3D (hors ImGui) selectionne l'entite dont la position (XZ) est la
			// plus proche du point de sol cliqué (seuil ~3 m), via EditorSceneModel
			// + EditorSelection. Geste Ctrl+clic distinct du clic d'edition
			// (sculpt/placement) pour ne pas interferer avec les deux systemes.
			// Roadmap-6 : Ctrl+Maj+clic BASCULE l'entité dans la multi-sélection ;
			// Ctrl+clic dans le vide vide la sélection.
			if (terrainPick && m_worldEditorShell && m_worldEditorShell->IsInitialized()
				&& !m_worldEditorImGui->WantsCaptureMouse()
				&& m_input.IsDown(engine::platform::Key::Control)
				&& m_input.WasMousePressed(engine::platform::MouseButton::Left))
			{
				const std::vector<engine::editor::scene::SceneEntity>& entities =
					m_worldEditorShell->GetSceneModel().Entities();
				const engine::editor::scene::SceneEntity* best = nullptr;
				float bestDist2 = 9.0f; // (3 m)^2
				for (const engine::editor::scene::SceneEntity& e : entities)
				{
					if (!e.hasTransform) continue;
					const float dx = e.transform.position.x - pickX;
					const float dz = e.transform.position.z - pickZ;
					const float d2 = dx * dx + dz * dz;
					if (d2 < bestDist2) { bestDist2 = d2; best = &e; }
				}
				const bool shiftHeld = m_input.IsDown(engine::platform::Key::Shift);
				if (best != nullptr)
				{
					if (shiftHeld)
						m_worldEditorShell->MutableSelection().Toggle(best->id);
					else
						m_worldEditorShell->MutableSelection().Select(best->id);
					LOG_INFO(EditorWorld, "[WorldEditor] Entite selectionnee (Ctrl+clic{}): {} ({} au total)",
						shiftHeld ? "+Maj" : "", best->label,
						m_worldEditorShell->GetSelection().Count());
				}
				else if (!shiftHeld)
				{
					m_worldEditorShell->MutableSelection().Clear();
				}
			}

			if (!modernEditActive && !buildingEditMode && !sceneGizmoGrabbed && m_terrain.IsValid() && m_worldEditorSession && terrainPick && m_vkDeviceContext.IsValid())
			{
				const bool cap = m_worldEditorImGui->WantsCaptureMouse();
				engine::editor::WorldEditorSession& ws = *m_worldEditorSession;
				if (!cap && ws.TerrainEditMode() == 4 && m_input.WasMousePressed(engine::platform::MouseButton::Left))
				{
					const float ox = m_terrain.GetTerrainOriginX();
					const float oz = m_terrain.GetTerrainOriginZ();
					const float wsiz = m_terrain.GetTerrainWorldSize();
					const float eps = wsiz * 1e-5f;
					const float px = std::clamp(pickX, ox + eps, ox + wsiz - eps);
					const float pz = std::clamp(pickZ, oz + eps, oz + wsiz - eps);
					ws.AddRouteDraftPoint(static_cast<double>(px), static_cast<double>(pz));
				}
				else if (!cap && ws.TerrainEditMode() == 3 && m_input.WasMousePressed(engine::platform::MouseButton::Left))
				{
					const engine::render::terrain::HeightmapData& hm = m_terrain.GetMutableHeightmapData();
					float wy = 0.f;
					if (TryTerrainWorldY(hm, overlay.terrainOriginX, overlay.terrainOriginZ, overlay.terrainWorldSize, overlay.heightScale,
							pickX, pickZ, wy))
					{
						// Roadmap-6 (2026-07-19) — placement/déplacement UNDOABLE :
						// le clic passe par des commandes sur la pile du shell
						// (adressage par guid stable). Fallback direct historique
						// si le shell n'est pas initialisé (pas de pile undo).
						if (m_worldEditorShell && m_worldEditorShell->IsInitialized())
						{
							// Foncteurs capturant [this] : l'Engine survit aux
							// commandes dans la pile undo (même pattern Lot 5).
							engine::editor::scene::LayoutPlacementOps ops;
							ops.add = [this](const engine::editor::WorldMapEditLayoutInstance& inst) -> bool
							{
								return m_worldEditorSession ? m_worldEditorSession->AddLayoutInstance(inst) : false;
							};
							ops.removeByGuid = [this](const std::string& guid) -> bool
							{
								return m_worldEditorSession ? m_worldEditorSession->RemoveLayoutInstanceByGuid(guid) : false;
							};
							ops.setPositionByGuid = [this](const std::string& guid, double x, double y, double z) -> bool
							{
								return m_worldEditorSession
									? m_worldEditorSession->SetLayoutInstancePositionByGuid(guid, x, y, z) : false;
							};
							const int selIdx = ws.SelectedLayoutInstanceIndex();
							if (selIdx >= 0 && selIdx < static_cast<int>(ws.Doc().layoutInstances.size()))
							{
								const engine::editor::WorldMapEditLayoutInstance& inst =
									ws.Doc().layoutInstances[static_cast<size_t>(selIdx)];
								m_worldEditorShell->MutableCommandStack().Push(
									std::make_unique<engine::editor::scene::MoveLayoutInstanceCommand>(
										inst.guid, inst.worldX, inst.worldY, inst.worldZ,
										static_cast<double>(pickX), static_cast<double>(wy), static_cast<double>(pickZ),
										ops));
								ws.Status() = "Instance deplacee.";
							}
							else
							{
								engine::editor::WorldMapEditLayoutInstance inst{};
								if (ws.BuildLayoutInstanceForPlacement(m_cfg, static_cast<double>(pickX),
										static_cast<double>(wy), static_cast<double>(pickZ), inst))
								{
									m_worldEditorShell->MutableCommandStack().Push(
										std::make_unique<engine::editor::scene::PlaceLayoutInstanceCommand>(
											std::move(inst), ops));
								}
							}
						}
						else
						{
							ws.PlaceOrMoveLayoutInstanceAtTerrainHit(m_cfg, static_cast<double>(pickX), static_cast<double>(wy),
								static_cast<double>(pickZ));
						}
					}
				}
				else if (m_worldEditorTerrainTools.IsValid() && !cap && m_input.IsMouseDown(engine::platform::MouseButton::Left)
					&& ws.TerrainEditMode() != 3 && ws.TerrainEditMode() != 4)
				{
					engine::render::terrain::BrushParams bp;
					bp.radius = ws.BrushRadius();
					bp.strength = ws.BrushStrength();
					bp.falloff = 1.f;
					bp.flattenTarget = 0.5f;
					if (ws.TerrainEditMode() == 1)
					{
						const uint32_t layer = static_cast<uint32_t>(std::clamp(ws.SplatLayer(), 0, 3));
						m_worldEditorTerrainTools.PaintSplat(pickX, pickZ, layer, bp);
						(void)m_worldEditorTerrainTools.FlushSplatMap(m_vkDeviceContext.GetDevice(),
							m_vkDeviceContext.GetPhysicalDevice(),
							m_vkDeviceContext.GetGraphicsQueue(),
							m_vkDeviceContext.GetGraphicsQueueFamilyIndex());
					}
					else if (ws.TerrainEditMode() == 2)
					{
						m_worldEditorTerrainTools.PaintGrassMask(pickX, pickZ, bp, ws.GrassMaskEraseBrush());
						(void)m_worldEditorTerrainTools.FlushGrassMask(m_terrain,
							m_vkDeviceContext.GetDevice(),
							m_vkDeviceContext.GetPhysicalDevice(),
							m_vkDeviceContext.GetGraphicsQueue(),
							m_vkDeviceContext.GetGraphicsQueueFamilyIndex());
					}
					else
					{
						engine::render::terrain::BrushOp op = engine::render::terrain::BrushOp::Raise;
						switch (ws.BrushOp())
						{
						case 1: op = engine::render::terrain::BrushOp::Lower; break;
						case 2: op = engine::render::terrain::BrushOp::Smooth; break;
						case 3: op = engine::render::terrain::BrushOp::Flatten; break;
						default: break;
						}
						m_worldEditorTerrainTools.ApplyBrush(pickX, pickZ, op, bp);
					}
				}
			}
		}
		else if (m_worldEditorImGui && m_worldEditorImGui->IsReady() && m_authImGui
			&& m_cfg.GetBool("render.auth_ui.imgui.enabled", false) && m_authUi.GetVisualState().active)
		{
			float dw = static_cast<float>(std::max(1, m_width));
			float dh = static_cast<float>(std::max(1, m_height));
			if (m_vkSwapchain.IsValid())
			{
				const VkExtent2D extUi = m_vkSwapchain.GetExtent();
				if (extUi.width > 0 && extUi.height > 0)
				{
					dw = static_cast<float>(extUi.width);
					dh = static_cast<float>(extUi.height);
				}
			}
			// Phase 2 — aperçu 3D : avance l'anim/orbit puis rend l'avatar dans
			// l'image offscreen AVANT que la draw list ImGui n'échantillonne la
			// texture (RenderOffscreen est autonome : submit + wait idle). No-op
			// si pas de mesh/pipeline forward -> l'image garde le clear de Init.
			{
				const float previewNowSec = EngineNowSec();
				float previewDt = (m_racePreviewLastNowSec > 0.0f)
					? (previewNowSec - m_racePreviewLastNowSec) : 0.0f;
				m_racePreviewLastNowSec = previewNowSec;
				if (previewDt < 0.0f)  previewDt = 0.0f;
				if (previewDt > 0.1f)  previewDt = 0.1f; // clamp gros hitch
				// L'écran de création garde la rotation auto (la fenêtre Personnage la
				// coupe ; on la réactive ici au cas où elle a servi avant).
				m_racePreviewViewport.SetAutoOrbit(true);
				m_racePreviewViewport.Tick(previewDt);
				m_racePreviewViewport.RenderOffscreen();
			}

			const engine::client::AuthUiPresenter::VisualState authVsImgui = m_authUi.GetVisualState();
			const engine::client::AuthUiPresenter::RenderModel authRmImgui = m_authUi.BuildRenderModel();
			m_authImGui->Render(authVsImgui, authRmImgui, dw, dh);
			// Chat HUD desactive sur les ecrans pre-EnterWorld (auth/ShardPick/
			// CharacterSelect/CharacterCreate) suite a retour utilisateur "on laisse
			// tomber pour le moment, l'affichage du CHAT juste apres l'authentification".
			// Le chat reapparaitra une fois la branche !authGateActive prise (in-game).
			ImGui::Render();
		}
		else if (m_worldEditorImGui && m_worldEditorImGui->IsReady() && m_chatImGui
			&& m_chatUi.IsInitialized()
			&& m_authUi.IsInitialized() && m_authUi.IsMasterAuthenticated()
			&& !m_worldEditorExe
			&& (m_cfg.GetBool("render.chat_imgui.enabled", true) || m_inGamePauseMenuVisible || m_inGameOptionsPanelVisible))
		{
			// Phase 3.11.1 — Rendu du panneau chat. NewFrame déjà appelé plus haut via
			// chatImguiOverlayNewFrame. ImGui::Render finalise la draw list pour le RecordToBackbuffer.
			float dw = static_cast<float>(std::max(1, m_width));
			float dh = static_cast<float>(std::max(1, m_height));
			if (m_vkSwapchain.IsValid())
			{
				const VkExtent2D extUi = m_vkSwapchain.GetExtent();
				if (extUi.width > 0 && extUi.height > 0)
				{
					dw = static_cast<float>(extUi.width);
					dh = static_cast<float>(extUi.height);
				}
			}
			// `inWorldShard` = true uniquement post-EnterWorld : ajoute le canal Zone.
			m_chatImGui->Render(dw, dh, m_authUi.IsInWorldShard());
#if defined(_WIN32)
			// Boussole HUD : visible en jeu. Forward caméra = row 2 du view matrix.
			// L'aiguille rouge pointe le Nord et tourne quand le joueur pivote.
			// Masquée avec le radar/tracker par la bascule HUD (m_hudMapClusterHidden,
			// touche controls.keybind.hud_toggle).
			if (m_authUi.IsInWorldShard() && !m_hudMapClusterHidden)
			{
				const uint32_t rIdxHud = m_renderReadIndex.load(std::memory_order_acquire);
				const engine::RenderState& rsHud = m_renderStates[rIdxHud];
				const engine::render::DayNightCycle::State& dnHud = m_dayNight.GetState();
				engine::render::DrawCompassHud(
					rsHud.viewMatrix.m[2], rsHud.viewMatrix.m[10],
					dnHud.timeOfDay, dnHud.isDaytime,
					dw, dh);
			}
#endif
			// Cellule de dialogue PNJ : fenêtre centrale dédiée (no-op si inactif).
			// Partage la frame ImGui en cours (NewFrame déjà appelé plus haut).
			if (m_dialogueImGui && m_dialogue.IsActive())
				m_dialogueImGui->Render(m_dialogue, dw, dh);
			// SP2 Task 5 — Journal + tracker + panneau donneur. No-op si non bindé
			// (BindQuestUi est appelé au boot, cf. plus haut) ou si giverList/
			// journalEntries sont vides (rien à afficher).
			// Radar : alimente le presenter avec la position CLIENT-AUTORITAIRE du
			// joueur AVANT le rendu. Le serveur exclut le joueur local de son propre
			// snapshot (AoI self-skip) -> UIModel.playerStats.position reste figée au
			// spawn et le radar ne suivait jamais le joueur (retour joueur 2026-07-07).
			// In-world uniquement (contrôleur valide). UpdatePlayerWorldPosition ne
			// reconstruit le radar que si le déplacement dépasse ~5 cm.
			if (m_authUi.IsInWorldShard())
			{
				const engine::math::Vec3 radarPlayerPos = m_characterController.GetPosition();
				m_questUi.UpdatePlayerWorldPosition(radarPlayerPos.x, radarPlayerPos.z);
			}
			// PR-B — quand un dialogue PNJ est actif, on supprime le panneau donneur
			// séparé : l'acceptation/rendu se fait DANS la conversation (boutons injectés
			// par DialogueImGuiRenderer). Le panneau reste le fallback hors dialogue.
			if (m_questImGui)
			{
				m_questImGui->SetGiverPanelSuppressed(m_dialogue.IsActive());
				m_questImGui->SetMinimapZoomIndex(m_minimapZoomIndex);
				m_questImGui->Render(dw, dh, m_authUi.IsInWorldShard(), !m_hudMapClusterHidden);
			}
			// Marqueurs ImGui des interactibles : label flottant projete (visibilite v1
			// sans mesh). Surligne + " [E]" si a portee. Cf. m_interactables (#39/#40).
			{
				const int ivw = static_cast<int>(dw);
				const int ivh = static_cast<int>(dh);
				ImDrawList* fg = ImGui::GetForegroundDrawList();
				// Z-order : un menu modal (pause / options) doit recouvrir TOUT le monde.
				// Les labels world-space (interactibles, plaques de nom) sont traces sur la
				// foreground draw list ImGui, qu'ImGui composite TOUJOURS au-dessus des fenetres
				// (dont le panneau du menu pause, qui est une simple fenetre). On suspend donc
				// ces labels quand un menu modal est ouvert, sinon ils perforent le menu.
				// Les plaques/labels monde (nameplates mobs, noms joueurs, butin) passent
				// par le foreground draw list, donc AU-DESSUS des fenetres ImGui. On les
				// masque des qu'un menu OU un panneau in-game est ouvert, sinon ils
				// debordent par-dessus et rendent le panneau illisible (ex. le label
				// « Sanglier des collines » au milieu de l'arbre de competences).
				const bool worldOverlaysHidden = m_inGamePauseMenuVisible || m_inGameOptionsPanelVisible
					|| (m_characterWindowImGui && m_characterWindowImGui->IsVisible())
					|| m_auctionHouseVisible || m_arenaVisible || m_battleGroundVisible
					|| m_outdoorPvpVisible || m_guildVisible || m_lootRollVisible
					|| m_craftingVisible || m_advancedCombatVisible
					|| m_weatherVisible;
				if (!worldOverlaysHidden)
				for (std::size_t ii = 0; ii < m_interactables.size(); ++ii)
				{
					const InteractableEntity& e = m_interactables[ii];
					// Pendant un dialogue PNJ : on masque le label flottant + badge « E » de
						// l'interactible (le PNJ engagé est derrière la cellule, son label
						// transparaîtrait au travers).
						if (m_dialogueActive)
							continue;
						const float my = m_terrainCollider.GroundHeightAt(e.position.x, e.position.z) + 1.9f;
					float sx = 0.0f, sy = 0.0f;
					if (!WorldToScreenPx(out.viewProjMatrix.m, e.position.x, my, e.position.z, ivw, ivh, sx, sy))
						continue;
					const bool inRange = (static_cast<int>(ii) == m_interactableInRange);
					const ImU32 col = inRange ? IM_COL32(255, 220, 80, 255) : IM_COL32(210, 210, 210, 200);
					// Label du nom seul ; le "[E]" texte est remplace par un badge touche
					// (chantier C) + la surbrillance 3D du prop signale l'interactivite.
					const ImVec2 ts = ImGui::CalcTextSize(e.label.c_str());
					fg->AddText(ImVec2(sx - ts.x * 0.5f, sy - ts.y), col, e.label.c_str());
					// Repere d'interaction : badge "touche E" sous le label quand a portee.
					if (inRange)
					{
						const ImVec2 keySz = ImGui::CalcTextSize("E");
						const float pad = 6.0f;
						const float bw = keySz.x + pad * 2.0f;
						const float bh = keySz.y + pad * 2.0f;
						const float bx = sx - bw * 0.5f;
						const float by = sy + 4.0f;
						fg->AddRectFilled(ImVec2(bx, by), ImVec2(bx + bw, by + bh), IM_COL32(20, 20, 24, 220), 4.0f);
						fg->AddRect(ImVec2(bx, by), ImVec2(bx + bw, by + bh), IM_COL32(255, 220, 80, 255), 4.0f, 0, 2.0f);
						fg->AddText(ImVec2(bx + pad, by + pad), IM_COL32(255, 220, 80, 255), "E");
					}
					// SP2 Task 6 — Marqueur world-space donneur de quête : rune procédurale
					// (losange + anneau) au-dessus des PNJ liés à une quête en cours de
					// proposition ou de rendu. Source des liens PNJ<->quête : table
					// data-driven chargée au boot (cf. Init, m_questGiverTable.Load).
					// Statuts UIModel.quests miroir de engine::client::QuestStatus (wire
					// uint8) — dupliqués en constantes locales comme QuestImGuiRenderer.cpp
					// pour ne pas tirer de dépendance supplémentaire ici.
					if (!e.npcTargetId.empty())
					{
						constexpr uint8_t kQuestStatusOffered = 1;
						constexpr uint8_t kQuestStatusReadyToTurnIn = 3;
						const std::vector<engine::client::QuestGiverLink>* links = m_questGiverTable.ForNpc(e.npcTargetId);
						if (links != nullptr && !links->empty())
						{
							// Culling distance : seulement si le PNJ est à portée « visible »
							// configurée (defaut 35 m), indépendamment du rayon d'interaction
							// (e.radius, ~2.5 m) qui régit le badge "E".
							const engine::math::Vec3 markerPlayerPos = m_characterController.GetPosition();
							// Distance XZ uniquement (comme le rayon d'interaction / badge "E",
							// cf. calcul dx²+dz² du "[E]") : la position des interactables est
							// chargée avec y=0 (jamais collée au sol) alors que le joueur est
							// ~100 m plus haut (plateau terrain). Inclure le Y ferait toujours
							// dépasser markerMaxDist → le rune ne s'afficherait jamais.
							const float mdx = e.position.x - markerPlayerPos.x;
							const float mdz = e.position.z - markerPlayerPos.z;
							const float markerDist = std::sqrt(mdx * mdx + mdz * mdz);
							const float markerMaxDist = static_cast<float>(
								m_cfg.GetDouble("client.quest.giver_marker_distance_m", 35.0));
							if (markerDist <= markerMaxDist)
							{
								// Priorité d'affichage si plusieurs liens : rendre > proposer
								// (un PNJ à la fois donneur ET receveur pour des quêtes
								// différentes doit d'abord signaler ce qu'il attend du joueur).
								bool hasOffer = false;
								bool hasTurnIn = false;
								const auto& questsModel = m_uiModelBinding.GetModel().quests;
								for (const engine::client::QuestGiverLink& link : *links)
								{
									for (const engine::client::UIQuestEntry& q : questsModel)
									{
										if (q.questId != link.questId)
											continue;
										if (link.role == 0 && q.status == kQuestStatusOffered)
											hasOffer = true;
										else if (link.role == 1 && q.status == kQuestStatusReadyToTurnIn)
											hasTurnIn = true;
									}
								}
								if (hasOffer || hasTurnIn)
								{
									// Ancrage : ~2.2 m au-dessus de la tête (my = sol + 1.9, tête
									// approx au-dessus -> on projette depuis le sol + 2.2 direct
									// pour rester simple et stable si le mesh du PNJ varie).
									const float runeWorldY = m_terrainCollider.GroundHeightAt(e.position.x, e.position.z) + 2.2f;
									float rsx = 0.0f, rsy = 0.0f;
									if (WorldToScreenPx(out.viewProjMatrix.m, e.position.x, runeWorldY, e.position.z, ivw, ivh, rsx, rsy))
									{
										// Rendre prioritaire sur proposer si les deux sont vrais.
										const bool turnInVariant = hasTurnIn;
										const ImU32 runeFill = turnInVariant
											? IM_COL32(120, 200, 255, 235)   // rendre : teinte bleu-glacé
											: IM_COL32(255, 205, 60, 235);   // proposer : doré plein
										const ImU32 runeRing = turnInVariant
											? IM_COL32(230, 245, 255, 255)
											: IM_COL32(255, 240, 190, 255);
										const float runeR = 18.0f; // agrandie (retour joueur : trop petite a 11)
										// Losange (4 sommets) rempli.
										const ImVec2 diamond[4] = {
											ImVec2(rsx, rsy - runeR),
											ImVec2(rsx + runeR * 0.62f, rsy),
											ImVec2(rsx, rsy + runeR),
											ImVec2(rsx - runeR * 0.62f, rsy),
										};
										fg->AddConvexPolyFilled(diamond, 4, runeFill);
										// Anneau autour du losange pour la lisibilité + signature "rune".
										fg->AddCircle(ImVec2(rsx, rsy), runeR + 4.0f, runeRing, 24, 2.5f);
									}
								}
							}
						}
					}
				}
				// TD.4 — Plaques de nom au-dessus des avatars joueurs distants.
				// Source : `m_uiModelBinding.GetModel().remoteEntities` (peuple par
				// ApplySnapshot, cf. TD.1) ; le serveur expose `playerClientId` ≠ 0
				// uniquement pour les joueurs (mobs/lootbags = 0 → pas de plaque).
				// Position : meme pattern que RecordRemoteAvatars — repli sur le
				// snapshot brut si l'etat lisse n'existe pas encore (graceful) ;
				// le `+ 1.2` est calibre sur la hauteur d'avatar (`py - 0.9` = pieds,
				// donc tete ~ `py + 0.9`, plaque au-dessus = `py + 1.2`).
				if (!worldOverlaysHidden)
				{
					const auto& remotes = m_uiModelBinding.GetModel().remoteEntities;
						// Anti-chevauchement des plaques : boites deja posees cette frame
						// (x,y,z,w = gauche,haut,droite,bas). Une nouvelle plaque qui en
						// recouvre une est remontee d'une hauteur de plaque (empilement).
						std::vector<ImVec4> placedLabelRects;
					// Le Journal de quêtes (coin haut-gauche, toujours visible) est une
					// fenêtre ImGui ; les plaques de mob passent par le foreground draw
					// list, donc AU-DESSUS. On récupère son rectangle pour sauter toute
					// plaque qui retomberait dessus (retour joueur 2026-07-09 : « Sanglier
					// des collines » s'affichait par-dessus le journal).
					ImVec4 journalRect(0.0f, 0.0f, 0.0f, 0.0f);
					bool hasJournalRect = false;
					{
						const engine::client::QuestUiState& qs = m_questUi.GetState();
						if (qs.layoutValid)
						{
							const engine::client::QuestUiRect& jb = qs.journalPanelBounds;
							journalRect = ImVec4(jb.x, jb.y, jb.x + jb.width, jb.y + jb.height);
							hasJournalRect = true;
						}
					}
					for (const engine::client::UIRemoteEntity& re : remotes)
					{
						// Combat SP1 : les mobs (archetypeId != 0) ont leur plaque.
						// Validation v12 — les loot bags (les deux ids à 0) aussi :
						// « Butin [F] » au sol (le serveur les répliquait depuis M28
						// mais le client ne les montrait jamais — butin imperdable
						// mais invisible). Ramassage : touche F, bloc combat plus bas.
						// Mob mort en attente de despawn : pas de plaque (kEntityStateDead).
						// Métiers SP1 — les nodes échappent à ce skip : un node épuisé
						// garde sa plaque, grisée avec le suffixe « (epuise) ».
						if (re.playerClientId == 0u && (re.stateFlags & 1u) != 0u
							&& re.archetypeId < engine::server::kGatheringNodeArchetypeBase)
							continue;
						float wx = re.positionX, wy = re.positionY, wz = re.positionZ;
						const auto sit = m_remoteSmoothed.find(re.entityId);
						if (sit != m_remoteSmoothed.end() && sit->second.valid)
						{
							wx = sit->second.x; wy = sit->second.y; wz = sit->second.z;
						}
						// Combat SP1 fix — mobs/nodes : Y de spawn brut (souvent 0, jamais
						// collé au terrain par le serveur) → snap visuel au sol client,
						// sinon la plaque flotte au ras du sol (cf. ResolveRemoteDisplayCenterY).
						wy = ResolveRemoteDisplayCenterY(re.playerClientId != 0u, wy, wx, wz);
						float sxp = 0.0f, syp = 0.0f;
						if (!WorldToScreenPx(out.viewProjMatrix.m, wx, wy + 1.2f, wz, ivw, ivh, sxp, syp))
							continue;
						// TD.5 : on affiche le vrai nom du perso s'il est connu (chargé depuis
						// la DB serveur et propagé via le SnapshotEntity). Sinon, fallback sur
						// "P<clientId>" — utile quand la DB serveur ne sert pas (Windows / DB
						// indisponible) ou pour identifier l'entité dans un log.
						// Combat SP1 : plaque mob "Nom (niv. N)  PV/PVmax" résolue via le
						// CreatureCatalog ; fallback générique si l'archétype est inconnu
						// du catalogue client (catalogue absent ou désynchronisé).
						std::string label;
						// SP — suffixe « (niv. N) » des mobs, rendu en plus petit a droite
						// du nom. Vide pour joueurs/nodes/butin (pas de niveau affiche).
						std::string levelSuffix;
						// Validation v12 — armé par la branche mob ci-dessous : dessine la
						// barre de vie sous la plaque (jamais pour joueurs/nodes).
						bool drawMobHealthBar = false;
						if (re.playerClientId != 0u)
						{
							label = !re.displayName.empty()
								? re.displayName
								: ("P" + std::to_string(re.playerClientId));
						}
						else if (re.archetypeId == 0u)
						{
							// Validation v12 — sac de butin au sol.
							label = "Butin [F]";
						}
						else if (re.archetypeId >= engine::server::kGatheringNodeArchetypeBase)
						{
							// Métiers SP1 — label de node de récolte : nom FR par type
							// (V1 : map locale ; les noms en data = amélioration future)
							// + « [E] » si disponible et à portée de récolte (~5 m).
							const uint32_t nodeTypeId =
								re.archetypeId - engine::server::kGatheringNodeArchetypeBase;
							const char* nodeName = "Ressource";
							switch (nodeTypeId)
							{
							case 1u: nodeName = "Filon de minerai"; break;
							case 2u: nodeName = "Herbes sauvages"; break;
							case 3u: nodeName = "Arbre a abattre"; break;
							case 4u: nodeName = "Carcasse"; break;
							default: break;
							}
							label = nodeName;
							if ((re.stateFlags & 1u) != 0u)
							{
								label += " (epuise)";
							}
							else
							{
								const engine::math::Vec3 playerPosNode = m_characterController.GetPosition();
								const float dxNode = re.positionX - playerPosNode.x;
								const float dzNode = re.positionZ - playerPosNode.z;
								if ((dxNode * dxNode + dzNode * dzNode) <= 25.0f)
								{
									label += " [E]";
								}
							}
						}
						else
						{
							const engine::client::CreatureAppearance* appearance =
								m_creatureCatalog.Find(re.archetypeId);
							const std::string mobName = (appearance != nullptr)
								? appearance->name
								: ("Creature " + std::to_string(re.archetypeId));
							const uint32_t mobLevel = (appearance != nullptr) ? appearance->level : 1u;
							// Validation v12 — les PV chiffrés quittent le label : ils sont
							// désormais portés par la barre de vie dessinée sous la plaque.
							label = mobName;
							levelSuffix = " (niv. " + std::to_string(mobLevel) + ")";
							drawMobHealthBar = (re.maxHealth > 0u);
						}
						// Largeur TOTALE = nom (taille normale) + suffixe « (niv. N) » (0.78x).
						ImFont* plateFont = ImGui::GetFont();
						const float nivFontSize = ImGui::GetFontSize() * 0.78f;
						const ImVec2 nameTs = ImGui::CalcTextSize(label.c_str());
						const ImVec2 nivTs = levelSuffix.empty()
							? ImVec2(0.0f, 0.0f)
							: plateFont->CalcTextSizeA(nivFontSize, FLT_MAX, 0.0f, levelSuffix.c_str());
						const ImVec2 ts(nameTs.x + nivTs.x, nameTs.y);
						// Empilement vertical : remonte la plaque tant qu'elle recouvre une
						// plaque deja posee cette frame (mobs superposes -> textes lisibles).
						const float plateHalfW = ts.x * 0.5f + 6.0f;
						const float plateH = ts.y + 2.0f + (drawMobHealthBar ? 24.0f : 4.0f);
						for (int attempt = 0; attempt < 12; ++attempt)
						{
							const float top = syp - ts.y - 2.0f;
							const float bot = syp + (drawMobHealthBar ? 24.0f : 4.0f);
							bool overlap = false;
							for (const ImVec4& r : placedLabelRects)
							{
								if ((sxp - plateHalfW) < r.z && (sxp + plateHalfW) > r.x
									&& top < r.w && bot > r.y)
								{ overlap = true; break; }
							}
							if (!overlap) break;
							syp -= (plateH + 3.0f);
						}
						// Ne pas dessiner la plaque si elle retombe sur le Journal de quêtes
						// (elle serait masquée par la fenêtre de toute façon, mais le
						// foreground la peindrait par-dessus -> illisible).
						if (hasJournalRect
							&& (sxp - plateHalfW) < journalRect.z && (sxp + plateHalfW) > journalRect.x
							&& (syp - ts.y - 2.0f) < journalRect.w
							&& (syp + (drawMobHealthBar ? 24.0f : 4.0f)) > journalRect.y)
						{
							continue;
						}
						placedLabelRects.emplace_back(sxp - plateHalfW, syp - ts.y - 2.0f,
							sxp + plateHalfW, syp + (drawMobHealthBar ? 24.0f : 4.0f));
						// Halo noir derriere le texte pour la lisibilite sur fond clair (ciel).
						fg->AddRectFilled(
							ImVec2(sxp - ts.x * 0.5f - 4.0f, syp - ts.y - 2.0f),
							ImVec2(sxp + ts.x * 0.5f + 4.0f, syp + 2.0f),
							IM_COL32(0, 0, 0, 140), 3.0f);
						// Validation v12 — surbrillance de la CIBLE courante : avec
						// plusieurs mobs au même nom et aux mêmes PV, la bascule Tab
						// était invisible (le cadre cible affichait la même chose).
						// Bordure dorée autour de la plaque du mob ciblé.
						{
							const engine::client::UIModel& plateModel = m_uiModelBinding.GetModel();
							if (plateModel.targetStats.hasTarget
								&& plateModel.targetStats.entityId == re.entityId)
							{
								fg->AddRect(
									ImVec2(sxp - ts.x * 0.5f - 6.0f, syp - ts.y - 4.0f),
									ImVec2(sxp + ts.x * 0.5f + 6.0f, syp + 4.0f),
									IM_COL32(235, 190, 60, 255), 3.0f, 0, 2.0f);
							}
						}
						// Nom a taille normale, puis suffixe niveau plus petit, aligne en bas.
						const float labelLeft = sxp - ts.x * 0.5f;
						fg->AddText(ImVec2(labelLeft, syp - ts.y), IM_COL32(220, 230, 255, 255), label.c_str());
						if (!levelSuffix.empty())
						{
							fg->AddText(plateFont, nivFontSize,
								ImVec2(labelLeft + nameTs.x, syp - nivTs.y),
								IM_COL32(180, 195, 225, 230), levelSuffix.c_str());
						}
						// Validation v12 — barre de vie du mob sous la plaque, alimentée
						// par currentHealth/maxHealth du snapshot (10 Hz) : elle descend
						// donc en direct à chaque coup porté. Couleur par tiers de PV
						// (vert > 50 %, orange > 25 %, rouge en dessous).
						if (drawMobHealthBar)
						{
							const float hpFrac = std::clamp(
								static_cast<float>(re.currentHealth) / static_cast<float>(re.maxHealth),
								0.0f, 1.0f);
							const float barW = std::max(64.0f, ts.x);
							const float barH = 5.0f;
							const float barLeft = sxp - barW * 0.5f;
							const float barTop = syp + 4.0f;
							const ImU32 fillColor = (hpFrac > 0.5f)
								? IM_COL32(80, 200, 80, 230)
								: ((hpFrac > 0.25f)
									? IM_COL32(230, 160, 40, 230)
									: IM_COL32(210, 60, 50, 230));
							fg->AddRectFilled(ImVec2(barLeft, barTop),
								ImVec2(barLeft + barW, barTop + barH), IM_COL32(20, 22, 26, 200), 2.0f);
							fg->AddRectFilled(ImVec2(barLeft, barTop),
								ImVec2(barLeft + barW * hpFrac, barTop + barH), fillColor, 2.0f);
							fg->AddRect(ImVec2(barLeft, barTop),
								ImVec2(barLeft + barW, barTop + barH), IM_COL32(0, 0, 0, 160), 2.0f);
							// PV chiffrés compacts au-dessus de la barre, à droite.
							const std::string hpText = std::to_string(re.currentHealth) + "/"
								+ std::to_string(re.maxHealth);
							const ImVec2 hpTs = ImGui::CalcTextSize(hpText.c_str());
							fg->AddText(ImVec2(barLeft + barW - hpTs.x, barTop + barH + 1.0f),
								IM_COL32(200, 205, 215, 220), hpText.c_str());
						}
					}
				}
				// =============================================================
				// Combat SP2 — ciblage (clic / Tab), attaque (T), cadre cible,
				// log combat HUD, panneau avance (J) et ecran de mort. Touches
				// declarees dans le registre de binds combat (cf. Engine.h).
				// Libelles ASCII : la police in-game (Windlass) n'a pas tous
				// les glyphes accentues (convention du menu pause).
				// =============================================================
				if (m_gameplayNetInitialized && m_authUi.IsInWorldShard())
				{
					const engine::client::UIModel& uiModel = m_uiModelBinding.GetModel();
					const bool localDead = (uiModel.playerStats.stateFlags & 1u) != 0u;
					const bool chatFocus = m_chatUi.IsInitialized() && m_chatUi.IsChatFocusActive();
					const bool menuOpen = m_inGamePauseMenuVisible || m_inGameOptionsPanelVisible;
					const bool keysAllowed = !menuOpen && !chatFocus && !localDead
						&& !ImGui::GetIO().WantCaptureKeyboard;
					const bool mouseAllowed = !menuOpen && !chatFocus && !localDead
						&& !ImGui::GetIO().WantCaptureMouse;

					// --- Selection / attaque a la souris. Pick ecran-espace du mob
					// vivant le plus proche du curseur (rayon kPickRadiusPx), sans
					// raycast 3D. Factorise : utilise par le clic GAUCHE (selection)
					// et par le clic DROIT relache sans drag (selection + attaque
					// auto — le drag droit reste le mouselook camera).
					// Validation v12 — rayon élargi (40 → 70 px : le joueur clique le
					// CORPS du mob, pas le point d'ancrage projeté) et ancre abaissée
					// vers le torse (+0.2 m au lieu de +0.5).
					constexpr float kPickRadiusPx = 70.0f;
					const auto pickMobAtScreen = [&](const ImVec2& screenPos) -> engine::server::EntityId
					{
						engine::server::EntityId bestId = 0;
						float bestPickDistSq = kPickRadiusPx * kPickRadiusPx;
						for (const engine::client::UIRemoteEntity& re : uiModel.remoteEntities)
						{
							if (re.archetypeId == 0u || (re.stateFlags & 1u) != 0u
								|| re.archetypeId >= engine::server::kGatheringNodeArchetypeBase)
								continue;
							float wx = re.positionX, wy = re.positionY, wz = re.positionZ;
							const auto sit = m_remoteSmoothed.find(re.entityId);
							if (sit != m_remoteSmoothed.end() && sit->second.valid)
							{
								wx = sit->second.x; wy = sit->second.y; wz = sit->second.z;
							}
							wy = ResolveRemoteDisplayCenterY(false, wy, wx, wz);
							float sxp = 0.0f, syp = 0.0f;
							if (!WorldToScreenPx(out.viewProjMatrix.m, wx, wy + 0.2f, wz, ivw, ivh, sxp, syp))
								continue;
							const float dxPick = sxp - screenPos.x;
							const float dyPick = syp - screenPos.y;
							const float distSq = dxPick * dxPick + dyPick * dyPick;
							if (distSq < bestPickDistSq)
							{
								bestPickDistSq = distSq;
								bestId = re.entityId;
							}
						}
						// Diagnostic du pick (selection souris rapportee inoperante) :
						// une ligne par clic monde — a lire dans le log client.
						LOG_INFO(Core,
							"[CombatPick] clic=({:.0f},{:.0f}) picked_entity_id={} best_dist_px={:.1f} entites={}",
							screenPos.x, screenPos.y, bestId,
							(bestId != 0) ? std::sqrt(bestPickDistSq) : -1.0f,
							static_cast<int>(uiModel.remoteEntities.size()));
						return bestId;
					};

					if (mouseAllowed && m_input.WasMousePressed(engine::platform::MouseButton::Left))
					{
						const ImVec2 mousePos = ImGui::GetIO().MousePos;
						// Groupes SP1 — un clic sur un cadre de groupe est une sélection
						// d'allié (gérée plus bas), jamais un pick de mob.
						bool overPartyFrame = false;
						if (uiModel.inParty)
						{
							const engine::client::PartyHudState& partyPickState = m_partyHud.GetState();
							for (size_t pickFrame = 0; pickFrame < engine::client::kMaxPartyFrames; ++pickFrame)
							{
								const engine::client::PartyMemberFrame& pframe = partyPickState.frames[pickFrame];
								if (!pframe.visible)
								{
									continue;
								}
								if (mousePos.x >= pframe.frameBounds.x
									&& mousePos.x <= pframe.frameBounds.x + pframe.frameBounds.width
									&& mousePos.y >= pframe.frameBounds.y
									&& mousePos.y <= pframe.frameBounds.y + pframe.frameBounds.height)
								{
									overPartyFrame = true;
									break;
								}
							}
						}
						if (!overPartyFrame)
						{
							const engine::server::EntityId pickedId = pickMobAtScreen(mousePos);
							if (pickedId != 0)
							{
								// Validation v12 (décision design) — le clic GAUCHE sur un
								// mob cible ET attaque (un coup), comme le clic droit bref :
								// le combat se joue entièrement à la souris.
								(void)m_uiModelBinding.SetLocalTarget(pickedId);
								if (m_attackSendCooldownSec <= 0.0f)
								{
									const uint32_t lmbClientId = m_gameplayUdp.ServerClientId();
									if (lmbClientId != 0u)
									{
										(void)m_gameplayUdp.SendAttackRequest(lmbClientId, pickedId);
										m_attackSendCooldownSec = 0.25f;
										constexpr float kMeleeRangeHintMetersLmb = 4.0f;
										const engine::math::Vec3 playerPosLmb = m_characterController.GetPosition();
										for (const engine::client::UIRemoteEntity& re : uiModel.remoteEntities)
										{
											if (re.entityId != pickedId)
												continue;
											const float dxl = re.positionX - playerPosLmb.x;
											const float dzl = re.positionZ - playerPosLmb.z;
											if ((dxl * dxl + dzl * dzl)
												> kMeleeRangeHintMetersLmb * kMeleeRangeHintMetersLmb)
											{
												m_outOfRangeHintSec = 1.2f;
											}
											break;
										}
									}
								}
							}
						}
					}

					// --- Clic droit « court » = cibler + ATTAQUE AUTO (le drag droit
					// reste le mouselook caméra : on discrimine au relâchement par la
					// dérive du curseur depuis la pression).
					constexpr float kRmbClickMaxDriftPx = 6.0f;
					if (mouseAllowed && m_input.WasMousePressed(engine::platform::MouseButton::Right))
					{
						const ImVec2 rmbPos = ImGui::GetIO().MousePos;
						m_rmbPressMouseX = rmbPos.x;
						m_rmbPressMouseY = rmbPos.y;
						m_rmbClickCandidate = true;
					}
					if (m_input.WasMouseReleased(engine::platform::MouseButton::Right))
					{
						const ImVec2 rmbPos = ImGui::GetIO().MousePos;
						const float driftX = rmbPos.x - m_rmbPressMouseX;
						const float driftY = rmbPos.y - m_rmbPressMouseY;
						const bool isClick = m_rmbClickCandidate && mouseAllowed
							&& (driftX * driftX + driftY * driftY)
								<= kRmbClickMaxDriftPx * kRmbClickMaxDriftPx;
						m_rmbClickCandidate = false;
						if (isClick)
						{
							const engine::server::EntityId pickedId = pickMobAtScreen(rmbPos);
							if (pickedId != 0)
							{
								// Validation v12 (décision design) — PAS d'attaque auto :
								// chaque clic droit = cibler + UN coup (le serveur revalide
								// portée/cooldown ; indication « Hors de portee » si loin).
								(void)m_uiModelBinding.SetLocalTarget(pickedId);
								if (m_attackSendCooldownSec <= 0.0f)
								{
									const uint32_t rmbClientId = m_gameplayUdp.ServerClientId();
									if (rmbClientId != 0u)
									{
										(void)m_gameplayUdp.SendAttackRequest(rmbClientId, pickedId);
										m_attackSendCooldownSec = 0.25f;
										constexpr float kMeleeRangeHintMetersRmb = 4.0f;
										const engine::math::Vec3 playerPosRmb = m_characterController.GetPosition();
										for (const engine::client::UIRemoteEntity& re : uiModel.remoteEntities)
										{
											if (re.entityId != pickedId)
												continue;
											const float dxr = re.positionX - playerPosRmb.x;
											const float dzr = re.positionZ - playerPosRmb.z;
											if ((dxr * dxr + dzr * dzr)
												> kMeleeRangeHintMetersRmb * kMeleeRangeHintMetersRmb)
											{
												m_outOfRangeHintSec = 1.2f;
											}
											break;
										}
									}
								}
							}
						}
					}

					// --- Tab : cycle des mobs vivants par distance croissante au joueur.
					if (keysAllowed && m_input.WasPressed(engine::platform::Key::Tab))
					{
						const engine::math::Vec3 playerPos = m_characterController.GetPosition();
						// Filtre de visibilite : seuls les ennemis a l'ecran (dans le
						// frustum camera) sont ciblables au Tab — un mob dans le dos ou
						// hors champ ne doit pas etre selectionne. On reutilise la MEME
						// matrice view-projection que le rendu monde de cette frame
						// (out.viewProjMatrix = out.projMatrix * out.viewMatrix, cf. ~9852),
						// column-major (m[col*4+row], cf. Math.h). Pas d'operateur
						// Mat4*Vec4 dans Math.h : on projette le point a la main, comme
						// WorldToScreenPx plus haut. Retourne false si l'ennemi est
						// derriere la camera (w <= ~0) ou hors du carre NDC [-1,1]^2.
						// On vise le torse (y + 1.0 m) plutot que les pieds : un mob dont
						// les pieds tombent juste sous le bord bas mais dont le corps est
						// visible reste ciblable.
						const float* tabVp = out.viewProjMatrix.m;
						auto isOnScreen = [tabVp](float wx, float wy, float wz) -> bool
						{
							const float cx = tabVp[0] * wx + tabVp[4] * wy + tabVp[8] * wz + tabVp[12];
							const float cy = tabVp[1] * wx + tabVp[5] * wy + tabVp[9] * wz + tabVp[13];
							const float cw = tabVp[3] * wx + tabVp[7] * wy + tabVp[11] * wz + tabVp[15];
							if (cw <= 1e-5f)
								return false; // derriere la camera (ou plan near degenere)
							const float invW = 1.0f / cw;
							const float ndcX = cx * invW;
							const float ndcY = cy * invW;
							return ndcX >= -1.0f && ndcX <= 1.0f
								&& ndcY >= -1.0f && ndcY <= 1.0f;
						};
						std::vector<std::pair<float, engine::server::EntityId>> candidates;
						for (const engine::client::UIRemoteEntity& re : uiModel.remoteEntities)
						{
							if (re.archetypeId == 0u || (re.stateFlags & 1u) != 0u
								|| re.archetypeId >= engine::server::kGatheringNodeArchetypeBase)
								continue;
							// Rejet des ennemis non visibles (hors frustum / dans le dos).
							if (!isOnScreen(re.positionX, re.positionY + 1.0f, re.positionZ))
								continue;
							const float dxc = re.positionX - playerPos.x;
							const float dzc = re.positionZ - playerPos.z;
							candidates.emplace_back(dxc * dxc + dzc * dzc, re.entityId);
						}
						// Diag (sur appui Tab uniquement, non spammy) : si l'utilisateur
						// rapporte « Tab ne selectionne rien », ce log distingue les cas
						// « aucune entite repliquee » / « aucun candidat visible » /
						// « cible deja active ». L'analyse de code n'a revele aucun bug
						// dans le filtre (archetype/mort/node) ni dans la propagation
						// SetLocalTarget -> targetStats ; la cause racine d'une selection
						// vide doit donc etre tranchee en jeu via ce compteur.
						LOG_INFO(Core,
							"[TabTarget] remoteEntities={} candidates_visibles={} hasTarget={}",
							uiModel.remoteEntities.size(), candidates.size(),
							uiModel.targetStats.hasTarget);
						if (!candidates.empty())
						{
							std::sort(candidates.begin(), candidates.end());
							// Suivant de la cible courante (wrap) ; sans cible : la plus proche.
							size_t nextIndex = 0;
							if (uiModel.targetStats.hasTarget)
							{
								for (size_t ci = 0; ci < candidates.size(); ++ci)
								{
									if (candidates[ci].second == uiModel.targetStats.entityId)
									{
										nextIndex = (ci + 1u) % candidates.size();
										break;
									}
								}
							}
							(void)m_uiModelBinding.SetLocalTarget(candidates[nextIndex].second);
						}
					}

					// --- T : attaque de la cible courante. Throttle local anti-spam
					// uniquement : portee, cooldown reel et cible vivante sont
					// revalides par le serveur (HandleAttackRequest).
					if (keysAllowed && m_input.WasPressed(engine::platform::Key::T)
						&& uiModel.targetStats.hasTarget
						&& (uiModel.targetStats.stateFlags & 1u) == 0u
						&& m_attackSendCooldownSec <= 0.0f)
					{
						const uint32_t gameplayClientId = m_gameplayUdp.ServerClientId();
						if (gameplayClientId != 0u)
						{
							(void)m_gameplayUdp.SendAttackRequest(gameplayClientId, uiModel.targetStats.entityId);
							m_attackSendCooldownSec = 0.25f;
							// Validation v12 — le rejet « hors de portée » du serveur est
							// SILENCIEUX (aucun message wire). Indication locale : si la
							// cible est au-delà de la portée de mêlée (4 m, doit suivre
							// kDefaultAttackRangeMeters de ServerApp.cpp), on affiche
							// « Hors de portee » 1,2 s sous le cadre cible. Le serveur
							// reste l'autorité : la requête est envoyée quand même.
							constexpr float kMeleeRangeHintMeters = 4.0f;
							const engine::math::Vec3 playerPosAtk = m_characterController.GetPosition();
							for (const engine::client::UIRemoteEntity& re : uiModel.remoteEntities)
							{
								if (re.entityId != uiModel.targetStats.entityId)
									continue;
								const float dxa = re.positionX - playerPosAtk.x;
								const float dza = re.positionZ - playerPosAtk.z;
								if ((dxa * dxa + dza * dza) > kMeleeRangeHintMeters * kMeleeRangeHintMeters)
								{
									m_outOfRangeHintSec = 1.2f;
								}
								break;
							}
						}
					}

					// --- J : panneau combat avance (DPS meter + log filtrable).
					if (keysAllowed && m_input.WasPressed(engine::platform::Key::J))
					{
						m_advancedCombatVisible = !m_advancedCombatVisible;
					}

					// Chantier 1 — la touche I est LIBÉRÉE : l'inventaire est désormais
					// l'onglet Personnage de la fenêtre unifiée (ouverte par F1).

					// --- Cadre cible (haut-centre) : nom + barre de PV. Les PV de la
					// cible suivent les snapshots (cf. UIModelBinding::ApplySnapshot).
					// On masque le cadre dès que la cible est morte (bit 1 de stateFlags) :
					// inutile de laisser traîner « (MORT) » tant qu'aucune autre cible n'est
					// sélectionnée — ça encombrait le HUD (retour joueur 2026-07-04).
					if (uiModel.targetStats.hasTarget
						&& (uiModel.targetStats.stateFlags & 1u) == 0u)
					{
						const float frameW = 260.0f;
						const float frameH = 54.0f;
						const float fx = (dw - frameW) * 0.5f;
						const float fy = 56.0f;
						const bool targetDead = (uiModel.targetStats.stateFlags & 1u) != 0u;
						// Nom : catalogue creatures (la cible V1 est toujours un mob).
						std::string targetName = "Cible";
						for (const engine::client::UIRemoteEntity& re : uiModel.remoteEntities)
						{
							if (re.entityId != uiModel.targetStats.entityId)
								continue;
							const engine::client::CreatureAppearance* app = m_creatureCatalog.Find(re.archetypeId);
							if (app != nullptr)
								targetName = app->name;
							break;
						}
						if (targetDead)
							targetName += "  (MORT)";
						fg->AddRectFilled(ImVec2(fx, fy), ImVec2(fx + frameW, fy + frameH), IM_COL32(10, 12, 16, 200), 6.0f);
						fg->AddRect(ImVec2(fx, fy), ImVec2(fx + frameW, fy + frameH),
							targetDead ? IM_COL32(120, 120, 120, 200) : IM_COL32(200, 60, 50, 220), 6.0f, 0, 2.0f);
						fg->AddText(ImVec2(fx + 10.0f, fy + 6.0f),
							targetDead ? IM_COL32(150, 150, 150, 255) : IM_COL32(235, 235, 235, 255), targetName.c_str());
						// Distance XZ joueur→cible : affichée SOUS le cadre, centrée et en
						// plus petit (0.8x). Avant elle etait en haut a droite DANS le cadre
						// et chevauchait le nom (souvent long) -> illisible. Orange au-dela
						// de 4 m (coherent avec « Hors de portee »), vert sinon.
						{
							const engine::math::Vec3 playerPosFrame = m_characterController.GetPosition();
							for (const engine::client::UIRemoteEntity& re : uiModel.remoteEntities)
							{
								if (re.entityId != uiModel.targetStats.entityId)
									continue;
								const float dxf = re.positionX - playerPosFrame.x;
								const float dzf = re.positionZ - playerPosFrame.z;
								const float distM = std::sqrt(dxf * dxf + dzf * dzf);
								const std::string distText =
									std::to_string(static_cast<int>(std::lround(distM))) + " m";
								ImFont* distFont = ImGui::GetFont();
								const float distFontSize = ImGui::GetFontSize() * 0.8f;
								const ImVec2 distTs =
									distFont->CalcTextSizeA(distFontSize, FLT_MAX, 0.0f, distText.c_str());
								fg->AddText(distFont, distFontSize,
									ImVec2(fx + (frameW - distTs.x) * 0.5f, fy + frameH + 2.0f),
									(distM > 4.0f) ? IM_COL32(240, 170, 60, 255) : IM_COL32(170, 220, 170, 255),
									distText.c_str());
								break;
							}
						}
						const float barX = fx + 10.0f, barY = fy + 30.0f, barW = frameW - 20.0f, barH = 14.0f;
						const float hpFrac = (uiModel.targetStats.maxHealth > 0u)
							? std::clamp(static_cast<float>(uiModel.targetStats.currentHealth)
								/ static_cast<float>(uiModel.targetStats.maxHealth), 0.0f, 1.0f)
							: 0.0f;
						fg->AddRectFilled(ImVec2(barX, barY), ImVec2(barX + barW, barY + barH), IM_COL32(40, 40, 44, 220), 3.0f);
						fg->AddRectFilled(ImVec2(barX, barY), ImVec2(barX + barW * hpFrac, barY + barH),
							targetDead ? IM_COL32(110, 110, 110, 230) : IM_COL32(190, 50, 40, 230), 3.0f);
						const std::string hpText = std::to_string(uiModel.targetStats.currentHealth) + "/"
							+ std::to_string(uiModel.targetStats.maxHealth);
						const ImVec2 hpTs = ImGui::CalcTextSize(hpText.c_str());
						fg->AddText(ImVec2(barX + (barW - hpTs.x) * 0.5f, barY - 1.0f), IM_COL32(255, 255, 255, 255), hpText.c_str());
						// Validation v12 — indication transitoire « Hors de portee »
						// (cf. armement dans le bloc T ci-dessus).
						if (m_outOfRangeHintSec > 0.0f)
						{
							const char* rangeHint = "Hors de portee  (approchez-vous)";
							const ImVec2 hintTs = ImGui::CalcTextSize(rangeHint);
							fg->AddText(ImVec2(fx + (frameW - hintTs.x) * 0.5f, fy + frameH + 6.0f),
								IM_COL32(240, 170, 60, 255), rangeHint);
						}
					}

					const engine::client::CombatHudState& hudState = m_combatHud.GetState();

					// NB : les jauges PV/ressource du joueur sont rendues en bas-centre
					// par le bloc « Validation v12 » plus bas (uiModel.playerStats) — ne
					// PAS les redessiner ici depuis les bounds du presenter (bas-gauche),
					// qui chevauchent le panneau de chat.

					// --- Log combat HUD (bas-droite) : dernieres lignes formatees par
					// le CombatHudPresenter (suffixes critique/rate inclus).
					if (!hudState.combatLogLines.empty())
					{
						constexpr size_t kHudLogMaxLines = 6u;
						const size_t lineCount = std::min(hudState.combatLogLines.size(), kHudLogMaxLines);
						const float lineH = ImGui::GetTextLineHeight() + 2.0f;
						float ly = dh - 140.0f - lineH * static_cast<float>(lineCount);
						for (size_t li = hudState.combatLogLines.size() - lineCount; li < hudState.combatLogLines.size(); ++li)
						{
							const engine::client::HudCombatLogLine& logLine = hudState.combatLogLines[li];
							const ImU32 col = logLine.incoming ? IM_COL32(255, 140, 120, 230) : IM_COL32(220, 220, 180, 230);
							const ImVec2 lts = ImGui::CalcTextSize(logLine.text.c_str());
							fg->AddText(ImVec2(dw - lts.x - 24.0f, ly), col, logLine.text.c_str());
							ly += lineH;
						}
					}

					// --- Panneau combat avance (J) : DPS meter + log filtrable (M39.4).
					if (m_advancedCombatVisible)
					{
						const engine::client::AdvancedCombatState& acs = m_advancedCombat.GetState();
						ImGui::SetNextWindowPos(ImVec2(dw - 420.0f, 90.0f), ImGuiCond_FirstUseEver);
						ImGui::SetNextWindowSize(ImVec2(380.0f, 420.0f), ImGuiCond_FirstUseEver);
						if (ImGui::Begin("Combat avance", &m_advancedCombatVisible))
						{
							ImGui::Text("Combat : %s  (%.1f s)", acs.inCombat ? "en cours" : "inactif", acs.fightElapsedSec);
							ImGui::Separator();
							ImGui::TextUnformatted("DPS");
							for (const engine::client::DpsMeterEntry& row : acs.dpsMeter)
							{
								ImGui::Text("%u. %s", row.rank, row.displayName.c_str());
								ImGui::SameLine(200.0f);
								ImGui::Text("%.1f dps", row.dps);
								ImGui::ProgressBar(row.barFraction, ImVec2(-1.0f, 6.0f), "");
							}
							// Combat SP4 — menace sur la cible courante (ThreatUpdate).
							if (acs.threatMeterVisible && !acs.threatMeter.empty())
							{
								ImGui::Separator();
								ImGui::TextUnformatted("Menace");
								for (const engine::client::ThreatMeterEntry& threatRow : acs.threatMeter)
								{
									const ImVec4 rowColor = (threatRow.color == engine::client::ThreatColor::Red)
										? ImVec4(0.9f, 0.3f, 0.25f, 1.0f)
										: (threatRow.color == engine::client::ThreatColor::Yellow)
											? ImVec4(0.9f, 0.8f, 0.3f, 1.0f)
											: ImVec4(0.4f, 0.8f, 0.4f, 1.0f);
									ImGui::TextColored(rowColor, "%s", threatRow.displayName.c_str());
									ImGui::SameLine(200.0f);
									ImGui::TextColored(rowColor, "%.0f %%", threatRow.threatPercent);
									ImGui::ProgressBar(threatRow.barFraction, ImVec2(-1.0f, 6.0f), "");
								}
							}
							ImGui::Separator();
							uint32_t filter = acs.activeFilter;
							bool filterChanged = false;
							auto toggleChip = [&filter, &filterChanged](const char* lbl, uint32_t mask)
							{
								bool on = (filter & mask) != 0u;
								if (ImGui::Checkbox(lbl, &on))
								{
									filter = on ? (filter | mask) : (filter & ~mask);
									filterChanged = true;
								}
								ImGui::SameLine();
							};
							toggleChip("Degats", engine::client::CombatLogFilterDamage);
							toggleChip("Soins", engine::client::CombatLogFilterHealing);
							toggleChip("Morts", engine::client::CombatLogFilterDeaths);
							ImGui::NewLine();
							if (filterChanged)
								m_advancedCombat.SetLogFilter(filter);
							ImGui::Separator();
							ImGui::BeginChild("##ln_combat_log", ImVec2(0.0f, 0.0f), true);
							for (const engine::client::CombatLogLine& logLine : acs.visibleLogLines)
								ImGui::TextUnformatted(logLine.text.c_str());
							ImGui::EndChild();
						}
						ImGui::End();
					}

					// --- Combat SP3 : barre d'action (touches 1-4, sorts du profil).
					// Cooldowns affichés localement (cosmétique) ; le serveur revalide
					// kit/cooldown/coût/cible/portée à chaque CastRequest.
					// SP-C — kit effectif : compétences de classe connues (fallback kit profil).
					// BuildEffectiveKit retourne un vecteur valide pour toute la durée du bloc.
					auto BuildEffectiveKit = [&]() -> std::vector<engine::client::SpellDisplay>
					{
						const std::string& classId    = uiModel.classId;
						const std::string& profileId  = uiModel.playerStats.profileId;
						const std::vector<std::string>& knownIds = uiModel.knownSkillIds;
						// Chemin compétences de classe : classId connu + skills connus + catalogue disponible.
						if (!classId.empty() && !knownIds.empty())
						{
							const std::vector<engine::client::ClassSkillDisplay>* classSkills =
								m_classSkillCatalog.GetClassSkills(classId);
							if (classSkills != nullptr)
							{
								std::vector<engine::client::SpellDisplay> kit;
								kit.reserve(knownIds.size());
								for (const std::string& skillId : knownIds)
								{
									for (const engine::client::ClassSkillDisplay& cs : *classSkills)
									{
										if (cs.skillId == skillId)
										{
											kit.push_back(engine::client::ToSpellDisplay(cs, classId));
											break;
										}
									}
								}
								if (!kit.empty())
								{
									return kit;
								}
							}
						}
						// Fallback : kit profil depuis le catalogue de sorts (comportement original).
						if (!profileId.empty())
						{
							const std::vector<engine::client::SpellDisplay>* profileKit =
								m_spellCatalog.FindKit(profileId);
							if (profileKit != nullptr)
							{
								return *profileKit;
							}
						}
						return {};
					};
					const std::vector<engine::client::SpellDisplay> effectiveKit = BuildEffectiveKit();
					if (!effectiveKit.empty())
					{
						const float nowSec = EngineNowSec();
						const float slotSize = 58.0f;
						const float slotGap = 8.0f;
						// Grimoire — layout effectif des 10 slots (slot i → spellId).
						const std::array<std::string, 10> resolvedLayout =
							engine::client::ResolveActionBarLayout(uiModel.playerStats.actionBarLayout, effectiveKit);
						const size_t slotCount = resolvedLayout.size(); // 10
						const float barWidth = slotSize * static_cast<float>(slotCount)
							+ slotGap * static_cast<float>(slotCount - 1);
						const float barX = (dw - barWidth) * 0.5f;
						const float barY = dh - slotSize - 16.0f;
						for (size_t slotIndex = 0; slotIndex < slotCount; ++slotIndex)
						{
							const std::string& slotSpellId = resolvedLayout[slotIndex];
							const engine::client::SpellDisplay* spellPtr =
								slotSpellId.empty() ? nullptr : engine::client::FindSpellInKit(effectiveKit, slotSpellId);
							const float sx0 = barX + static_cast<float>(slotIndex) * (slotSize + slotGap);
							// Touche du slot (remappable) : défaut Digit1..Digit9 puis Digit0.
							const engine::platform::Key slotKey = KeyFromName(
								m_cfg.GetString("controls.keybind.action_slot_" + std::to_string(slotIndex + 1),
									(slotIndex < 9) ? std::string(1, static_cast<char>('1' + slotIndex)) : std::string("0")),
								(slotIndex < 9)
									? static_cast<engine::platform::Key>('1' + static_cast<int>(slotIndex))
									: engine::platform::Key::Digit0);
							// SP3 anniversaires (2026-07-18) — slot occupé par un
							// GÂTEAU (jeton "item:<id>") : case dédiée, la touche
							// envoie un CastRequest avec le jeton (activation
							// serveur : buff groupe/guilde tant que slotté).
							uint32_t cakeItemId = 0u;
							if (engine::anniversary::ParseCakeToken(slotSpellId, cakeItemId))
							{
								fg->AddRectFilled(ImVec2(sx0, barY), ImVec2(sx0 + slotSize, barY + slotSize),
									IM_COL32(58, 34, 52, 220), 6.0f);
								fg->AddRect(ImVec2(sx0, barY), ImVec2(sx0 + slotSize, barY + slotSize),
									IM_COL32(220, 150, 190, 200), 6.0f, 0, 2.0f);
								const engine::items::ItemDefinition* cakeDef = m_itemCatalog.Find(cakeItemId);
								const char* cakeLabel = (cakeDef != nullptr && !cakeDef->name.empty())
									? cakeDef->name.c_str() : "Gateau";
								fg->AddText(ImVec2(sx0 + 4.0f, barY + slotSize * 0.5f - 8.0f),
									IM_COL32(255, 230, 245, 255), cakeLabel);
								const std::string cakeKeyLabel = KeyGlyph(slotKey);
								fg->AddText(ImVec2(sx0 + 4.0f, barY + 2.0f),
									IM_COL32(255, 220, 240, 220), cakeKeyLabel.c_str());
								if (keysAllowed && m_input.WasPressed(slotKey))
								{
									const uint32_t cakeClientId = m_gameplayUdp.ServerClientId();
									if (cakeClientId != 0u)
										(void)m_gameplayUdp.SendCastRequest(cakeClientId, 0ull, slotSpellId);
								}
								continue;
							}
							if (spellPtr == nullptr)
							{
								// Slot vide : case grisée + glyphe touche, pas d'action.
								fg->AddRectFilled(ImVec2(sx0, barY), ImVec2(sx0 + slotSize, barY + slotSize),
									IM_COL32(14, 16, 22, 160), 6.0f);
								fg->AddRect(ImVec2(sx0, barY), ImVec2(sx0 + slotSize, barY + slotSize),
									IM_COL32(70, 70, 76, 160), 6.0f, 0, 2.0f);
								const std::string emptyKeyLabel = KeyGlyph(slotKey);
								fg->AddText(ImVec2(sx0 + 4.0f, barY + 2.0f), IM_COL32(150, 140, 110, 200), emptyKeyLabel.c_str());
								continue;
							}
							const engine::client::SpellDisplay& spell = *spellPtr;
							// Coût payable + cooldown : visuel grisé si indisponible.
							const uint32_t cost = spell.resourceCostPercent
								* uiModel.playerStats.secondaryResourceMax / 100u;
							const bool affordable = uiModel.playerStats.secondaryResourceCurrent >= cost;
							const auto cooldownIt = m_spellCooldownUiUntilSec.find(spell.spellId);
							const float cooldownRemaining = (cooldownIt != m_spellCooldownUiUntilSec.end())
								? std::max(0.0f, cooldownIt->second - nowSec)
								: 0.0f;
							const bool ready = affordable && cooldownRemaining <= 0.0f;
							fg->AddRectFilled(ImVec2(sx0, barY), ImVec2(sx0 + slotSize, barY + slotSize),
								IM_COL32(14, 16, 22, 215), 6.0f);
							fg->AddRect(ImVec2(sx0, barY), ImVec2(sx0 + slotSize, barY + slotSize),
								ready ? IM_COL32(200, 180, 90, 220) : IM_COL32(90, 90, 96, 200), 6.0f, 0, 2.0f);
							// SP-E — icône du sort en fond du slot (compétences de classe).
							// Repli sur le nom texte si pas d'icône (kit profil, fichier absent).
							bool slotHasIcon = false;
							if (!spell.iconPath.empty())
							{
								const uint64_t slotTexId = m_skillIconCache.GetOrLoad(spell.iconPath);
								if (slotTexId != 0)
								{
									const float pad = 3.0f;
									fg->AddImage(static_cast<ImTextureID>(slotTexId),
										ImVec2(sx0 + pad, barY + pad),
										ImVec2(sx0 + slotSize - pad, barY + slotSize - pad));
									slotHasIcon = true;
								}
							}
							// Numéro de touche (haut-gauche) + nom du sort (bas, tronqué) si pas d'icône.
							const std::string keyLabel = KeyGlyph(slotKey);
							fg->AddText(ImVec2(sx0 + 4.0f, barY + 2.0f), IM_COL32(255, 230, 150, 230), keyLabel.c_str());
							if (!slotHasIcon)
							{
								std::string spellLabel = spell.name.substr(0, 9);
								fg->AddText(ImVec2(sx0 + 4.0f, barY + slotSize - 18.0f),
									ready ? IM_COL32(225, 225, 225, 255) : IM_COL32(140, 140, 140, 255),
									spellLabel.c_str());
							}
							// Voile + décompte pendant le cooldown affiché.
							if (cooldownRemaining > 0.0f)
							{
								const float fillFraction = (spell.cooldownMs > 0u)
									? std::clamp(cooldownRemaining / (static_cast<float>(spell.cooldownMs) / 1000.0f), 0.0f, 1.0f)
									: 0.0f;
								fg->AddRectFilled(
									ImVec2(sx0, barY + slotSize * (1.0f - fillFraction)),
									ImVec2(sx0 + slotSize, barY + slotSize),
									IM_COL32(0, 0, 0, 150), 6.0f);
								char cooldownText[16];
								std::snprintf(cooldownText, sizeof(cooldownText), "%.0f", cooldownRemaining);
								const ImVec2 cdTs = ImGui::CalcTextSize(cooldownText);
								fg->AddText(ImVec2(sx0 + (slotSize - cdTs.x) * 0.5f, barY + (slotSize - cdTs.y) * 0.5f),
									IM_COL32(255, 255, 255, 255), cooldownText);
							}
							// Touche remappable du slot : envoi du CastRequest.
							if (keysAllowed && m_input.WasPressed(slotKey))
							{
								const bool targetOk = !spell.needsEnemyTarget
									|| (uiModel.targetStats.hasTarget
										&& (uiModel.targetStats.stateFlags & 1u) == 0u);
								const uint32_t gameplayClientId = m_gameplayUdp.ServerClientId();
								if (targetOk && ready && gameplayClientId != 0u)
								{
									// Groupes SP1 — sorts d'allié : envoie l'allié sélectionné au
									// cadre de groupe (0 = soi, défaut serveur).
									uint64_t castTarget = 0ull;
									if (spell.needsEnemyTarget)
									{
										castTarget = uiModel.targetStats.entityId;
									}
									else if (spell.targetsAlly)
									{
										castTarget = m_selectedAllyEntityId;
									}
									(void)m_gameplayUdp.SendCastRequest(gameplayClientId, castTarget, spell.spellId);
									if (spell.cooldownMs > 0u)
									{
										m_spellCooldownUiUntilSec[spell.spellId] =
											nowSec + static_cast<float>(spell.cooldownMs) / 1000.0f;
									}
								}
							}
						}

						// Ceinture v2 (2026-07-20) — barre d'objets ACTIFS à taille
						// DYNAMIQUE (4 par défaut, 12 max — capacité autoritaire
						// = ceinture équipée en slot Waist, reçue via kind 100).
						// Fenêtre ImGui invisible : chaque case est un vrai
						// widget → drag & drop natif (sac → case, case → case),
						// tooltips, clic droit pour vider. Activation : Maj+1..9
						// ou clic complet (relâché sans drag) sur la case.
						{
							const std::vector<std::string>& beltLayout = uiModel.playerStats.beltLayout;
							const size_t beltCount = beltLayout.size();
							if (beltCount > 0u)
							{
								const float beltSlotSz = 48.0f;
								const float beltGap = 6.0f;
								// Retour de test 2026-07-20 — disposition : la
								// rangée BASSE porte les slots 1..6 ; les slots
								// 7..12 s'empilent sur une 2e rangée AU-DESSUS,
								// alignés colonne par colonne (7 au-dessus du 1,
								// 8 au-dessus du 2, etc.).
								const size_t perRow = std::min<size_t>(beltCount, 6u);
								const size_t rowCount = (beltCount + 5u) / 6u;
								const float beltW = static_cast<float>(perRow) * beltSlotSz
									+ static_cast<float>(perRow - 1u) * beltGap;
								const float beltH = static_cast<float>(rowCount) * beltSlotSz
									+ static_cast<float>(rowCount - 1u) * beltGap;
								// Retour de test 2026-07-20 — séparation nette
								// avec les icônes de pouvoir : écart élargi +
								// trait séparateur vertical (dessiné plus bas).
								const float beltX = barX + barWidth + 56.0f;
								const float beltY = dh - beltH - 16.0f; // bord HAUT du bloc
								const bool shiftHeld = m_input.IsDown(engine::platform::Key::Shift);
								const uint32_t beltClientId = m_gameplayUdp.ServerClientId();

								ImGui::SetNextWindowPos(ImVec2(beltX - 34.0f, beltY - 20.0f));
								ImGui::SetNextWindowSize(ImVec2(beltW + 40.0f, beltH + 26.0f));
								ImGui::Begin("##belt_bar", nullptr,
									ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground
									| ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar
									| ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoFocusOnAppearing
									| ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoSavedSettings);
								ImDrawList* bdl = ImGui::GetWindowDrawList();
								char beltTitle[32];
								std::snprintf(beltTitle, sizeof(beltTitle), "Ceinture  %d/%d",
									static_cast<int>(beltCount),
									static_cast<int>(engine::items::kBeltSlotsMax));
								bdl->AddText(ImVec2(beltX, beltY - 18.0f),
									IM_COL32(190, 215, 195, 200), beltTitle);
								// Trait séparateur vertical entre la barre
								// d'action (icônes de pouvoir) et la ceinture.
								bdl->AddLine(ImVec2(beltX - 26.0f, dh - beltSlotSz - 20.0f),
									ImVec2(beltX - 26.0f, dh - 12.0f),
									IM_COL32(150, 170, 155, 120), 2.0f);

								// Copie de travail : mutée par drop/échange/vidage,
								// envoyée en une fois si changement.
								std::vector<std::string> newBelt(beltLayout.begin(), beltLayout.end());
								bool beltChanged = false;

								for (size_t bi = 0; bi < beltCount; ++bi)
								{
									// row 0 = rangée BASSE (slots 1..6) ; les rangées
									// suivantes montent AU-DESSUS, mêmes colonnes.
									const size_t row = bi / 6u;
									const size_t colIdx = bi % 6u;
									const float bx0 = beltX + static_cast<float>(colIdx) * (beltSlotSz + beltGap);
									const float by0 = beltY
										+ static_cast<float>(rowCount - 1u - row) * (beltSlotSz + beltGap);
									const std::string& beltTok = beltLayout[bi];
									uint32_t beltItemId = 0u;
									const bool occupied = engine::anniversary::ParseItemToken(beltTok, beltItemId);
									const engine::items::ItemDefinition* bdef =
										occupied ? m_itemCatalog.Find(beltItemId) : nullptr;

									ImGui::SetCursorScreenPos(ImVec2(bx0, by0));
									char beltBtnId[16];
									std::snprintf(beltBtnId, sizeof(beltBtnId), "##belt%d", static_cast<int>(bi));
									ImGui::InvisibleButton(beltBtnId, ImVec2(beltSlotSz, beltSlotSz));
									const bool hovered = ImGui::IsItemHovered();

									// Quantité restante en sac (somme des piles).
									uint32_t beltQty = 0u;
									if (occupied)
									{
										for (const engine::client::InventorySlotState& is : m_invUi.GetState().slots)
											if (is.occupied && is.itemId == beltItemId)
												beltQty += is.quantity;
									}

									// Visuel : fond, bordure (survol = doré), raccourci,
									// initiale de l'objet en grand + quantité en bas-droite.
									const ImU32 fillCol = occupied
										? IM_COL32(32, 46, 36, 235) : IM_COL32(14, 16, 22, 170);
									const ImU32 borderCol = hovered
										? IM_COL32(235, 205, 120, 255)
										: IM_COL32(120, 170, 130, occupied ? 220 : 110);
									bdl->AddRectFilled(ImVec2(bx0, by0),
										ImVec2(bx0 + beltSlotSz, by0 + beltSlotSz), fillCol, 6.0f);
									bdl->AddRect(ImVec2(bx0, by0),
										ImVec2(bx0 + beltSlotSz, by0 + beltSlotSz), borderCol, 6.0f, 0,
										hovered ? 2.5f : 2.0f);
									if (bi < 9u)
									{
										char beltKeyLabel[12];
										std::snprintf(beltKeyLabel, sizeof(beltKeyLabel), "M+%d",
											static_cast<int>(bi) + 1);
										bdl->AddText(ImVec2(bx0 + 3.0f, by0 + 2.0f),
											IM_COL32(200, 230, 205, 190), beltKeyLabel);
									}
									if (occupied)
									{
										// Initiale (2 lettres) au centre — lisible sans atlas d'icônes.
										const char* bname = (bdef != nullptr && !bdef->name.empty())
											? bdef->name.c_str() : "?";
										char initials[3] = { bname[0], bname[1] != '\0' ? bname[1] : ' ', '\0' };
										const ImVec2 initSz = ImGui::CalcTextSize(initials);
										bdl->AddText(ImVec2(bx0 + (beltSlotSz - initSz.x) * 0.5f,
											by0 + (beltSlotSz - initSz.y) * 0.5f),
											IM_COL32(240, 250, 240, 255), initials);
										char beltQtyTxt[12];
										std::snprintf(beltQtyTxt, sizeof(beltQtyTxt), "%u", beltQty);
										const ImVec2 qtySz = ImGui::CalcTextSize(beltQtyTxt);
										bdl->AddText(ImVec2(bx0 + beltSlotSz - qtySz.x - 3.0f,
											by0 + beltSlotSz - qtySz.y - 2.0f),
											beltQty > 0u ? IM_COL32(255, 240, 190, 255)
											             : IM_COL32(255, 120, 110, 255),
											beltQtyTxt);
									}

									// Source de drag (réorganisation case → case).
									if (occupied && ImGui::BeginDragDropSource(ImGuiDragDropFlags_None))
									{
										int fromIndex = static_cast<int>(bi);
										ImGui::SetDragDropPayload("LN_BELT_MOVE", &fromIndex, sizeof(int));
										ImGui::TextUnformatted((bdef != nullptr && !bdef->name.empty())
											? bdef->name.c_str() : "Objet");
										ImGui::EndDragDropSource();
									}
									// Cible de drop : échange interne OU objet du sac.
									if (ImGui::BeginDragDropTarget())
									{
										if (const ImGuiPayload* mv = ImGui::AcceptDragDropPayload("LN_BELT_MOVE"))
										{
											if (mv->DataSize == static_cast<int>(sizeof(int)))
											{
												const int from = *static_cast<const int*>(mv->Data);
												if (from >= 0 && from < static_cast<int>(newBelt.size())
													&& from != static_cast<int>(bi))
												{
													std::swap(newBelt[static_cast<size_t>(from)], newBelt[bi]);
													beltChanged = true;
												}
											}
										}
										if (const ImGuiPayload* it = ImGui::AcceptDragDropPayload("LN_EQUIP_ITEM"))
										{
											if (it->DataSize == static_cast<int>(sizeof(uint32_t)))
											{
												const uint32_t droppedId = *static_cast<const uint32_t*>(it->Data);
												const std::string droppedTok =
													engine::anniversary::MakeItemToken(droppedId);
												bool already = false;
												for (const std::string& s : newBelt)
													if (s == droppedTok) { already = true; break; }
												if (!already)
												{
													newBelt[bi] = droppedTok;
													beltChanged = true;
												}
											}
										}
										ImGui::EndDragDropTarget();
									}
									// Clic droit : vider la case.
									if (occupied && hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
									{
										newBelt[bi].clear();
										beltChanged = true;
									}
									// Tooltip riche.
									if (hovered && occupied)
									{
										ImGui::BeginTooltip();
										ImGui::TextUnformatted((bdef != nullptr && !bdef->name.empty())
											? bdef->name.c_str() : "Objet");
										if (bdef != nullptr && !bdef->description.empty())
											ImGui::TextDisabled("%s", bdef->description.c_str());
										ImGui::Separator();
										if (bi < 9u)
											ImGui::TextDisabled("Maj+%d ou clic : utiliser", static_cast<int>(bi) + 1);
										else
											ImGui::TextDisabled("Clic : utiliser");
										ImGui::TextDisabled("Clic droit : retirer  |  Glisser : deplacer");
										ImGui::TextDisabled("En sac : %u", beltQty);
										ImGui::EndTooltip();
									}

									// Activation : Maj+1..9, ou clic COMPLET (press +
									// release dans la case, sans drag en cours — permet
									// de glisser sans consommer l'objet).
									const bool beltKeyPressed = keysAllowed && shiftHeld && bi < 9u
										&& m_input.WasPressed(static_cast<engine::platform::Key>('1' + static_cast<int>(bi)));
									const bool beltClickCompleted = ImGui::IsItemDeactivated()
										&& hovered && ImGui::GetDragDropPayload() == nullptr;
									if (occupied && (beltKeyPressed || beltClickCompleted)
										&& beltClientId != 0u)
									{
										(void)m_gameplayUdp.SendCastRequest(beltClientId, 0ull, beltTok);
									}
								}
								ImGui::End();

								if (beltChanged && beltClientId != 0u)
								{
									(void)m_gameplayUdp.SendSetBeltLayout(beltClientId, newBelt);
								}
							}
						}

						// --- Barre de cast (au-dessus de la barre d'action) pilotée
						// par CastBarUpdate ; progression calculée localement.
						if (uiModel.castBar.active && uiModel.castBar.durationMs > 0u)
						{
							const uint64_t nowNs = static_cast<uint64_t>(
								std::chrono::duration_cast<std::chrono::nanoseconds>(
									std::chrono::steady_clock::now().time_since_epoch()).count());
							const float elapsedSec = static_cast<float>(nowNs - uiModel.castBar.startedAtNs) / 1.0e9f;
							const float durationSec = static_cast<float>(uiModel.castBar.durationMs) / 1000.0f;
							const float progress = std::clamp(elapsedSec / durationSec, 0.0f, 1.0f);
							const float castBarWidth = 260.0f;
							const float castBarHeight = 18.0f;
							const float cbx = (dw - castBarWidth) * 0.5f;
							const float cby = barY - castBarHeight - 10.0f;
							fg->AddRectFilled(ImVec2(cbx, cby), ImVec2(cbx + castBarWidth, cby + castBarHeight),
								IM_COL32(20, 22, 28, 220), 4.0f);
							fg->AddRectFilled(ImVec2(cbx, cby), ImVec2(cbx + castBarWidth * progress, cby + castBarHeight),
								IM_COL32(120, 170, 230, 230), 4.0f);
							const std::string castLabel = m_spellCatalog.ResolveSpellName(uiModel.castBar.spellId);
							const ImVec2 castTs = ImGui::CalcTextSize(castLabel.c_str());
							fg->AddText(ImVec2(cbx + (castBarWidth - castTs.x) * 0.5f, cby + 1.0f),
								IM_COL32(255, 255, 255, 255), castLabel.c_str());
						}
					}

					// --- Combat SP3 : BuffBar (M31.2 enfin câblée) — auras du joueur
					// (au-dessus de la barre d'action) et de la cible (sous le cadre).
					{
						const uint64_t nowNs = static_cast<uint64_t>(
							std::chrono::duration_cast<std::chrono::nanoseconds>(
								std::chrono::steady_clock::now().time_since_epoch()).count());
						// Construit la liste StatusEffect d'une entité depuis le modèle
						// (les timers décrémentent localement entre deux AuraUpdate).
						auto buildEffects = [&uiModel, nowNs](engine::server::EntityId entityId,
							std::list<engine::gameplay::StatusEffect>& outEffects)
						{
							const auto auraIt = uiModel.entityAuras.find(entityId);
							if (auraIt == uiModel.entityAuras.end())
							{
								return;
							}
							for (const engine::client::UIAuraEntry& aura : auraIt->second)
							{
								engine::gameplay::StatusEffect effect{};
								effect.effectId = aura.spellId;
								effect.targetId = entityId;
								effect.stacks = aura.stacks;
								// Mapping wire (SpellEffectType serveur) → type client.
								switch (aura.effectType)
								{
								case 1u: effect.type = engine::gameplay::StatusEffectType::DoT; break;
								case 3u: effect.type = engine::gameplay::StatusEffectType::HoT; break;
								case 5u: effect.type = engine::gameplay::StatusEffectType::Debuff; break;
								case 7u: effect.type = engine::gameplay::StatusEffectType::Slow; break;
								default: effect.type = engine::gameplay::StatusEffectType::Buff; break;
								}
								effect.startTimeNs = aura.receivedAtNs;
								effect.durationSeconds = static_cast<float>(aura.remainingMs) / 1000.0f;
								effect.expireTimeNs = aura.receivedAtNs
									+ static_cast<uint64_t>(aura.remainingMs) * 1000000ull;
								outEffects.push_back(std::move(effect));
							}
						};
						std::list<engine::gameplay::StatusEffect> playerEffects;
						buildEffects(static_cast<engine::server::EntityId>(uiModel.playerStats.playerEntityId), playerEffects);
						m_buffBar.UpdatePlayer(playerEffects.empty() ? nullptr : &playerEffects, nowNs);
						std::list<engine::gameplay::StatusEffect> targetEffects;
						if (uiModel.targetStats.hasTarget)
						{
							buildEffects(uiModel.targetStats.entityId, targetEffects);
						}
						m_buffBar.UpdateTarget(targetEffects.empty() ? nullptr : &targetEffects, nowNs);

						// Rendu en pastilles texte (icônes graphiques = chantier atlas).
						auto drawBar = [this, fg](const engine::client::BuffBarWidget& bar, float x, float y, bool harmful)
						{
							if (!bar.visible)
							{
								return;
							}
							float cursorX = x;
							for (const engine::client::BuffIconWidget& icon : bar.icons)
							{
								if (!icon.visible)
								{
									continue;
								}
								std::string chip = m_spellCatalog.ResolveSpellName(icon.effectId);
								if (!icon.isPermanent && icon.remainingSeconds > 0.0f)
								{
									char timerText[16];
									std::snprintf(timerText, sizeof(timerText), " %.0fs", icon.remainingSeconds);
									chip += timerText;
								}
								if (icon.stacks > 1u)
								{
									chip += " x" + std::to_string(icon.stacks);
								}
								const ImVec2 chipTs = ImGui::CalcTextSize(chip.c_str());
								fg->AddRectFilled(ImVec2(cursorX, y), ImVec2(cursorX + chipTs.x + 10.0f, y + chipTs.y + 6.0f),
									harmful ? IM_COL32(70, 20, 20, 210) : IM_COL32(20, 50, 25, 210), 4.0f);
								fg->AddText(ImVec2(cursorX + 5.0f, y + 3.0f), IM_COL32(230, 230, 230, 255), chip.c_str());
								cursorX += chipTs.x + 16.0f;
							}
						};
						const engine::client::BuffBarState& playerBuffState = m_buffBar.GetPlayerState();
						drawBar(playerBuffState.buffBar, 18.0f, dh - 120.0f, false);
						drawBar(playerBuffState.debuffBar, 18.0f, dh - 92.0f, true);
						if (uiModel.targetStats.hasTarget)
						{
							const engine::client::BuffBarState& targetBuffState = m_buffBar.GetTargetState();
							// Sous le cadre cible (haut-centre, cf. bloc SP2 : fy 56 + 54).
							drawBar(targetBuffState.buffBar, (dw - 260.0f) * 0.5f, 118.0f, false);
							drawBar(targetBuffState.debuffBar, (dw - 260.0f) * 0.5f, 144.0f, true);
						}

						// --- Combat SP4 : threat meter — alimente le présentateur pour la
						// cible courante (rebuild complet, ≤ 5 entrées : coût négligeable).
						if (uiModel.targetStats.hasTarget)
						{
							m_advancedCombat.ClearThreat(uiModel.targetStats.entityId);
							const auto threatIt = uiModel.threatByMob.find(uiModel.targetStats.entityId);
							if (threatIt != uiModel.threatByMob.end())
							{
								for (const engine::client::UIThreatEntry& threatEntry : threatIt->second)
								{
									std::string threatName = "Joueur";
									if (threatEntry.playerEntityId == uiModel.playerStats.playerEntityId)
									{
										threatName = "Vous";
									}
									else
									{
										for (const engine::client::UIRemoteEntity& re : uiModel.remoteEntities)
										{
											if (re.entityId == threatEntry.playerEntityId)
											{
												threatName = !re.displayName.empty()
													? re.displayName
													: ("P" + std::to_string(re.playerClientId));
												break;
											}
										}
									}
									m_advancedCombat.UpdateThreat(uiModel.targetStats.entityId,
										threatEntry.playerEntityId, threatName, threatEntry.threatValue);
								}
							}
						}

						// --- Combat SP4 : FX d'auras — sync de la couche données
						// (AuraFXSystem, M39.4 enfin câblé) puis halo écran-espace coloré
						// aux pieds de chaque entité sous aura (couleur ResolveAuraVisuals).
						{
							const engine::math::Vec3 localFxPos = m_characterController.GetPosition();
							std::list<engine::gameplay::StatusEffect> fxEffects;
							for (const auto& auraPair : uiModel.entityAuras)
							{
								const engine::server::EntityId fxEntityId = auraPair.first;
								float fxX = localFxPos.x, fxY = localFxPos.y, fxZ = localFxPos.z;
								if (fxEntityId != uiModel.playerStats.playerEntityId)
								{
									const auto smoothedIt = m_remoteSmoothed.find(fxEntityId);
									if (smoothedIt == m_remoteSmoothed.end() || !smoothedIt->second.valid)
									{
										continue; // entité sans position connue : pas de FX ce frame.
									}
									fxX = smoothedIt->second.x;
									fxY = smoothedIt->second.y;
									fxZ = smoothedIt->second.z;
									// Combat SP1 fix — un halo de mob doit suivre le snap visuel
									// au sol (sinon l'anneau se projette sous le terrain). On
									// retrouve l'entité pour distinguer joueur (Y fiable) / mob.
									for (const engine::client::UIRemoteEntity& fxRe : uiModel.remoteEntities)
									{
										if (fxRe.entityId != fxEntityId)
											continue;
										fxY = ResolveRemoteDisplayCenterY(
											fxRe.playerClientId != 0u, fxY, fxX, fxZ);
										break;
									}
								}
								fxEffects.clear();
								buildEffects(fxEntityId, fxEffects);
								m_auraFx.Sync(fxEntityId, fxEffects.empty() ? nullptr : &fxEffects, fxX, fxY, fxZ);
							}
							// Purge des FX d'entités qui n'ont plus aucune aura répliquée.
							std::vector<uint64_t> staleFxEntities;
							for (const engine::client::ActiveAura& fxAura : m_auraFx.GetAuras())
							{
								if (uiModel.entityAuras.find(fxAura.entityId) == uiModel.entityAuras.end())
								{
									staleFxEntities.push_back(fxAura.entityId);
								}
							}
							for (const uint64_t staleEntityId : staleFxEntities)
							{
								m_auraFx.RemoveEntity(staleEntityId);
							}
							// Rendu : un anneau par aura, aux pieds de l'entité.
							for (const engine::client::ActiveAura& fxAura : m_auraFx.GetAuras())
							{
								if (!fxAura.alive)
								{
									continue;
								}
								float haloX = 0.0f, haloY = 0.0f;
								if (!WorldToScreenPx(out.viewProjMatrix.m,
									fxAura.positionX, fxAura.positionY - 0.85f, fxAura.positionZ,
									ivw, ivh, haloX, haloY))
								{
									continue;
								}
								const ImU32 haloColor = IM_COL32(
									static_cast<int>(fxAura.glowColor.r * 255.0f),
									static_cast<int>(fxAura.glowColor.g * 255.0f),
									static_cast<int>(fxAura.glowColor.b * 255.0f),
									190);
								fg->AddCircle(ImVec2(haloX, haloY), 20.0f, haloColor, 24, 2.5f);
							}
						}
					}

					// --- Groupes SP1 : cadres de groupe (PartyHudPresenter M32.2 enfin
					// câblé) + ciblage allié au clic (consommé par les sorts SingleAlly).
					if (uiModel.inParty)
					{
						const engine::client::PartyHudState& partyState = m_partyHud.GetState();
						// Mapping cadre → membre : même ordre que le présentateur
						// (slot 0 = joueur local, puis l'ordre de partyMembers).
						std::vector<uint32_t> frameClientIds;
						frameClientIds.push_back(uiModel.playerStats.clientId);
						for (const engine::client::UIPartyMemberEntry& member : uiModel.partyMembers)
						{
							if (member.clientId != uiModel.playerStats.clientId)
							{
								frameClientIds.push_back(member.clientId);
							}
						}
						const bool clickThisFrame = mouseAllowed
							&& m_input.WasMousePressed(engine::platform::MouseButton::Left);
						const ImVec2 mousePosParty = ImGui::GetIO().MousePos;
						for (size_t frameIndex = 0; frameIndex < engine::client::kMaxPartyFrames; ++frameIndex)
						{
							const engine::client::PartyMemberFrame& frame = partyState.frames[frameIndex];
							if (!frame.visible)
							{
								continue;
							}
							const engine::client::HudRect& fb = frame.frameBounds;
							// entityId == clientId pour les joueurs (invariant HandleHello).
							const uint64_t memberEntityId = (frameIndex < frameClientIds.size())
								? static_cast<uint64_t>(frameClientIds[frameIndex])
								: 0ull;
							const bool isSelectedAlly = (memberEntityId != 0ull
								&& memberEntityId == m_selectedAllyEntityId);
							fg->AddRectFilled(ImVec2(fb.x, fb.y), ImVec2(fb.x + fb.width, fb.y + fb.height),
								IM_COL32(12, 14, 20, 205), 5.0f);
							fg->AddRect(ImVec2(fb.x, fb.y), ImVec2(fb.x + fb.width, fb.y + fb.height),
								isSelectedAlly ? IM_COL32(220, 190, 90, 240) : IM_COL32(80, 84, 96, 200),
								5.0f, 0, isSelectedAlly ? 2.5f : 1.5f);
							std::string memberLabel = frame.displayName;
							if (frame.isLeader)
							{
								memberLabel += "  [C]";
							}
							fg->AddText(ImVec2(fb.x + 6.0f, fb.y + 4.0f), IM_COL32(230, 230, 230, 255), memberLabel.c_str());
							// Barres PV / mana depuis les widgets du présentateur.
							auto drawMemberBar = [fg](const engine::client::HudBarWidget& bar, ImU32 fillColor)
							{
								if (!bar.visible || bar.bounds.width <= 0.0f)
								{
									return;
								}
								const float fillFraction = (bar.maxValue > 0u)
									? std::clamp(static_cast<float>(bar.currentValue) / static_cast<float>(bar.maxValue), 0.0f, 1.0f)
									: 0.0f;
								fg->AddRectFilled(ImVec2(bar.bounds.x, bar.bounds.y),
									ImVec2(bar.bounds.x + bar.bounds.width, bar.bounds.y + bar.bounds.height),
									IM_COL32(34, 36, 42, 220), 2.0f);
								fg->AddRectFilled(ImVec2(bar.bounds.x, bar.bounds.y),
									ImVec2(bar.bounds.x + bar.bounds.width * fillFraction, bar.bounds.y + bar.bounds.height),
									fillColor, 2.0f);
							};
							drawMemberBar(frame.hpBar, IM_COL32(70, 170, 80, 235));
							drawMemberBar(frame.manaBar, IM_COL32(70, 110, 200, 235));
							// Clic sur le cadre = sélection/désélection de l'allié (soins).
							if (clickThisFrame && memberEntityId != 0ull
								&& mousePosParty.x >= fb.x && mousePosParty.x <= fb.x + fb.width
								&& mousePosParty.y >= fb.y && mousePosParty.y <= fb.y + fb.height)
							{
								m_selectedAllyEntityId = isSelectedAlly ? 0ull : memberEntityId;
								LOG_INFO(Core, "[Engine] Allié sélectionné (entity_id={})", m_selectedAllyEntityId);
							}
						}
						// Label du mode de loot sous les cadres.
						if (partyState.visibleCount > 0u)
						{
							const engine::client::HudRect& lastFrame =
								partyState.frames[partyState.visibleCount - 1u].frameBounds;
							const std::string lootLabel = "Loot: " + partyState.lootModeLabel;
							fg->AddText(ImVec2(lastFrame.x, lastFrame.y + lastFrame.height + 6.0f),
								IM_COL32(190, 190, 190, 220), lootLabel.c_str());
						}
					}
					else if (m_selectedAllyEntityId != 0ull)
					{
						// Sorti du groupe : plus d'allié sélectionnable.
						m_selectedAllyEntityId = 0ull;
					}

					// --- Groupes SP1 : popup d'invitation (Accepter / Refuser).
					// Modale souris : affichée même pendant un cast ou un menu.
					if (uiModel.partyInvite.pending)
					{
						const float inviteW = 380.0f;
						const float inviteH = 140.0f;
						ImGui::SetNextWindowPos(ImVec2((dw - inviteW) * 0.5f, dh * 0.22f), ImGuiCond_Always);
						ImGui::SetNextWindowSize(ImVec2(inviteW, inviteH), ImGuiCond_Always);
						ImGui::SetNextWindowBgAlpha(0.96f);
						ImGui::Begin("##ln_party_invite", nullptr,
							ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
							| ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav);
						const std::string inviteText = uiModel.partyInvite.inviterName + " vous invite dans un groupe";
						const float inviteTextW = ImGui::CalcTextSize(inviteText.c_str()).x;
						ImGui::SetCursorPosX((inviteW - inviteTextW) * 0.5f);
						ImGui::TextUnformatted(inviteText.c_str());
						ImGui::Separator();
						ImGui::Spacing();
						const uint32_t inviteClientId = m_gameplayUdp.ServerClientId();
						ImGui::SetCursorPosX((inviteW - 2.0f * 150.0f - 12.0f) * 0.5f);
						if (ImGui::Button("Accepter", ImVec2(150.0f, 36.0f)) && inviteClientId != 0u)
						{
							(void)m_gameplayUdp.SendPartyAccept(inviteClientId);
							m_uiModelBinding.ClearPartyInvite();
						}
						ImGui::SameLine();
						if (ImGui::Button("Refuser", ImVec2(150.0f, 36.0f)) && inviteClientId != 0u)
						{
							(void)m_gameplayUdp.SendPartyDecline(inviteClientId);
							m_uiModelBinding.ClearPartyInvite();
						}
						ImGui::End();
					}


					// (Le cercle de sélection provisoire — anneau + cône de vision en
					// ImGui foreground — a été remplacé par TargetReticleSystem :
					// decal orienté au sol, mis à jour chaque frame côté Update.)

					// Marqueurs de reapparition (cimetiere/auberge) : le rendu placeholder
					// (label "Auberge"/"Cimetiere" + anneau au sol sur la foreground draw
					// list) a ete RETIRE. Ces lieux sont desormais materialises par du DECOR
					// physique pose dans world.scenery (cf. config.json, positions issues de
					// respawn/respawn_points.txt : cimetiere ~120,120 ; auberge ~88,100).
					// m_respawnMarkers reste charge par LoadRespawnMarkers() (donnees) mais
					// n'est plus dessine cote client.

					// --- Validation v12 : ramassage du butin — touche F sur le sac
					// le plus proche (~3 m, le serveur revalide). L'InventoryDelta
					// de retour est déjà routé (UIModelBinding) : les objets
					// apparaissent dans l'inventaire, le serveur despawne le sac.
					{
						const engine::math::Vec3 playerPosLoot = m_characterController.GetPosition();
						uint64_t nearestBagId = 0;
						float nearestBagDistSq = 3.0f * 3.0f;
						for (const engine::client::UIRemoteEntity& re : uiModel.remoteEntities)
						{
							if (re.playerClientId != 0u || re.archetypeId != 0u)
								continue; // seuls les sacs de butin ont les deux ids à 0.
							const float dxb = re.positionX - playerPosLoot.x;
							const float dzb = re.positionZ - playerPosLoot.z;
							const float distSqB = dxb * dxb + dzb * dzb;
							if (distSqB < nearestBagDistSq)
							{
								nearestBagDistSq = distSqB;
								nearestBagId = re.entityId;
							}
						}
						if (keysAllowed && nearestBagId != 0ull
							&& m_input.WasPressed(engine::platform::Key::F))
						{
							const uint32_t lootClientId = m_gameplayUdp.ServerClientId();
							if (lootClientId != 0u)
								(void)m_gameplayUdp.SendPickupRequest(lootClientId, nearestBagId);
						}
					}

					// --- Métiers SP1 : récolte — touche E sur le node disponible le
					// plus proche (~5 m) ; les interactibles locaux gardent la priorité.
					{
						const engine::math::Vec3 playerPosHarvest = m_characterController.GetPosition();
						uint64_t nearestNodeId = 0;
						float nearestNodeDistSq = 25.0f; // 5 m de portée de récolte V1.
						for (const engine::client::UIRemoteEntity& re : uiModel.remoteEntities)
						{
							if (re.archetypeId < engine::server::kGatheringNodeArchetypeBase)
								continue;
							if ((re.stateFlags & 1u) != 0u)
								continue; // épuisé
							const float dxn = re.positionX - playerPosHarvest.x;
							const float dzn = re.positionZ - playerPosHarvest.z;
							const float distSqN = dxn * dxn + dzn * dzn;
							if (distSqN < nearestNodeDistSq)
							{
								nearestNodeDistSq = distSqN;
								nearestNodeId = re.entityId;
							}
						}
						if (keysAllowed && nearestNodeId != 0ull
							&& m_interactableInRange < 0
							&& !uiModel.harvest.inProgress
							&& m_input.WasPressed(engine::platform::Key::E))
						{
							const uint32_t harvestClientId = m_gameplayUdp.ServerClientId();
							if (harvestClientId != 0u)
								(void)m_gameplayUdp.SendHarvestRequest(harvestClientId, nearestNodeId);
						}
						// Barre de progression de récolte (présentateur M36.1).
						const engine::client::HarvestCastBarState& harvestState = m_harvestBar.GetState();
						if (harvestState.visible)
						{
							fg->AddRectFilled(ImVec2(harvestState.barX, harvestState.barY),
								ImVec2(harvestState.barX + harvestState.barWidth,
									harvestState.barY + harvestState.barHeight),
								IM_COL32(20, 22, 28, 220), 4.0f);
							fg->AddRectFilled(ImVec2(harvestState.barX, harvestState.barY),
								ImVec2(harvestState.barX + harvestState.barWidth
										* std::clamp(harvestState.fillFraction, 0.0f, 1.0f),
									harvestState.barY + harvestState.barHeight),
								IM_COL32(120, 200, 120, 230), 4.0f);
							fg->AddText(ImVec2(harvestState.barX, harvestState.barY - 18.0f),
								IM_COL32(230, 230, 230, 255), harvestState.label.c_str());
						}
					}

					// --- Validation v12 : fenêtre de butin AUTOMATIQUE. S'ouvre seule
					// dès qu'un LootNotify arrive (mort d'un mob avec loot, objets déjà
					// crédités à l'inventaire par le serveur) ; les morts suivantes
					// ABONDENT la même fenêtre (cumul par objet) ; « Fermer » la vide.
					if (uiModel.lootWindow.visible)
					{
						ImGui::SetNextWindowPos(ImVec2(dw - 320.0f, dh * 0.5f - 120.0f), ImGuiCond_FirstUseEver);
						ImGui::SetNextWindowSize(ImVec2(280.0f, 0.0f), ImGuiCond_Always);
						bool lootOpen = true;
						if (ImGui::Begin("Butin", &lootOpen,
							ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse))
						{
							for (const engine::server::ItemStack& lootEntry : uiModel.lootWindow.entries)
							{
								ImGui::TextUnformatted(
									m_invUi.ResolveItemLabel(lootEntry.itemId, lootEntry.quantity).c_str());
							}
							ImGui::Separator();
							if (ImGui::Button("Fermer", ImVec2(-1.0f, 26.0f)))
							{
								lootOpen = false;
							}
						}
						ImGui::End();
						if (!lootOpen)
						{
							m_uiModelBinding.CloseLootWindow();
						}
					}

					// --- Métiers SP1 : panneau d'artisanat (touche K, M36.2/M36.3).
					if (keysAllowed && m_input.WasPressed(engine::platform::Key::K))
					{
						m_craftingVisible = !m_craftingVisible;
						// Première ouverture : charge la liste du premier métier connu.
						if (m_craftingVisible && !uiModel.crafting.professions.empty()
							&& uiModel.crafting.recipes.empty())
						{
							const uint32_t craftClientId = m_gameplayUdp.ServerClientId();
							if (craftClientId != 0u)
								(void)m_gameplayUdp.SendCraftRecipeListRequest(craftClientId,
									uiModel.crafting.professions.front().professionKey);
						}
					}
					if (m_craftingVisible)
					{
						ImGui::SetNextWindowPos(ImVec2(dw * 0.5f - 230.0f, 120.0f), ImGuiCond_FirstUseEver);
						ImGui::SetNextWindowSize(ImVec2(460.0f, 430.0f), ImGuiCond_FirstUseEver);
						if (ImGui::Begin("Artisanat", &m_craftingVisible))
						{
							const uint32_t craftClientId = m_gameplayUdp.ServerClientId();
							if (uiModel.crafting.professions.empty())
							{
								ImGui::TextUnformatted("Aucun metier connu.");
							}
							// Onglets métiers → demande de liste de recettes.
							for (size_t professionIndex = 0;
								professionIndex < uiModel.crafting.professions.size(); ++professionIndex)
							{
								const engine::client::UIProfessionEntry& prof =
									uiModel.crafting.professions[professionIndex];
								if (professionIndex > 0)
									ImGui::SameLine();
								const std::string tabLabel = prof.professionKey
									+ " (" + std::to_string(prof.skillLevel) + ")";
								if (ImGui::SmallButton(tabLabel.c_str())
									&& prof.professionKey != uiModel.crafting.activeProfessionKey
									&& craftClientId != 0u)
								{
									(void)m_gameplayUdp.SendCraftRecipeListRequest(
										craftClientId, prof.professionKey);
								}
							}
							ImGui::Separator();
							// Liste des recettes (clic = sélection).
							ImGui::BeginChild("##ln_craft_recipes", ImVec2(0.0f, 220.0f), true);
							for (size_t recipeIndex = 0;
								recipeIndex < uiModel.crafting.recipes.size(); ++recipeIndex)
							{
								const engine::client::UICraftRecipeRow& recipe =
									uiModel.crafting.recipes[recipeIndex];
								const bool selected = (uiModel.crafting.selectedRecipeIndex
									== static_cast<uint32_t>(recipeIndex));
								const std::string rowLabel = recipe.recipeId
									+ "  (comp. " + std::to_string(recipe.skillRequired)
									+ ", x" + std::to_string(recipe.outputQuantity) + ")";
								if (ImGui::Selectable(rowLabel.c_str(), selected))
								{
									(void)m_uiModelBinding.SelectCraftRecipe(
										static_cast<uint32_t>(recipeIndex));
								}
							}
							ImGui::EndChild();
							// Fabrication : bouton + annulation + cast bar + qualité.
							const bool craftInProgress = uiModel.crafting.craftFillFraction > 0.0f;
							const bool canCraft = !craftInProgress
								&& uiModel.crafting.selectedRecipeIndex < uiModel.crafting.recipes.size();
							if (ImGui::Button("Fabriquer", ImVec2(150.0f, 32.0f))
								&& canCraft && craftClientId != 0u)
							{
								(void)m_gameplayUdp.SendCraftRequest(craftClientId,
									uiModel.crafting.recipes[uiModel.crafting.selectedRecipeIndex].recipeId);
							}
							if (craftInProgress)
							{
								ImGui::SameLine();
								if (ImGui::Button("Annuler", ImVec2(110.0f, 32.0f)) && craftClientId != 0u)
								{
									(void)m_gameplayUdp.SendCraftCancelRequest(craftClientId);
								}
								ImGui::ProgressBar(uiModel.crafting.craftFillFraction, ImVec2(-1.0f, 10.0f), "");
							}
							else if (!canCraft)
							{
								ImGui::TextUnformatted("Selectionnez une recette.");
							}
							// Qualité du dernier craft (M36.3) via le présentateur câblé.
							const engine::client::CraftingPanelState& craftState = m_craftingUi.GetState();
							if (!craftState.lastQualityLabel.empty())
							{
								ImGui::TextColored(
									ImVec4(craftState.lastQualityColor.r, craftState.lastQualityColor.g,
										craftState.lastQualityColor.b, 1.0f),
									"Derniere fabrication : %s", craftState.lastQualityLabel.c_str());
							}
							if (!craftState.statusText.empty())
							{
								ImGui::TextUnformatted(craftState.statusText.c_str());
							}
						}
						ImGui::End();
					}

					// --- Validation v12 : jauges du joueur (PV + ressource), bas-centre.
					// PV rafraîchis en temps réel par les CombatEvent (chaque coup reçu)
					// et l'événement de résurrection. Avant le premier coup reçu,
					// maxHealth vaut 0 → on affiche la feuille de stats (pleine).
					{
						const uint32_t playerMaxHp = (uiModel.playerStats.maxHealth > 0u)
							? uiModel.playerStats.maxHealth
							: uiModel.playerStats.sheetMaxHealth;
						if (playerMaxHp > 0u)
						{
							const uint32_t playerCurHp = (uiModel.playerStats.maxHealth > 0u)
								? uiModel.playerStats.currentHealth
								: playerMaxHp;
							const float hpFracPlayer = std::clamp(
								static_cast<float>(playerCurHp) / static_cast<float>(playerMaxHp), 0.0f, 1.0f);
							const float gaugeW = 320.0f;
							const float gaugeH = 18.0f;
							const float gaugeX = (dw - gaugeW) * 0.5f;
							const float gaugeY = dh - 152.0f;
							const ImU32 hpColor = (hpFracPlayer > 0.5f)
								? IM_COL32(80, 200, 80, 235)
								: ((hpFracPlayer > 0.25f)
									? IM_COL32(230, 160, 40, 235)
									: IM_COL32(210, 60, 50, 235));
							fg->AddRectFilled(ImVec2(gaugeX - 2.0f, gaugeY - 2.0f),
								ImVec2(gaugeX + gaugeW + 2.0f, gaugeY + gaugeH + 2.0f), IM_COL32(0, 0, 0, 160), 4.0f);
							fg->AddRectFilled(ImVec2(gaugeX, gaugeY),
								ImVec2(gaugeX + gaugeW, gaugeY + gaugeH), IM_COL32(35, 38, 44, 230), 3.0f);
							fg->AddRectFilled(ImVec2(gaugeX, gaugeY),
								ImVec2(gaugeX + gaugeW * hpFracPlayer, gaugeY + gaugeH), hpColor, 3.0f);
							const std::string playerHpText =
								std::to_string(playerCurHp) + " / " + std::to_string(playerMaxHp);
							const ImVec2 playerHpTs = ImGui::CalcTextSize(playerHpText.c_str());
							fg->AddText(ImVec2(gaugeX + (gaugeW - playerHpTs.x) * 0.5f, gaugeY + 1.0f),
								IM_COL32(255, 255, 255, 255), playerHpText.c_str());
							// Ressource de classe (mana/énergie…) : barre fine dessous,
							// alimentée par les ResourceUpdate (SP3).
							if (uiModel.playerStats.secondaryResourceMax > 0u)
							{
								const float resFrac = std::clamp(
									static_cast<float>(uiModel.playerStats.secondaryResourceCurrent)
										/ static_cast<float>(uiModel.playerStats.secondaryResourceMax),
									0.0f, 1.0f);
								const float resY = gaugeY + gaugeH + 4.0f;
								fg->AddRectFilled(ImVec2(gaugeX, resY),
									ImVec2(gaugeX + gaugeW, resY + 8.0f), IM_COL32(35, 38, 44, 230), 3.0f);
								fg->AddRectFilled(ImVec2(gaugeX, resY),
									ImVec2(gaugeX + gaugeW * resFrac, resY + 8.0f), IM_COL32(70, 130, 220, 235), 3.0f);
							}
							// --- Barre d'XP (PR-C) : fine barre dorée sous la vie/ressource,
							// alimentée par PlayerXpUpdate (serveur, enter-world + chaque gain).
							// Niveau à gauche, progression xp/prochain niveau centrée dessous.
							// hasXp faux tant qu'aucun PlayerXpUpdate reçu -> pas de barre.
							if (uiModel.playerStats.hasXp)
							{
								const bool atCap = (uiModel.playerStats.xpForNextLevel == 0u);
								const float xpFrac = atCap ? 1.0f : std::clamp(
									static_cast<float>(uiModel.playerStats.xpIntoLevel)
										/ static_cast<float>(uiModel.playerStats.xpForNextLevel),
									0.0f, 1.0f);
								const float xpY = gaugeY + gaugeH + 16.0f;
								const float xpH = 6.0f;
								fg->AddRectFilled(ImVec2(gaugeX, xpY),
									ImVec2(gaugeX + gaugeW, xpY + xpH), IM_COL32(30, 26, 12, 230), 2.0f);
								fg->AddRectFilled(ImVec2(gaugeX, xpY),
									ImVec2(gaugeX + gaugeW * xpFrac, xpY + xpH), IM_COL32(220, 180, 60, 235), 2.0f);
								// Niveau, à gauche de la barre.
								char lvlBuf[32];
								std::snprintf(lvlBuf, sizeof(lvlBuf), "Nv. %u", uiModel.playerStats.level);
								const ImVec2 lvlTs = ImGui::CalcTextSize(lvlBuf);
								fg->AddText(ImVec2(gaugeX - lvlTs.x - 8.0f, xpY - 4.0f),
									IM_COL32(230, 210, 150, 255), lvlBuf);
								// Progression xp/prochain niveau (ou « Niveau max ») centrée dessous.
								char xpBuf[48];
								if (atCap)
									std::snprintf(xpBuf, sizeof(xpBuf), "Niveau max");
								else
									std::snprintf(xpBuf, sizeof(xpBuf), "%u / %u XP",
										uiModel.playerStats.xpIntoLevel, uiModel.playerStats.xpForNextLevel);
								const ImVec2 xpTs = ImGui::CalcTextSize(xpBuf);
								fg->AddText(ImVec2(gaugeX + (gaugeW - xpTs.x) * 0.5f, xpY + xpH + 1.0f),
									IM_COL32(200, 190, 160, 220), xpBuf);
							}
						}
					}

					// --- Bourse : pièces or / argent / bronze du joueur, coin bas-droit.
					// Alimentée par WalletUpdate (UIModel.wallet.gold), réinterprétée comme
					// un total en BRONZE (unité de base : 100 bronze = 1 argent, 100 argent
					// = 1 or ; retour joueur 2026-07-08). Chaque palier = une pastille teintée
					// + son compte. On masque or/argent tant qu'ils sont nuls (bronze toujours
					// visible). Non concernée par la bascule HUD carte (bourse toujours visible).
					{
						const engine::client::CoinBreakdown coins =
							engine::client::SplitCoins(uiModel.wallet.gold);
						struct CoinTier { uint32_t value; ImU32 fill; ImU32 ring; };
						std::vector<CoinTier> tiers;
						if (coins.gold > 0u)
							tiers.push_back({coins.gold, IM_COL32(240, 200, 70, 255), IM_COL32(180, 140, 30, 255)});
						if (coins.gold > 0u || coins.silver > 0u)
							tiers.push_back({coins.silver, IM_COL32(214, 220, 228, 255), IM_COL32(150, 160, 172, 255)});
						tiers.push_back({coins.bronze, IM_COL32(205, 125, 60, 255), IM_COL32(150, 80, 35, 255)});

						const float padX = 11.0f;
						const float padY = 6.0f;
						const float coinR = 6.0f;
						const float coinTextGap = 5.0f;
						const float groupGap = 12.0f;
						const float textH = ImGui::GetTextLineHeight();
						std::vector<std::string> labels;
						labels.reserve(tiers.size());
						float contentW = 0.0f;
						for (size_t i = 0; i < tiers.size(); ++i)
						{
							labels.push_back(std::to_string(tiers[i].value));
							const ImVec2 ts = ImGui::CalcTextSize(labels[i].c_str());
							contentW += coinR * 2.0f + coinTextGap + ts.x;
							if (i + 1 < tiers.size())
								contentW += groupGap;
						}
						const float pillW = padX * 2.0f + contentW;
						const float pillH = padY * 2.0f + std::max(textH, coinR * 2.0f);
						const float bourseMargin = 16.0f;
						const float px1 = dw - bourseMargin;
						const float py1 = dh - bourseMargin;
						const float px0 = px1 - pillW;
						const float py0 = py1 - pillH;
						const float midY = (py0 + py1) * 0.5f;
						fg->AddRectFilled(ImVec2(px0, py0), ImVec2(px1, py1), IM_COL32(18, 20, 26, 215), 7.0f);
						fg->AddRect(ImVec2(px0, py0), ImVec2(px1, py1), IM_COL32(120, 100, 40, 220), 7.0f, 0, 1.5f);
						float cursorX = px0 + padX;
						for (size_t i = 0; i < tiers.size(); ++i)
						{
							const ImVec2 cc(cursorX + coinR, midY);
							fg->AddCircleFilled(cc, coinR, tiers[i].fill);
							fg->AddCircle(cc, coinR, tiers[i].ring, 14, 1.2f);
							cursorX += coinR * 2.0f + coinTextGap;
							const ImVec2 ts = ImGui::CalcTextSize(labels[i].c_str());
							fg->AddText(ImVec2(cursorX, midY - ts.y * 0.5f),
								IM_COL32(245, 235, 205, 255), labels[i].c_str());
							cursorX += ts.x + groupGap;
						}
					}


					// --- Ecran de mort : overlay sombre + bouton Reapparaitre →
					// RespawnRequest (le serveur ne l'honore que si le flag dead est pose ;
					// l'overlay disparait quand le snapshot post-respawn retire le flag).
					if (localDead)
					{
						fg->AddRectFilled(ImVec2(0.0f, 0.0f), ImVec2(dw, dh), IM_COL32(10, 0, 0, 140));
						const float panelW = 360.0f;
						const float panelH = 170.0f;
						ImGui::SetNextWindowPos(ImVec2((dw - panelW) * 0.5f, (dh - panelH) * 0.5f), ImGuiCond_Always);
						ImGui::SetNextWindowSize(ImVec2(panelW, panelH), ImGuiCond_Always);
						ImGui::SetNextWindowBgAlpha(0.95f);
						ImGui::Begin("##ln_death_overlay", nullptr,
							ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
							| ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav);
						ImGui::SetWindowFontScale(1.3f);
						const char* deadTitle = "VOUS ETES MORT";
						const float deadTitleW = ImGui::CalcTextSize(deadTitle).x;
						ImGui::SetCursorPosX((panelW - deadTitleW) * 0.5f);
						ImGui::TextUnformatted(deadTitle);
						ImGui::SetWindowFontScale(1.0f);
						ImGui::Separator();
						ImGui::Spacing();
						ImGui::Spacing();
						// Validation v12 (wire v13) — deux destinations : cimetière ou
						// auberge la plus proche (le serveur choisit le point du type
						// demandé le plus proche du lieu de mort).
						const float deathBtnW = (panelW - 30.0f) * 0.5f;
						ImGui::SetCursorPosX(10.0f);
						if (ImGui::Button("Cimetiere le plus proche", ImVec2(deathBtnW, 40.0f)))
						{
							const uint32_t gameplayClientId = m_gameplayUdp.ServerClientId();
							if (gameplayClientId != 0u)
								(void)m_gameplayUdp.SendRespawnRequest(gameplayClientId,
									engine::server::kRespawnDestinationGraveyard);
						}
						ImGui::SameLine();
						if (ImGui::Button("Auberge la plus proche", ImVec2(deathBtnW, 40.0f)))
						{
							const uint32_t gameplayClientId = m_gameplayUdp.ServerClientId();
							if (gameplayClientId != 0u)
								(void)m_gameplayUdp.SendRespawnRequest(gameplayClientId,
									engine::server::kRespawnDestinationInn);
						}
						ImGui::End();
					}
				}
			}
			// Menu de panneaux : barre de menus ImGui toujours visible en jeu,
			// acces souris a tous les panneaux togglables sans raccourci clavier
			// dedie. Les panneaux gardent par ailleurs leurs touches existantes ;
			// GameEvents n'est plus accessible que par ce menu (touche E liberee).
			// Chaque MenuItem reflete l'etat *Visible et reproduit le RequestList
			// d'ouverture du toggle clavier correspondant.
			if (ImGui::BeginMainMenuBar())
			{
				if (ImGui::BeginMenu("Panneaux"))
				{
					if (ImGui::MenuItem("Personnage (F1)", nullptr,
						m_characterWindowImGui && m_characterWindowImGui->IsVisible()))
					{
						// Ouvre la fenêtre Personnage unifiée (onglet Compétences).
						if (m_characterWindowImGui)
							m_characterWindowImGui->OpenAtTab(engine::render::CharacterWindowImGuiRenderer::Tab::Competences);
						m_skillBookUi.RequestList();
					}
					if (ImGui::MenuItem("Arenes", nullptr, m_arenaVisible))
					{
						m_arenaVisible = !m_arenaVisible;
						if (m_arenaVisible) m_arenaUi.RequestTeams();
					}
					if (ImGui::MenuItem("Champs de bataille", nullptr, m_battleGroundVisible))
					{
						m_battleGroundVisible = !m_battleGroundVisible;
						if (m_battleGroundVisible) m_battleGroundUi.RequestList();
					}
					if (ImGui::MenuItem("PvP exterieur", nullptr, m_outdoorPvpVisible))
					{
						m_outdoorPvpVisible = !m_outdoorPvpVisible;
						if (m_outdoorPvpVisible) m_outdoorPvpUi.RequestList();
					}
					if (ImGui::MenuItem("Meteo", nullptr, m_weatherVisible))
					{
						m_weatherVisible = !m_weatherVisible;
						if (m_weatherVisible) m_weatherUi.RequestList();
					}
					if (ImGui::MenuItem("Evenements", nullptr, m_gameEventVisible))
					{
						m_gameEventVisible = !m_gameEventVisible;
						if (m_gameEventVisible) m_gameEventUi.RequestList();
					}
					if (ImGui::MenuItem("Guilde", nullptr, m_guildVisible))
					{
						m_guildVisible = !m_guildVisible;
						if (m_guildVisible) m_guildUi.RequestList();
					}
					if (ImGui::MenuItem("Hotel des ventes", nullptr, m_auctionHouseVisible))
					{
						m_auctionHouseVisible = !m_auctionHouseVisible;
						if (m_auctionHouseVisible) m_auctionHouseUi.RequestList(0u);
					}
					if (ImGui::MenuItem("Jets de butin", nullptr, m_lootRollVisible))
					{
						m_lootRollVisible = !m_lootRollVisible;
					}
					ImGui::EndMenu();
				}
				ImGui::EndMainMenuBar();
			}
			// CMANGOS.18 (Phase 3.18 step 4) — Render du panneau Mail si visible.
			// Le panneau partage la frame ImGui en cours (NewFrame deja appele
			// par chatImguiOverlayNewFrame plus haut). Visible uniquement quand
			// l'utilisateur a fait /mail (toggle dans le SendCallback du chat).
			if (m_mailVisible && m_mailImGui && m_mailUi.IsInitialized())
			{
				m_mailImGui->SetEnabled(true);
				m_mailImGui->SetViewportSize(static_cast<uint32_t>(dw), static_cast<uint32_t>(dh));
				m_mailImGui->Render();
			}
			// CMANGOS.32 (Phase 5.32 step 3+4) — Render du panneau Support GM si visible.
			// Le panneau partage la frame ImGui en cours (cf. m_mailImGui ci-dessus).
			if (m_gmTicketsVisible && m_gmTicketImGui && m_gmTicketUi.IsInitialized())
			{
				m_gmTicketImGui->SetEnabled(true);
				m_gmTicketImGui->SetViewportSize(static_cast<uint32_t>(dw), static_cast<uint32_t>(dh));
				m_gmTicketImGui->Render();
			}
			// CMANGOS.24 (Phase 3.24 step 3+4) — Tick le toast (expire ~3s) puis
			// render le panneau Reputation si visible. Le toast push est rendu
			// en plus de la liste si actif (overlay non bloquant).
			m_reputationUi.TickToast(static_cast<float>(m_time.DeltaSeconds()));
			if (m_reputationVisible && m_reputationImGui && m_reputationUi.IsInitialized())
			{
				m_reputationImGui->SetEnabled(true);
				m_reputationImGui->SetViewportSize(static_cast<uint32_t>(dw), static_cast<uint32_t>(dh));
				m_reputationImGui->Render();
			}
			// Chantier 1 — les caractéristiques (ex-fiche F1) sont désormais dans
			// l'onglet Personnage de la fenêtre unifiée (rendu plus bas). Ancien
			// panneau F1 autonome retiré.
			// CMANGOS.33 (Phase 5.33 step 3+4) — Render du panneau LFG si visible.
			// Le modal proposal s'affiche aussi quand hasProposal == true (meme si
			// le panneau principal est masque), pour que le joueur ne rate pas
			// la formation de groupe.
			if (m_lfgImGui && m_lfgUi.IsInitialized()
				&& (m_lfgVisible || m_lfgUi.GetState().hasProposal))
			{
				m_lfgImGui->SetEnabled(true);
				m_lfgImGui->SetViewportSize(static_cast<uint32_t>(dw), static_cast<uint32_t>(dh));
				m_lfgImGui->Render();
			}
			// CMANGOS.30 (Phase 5.30 step 3+4) — Render de l'overlay cinematique
			// (black bars + skip hint) pendant la lecture d'une cinematique.
			// state.isPlaying == false => pas de rendering (no-op interne).
			if (m_cinematicImGui && m_cinematicUi.IsInitialized()
				&& m_cinematicUi.GetState().isPlaying)
			{
				m_cinematicImGui->SetViewportSize(static_cast<uint32_t>(dw), static_cast<uint32_t>(dh));
				m_cinematicImGui->Render();
			}
			// CMANGOS.39 (Phase 4.39 step 3+4) — Tick l'indicateur Use puis
			// render le panneau Skill Book si visible. L'indicateur Use est rendu
			// en plus de la liste si actif (overlay non bloquant).
			m_skillBookUi.TickIndicator(static_cast<float>(m_time.DeltaSeconds()));
			// Chantier 1 — le Skill Book est désormais l'onglet Compétences de la
			// fenêtre unifiée (rendu embarqué plus bas). Panneau autonome retiré.
			// Grimoire (Task 13) — Sync du presenter (profil + layout autoritaire)
			// puis render conditionnel si le panneau est ouvert.
			// SP-C — kit effectif : compétences de classe connues (fallback kit profil).
			const engine::client::UIModel& grimoireModel = m_uiModelBinding.GetModel();
			{
				std::vector<engine::client::SpellDisplay> grimoireEffectiveKit;
				const std::string& gClassId   = grimoireModel.classId;
				const std::string& gProfileId = grimoireModel.playerStats.profileId;
				const std::vector<std::string>& gKnownIds = grimoireModel.knownSkillIds;
				if (!gClassId.empty() && !gKnownIds.empty())
				{
					const std::vector<engine::client::ClassSkillDisplay>* gClassSkills =
						m_classSkillCatalog.GetClassSkills(gClassId);
					if (gClassSkills != nullptr)
					{
						grimoireEffectiveKit.reserve(gKnownIds.size());
						for (const std::string& skillId : gKnownIds)
						{
							for (const engine::client::ClassSkillDisplay& cs : *gClassSkills)
							{
								if (cs.skillId == skillId)
								{
									grimoireEffectiveKit.push_back(engine::client::ToSpellDisplay(cs, gClassId));
									break;
								}
							}
						}
					}
				}
				m_grimoireUi.Sync(gProfileId, grimoireModel.playerStats.actionBarLayout, grimoireEffectiveKit);
			}
			// Chantier 1 — le Grimoire est désormais l'onglet Techniques de la fenêtre
			// unifiée (rendu embarqué plus bas). Le Sync ci-dessus reste nécessaire.
			// SP-D — Sync + render conditionnel de l'arbre de compétences par-classe.
			// Niveau réel du perso (capturé à EnterWorld depuis CHARACTER_LIST) : verrouille
			// les paliers > niveau et affiche le bon « Niveau joueur ». Le serveur revalide.
			{
				const engine::client::UIModel& treeModel = m_uiModelBinding.GetModel();
				// Le level-up EN JEU doit débloquer les paliers de l'arbre. Le niveau
				// live vient de PlayerXpUpdate (barre d'XP, playerStats.level), qui suit
				// les montées de niveau ; m_activeCharacterLevel n'était capturé qu'à
				// l'EnterWorld et jamais remis à jour -> l'arbre restait bloqué au niveau
				// d'entrée (retour joueur 2026-07-07 : perso niv. 5 mais arbre « Niveau
				// joueur : 1 », compétences verrouillées). On synchronise donc le niveau
				// actif sur le niveau serveur (monotone ; fallback = EnterWorld si aucun
				// PlayerXpUpdate reçu, ex. serveur ancien).
				if (treeModel.playerStats.hasXp && treeModel.playerStats.level > m_activeCharacterLevel)
					m_activeCharacterLevel = treeModel.playerStats.level;
				m_classSkillTreeUi.Sync(treeModel.classId, treeModel.knownSkillIds, m_activeCharacterLevel);
				// Chantier 1 — l'arbre est désormais l'onglet Arbre de la fenêtre unifiée
				// (rendu embarqué juste après). Le Sync ci-dessus reste nécessaire.
			}
			// Task 4 — aperçu 3D : fait tourner + rend l'avatar du joueur dans l'image
			// offscreen tant que la fenêtre Personnage est ouverte, AVANT que la draw
			// list ImGui n'échantillonne la texture. RenderOffscreen est autonome
			// (submit + wait idle) : coûteux (stall GPU) -> uniquement fenêtre ouverte.
			// Le mesh/genre/teinte sont posés à l'ouverture (toggle F1) ; ici on ne fait
			// qu'avancer l'anim/orbit et rendre.
			if (m_characterWindowImGui && m_characterWindowImGui->IsVisible())
			{
				const float pNow = EngineNowSec();
				float pDt = (m_racePreviewLastNowSec > 0.0f) ? (pNow - m_racePreviewLastNowSec) : 0.0f;
				m_racePreviewLastNowSec = pNow;
				if (pDt < 0.0f) pDt = 0.0f;
				if (pDt > 0.1f) pDt = 0.1f; // clamp gros hitch
				m_racePreviewViewport.Tick(pDt);
				m_racePreviewViewport.RenderOffscreen();
			}
			// Chantier 1 — fenêtre Personnage unifiée à onglets (F1). Rendue APRÈS les
			// Sync du Grimoire et de l'arbre (leurs presenters embarqués lisent l'état à
			// jour). Point single-pass (même bloc que les autres panneaux) -> pas de
			// doublon d'inventaire.
			if (m_characterWindowImGui)
			{
				m_characterWindowImGui->SetViewportSize(static_cast<uint32_t>(dw), static_cast<uint32_t>(dh));
				m_characterWindowImGui->Render(m_uiModelBinding.GetModel());
				// Chantier 2 SP-A — draine l'intention equip/unequip signalée au clic et
				// l'envoie au serveur (le renderer n'a pas accès au socket). Le serveur
				// revalide possession + slot.
				engine::render::CharacterWindowImGuiRenderer::PendingEquipAction equipAction;
				if (m_characterWindowImGui->ConsumeEquipAction(equipAction))
				{
					const uint32_t equipClientId = m_gameplayUdp.ServerClientId();
					if (equipAction.kind ==
						engine::render::CharacterWindowImGuiRenderer::PendingEquipAction::Kind::Equip)
						(void)m_gameplayUdp.SendEquipRequest(equipClientId, equipAction.itemId);
					else if (equipAction.kind ==
						engine::render::CharacterWindowImGuiRenderer::PendingEquipAction::Kind::Unequip)
						(void)m_gameplayUdp.SendUnequipRequest(equipClientId, equipAction.slot);
				// Roadmap-3 (2026-07-19) — consommable cliqué dans le sac :
				// placé au 1er slot LIBRE de la CEINTURE (slot 1 remplacé si
				// pleine). Envoi kind 99 ; le serveur valide la possession et
				// renvoie le layout autoritaire (kind 100).
				else if (equipAction.kind ==
					engine::render::CharacterWindowImGuiRenderer::PendingEquipAction::Kind::SlotCake)
				{
					// Ceinture v2 — layout à taille dynamique (vector).
					std::vector<std::string> belt = m_uiModelBinding.GetModel().playerStats.beltLayout;
					const std::string beltToken = engine::anniversary::MakeItemToken(equipAction.itemId);
					bool alreadySlotted = false;
					for (const std::string& s : belt)
						if (s == beltToken) { alreadySlotted = true; break; }
					if (!alreadySlotted && !belt.empty())
					{
						size_t target = 0;
						for (size_t i = 0; i < belt.size(); ++i)
							if (belt[i].empty()) { target = i; break; }
						belt[target] = beltToken;
						(void)m_gameplayUdp.SendSetBeltLayout(equipClientId, belt);
					}
				}
				}
			}
			// CMANGOS.21 (Phase 5.21 step 3+4) — Render du panneau Arena si
			// visible. Le popup proposal s'affiche aussi quand pendingProposalId
			// est set (meme si le panneau principal est masque), pour que le
			// joueur ne rate pas la formation de match.
			if (m_arenaImGui && m_arenaUi.IsInitialized()
				&& (m_arenaVisible || m_arenaUi.GetState().pendingProposalId.has_value()))
			{
				m_arenaImGui->SetEnabled(true);
				m_arenaImGui->SetViewportSize(static_cast<uint32_t>(dw), static_cast<uint32_t>(dh));
				m_arenaImGui->Render();
			}
			// CMANGOS.10 (Phase 5 step 3+4) — Render du panneau BattleGround si
			// visible OU si un match est actif (le scoreboard s'affiche tout
			// seul tant que activeMatchId est set, meme si le panneau principal
			// est masque, pour que le joueur ne rate pas le match push-pousse).
			if (m_battleGroundImGui && m_battleGroundUi.IsInitialized()
				&& (m_battleGroundVisible || m_battleGroundUi.GetState().activeMatchId.has_value()))
			{
				m_battleGroundImGui->SetEnabled(true);
				m_battleGroundImGui->SetViewportSize(static_cast<uint32_t>(dw), static_cast<uint32_t>(dh));
				m_battleGroundImGui->Render();
			}
			// CMANGOS.36 (Phase 5.36 step 3+4) — Render du panneau OutdoorPvp
			// si visible. Pas de scoreboard auto-affiche : V1 le panneau est
			// strictement toggle-only via /pvp ou la touche P.
			if (m_outdoorPvpImGui && m_outdoorPvpUi.IsInitialized()
				&& m_outdoorPvpVisible)
			{
				m_outdoorPvpImGui->SetEnabled(true);
				m_outdoorPvpImGui->SetViewportSize(static_cast<uint32_t>(dw), static_cast<uint32_t>(dh));
				m_outdoorPvpImGui->Render();
			}
			// CMANGOS.42 (Phase 4.42 step 3+4) — Render du panel Weather si
			// m_weatherVisible (toggle via /weather ou touche Y), ET du HUD
			// top-right si activeZoneId set. Render() lui-meme gere ces
			// 2 cas independamment : on lui passe juste l'enabled flag pour
			// le panel ; le HUD est conditionne par le presenter state.
			if (m_weatherImGui && m_weatherUi.IsInitialized())
			{
				m_weatherImGui->SetEnabled(m_weatherVisible);
				m_weatherImGui->SetViewportSize(static_cast<uint32_t>(dw), static_cast<uint32_t>(dh));
				m_weatherImGui->Render();
			}
			// CMANGOS.31 (Phase 5.31 step 3+4) — Render du panel GameEvents
			// si m_gameEventVisible (toggle via /events ou touche E), ET du
			// toast 5s sur dernier StateChange reçu (rendu independamment
			// par Render() si lastChangeTimeMs est recent).
			if (m_gameEventImGui && m_gameEventUi.IsInitialized())
			{
				m_gameEventImGui->SetEnabled(m_gameEventVisible);
				m_gameEventImGui->SetViewportSize(static_cast<uint32_t>(dw), static_cast<uint32_t>(dh));
				m_gameEventImGui->Render();
			}
			// CMANGOS.21 (Phase 5.21 step 3+4 Guilds) — Render du panel Guildes
			// si m_guildVisible (toggle via /guild ou touche U), ET du toast 5s
			// sur dernier MotdUpdate reçu (rendu independamment par Render()
			// si lastMotdChangeTimeMs est recent).
			if (m_guildImGui && m_guildUi.IsInitialized())
			{
				m_guildImGui->SetEnabled(m_guildVisible);
				m_guildImGui->SetViewportSize(static_cast<uint32_t>(dw), static_cast<uint32_t>(dh));
				m_guildImGui->Render();
			}
			// CMANGOS.09 (Phase 5.09 step 3+4 AuctionHouse) — Render du panel
			// Hotel des Ventes si m_auctionHouseVisible (toggle via /ah ou
			// touche H), ET des toasts 5s sur derniere bid + dernier
			// AuctionExpired (rendus independamment par Render() si
			// lastBidTimeMs / lastExpirationTimeMs sont recents).
			if (m_auctionHouseImGui && m_auctionHouseUi.IsInitialized())
			{
				m_auctionHouseImGui->SetEnabled(m_auctionHouseVisible);
				m_auctionHouseImGui->SetViewportSize(static_cast<uint32_t>(dw), static_cast<uint32_t>(dh));
				m_auctionHouseImGui->Render();
			}
			// CMANGOS.17 (Phase 3.17 step 3+4 Loot) — Render du panneau Loot
			// Roll si m_lootRollVisible (toggle via /loot ou touche L), ET du
			// toast 5s sur dernier RollResult reçu (rendu independamment par
			// Render() si lastResultTimeMs est recent).
			if (m_lootRollImGui && m_lootRollUi.IsInitialized())
			{
				m_lootRollImGui->SetEnabled(m_lootRollVisible);
				m_lootRollImGui->SetViewportSize(static_cast<uint32_t>(dw), static_cast<uint32_t>(dh));
				m_lootRollImGui->Render();
			}
			// DIAG chat-only branch (in-game).
			if ((m_currentFrame % 60u) == 0u)
			{
				LOG_INFO(Render, "[ChatDiag-InGameBranch] frame={} dw={} dh={} inWorldShard={} chatFocus={}",
					m_currentFrame, dw, dh, m_authUi.IsInWorldShard(), m_chatUi.IsChatFocusActive());
			}
			// Menu pause in-game superpose au chat HUD : meme branche de rendu pour
			// que le ImGui::Render() finalise les deux draw lists en une seule passe.
			if (m_inGamePauseMenuVisible)
			{
				const LnTheme::Palette& th = LnTheme::Active();
				// Conversion couleur thème -> ImVec4 ImGui, locale au menu pause.
				auto iv = [](const LnTheme::Rgba& c, float a) -> ImVec4 {
					return ImVec4(c.r, c.g, c.b, a);
				};
				const float menuW = 340.f;
				const float menuH = 250.f;
				const float btnW = menuW - 64.f;
				ImGui::SetNextWindowPos(ImVec2((dw - menuW) * 0.5f, (dh - menuH) * 0.5f), ImGuiCond_Always);
				ImGui::SetNextWindowSize(ImVec2(menuW, menuH), ImGuiCond_Always);
				// Chrome variante C : fond panneau + cadre accentué (doré en Or royal,
				// vert en Sylve émeraude). On retire SetNextWindowBgAlpha : il écraserait
				// l'alpha du WindowBg ci-dessous.
				ImGui::PushStyleColor(ImGuiCol_WindowBg, iv(th.panel, 0.96f));
				ImGui::PushStyleColor(ImGuiCol_Border, iv(th.accent, 1.f));
				ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.5f);
				ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 4.f);
				ImGui::Begin("##ln_pause_menu", nullptr,
					ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize
					| ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse
					| ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav);

				// Titre PAUSE : couleur accent, centré sur la zone de contenu réelle
				// (GetContentRegionAvail tient compte du padding -> centrage correct).
				ImGui::SetWindowFontScale(1.3f);
				ImGui::PushStyleColor(ImGuiCol_Text, iv(th.accent, 1.f));
				const char* title = "PAUSE";
				const float titleW = ImGui::CalcTextSize(title).x;
				ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (ImGui::GetContentRegionAvail().x - titleW) * 0.5f);
				ImGui::TextUnformatted(title);
				ImGui::PopStyleColor();
				ImGui::SetWindowFontScale(1.f);

				// Séparateur doré.
				ImGui::PushStyleColor(ImGuiCol_Separator, iv(th.accent, 0.7f));
				ImGui::Separator();
				ImGui::PopStyleColor();
				ImGui::Spacing();
				ImGui::Spacing();

				// Bouton thémé centré, avec halo de bordure (accent, ou rouge danger)
				// dessiné par-dessus au survol.
				auto pauseButton = [&](const char* label, bool danger) -> bool {
					const LnTheme::Rgba hov = danger ? th.errorCol : th.accent;
					ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (ImGui::GetContentRegionAvail().x - btnW) * 0.5f);
					ImGui::PushStyleColor(ImGuiCol_Button, iv(th.surface, 1.f));
					ImGui::PushStyleColor(ImGuiCol_ButtonHovered, iv(hov, 0.22f));
					ImGui::PushStyleColor(ImGuiCol_ButtonActive, iv(hov, 0.35f));
					ImGui::PushStyleColor(ImGuiCol_Border, iv(th.border, 1.f));
					ImGui::PushStyleColor(ImGuiCol_Text, danger ? iv(th.errorCol, 1.f) : iv(th.text, 1.f));
					ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.f);
					ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.f);
					const bool pressed = ImGui::Button(label, ImVec2(btnW, 34.f));
					if (ImGui::IsItemHovered())
					{
						ImGui::GetWindowDrawList()->AddRect(
							ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
							ImGui::ColorConvertFloat4ToU32(iv(hov, 1.f)), 3.f, 0, 1.5f);
					}
					ImGui::PopStyleVar(2);
					ImGui::PopStyleColor(5);
					return pressed;
				};

				if (pauseButton("Reprendre", false))
				{
					m_inGamePauseMenuVisible = false;
				}
				ImGui::Spacing();
				if (pauseButton("Options", false))
				{
					m_inGamePauseMenuVisible = false;
					m_inGameOptionsPanelVisible = true;
					// B2/ST5 — pose le contexte in-game et initialise les valeurs *Pending
					// de l'écran d'options unifié (sans toucher la phase auth).
					m_authUi.OpenLanguageOptionsInGame();
				}
				ImGui::Spacing();
				if (pauseButton("Se deconnecter", false))
				{
					RequestLogoutToLoginScreen();
				}
				ImGui::Spacing();
				if (pauseButton("Quitter le jeu", true))
				{
					OnQuit();
				}
				ImGui::End();
				ImGui::PopStyleVar(2);
				ImGui::PopStyleColor(2);
			}
			// Menu Options en jeu : on réutilise EXACTEMENT l'écran d'options de l'auth
			// (source unique, spec B2). Le menu Pause (ESC) reste inchangé ; seul son lien
			// « Options » mène ici. RenderOptionsOverlay renvoie false à la fermeture.
			// Garde m_authImGui : aligne sur la défensive des autres panneaux in-game
			// du même bloc (m_mailImGui &&, m_gmTicketImGui &&, …). m_authImGui null
			// (shutdown) -> on referme proprement l'overlay plutôt que de déréférencer.
			if (m_inGameOptionsPanelVisible)
			{
				const bool stillOpen = m_authImGui && m_authImGui->RenderOptionsOverlay(dw, dh);
				if (!stillOpen)
				{
					m_inGameOptionsPanelVisible = false;
					m_authUi.CloseLanguageOptionsInGame();
					m_inGamePauseMenuVisible = true; // retour au menu Pause
					ReloadKeybindsFromDisk(); // prise d'effet live des rebinds
				}
			}
			ImGui::Render();
		}
		else if (m_worldEditorImGui && m_worldEditorImGui->IsReady() && m_editorHubImGui
			&& m_editorEnabled && !m_worldEditorExe)
		{
			// M43.4 — Rendu du panneau Editor Hub overlay quand --editor (sans world-editor).
			float dw = static_cast<float>(std::max(1, m_width));
			float dh = static_cast<float>(std::max(1, m_height));
			if (m_vkSwapchain.IsValid())
			{
				const VkExtent2D extUi = m_vkSwapchain.GetExtent();
				if (extUi.width > 0 && extUi.height > 0)
				{
					dw = static_cast<float>(extUi.width);
					dh = static_cast<float>(extUi.height);
				}
			}
			m_editorHubImGui->Render(dw, dh);
			ImGui::Render();
		}
#endif

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

		// Snapshot des point lights actives dans le RenderState (anti data-race).
		// COPIE ici (assemblage de `out`) ; le lambda Lighting lit rs.pointLights
		// et ne touche jamais m_dynamicLights. Tick() déjà appelé plus tôt (Update).
		out.pointLights = m_dynamicLights.GetActiveLights();

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
	    // Skip render uniquement quand la fenetre est minimisee (m_width/m_height = 0).
	    // Ne PAS skipper sur m_swapchainResizeRequested : le code de resize est traite
	    // dans BeginFrame du frame suivant, et vkAcquireNextImageKHR retourne
	    // OUT_OF_DATE_KHR / SUBOPTIMAL_KHR qui sont gerees plus bas. Skipper en plus
	    // du flag risquait de figer le rendu de l'editeur monde dont la fenetre
	    // ne plante plus mais reste noire en permanence (signale par utilisateur).
	    if (m_width <= 0 || m_height <= 0)
	    {
	        LOG_DEBUG(Render, "[Engine] Render skipped (window minimized w={} h={})", m_width, m_height);
	        return;
	    }
	    LOG_DEBUG(Render, "[Engine] Render begin frame={}", m_currentFrame);
	    const uint32_t frameIndex          = m_currentFrame % 2;
	    engine::render::FrameResources& fr = m_frameResources[frameIndex];
	    ::VkDevice     device              = m_vkDeviceContext.GetDevice();
	    VkQueue        graphicsQueue       = m_vkDeviceContext.GetGraphicsQueue();
	    VkQueue        presentQueue        = m_vkDeviceContext.GetPresentQueue();
	    VkSwapchainKHR swapchain           = m_vkSwapchain.GetSwapchain();
	    // Utiliser l'extent réel de la swapchain pour que le FrameGraph alloue/recrée
	    // ses rendertargets avec les bonnes dimensions.
	    VkExtent2D extent = m_vkSwapchain.GetExtent();
	
	    LOG_DEBUG(Render, "[DIAG] vkWaitForFences begin frame={} frameIndex={}", m_currentFrame, frameIndex);
	    vkWaitForFences(device, 1, &fr.fence, VK_TRUE, UINT64_MAX);
	    LOG_DEBUG(Render, "[DIAG] vkWaitForFences done frame={}", m_currentFrame);
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
	        // Bornes de l'exposition adaptée. Le plafond (défaut 3.0 au lieu de 10.0)
	        // empêche l'auto-exposition de surcompenser une scène assombrie par les
	        // ombres et de cramer le ciel/horizon en blanc. Réglable via config.
	        const float minExp = static_cast<float>(m_cfg.GetDouble("exposure.min", 0.1));
	        // Plafond d'exposition piloté par l'heure : bas la nuit (night_max),
	        // normal le jour (max), interpolé linéairement via le facteur jour
	        // (élévation solaire clampée [0,1]). À dayFactor=1, effectiveMax==dayMax
	        // (jour inchangé). Comme tout passe par l'exposition, l'ambient IBL
	        // nocturne est aussi assombri. Lu chaque frame → night_max réglable à chaud.
	        const float dayMax    = static_cast<float>(m_cfg.GetDouble("exposure.max", 3.0));
	        const float nightMax  = static_cast<float>(m_cfg.GetDouble("exposure.night_max", 0.8));
	        const float dayFactor = m_dayNight.GetState().dayFactor; // 0=nuit, 1=jour
	        const float effectiveMax = nightMax + (dayMax - nightMax) * dayFactor;
	        m_pipeline->GetAutoExposure().Update(device, dt, key, speed, minExp, effectiveMax, frameIndex);
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
	    LOG_DEBUG(Render, "[Engine] Render vkAcquireNextImageKHR begin frame={}", m_currentFrame);
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
	    LOG_DEBUG(Render, "[Engine] Render vkAcquireNextImageKHR OK imageIndex={} frame={}", imageIndex, m_currentFrame);

	    vkResetCommandPool(device, fr.cmdPool, 0);
	
	    VkCommandBufferBeginInfo beginInfo{};
	    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	    if (vkBeginCommandBuffer(fr.cmdBuffer, &beginInfo) != VK_SUCCESS) return;
	    if (m_profiler.IsInitialized())
	    {
	        m_profiler.BeginGpuFrame(fr.cmdBuffer, frameIndex);
	    }

#if defined(_WIN32)
	    if (m_worldEditorExe && m_worldEditorTerrainTools.IsValid() && m_worldEditorTerrainTools.IsDirtyHeightmap()
	        && m_terrain.IsValid())
	    {
		    const engine::render::terrain::HeightmapData& hm = m_terrain.GetMutableHeightmapData();
		    const VkImage hmImg = m_terrain.GetHeightmapGpu().image;
		    if (hmImg != VK_NULL_HANDLE && hm.width > 0u && hm.height > 0u && !hm.heights.empty())
		    {
			    const size_t bytes = static_cast<size_t>(hm.width) * static_cast<size_t>(hm.height) * sizeof(uint16_t);
			    VkDeviceSize stagingOff = 0;
			    const VkBuffer stBuf = m_stagingAllocator.Allocate(bytes, stagingOff);
			    void* mappedBase = m_stagingAllocator.GetCurrentMappedBase();
			    if (stBuf != VK_NULL_HANDLE && mappedBase != nullptr)
			    {
				    std::memcpy(static_cast<char*>(mappedBase) + static_cast<size_t>(stagingOff), hm.heights.data(), bytes);
				    engine::render::terrain::RecordHeightmapR16UploadCommands(
				        fr.cmdBuffer, stBuf, stagingOff, hmImg, hm.width, hm.height);
				    m_worldEditorTerrainTools.AckHeightmapGpuUploaded();
			    }
			    else
			    {
				    (void)m_worldEditorTerrainTools.FlushHeightmap(device,
				        m_vkDeviceContext.GetPhysicalDevice(),
				        graphicsQueue,
				        m_vkDeviceContext.GetGraphicsQueueFamilyIndex(),
				        hmImg);
			    }
		    }
	    }
#endif

	    // M100.14 — Live update WaterMeshGpu si la WaterScene est dirty.
	    // No-op si :
	    //   • pas de WaterMeshGpu valide (typique : VMA disabled STAB.7),
	    //   • aucune scene (mode jeu sans m_clientWaterScene, ou éditeur sans shell),
	    //   • flag dirty == false (cas nominal régime établi).
	    if (m_waterMeshGpu.IsInitialized())
	    {
	        const engine::world::water::WaterScene* scene = nullptr;
	        bool dirty = false;
	        if (m_worldEditorExe && m_worldEditorShell)
	        {
	            scene = &m_worldEditorShell->GetWaterDocument().Get();
	            dirty = m_worldEditorShell->GetWaterDocument().IsDirty();
	        }
	        else if (m_clientWaterScene)
	        {
	            scene = m_clientWaterScene.get();
	            dirty = m_waterClientSceneDirty;
	        }
	        if (scene && dirty)
	        {
	            // Réutilise le pool long-lived créé au boot pour éviter un create/destroy par frame.
	            if (m_waterTransferPool == VK_NULL_HANDLE)
	            {
	                LOG_WARN(Render, "[Water] m_waterTransferPool non disponible — skipping rebuild this frame");
	            }
	            else
	            {
	                // Reset cheap : libère les command buffers passés sans détruire le pool.
	                vkResetCommandPool(m_vkDeviceContext.GetDevice(), m_waterTransferPool, 0);
	                if (m_waterMeshGpu.Rebuild(m_waterTransferPool, m_vkDeviceContext.GetGraphicsQueue(), *scene))
	                {
	                    if (m_worldEditorExe && m_worldEditorShell)
	                        m_worldEditorShell->MutableWaterDocument().ClearDirty();
	                    else
	                        m_waterClientSceneDirty = false;
	                }
	            }
	        }
	    }

	    if (m_fgSceneColorHDRId != engine::render::kInvalidResourceId && m_fgBackbufferId != engine::render::kInvalidResourceId)
	    {
	        VkImage     backbufferImage = m_vkSwapchain.GetImage(imageIndex);
	        VkImageView backbufferView  = m_vkSwapchain.GetImageView(imageIndex);
	        m_fgRegistry.bindImage(m_fgBackbufferId, backbufferImage, backbufferView);
	        m_frameGraph.execute(m_vkDeviceContext.GetDevice(), m_vkDeviceContext.GetPhysicalDevice(), m_vmaAllocator, fr.cmdBuffer, m_fgRegistry, frameIndex, extent, 2u, m_vkDeviceContext.SupportsSynchronization2(), m_profiler.IsInitialized() ? &m_profiler : nullptr);

	        // M100.34 incrément 2 — Pont SceneColor_LDR → EditorViewportRenderTarget.
	        // Copie la scène rendue (post-tonemap, post-TAA, AVANT ImGui — donc le
	        // ScenePanel n'affiche pas son propre overlay = pas de récursion) vers
	        // l'image offscreen. Barriers à la main parce qu'on est hors FrameGraph
	        // ici (juste après son execute()).
	        if (m_worldEditorExe && m_editorViewportTarget.IsValid()
	            && m_fgSceneColorLDRId != engine::render::kInvalidResourceId)
	        {
	            VkImage sceneLdr = m_fgRegistry.getImage(m_fgSceneColorLDRId);
	            VkImage dst      = m_editorViewportTarget.GetImage();
	            if (sceneLdr != VK_NULL_HANDLE && dst != VK_NULL_HANDLE)
	            {
	                // Step 1: transitions de layout.
	                //   src (SceneColor_LDR) : `TRANSFER_SRC_OPTIMAL` post-CopyPresent
	                //     (cf. ligne 4538 — le dernier consumer FrameGraph le lit
	                //     en TransferSrc) → on garde tel quel, pas de barrier.
	                //   dst (ma target) : `SHADER_READ_ONLY` (init PR 1) →
	                //     `TRANSFER_DST_OPTIMAL` pour le blit.
	                VkImageMemoryBarrier bDst{};
	                bDst.sType         = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	                bDst.oldLayout     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	                bDst.newLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	                bDst.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
	                bDst.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	                bDst.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	                bDst.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	                bDst.image            = dst;
	                bDst.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
	                vkCmdPipelineBarrier(fr.cmdBuffer,
	                    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
	                    VK_PIPELINE_STAGE_TRANSFER_BIT,
	                    0, 0, nullptr, 0, nullptr, 1, &bDst);

	                // Step 2: vkCmdBlitImage (gère size + format mismatch via
	                // LINEAR filter — utile parce que SceneColor_LDR est au
	                // format swapchain (typiquement BGRA8) et ma target est RGBA8).
	                VkImageBlit region{};
	                region.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
	                region.srcOffsets[0]  = { 0, 0, 0 };
	                region.srcOffsets[1]  = {
	                    static_cast<int32_t>(extent.width),
	                    static_cast<int32_t>(extent.height),
	                    1
	                };
	                region.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
	                region.dstOffsets[0]  = { 0, 0, 0 };
	                region.dstOffsets[1]  = {
	                    static_cast<int32_t>(m_editorViewportTarget.GetWidth()),
	                    static_cast<int32_t>(m_editorViewportTarget.GetHeight()),
	                    1
	                };
	                vkCmdBlitImage(fr.cmdBuffer,
	                    sceneLdr,  VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
	                    dst,       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
	                    1, &region, VK_FILTER_LINEAR);

	                // Step 3: transition retour de dst → SHADER_READ_ONLY pour
	                // qu'ImGui::Image puisse sampler dans le ScenePanel cette
	                // même frame. SceneColor_LDR reste en TRANSFER_SRC ; le
	                // FrameGraph re-transitionnera au prochain frame selon
	                // ses propres `read/write` declarations (auto-géré).
	                VkImageMemoryBarrier bDst2{};
	                bDst2.sType         = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	                bDst2.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	                bDst2.newLayout     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	                bDst2.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	                bDst2.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
	                bDst2.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	                bDst2.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	                bDst2.image            = dst;
	                bDst2.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
	                vkCmdPipelineBarrier(fr.cmdBuffer,
	                    VK_PIPELINE_STAGE_TRANSFER_BIT,
	                    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
	                    0, 0, nullptr, 0, nullptr, 1, &bDst2);
	            }
	        }
	    }
	    LOG_DEBUG(Render, "[DIAG] FrameGraph execute returned frame={}", m_currentFrame);

	    LOG_DEBUG(Render, "[DIAG] vkEndCommandBuffer begin frame={}", m_currentFrame);
	    if (vkEndCommandBuffer(fr.cmdBuffer) != VK_SUCCESS) return;
	    LOG_DEBUG(Render, "[DIAG] vkEndCommandBuffer OK frame={}", m_currentFrame);

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
	    LOG_DEBUG(Render, "[DIAG] vkResetFences begin frame={}", m_currentFrame);
	    vkResetFences(device, 1, &fr.fence);
	    LOG_DEBUG(Render, "[DIAG] vkQueueSubmit begin frame={}", m_currentFrame);
	    VkResult submitResult = vkQueueSubmit(graphicsQueue, 1, &submitInfo, fr.fence);
	    LOG_DEBUG(Render, "[DIAG] vkQueueSubmit result={} frame={}", static_cast<int>(submitResult), m_currentFrame);
	    if (submitResult != VK_SUCCESS) return;

	    VkPresentInfoKHR presentInfo{};
	    presentInfo.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	    presentInfo.waitSemaphoreCount = 1;
	    presentInfo.pWaitSemaphores    = signalSemaphores;
	    presentInfo.swapchainCount     = 1;
	    presentInfo.pSwapchains        = &swapchain;
	    presentInfo.pImageIndices      = &imageIndex;
	    LOG_DEBUG(Render, "[DIAG] vkQueuePresentKHR begin frame={}", m_currentFrame);
	    result = vkQueuePresentKHR(presentQueue, &presentInfo);
	    LOG_DEBUG(Render, "[DIAG] vkQueuePresentKHR result={} frame={}", static_cast<int>(result), m_currentFrame);
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

	    LOG_DEBUG(Render, "[DIAG] Render() complete frame={}", m_currentFrame);
	    m_currentFrame++;
	}

	void Engine::EndFrame()
	{
		// PROFILE_FUNCTION();
		LOG_DEBUG(Render, "[DIAG] EndFrame enter frame={}", m_currentFrame);
		if (m_profiler.IsInitialized())
		{
			m_profiler.EndFrame();
		}
		if (m_currentFrame > 0 && (m_currentFrame % 60) == 0)
			m_chunkStats.LogStats();
		LOG_DEBUG(Render, "[DIAG] EndFrame done frame={}", m_currentFrame);
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

	/// Sous-projet C MVP — Resout race_str (string DB) en pointeur SkinnedMesh*
	/// depuis m_raceMeshes. Si la race demandee n'est pas chargee (asset manquant
	/// ou race hors-MVP), fallback sur "humains" avec un LOG_WARN. Si meme
	/// "humains" est absent (boot rate : SkinnedMeshLoader::Load a echoue pour
	/// toutes les races), retourne nullptr — le caller doit alors retomber sur
	/// le cube placeholder via m_skinnedAvatarReady = false.
	///
	/// Effet de bord : aucun (lecture seule sur m_raceMeshes + log).
	/// Thread : main thread uniquement (m_raceMeshes n'est ni mute ni protege).
	engine::render::skinned::SkinnedMesh* Engine::GetRaceMesh(const std::string& raceId)
	{
		return GetRaceMesh(raceId, m_avatarGender);
	}

	engine::render::skinned::SkinnedMesh* Engine::GetRaceMesh(const std::string& raceId,
	                                                          const std::string& gender)
	{
		// 1. Mesh exact race+genre demande.
		auto it = m_raceMeshes.find(RaceMeshKey(raceId, gender));
		if (it != m_raceMeshes.end()) return &it->second;
		// 2. Pas de variante pour ce genre (ex. femelle absente pour nains/orcs)
		//    -> male de la meme race (toujours charge).
		if (gender != "male") {
			auto maleIt = m_raceMeshes.find(RaceMeshKey(raceId, "male"));
			if (maleIt != m_raceMeshes.end()) {
				return &maleIt->second;
			}
		}
		// 3. Race inconnue / non chargee -> humains male.
		auto humansIt = m_raceMeshes.find(RaceMeshKey("humains", "male"));
		if (humansIt != m_raceMeshes.end()) {
			LOG_WARN(Render, "[Engine] GetRaceMesh('{}','{}') fallback humains|male", raceId, gender);
			return &humansIt->second;
		}
		// 4. Boot rate : meme humains|male absent -> cube placeholder.
		LOG_ERROR(Render, "[Engine] GetRaceMesh('{}','{}') ECHEC (humains|male absent)", raceId, gender);
		return nullptr;
	}

	void Engine::LoadRespawnMarkers()
	{
		m_respawnMarkers.clear();
		// Marqueurs de la zone active (game/data/zones/<zone>/respawn_points.txt) si
		// world.active_zone est défini, sinon repli sur l'ancien chemin global.
		// Le parseur ci-dessous ignore les colonnes faction/rayon (réservées au shard).
		const std::string activeZone = m_cfg.GetString("world.active_zone", "");
		const std::string markersPath = activeZone.empty()
			? m_cfg.GetString("server.respawn_points_path", "respawn/respawn_points.txt")
			: ("zones/" + activeZone + "/respawn_points.txt");
		const std::string markersText = engine::platform::FileSystem::ReadAllTextContent(m_cfg, markersPath);
		if (markersText.empty())
		{
			LOG_WARN(Core, "[Engine] Marqueurs de réapparition absents ({})", markersPath);
			return;
		}
		std::istringstream input(markersText);
		std::string line;
		while (std::getline(input, line))
		{
			if (line.empty() || line[0] == '#')
				continue;
			std::istringstream lineStream(line);
			RespawnMarker marker{};
			std::string typeToken;
			float unusedY = 0.0f;
			if (!(lineStream >> marker.zoneId >> typeToken >> marker.x >> unusedY >> marker.z))
				continue;
			if (typeToken == "graveyard")
				marker.destinationType = 0;
			else if (typeToken == "inn")
				marker.destinationType = 1;
			else
				continue;
			m_respawnMarkers.push_back(marker);
		}
		LOG_INFO(Core, "[Engine] Marqueurs de réapparition charges ({})", m_respawnMarkers.size());
	}

	float Engine::ResolveRemoteDisplayCenterY(bool isPlayer, float serverY,
	                                          float worldX, float worldZ) const
	{
		if (isPlayer)
		{
			return serverY; // joueur : centre capsule fiable répliqué par le shard
		}
		// Mob / node / loot bag : Y de spawn brut (souvent 0) → snap visuel au sol
		// client. SampleHeightAtWorldXZ retombe sur 0 si la heightmap n'est pas
		// chargée — comportement identique à l'ancien rendu dans ce cas.
		return m_terrain.SampleHeightAtWorldXZ(worldX, worldZ) + 0.9f;
	}

	void Engine::SetAvatarGender(const std::string& gender)
	{
		if (gender != "male" && gender != "female") {
			LOG_WARN(Core, "[Avatar] SetAvatarGender('{}') ignore (attendu male/female)", gender);
			return;
		}
		if (gender == m_avatarGender)
			return;
		m_avatarGender = gender;
		// Runtime : la valeur sert au prochain EnterWorld (mesh) et aux apercus.
		m_cfg.SetValue("client.character_creation.gender", gender);
		// Persistance : fichier dedie merge au boot (comme keybinds.json) -> le
		// choix survit au relog. JSON imbrique = aplati en client.character_creation.gender.
		const std::string json =
			std::string("{\n  \"client\": {\n    \"character_creation\": {\n")
			+ "      \"gender\": \"" + gender + "\"\n"
			+ "    }\n  }\n}\n";
		if (!engine::platform::FileSystem::WriteAllText("character_appearance.json", json))
			LOG_WARN(Core, "[Avatar] character_appearance.json non ecrit (genre non persiste)");
		LOG_INFO(Core, "[Avatar] Genre actif -> {}", gender);
	}

	void Engine::SetCharacterGender(const std::string& characterName, const std::string& gender)
	{
		if (gender != "male" && gender != "female") {
			LOG_WARN(Core, "[Avatar] SetCharacterGender('{}','{}') ignore (genre attendu male/female)",
				characterName, gender);
			return;
		}
		if (characterName.empty()) {
			// Cas limite (nom vide) : repli sur le genre global de session.
			SetAvatarGender(gender);
			return;
		}
		// Session : applique tout de suite. Le 1er EnterWorld apres creation lit
		// m_avatarGender (la persistance ci-dessous sert surtout au relog ulterieur).
		if (gender != m_avatarGender) {
			m_avatarGender = gender;
			m_avatarSkinDiagLoggedGender.clear();  // force le re-log du diagnostic peau
		}
		m_cfg.SetValue("client.character_creation.gender", gender);
		m_cfg.SetValue("characters." + characterName + ".gender", gender);

		// Persistance : rebatit character_appearance.json = genre global (defaut) +
		// map genre-par-personnage (characters.<nom>.gender), pour que l'EnterWorld
		// d'un perso existant retrouve son genre au relog. Fix client interim ; le
		// stockage serveur (DB) remplacera ce fichier le moment venu.
		std::unordered_map<std::string, std::string> genders;
		for (const auto& kv : m_cfg.GetStringMapUnderPrefix("characters")) {
			const std::string::size_type dot = kv.first.rfind('.');
			if (dot == std::string::npos) continue;
			if (kv.first.substr(dot + 1) != "gender") continue;
			genders[kv.first.substr(0, dot)] = kv.second;
		}
		genders[characterName] = gender;

		auto jsonEscape = [](const std::string& s) {
			std::string out;
			out.reserve(s.size());
			for (char c : s) {
				if (c == '\\' || c == '"') out.push_back('\\');
				out.push_back(c);
			}
			return out;
		};

		std::string json =
			"{\n  \"client\": {\n    \"character_creation\": {\n      \"gender\": \""
			+ gender + "\"\n    }\n  },\n  \"characters\": {\n";
		std::size_t i = 0;
		for (const auto& kv : genders) {
			json += "    \"" + jsonEscape(kv.first) + "\": { \"gender\": \"" + kv.second + "\" }";
			json += (++i < genders.size()) ? ",\n" : "\n";
		}
		json += "  }\n}\n";

		if (!engine::platform::FileSystem::WriteAllText("character_appearance.json", json))
			LOG_WARN(Core, "[Avatar] character_appearance.json non ecrit (genre perso non persiste)");
		LOG_INFO(Core, "[Avatar] Genre perso '{}' -> {} (persiste, {} perso(s))",
			characterName, gender, genders.size());
	}

	void Engine::LoadInteractableProps()
	{
		m_props.clear();
		m_trimMatCache.clear();
		// Le collisionneur composite est (re)construit ici : terrain + cylindres des
		// props interactifs (coffre, villageois) puis du décor (LoadScenery, appelé après).
		m_worldCollider.SetTerrain(&m_terrainCollider);
		m_worldCollider.ClearCylinders();
		if (!m_pipeline) return;

		for (std::size_t ii = 0; ii < m_interactables.size(); ++ii)
		{
			const InteractableEntity& e = m_interactables[ii];
			if (e.meshPath.empty()) continue;
			// Le coffre anime (LoadAnimatedChest) est rendu via le pipeline skinne ; on
			// saute son rendu statique (mais on lui pose un cylindre de collision)
			// UNIQUEMENT pour l'interactible EXACT charge en anime (m_chestInteractableIndex).
			// Les autres coffres eventuels restent rendus en statique normalement.
			if (m_chestLoaded && static_cast<int>(ii) == m_chestInteractableIndex)
			{
				const float gy = m_terrainCollider.GroundHeightAt(e.position.x, e.position.z);
				m_worldCollider.AddCylinder(engine::gameplay::PropCylinder{
					e.position.x, e.position.z, 0.6f, gy, gy + 1.0f });
				continue;
			}
			// Interactibles solides (coffre, PNJ...) : collision auto (empreinte XZ du mesh).
			BuildPropFromMesh(e.meshPath, e.position.x, e.position.z, e.meshYawDeg,
			                  e.meshRotXDeg, e.meshScale, static_cast<int>(ii),
			                  /*solid*/ true, /*collisionRadius*/ 0.0f);
		}
		LOG_INFO(Render, "[Props] {} prop(s) interactif(s) rendus", m_props.size());
	}

	void Engine::LoadScenery()
	{
		m_scenery.clear();
		if (!m_pipeline) return;
		const int n = static_cast<int>(m_cfg.GetInt("world.scenery.count", 0));
		for (int i = 0; i < n; ++i)
		{
			const std::string base = "world.scenery." + std::to_string(i) + ".";
			SceneryInstance s;
			s.meshPath = m_cfg.GetString(base + "mesh", "");
			if (s.meshPath.empty()) continue;
			s.x = static_cast<float>(m_cfg.GetDouble(base + "x", 0.0));
			s.z = static_cast<float>(m_cfg.GetDouble(base + "z", 0.0));
			s.yawDeg = static_cast<float>(m_cfg.GetDouble(base + "yaw_deg", 0.0));
			s.scale = static_cast<float>(m_cfg.GetDouble(base + "scale", 1.0));
			s.collisionRadius = static_cast<float>(m_cfg.GetDouble(base + "collision_radius", 0.0));
			s.solid = m_cfg.GetBool(base + "solid", true);
			m_scenery.push_back(s);
		}
		for (const auto& s : m_scenery)
			BuildPropFromMesh(s.meshPath, s.x, s.z, s.yawDeg, /*rotXDeg*/ 0.0f, s.scale,
			                  /*interactableIndex*/ -1, /*solid*/ s.solid, s.collisionRadius);
		LOG_INFO(Render, "[Scenery] {} element(s) decor charge(s) ; {} cylindre(s) collision",
		         static_cast<int>(m_scenery.size()), static_cast<int>(m_worldCollider.CylinderCount()));
	}

	void Engine::LoadAnimatedChest()
	{
		m_chestLoaded = false;
		if (!m_pipeline) return;
		auto& materialCache = m_pipeline->GetMaterialDescriptorCache();
		if (!materialCache.IsValid()) return;
		const std::string contentRoot = m_cfg.GetString("paths.content", "game/data");

		const InteractableEntity* chest = nullptr;
		int chestIdx = -1;
		for (std::size_t i = 0; i < m_interactables.size(); ++i)
			if (m_interactables[i].meshPath.find("Chest_Wood") != std::string::npos)
			{
				chest = &m_interactables[i];
				chestIdx = static_cast<int>(i);
				break;
			}
		if (!chest) return;

		const std::string full = contentRoot + "/" + chest->meshPath;
		auto loaded = engine::render::skinned::SkinnedMeshLoader::Load(
			m_vkDeviceContext.GetDevice(), m_vkDeviceContext.GetPhysicalDevice(), full);
		if (!loaded)
		{
			LOG_WARN(Render, "[Chest] chargement skinne ECHEC '{}' (coffre non anime)", full);
			return;
		}
		m_chestMesh = std::move(*loaded);
		m_chestInteractableIndex = chestIdx;
		m_chestPos = engine::math::Vec3{ chest->position.x,
			m_terrainCollider.GroundHeightAt(chest->position.x, chest->position.z), chest->position.z };
		m_chestYawDeg = chest->meshYawDeg;
		m_chestScale = chest->meshScale;
		m_chestRotXDeg = chest->meshRotXDeg;

		std::string meshDir;
		{
			const auto sl = chest->meshPath.find_last_of('/');
			if (sl != std::string::npos) meshDir = chest->meshPath.substr(0, sl + 1);
		}
		auto makeTrim = [&](const std::string& trimType) -> uint32_t
		{
			const engine::render::TextureHandle bc =
				m_assetRegistry.LoadTexture(meshDir + "T_Trim_" + trimType + "_BaseColor.png", true);
			if (!bc.IsValid()) return materialCache.GetDefaultMaterialIndex();
			engine::render::Material mat{};
			mat.baseColor = bc;
			const engine::render::TextureHandle n =
				m_assetRegistry.LoadTexture(meshDir + "T_Trim_" + trimType + "_Normal.png", false);
			if (n.IsValid()) mat.normal = n;
			const engine::render::TextureHandle o =
				m_assetRegistry.LoadTexture(meshDir + "T_Trim_" + trimType + "_ORM.png", false);
			if (o.IsValid()) mat.orm = o;
			return materialCache.CreateMaterial(m_vkDeviceContext.GetDevice(), mat);
		};
		m_chestMatFurniture = makeTrim("Furniture");
		m_chestMatMetal = makeTrim("Metal");

		m_chestSubmeshMat.clear();
		for (const auto& sm : m_chestMesh.submeshes)
			m_chestSubmeshMat.push_back(
				sm.materialName.find("Metal") != std::string::npos ? m_chestMatMetal : m_chestMatFurniture);

		m_chestLoaded = true;
		LOG_INFO(Render,
			"[Chest] coffre anime charge ({} bones, {} clips, {} sous-maillages)",
			static_cast<int>(m_chestMesh.skeleton.bones.size()),
			static_cast<int>(m_chestMesh.clips.size()),
			static_cast<int>(m_chestMesh.submeshes.size()));
	}

	void Engine::RecordAnimatedChest(VkCommandBuffer cmd, engine::render::Registry& reg,
	                                 const engine::RenderState& rs)
	{
		if (!m_chestLoaded || !m_pipeline || !m_skinnedRenderer.IsValid()) return;
		if (m_chestMesh.skeleton.bones.empty()) return;
		auto& materialCache = m_pipeline->GetMaterialDescriptorCache();
		if (!materialCache.IsValid()) return;

		const float nowSec = EngineNowSec();
		auto locals  = m_chestCrossfade.Sample(m_chestMesh.skeleton, nowSec);
		auto globals = engine::render::skinned::AnimationSampler::ComputeGlobalMatrices(m_chestMesh.skeleton, locals);
		auto finals  = engine::render::skinned::AnimationSampler::ComputeFinalMatrices(m_chestMesh.skeleton, globals);

		constexpr float kDeg2Rad = 3.14159265f / 180.f;
		// Meme composition que BuildPropFromMesh : T * Ry(yaw) * Rx(rotX) * Scale * import,
		// pour honorer les champs scale / rot_x_deg de l'interactable coffre.
		engine::math::Mat4 scaleM = engine::math::Mat4::Identity();
		scaleM.m[0] = m_chestScale; scaleM.m[5] = m_chestScale; scaleM.m[10] = m_chestScale;
		const engine::math::Mat4 model =
			engine::math::Mat4::Translate(m_chestPos) *
			engine::math::Mat4::RotateY(m_chestYawDeg * kDeg2Rad) *
			engine::math::Mat4::RotateX(m_chestRotXDeg * kDeg2Rad) *
			scaleM *
			m_chestMesh.importTransform;

		std::vector<uint32_t> subMat = m_chestSubmeshMat;
		m_pipeline->GetGeometryPass().RecordTerrainChunkBatch(
			m_vkDeviceContext.GetDevice(), cmd, reg, m_vkSwapchain.GetExtent(),
			m_fgGBufferAId, m_fgGBufferBId, m_fgGBufferCId, m_fgGBufferVelocityId, m_fgDepthId,
			[this, &rs, finals = std::move(finals), &materialCache, model,
			 subMat = std::move(subMat)](VkCommandBuffer innerCmd) {
				m_skinnedRenderer.Record(
					m_vkDeviceContext.GetDevice(), innerCmd, m_vkSwapchain.GetExtent(),
					m_pipeline->GetGeometryPass().GetRenderPassLoad(), VK_NULL_HANDLE,
					rs.prevViewProjMatrix.m, rs.viewProjMatrix.m,
					m_chestMesh, finals, materialCache.GetDescriptorSet(),
					model.m, m_chestMatFurniture, subMat, m_chestMatMetal, 0.0f, 0.0f);
			});
	}

	// Charge un mesh statique (prop/décor), CUIT sa transformation monde dans les
	// sommets (contournement du buffer d'instance partagé, cf. plus bas), l'ancre au
	// sol, crée ses matériaux et l'ajoute à m_props. Si \p solid, enregistre un
	// cylindre de collision dans m_worldCollider.
	// \param wx,wz             Position monde (m) ; Y posé au sol via GroundHeightAt.
	// \param yawDeg,rotXDeg    Rotations (degrés) ; \param scale échelle uniforme.
	// \param interactableIndex Index dans m_interactables (-1 pour le décor non interactif).
	// \param solid             Si true, ajoute un cylindre de collision.
	// \param collisionRadius   Rayon du cylindre (m) ; 0 = empreinte XZ auto du mesh.
	void Engine::BuildPropFromMesh(const std::string& meshPath, float wx, float wz,
		float yawDeg, float rotXDeg, float scale, int interactableIndex,
		bool solid, float collisionRadius)
	{
		if (!m_pipeline) return;
		auto& materialCache = m_pipeline->GetMaterialDescriptorCache();
		if (!materialCache.IsValid()) return;
		const std::string contentRoot = m_cfg.GetString("paths.content", "game/data");
		constexpr float kDeg2Rad = 3.14159265f / 180.f;

		const std::string full = contentRoot + "/" + meshPath;
		auto cpu = engine::render::staticmesh::StaticMeshLoader::LoadCpuOnlyForTests(full);
		if (!cpu || cpu->vertices.empty() || cpu->submeshes.empty())
		{
			LOG_WARN(Render, "[Props] mesh load FAIL '{}'", full);
			return;
		}
		// Dossier du mesh : les URI de textures sont relatives au .gltf.
		std::string meshDir;
		{
			const auto slash = meshPath.find_last_of('/');
			if (slash != std::string::npos) meshDir = meshPath.substr(0, slash + 1);
		}
		// Groupe les indices par nom de materiau + un sous-maillage representatif (URIs).
		std::unordered_map<std::string, std::vector<uint32_t>> idxByMat;
		std::unordered_map<std::string, const engine::render::staticmesh::StaticSubMesh*> repByMat;
		for (const auto& sub : cpu->submeshes)
		{
			std::vector<uint32_t>& v = idxByMat[sub.materialName];
			v.insert(v.end(), cpu->indices.begin() + sub.firstIndex,
			         cpu->indices.begin() + sub.firstIndex + sub.indexCount);
			if (repByMat.find(sub.materialName) == repByMat.end()) repByMat[sub.materialName] = &sub;
		}

		PropRenderable prop;
		const float groundY = m_terrainCollider.GroundHeightAt(wx, wz);
		engine::math::Mat4 scaleM = engine::math::Mat4::Identity();
		scaleM.m[0] = scale; scaleM.m[5] = scale; scaleM.m[10] = scale;
		// Matrice modèle monde du prop (translation au sol + yaw + rotX + échelle).
		const engine::math::Mat4 bakeM =
			engine::math::Mat4::Translate(engine::math::Vec3{ wx, groundY, wz }) *
			engine::math::Mat4::RotateY(yawDeg * kDeg2Rad) *
			engine::math::Mat4::RotateX(rotXDeg * kDeg2Rad) *
			scaleM;
		// CONTOURNEMENT bug moteur : GeometryPass::Record applique la matrice d'instance
		// via un buffer GPU PARTAGE (m_identityInstanceBuffer) reecrit a chaque draw au
		// moment du RECORD. Avec plusieurs props dans le meme command buffer, seule la
		// DERNIERE matrice survit a l'execution -> tous les props se dessinent au meme
		// endroit (empiles). On contourne en CUISANT la transformation monde directement
		// dans les sommets (chaque prop a deja son propre mesh CPU), puis on laisse la
		// matrice d'instance a l'identite.
		float topY = groundY;     // haut du cylindre de collision (monde)
		float radiusAuto = 0.0f;  // empreinte XZ max autour de (wx, wz)
		{
			const float* M = bakeM.m;  // column-major : M[col*4+row]
			float minY = 1e30f, maxY = -1e30f;
			// AABB monde complète (anti-occlusion caméra) — XZ figés ici (le lift
			// ne décale que Y, appliqué plus bas aux bornes minY/maxY).
			float bbMinX = 1e30f, bbMaxX = -1e30f, bbMinZ = 1e30f, bbMaxZ = -1e30f;
			for (auto& v : cpu->vertices)
			{
				const float px = v.pos[0], py = v.pos[1], pz = v.pos[2];
				v.pos[0] = M[0]*px + M[4]*py + M[8]*pz  + M[12];
				v.pos[1] = M[1]*px + M[5]*py + M[9]*pz  + M[13];
				v.pos[2] = M[2]*px + M[6]*py + M[10]*pz + M[14];
				if (v.pos[1] < minY) minY = v.pos[1];
				if (v.pos[1] > maxY) maxY = v.pos[1];
				if (v.pos[0] < bbMinX) bbMinX = v.pos[0];
				if (v.pos[0] > bbMaxX) bbMaxX = v.pos[0];
				if (v.pos[2] < bbMinZ) bbMinZ = v.pos[2];
				if (v.pos[2] > bbMaxZ) bbMaxZ = v.pos[2];
				const float nx = v.normal[0], ny = v.normal[1], nz = v.normal[2];
				float rnx = M[0]*nx + M[4]*ny + M[8]*nz;
				float rny = M[1]*nx + M[5]*ny + M[9]*nz;
				float rnz = M[2]*nx + M[6]*ny + M[10]*nz;
				const float nlen = std::sqrt(rnx*rnx + rny*rny + rnz*rnz);
				if (nlen > 1e-6f) { rnx /= nlen; rny /= nlen; rnz /= nlen; }
				v.normal[0] = rnx; v.normal[1] = rny; v.normal[2] = rnz;
			}
			// Ancrage au sol : selon le modele, le pivot n'est pas a la base
			// -> le prop s'enterre (ou flotte). On decale tout le mesh pour que
			// son point le PLUS BAS repose exactement sur le sol (groundY).
			const float lift = groundY - minY;
			if (std::fabs(lift) > 1e-4f)
				for (auto& v : cpu->vertices)
					v.pos[1] += lift;
			topY = maxY + lift;
			// Anti-occlusion caméra — sphère englobante monde (AABB des sommets
			// bakés, Y décalé du lift). Sert d'occulteur dans CameraOcclusionFade.
			{
				const float wMinY = minY + lift, wMaxY = maxY + lift;
				prop.occluderCenter = engine::math::Vec3{
					0.5f * (bbMinX + bbMaxX), 0.5f * (wMinY + wMaxY),
					0.5f * (bbMinZ + bbMaxZ) };
				const engine::math::Vec3 ext{
					bbMaxX - bbMinX, wMaxY - wMinY, bbMaxZ - bbMinZ };
				prop.occluderRadius =
					0.5f * std::sqrt(ext.x * ext.x + ext.y * ext.y + ext.z * ext.z);
			}
			// Empreinte XZ auto (rayon max des sommets autour de l'axe (wx,wz)).
			for (const auto& v : cpu->vertices)
			{
				const float ddx = v.pos[0] - wx, ddz = v.pos[2] - wz;
				const float r = std::sqrt(ddx*ddx + ddz*ddz);
				if (r > radiusAuto) radiusAuto = r;
			}
		}
		prop.modelMatrix = engine::math::Mat4::Identity();  // sommets deja en espace monde

		// M45.5 — sphère englobante monde du prop (centre du billboard impostor +
		// rayon = demi-taille). Le centre XZ est (wx,wz) ; en Y, milieu entre le
		// sol (groundY) et le sommet (topY). Le rayon couvre largeur (radiusAuto)
		// ET hauteur (demi-hauteur), pour un quad qui englobe tout le mesh.
		prop.meshPath = meshPath;
		{
			const float halfHeight = 0.5f * (topY - groundY);
			prop.impostorCenter = engine::math::Vec3{ wx, groundY + halfHeight, wz };
			prop.impostorRadius = std::max(radiusAuto, halfHeight);
			if (prop.impostorRadius < 0.05f) prop.impostorRadius = 0.5f;
		}

		for (const auto& kv : idxByMat)
		{
			const std::string& matName = kv.first;
			const std::vector<uint32_t>& idxs = kv.second;
			if (idxs.empty()) continue;
			engine::render::MeshHandle mh = m_assetRegistry.CreateMeshFromData(
				cpu->vertices.data(), static_cast<uint32_t>(cpu->vertices.size()),
				idxs.data(), static_cast<uint32_t>(idxs.size()));
			if (!mh.IsValid()) continue;

			uint32_t matIdx = materialCache.GetDefaultMaterialIndex();
			uint32_t hlIdx  = matIdx;
			auto cached = m_trimMatCache.find(matName);
			if (cached != m_trimMatCache.end())
			{
				matIdx = cached->second.first;
				hlIdx  = cached->second.second;
			}
			else
			{
				const engine::render::staticmesh::StaticSubMesh* rep = repByMat[matName];
				if (rep && !rep->baseColorUri.empty())
				{
					const engine::render::TextureHandle bc =
						m_assetRegistry.LoadTexture(meshDir + rep->baseColorUri, /*useSrgb*/ true);
					if (bc.IsValid())
					{
						engine::render::Material mat{};
						mat.baseColor = bc;
						if (!rep->normalUri.empty())
						{
							const engine::render::TextureHandle n = m_assetRegistry.LoadTexture(meshDir + rep->normalUri, false);
							if (n.IsValid()) mat.normal = n;
						}
						if (!rep->ormUri.empty())
						{
							const engine::render::TextureHandle o = m_assetRegistry.LoadTexture(meshDir + rep->ormUri, false);
							if (o.IsValid()) mat.orm = o;
						}
						// Feuillages (alphaMode != opaque) : decoupe alpha au fragment shader.
						if (rep->alphaCutout)
							mat.flags = engine::render::MaterialFlags::AlphaCutout;
						matIdx = materialCache.CreateMaterial(m_vkDeviceContext.GetDevice(), mat);
						// Variante surbrillance : memes textures + Highlight (+ cutout eventuel).
						mat.flags = static_cast<engine::render::MaterialFlags>(
							static_cast<uint32_t>(mat.flags) | static_cast<uint32_t>(engine::render::MaterialFlags::Highlight));
						hlIdx = materialCache.CreateMaterial(m_vkDeviceContext.GetDevice(), mat);
					}
				}
				else
				{
					// Pas de texture baseColor (props « nature » : arbres, herbe...) :
					// l'albedo provient de la couleur de sommet COLOR_0 (flag VertexColorAlbedo).
					engine::render::Material mat{};
					mat.flags = engine::render::MaterialFlags::VertexColorAlbedo;
					matIdx = materialCache.CreateMaterial(m_vkDeviceContext.GetDevice(), mat);
					hlIdx = matIdx;
				}
				m_trimMatCache[matName] = { matIdx, hlIdx };
			}

			PropPart part;
			part.mesh = mh;
			part.materialIndex = matIdx;
			part.highlightMaterialIndex = hlIdx;
			prop.parts.push_back(part);
		}

		prop.interactableIndex = interactableIndex;
		prop.worldPos = engine::math::Vec3{ wx, groundY, wz };
		if (!prop.parts.empty())
		{
			if (solid)
			{
				float radius = collisionRadius > 0.0f ? collisionRadius : radiusAuto;
				if (radius < 0.05f) radius = 0.5f;
				// Porte/escalier reconnus par le nom du mesh (insensible à la casse) :
				// « door » → passage franchissable (aucune collision) ; « escalier »
				// (ou « stair ») → surface gravissable. Le décor normal reste un mur plein.
				std::string meshLower = meshPath;
				for (char& ch : meshLower)
				{
					if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
				}
				engine::gameplay::PropCylinder cyl{ wx, wz, radius, groundY, topY };
				cyl.passable = meshLower.find("door") != std::string::npos;
				cyl.stair = (meshLower.find("escalier") != std::string::npos)
					|| (meshLower.find("stair") != std::string::npos);
				m_worldCollider.AddCylinder(cyl);
			}
			m_props.push_back(std::move(prop));
		}
	}

	void Engine::LoadBuildings()
	{
		if (!m_pipeline) return;
		namespace fs = std::filesystem;
		const std::string contentRoot = m_cfg.GetString("paths.content", "game/data");

		// Catalogue de collision des pièces (boîtes fines + portes passables).
		// Absent -> catalogue vide -> fallback cylindre (rétro-compatible).
		{
			const std::string catPath = contentRoot + "/collision/building_pieces.json";
			const std::string catJson = engine::platform::FileSystem::ReadAllText(catPath);
			if (!catJson.empty() && m_buildingCollisionCatalog.LoadFromJson(catJson))
				LOG_INFO(Render, "[Buildings] catalogue collision chargé ('{}')", catPath);
			else
				LOG_DEBUG(Render, "[Buildings] pas de catalogue collision ('{}') -> fallback cylindre", catPath);
		}

		const std::string zoneId = m_cfg.GetString("world.active_zone", "");

		// Résolution du chemin : namespacé par zone si présent, sinon legacy plat.
		fs::path path;
		if (!zoneId.empty())
		{
			const fs::path ns = fs::path(contentRoot) / "instances" /
				("zone_" + zoneId) / "buildings.bin";
			std::error_code ec;
			path = fs::exists(ns, ec) ? ns
				: fs::path(contentRoot) / "instances" / "buildings.bin";
		}
		else
		{
			path = fs::path(contentRoot) / "instances" / "buildings.bin";
		}

		std::ifstream f(path, std::ios::binary | std::ios::ate);
		if (!f.good())
		{
			// Absence de fichier = cas normal (zone sans bâtiment) -> Debug.
			LOG_DEBUG(Render, "[Buildings] aucun buildings.bin ('{}')", path.string());
			return;
		}
		const std::streamsize size = f.tellg();
		if (size <= 0) return;
		f.seekg(0, std::ios::beg);
		std::vector<uint8_t> bytes(static_cast<size_t>(size));
		f.read(reinterpret_cast<char*>(bytes.data()), size);

		std::vector<engine::world::instances::BuildingPlacement> placements;
		std::string err;
		if (!engine::world::instances::LoadBuildingsBin(
				std::span<const uint8_t>(bytes), placements, err))
		{
			LOG_WARN(Render, "[Buildings] buildings.bin invalide : {}", err);
			return;
		}

		// Bibliothèque des types de bâtiments : la carte ne stocke que des
		// références (type + variante) ; on résout ici contre les fichiers
		// `buildings/templates/<type>.json` pour obtenir les pièces.
		engine::world::instances::BuildingTemplateLibrary library;
		std::string libErr;
		if (!library.LoadFromContent(contentRoot, libErr) && !libErr.empty())
			LOG_WARN(Render, "[Buildings] bibliotheque partiellement chargee : {}", libErr);

		constexpr float kDeg2Rad = 3.14159265f / 180.f;
		// Helpers de composition (Math.h ne fournit ni Scale ni RotateZ).
		auto scaleMat = [](float s) {
			engine::math::Mat4 m = engine::math::Mat4::Identity();
			m.m[0] = s; m.m[5] = s; m.m[10] = s; return m;
		};
		auto rotZ = [](float r) {
			engine::math::Mat4 m = engine::math::Mat4::Identity();
			const float c = std::cos(r), s = std::sin(r);
			m.m[0] = c; m.m[1] = s; m.m[4] = -s; m.m[5] = c; return m;
		};

		int partCount = 0, resolved = 0;
		for (const auto& pl : placements)
		{
			const engine::world::instances::BuildingVariant* variant =
				library.Resolve(pl.templateType, pl.variantId);
			if (!variant)
			{
				LOG_WARN(Render, "[Buildings] reference non resolue: type='{}' variante='{}'",
					pl.templateType, pl.variantId);
				continue;
			}
			++resolved;
			// L'origine du bâtiment est ancrée au sol UNE fois ; les pièces
			// conservent ensuite leur Y local (toit/étage empilés).
			const float groundY = m_terrainCollider.GroundHeightAt(
				pl.worldPosition.x, pl.worldPosition.z);
			const engine::math::Mat4 groupM =
				engine::math::Mat4::Translate(engine::math::Vec3{
					pl.worldPosition.x, groundY + pl.worldPosition.y, pl.worldPosition.z }) *
				engine::math::Mat4::RotateY(pl.worldYawDeg * kDeg2Rad) *
				scaleMat(pl.worldScale);

			for (const auto& pt : variant->parts)
			{
				if (pt.gltfRelativePath.empty()) continue;
				const engine::math::Mat4 localM =
					engine::math::Mat4::Translate(pt.localPosition) *
					engine::math::Mat4::RotateY(pt.localEulerDeg.y * kDeg2Rad) *
					engine::math::Mat4::RotateX(pt.localEulerDeg.x * kDeg2Rad) *
					rotZ(pt.localEulerDeg.z * kDeg2Rad) *
					scaleMat(pt.localScale);
				const engine::math::Mat4 worldM = groupM * localM;
				BuildPropFromMeshMatrix(pt.gltfRelativePath, worldM,
					/*interactableIndex*/ -1, pt.solid, pt.collisionRadius);
				++partCount;
			}
		}
		LOG_INFO(Render, "[Buildings] {} placement(s), {} resolu(s), {} piece(s) rendue(s)",
			static_cast<int>(placements.size()), resolved, partCount);
	}

	void Engine::SyncEditorBuildingPreview()
	{
		if (!m_worldEditorShell || !m_pipeline) return;
		engine::editor::world::panels::BuildingEditorPanel* panel =
			m_worldEditorShell->GetBuildingEditorPanel();
		if (!panel) return;

		// Demande de re-centrage (ex: « Charger dans l'éditeur ») : on invalide
		// l'origine stable pour que la variante fraîchement chargée apparaisse
		// devant la caméra, pas à l'ancienne position (hors-champ).
		if (panel->ConsumeRecenterRequest())
			m_editorPreviewValid = false;

		// Edge-triggered : ne reconstruit qu'après un changement (évite la
		// création de ressources GPU à chaque frame). Pendant un drag du gizmo,
		// les mutations sont silencieuses (pas de dirty) ; le mesh n'est donc
		// reconstruit qu'au relâchement (MarkPreviewDirty), pas à chaque frame —
		// indispensable car BuildPropFromMeshMatrix ne libère pas les meshes GPU.
		if (!panel->ConsumePreviewDirty()) return;

		// On NE touche QU'AU brouillon : on retire les props d'aperçu ajoutés
		// précédemment (tout ce qui dépasse le « monde » chargé au boot : décor
		// + bâtiments posés), sans effacer ce monde.
		if (m_props.size() > m_editorBaselinePropCount)
			m_props.resize(m_editorBaselinePropCount);

		constexpr float kDeg2Rad = 3.14159265f / 180.f;
		auto scaleMat = [](float s) {
			engine::math::Mat4 m = engine::math::Mat4::Identity();
			m.m[0] = s; m.m[5] = s; m.m[10] = s; return m;
		};
		auto rotZ = [](float r) {
			engine::math::Mat4 m = engine::math::Mat4::Identity();
			const float c = std::cos(r), s = std::sin(r);
			m.m[0] = c; m.m[1] = s; m.m[4] = -s; m.m[5] = c; return m;
		};
		auto emitParts = [&](const std::vector<engine::world::instances::BuildingPart>& parts,
			float ox, float oz, float oyaw, float oscale)
		{
			const float groundY = m_terrainCollider.GroundHeightAt(ox, oz);
			const engine::math::Mat4 groupM =
				engine::math::Mat4::Translate(engine::math::Vec3{ ox, groundY, oz }) *
				engine::math::Mat4::RotateY(oyaw * kDeg2Rad) * scaleMat(oscale);
			for (const auto& pt : parts)
			{
				if (pt.gltfRelativePath.empty()) continue;
				const engine::math::Mat4 localM =
					engine::math::Mat4::Translate(pt.localPosition) *
					engine::math::Mat4::RotateY(pt.localEulerDeg.y * kDeg2Rad) *
					engine::math::Mat4::RotateX(pt.localEulerDeg.x * kDeg2Rad) *
					rotZ(pt.localEulerDeg.z * kDeg2Rad) * scaleMat(pt.localScale);
				BuildPropFromMeshMatrix(pt.gltfRelativePath, groupM * localM,
					/*interactableIndex*/ -1, pt.solid, pt.collisionRadius);
			}
		};

		// Origine du brouillon. PREMIÈRE apparition (m_editorPreviewValid==false,
		// ex. première pièce ajoutée ou variante chargée) : on la place au point
		// que REGARDE la caméra (projection du rayon de visée sur le sol) pour
		// qu'elle soit visible — sinon, à une coord fixe, elle tombe hors du
		// rayon de culling des props (~80 m). Une fois posée, l'origine reste
		// STABLE entre les rebuilds (sélection/édition de pièce), pour que le
		// bâtiment ne « téléporte » pas sous la caméra à chaque clic et que le
		// gizmo/picking reste cohérent. (Vider la variante remet valid=false →
		// re-centrage de la prochaine composition sur la vue.)
		float previewX;
		float previewZ;
		if (m_editorPreviewValid)
		{
			previewX = m_editorPreviewOriginX;
			previewZ = m_editorPreviewOriginZ;
		}
		else
		{
			const engine::render::Camera& cam =
				m_renderStates[m_renderReadIndex.load(std::memory_order_acquire)].camera;
			previewX = cam.position.x;
			previewZ = cam.position.z;
			const float cp = std::cos(cam.pitch), sp = std::sin(cam.pitch);
			const float fwdY = -sp; // forward.y (convention OrbitalCameraController)
			const float gy0 = m_terrainCollider.GroundHeightAt(cam.position.x, cam.position.z);
			if (fwdY < -1e-3f) // caméra regarde vers le bas : intersection avec le sol
			{
				float t = (gy0 - cam.position.y) / fwdY;
				if (t < 0.0f) t = 0.0f;
				if (t > 30.0f) t = 30.0f; // borne ~30 m : assez près pour être visible (rasant pouvait donner ~280 m, hors-champ)
				previewX = cam.position.x + t * (-std::sin(cam.yaw) * cp);
				previewZ = cam.position.z + t * (-std::cos(cam.yaw) * cp);
			}
			else // visée horizontale/vers le haut : 20 m devant, sur XZ
			{
				previewX = cam.position.x + (-std::sin(cam.yaw)) * 30.0f;
				previewZ = cam.position.z + (-std::cos(cam.yaw)) * 30.0f;
			}
		}
		// Brouillon + pièce EN COURS de configuration (aperçu live). Les
		// bâtiments DÉJÀ posés (dont l'auberge) sont déjà rendus par le boot
		// (LoadBuildings) et conservés dans le « monde » baseline ci-dessus.
		const std::vector<engine::world::instances::BuildingPart> previewParts =
			panel->PartsForPreview();
		emitParts(previewParts, previewX, previewZ,
			panel->PreviewYaw(), panel->PreviewScale());

		// Mémorise la transform du groupe pour le picking viewport (clic =
		// sélection), afin que la position monde calculée pour chaque pièce
		// corresponde exactement à ce qui vient d'être rendu.
		m_editorPreviewOriginX = previewX;
		m_editorPreviewOriginZ = previewZ;
		m_editorPreviewYaw     = panel->PreviewYaw();
		m_editorPreviewScale   = panel->PreviewScale();
		m_editorPreviewValid   = !previewParts.empty();

		// Cible du gizmo : position monde de la pièce active (sélectionnée ou en
		// cours), pour l'overlay DrawEditorBuildingGizmo.
		float activeLocal[3];
		if (panel->ActivePartLocalPos(activeLocal))
		{
			const float gY = m_terrainCollider.GroundHeightAt(previewX, previewZ);
			const engine::math::Mat4 grpM =
				engine::math::Mat4::Translate(engine::math::Vec3{ previewX, gY, previewZ }) *
				engine::math::Mat4::RotateY(panel->PreviewYaw() * kDeg2Rad) *
				scaleMat(panel->PreviewScale());
			const float* M = grpM.m;
			m_editorGizmoPos = engine::math::Vec3{
				M[0]*activeLocal[0] + M[4]*activeLocal[1] + M[8]*activeLocal[2]  + M[12],
				M[1]*activeLocal[0] + M[5]*activeLocal[1] + M[9]*activeLocal[2]  + M[13],
				M[2]*activeLocal[0] + M[6]*activeLocal[1] + M[10]*activeLocal[2] + M[14] };
			m_editorGizmoValid = true;
		}
		else
		{
			m_editorGizmoValid = false;
		}

		LOG_INFO(Render, "[Buildings][editeur] apercu : monde={} + brouillon/encours={} piece(s) @ ({:.1f},{:.1f}) -> total {}",
			static_cast<int>(m_editorBaselinePropCount),
			static_cast<int>(previewParts.size()),
			previewX, previewZ, static_cast<int>(m_props.size()));
	}

	void Engine::GizmoHandleSizes(float& axisLen, float& ringR) const
	{
		const uint32_t readIdx = m_renderReadIndex.load(std::memory_order_acquire);
		const engine::render::Camera& cam = m_renderStates[readIdx].camera;
		const float dx = cam.position.x - m_editorGizmoPos.x;
		const float dy = cam.position.y - m_editorGizmoPos.y;
		const float dz = cam.position.z - m_editorGizmoPos.z;
		const float dist = std::sqrt(dx*dx + dy*dy + dz*dz);
		// ~constant à l'écran : longueur d'axe proportionnelle à la distance.
		axisLen = std::clamp(dist * 0.09f, 0.5f, 10.0f);
		ringR   = axisLen * 0.8f;
	}

	void Engine::DrawEditorBuildingGizmo()
	{
#if defined(_WIN32)
		if (!m_editorEnabled || !m_editorGizmoValid) return;
		const uint32_t readIdx = m_renderReadIndex.load(std::memory_order_acquire);
		const engine::math::Mat4& vp = m_renderStates[readIdx].viewProjMatrix;
		const ImGuiIO& io = ImGui::GetIO();
		const float W = io.DisplaySize.x, H = io.DisplaySize.y;
		if (W <= 0.0f || H <= 0.0f) return;

		// Projection monde -> écran (pixels) via la viewProj courante.
		auto project = [&](const engine::math::Vec3& w, ImVec2& out) -> bool {
			const float* M = vp.m; // column-major
			const float cx = M[0]*w.x + M[4]*w.y + M[8]*w.z  + M[12];
			const float cy = M[1]*w.x + M[5]*w.y + M[9]*w.z  + M[13];
			const float cw = M[3]*w.x + M[7]*w.y + M[11]*w.z + M[15];
			if (cw <= 1e-4f) return false; // derrière la caméra
			out.x = (cx / cw * 0.5f + 0.5f) * W;
			out.y = (cy / cw * 0.5f + 0.5f) * H;
			return true;
		};

		ImVec2 so;
		if (!project(m_editorGizmoPos, so)) return;

		// Background draw list : par-dessus la 3D, sous les fenêtres ImGui.
		ImDrawList* dl = ImGui::GetBackgroundDrawList();
		float axisLen, ringR; GizmoHandleSizes(axisLen, ringR); // taille ~constante à l'écran
		const ImU32 colX = IM_COL32(235, 70, 70, 255);  // X rouge
		const ImU32 colY = IM_COL32(70, 210, 70, 255);  // Y vert
		const ImU32 colZ = IM_COL32(80, 130, 255, 255); // Z bleu
		const ImU32 hot  = IM_COL32(255, 240, 130, 255);// survol/actif : jaune vif
		const engine::math::Vec3 dirs[3] = { {1,0,0}, {0,1,0}, {0,0,1} };
		const engine::math::Vec3 perps[3] = { {0,1,0}, {0,0,1}, {1,0,0} };
		const ImU32 cols[3] = { colX, colY, colZ };
		const engine::math::Vec3 o = m_editorGizmoPos;

		// Géométrie écran des handles (axes + anneaux).
		ImVec2 se[3]; bool seOk[3]; float ringRad[3]; bool ringOk[3];
		for (int a = 0; a < 3; ++a)
		{
			const engine::math::Vec3 tip{ o.x + dirs[a].x*axisLen, o.y + dirs[a].y*axisLen, o.z + dirs[a].z*axisLen };
			seOk[a] = project(tip, se[a]);
			ImVec2 sp;
			const engine::math::Vec3 rp{ o.x + perps[a].x*ringR, o.y + perps[a].y*ringR, o.z + perps[a].z*ringR };
			ringOk[a] = project(rp, sp);
			ringRad[a] = ringOk[a] ? std::sqrt((sp.x-so.x)*(sp.x-so.x) + (sp.y-so.y)*(sp.y-so.y)) : 0.0f;
		}

		// Handle SURVOLÉ (ou actif pendant un drag) → mis en évidence. Même test
		// que le picking, pour que ce qui s'allume soit ce qu'on saisira.
		int hovAxis = -1, hovMode = 0;
		{
			const ImVec2 mp = io.MousePos;
			auto distToSeg = [](const ImVec2& p, const ImVec2& a, const ImVec2& b) -> float {
				const float vx=b.x-a.x, vy=b.y-a.y, wx=p.x-a.x, wy=p.y-a.y;
				const float len2=vx*vx+vy*vy; float t=(len2>1e-6f)?(wx*vx+wy*vy)/len2:0.0f;
				t=std::clamp(t,0.0f,1.0f); const float cx=a.x+t*vx, cy=a.y+t*vy;
				return std::sqrt((p.x-cx)*(p.x-cx)+(p.y-cy)*(p.y-cy));
			};
			float best = 9.0f;
			for (int a = 0; a < 3; ++a)
			{
				if (seOk[a]) { const float sl=std::sqrt((se[a].x-so.x)*(se[a].x-so.x)+(se[a].y-so.y)*(se[a].y-so.y));
					if (sl>12.0f) { const float d=distToSeg(mp,so,se[a]); if (d<best){best=d;hovAxis=a;hovMode=0;} } }
				if (ringOk[a] && ringRad[a]>12.0f) { const float dm=std::sqrt((mp.x-so.x)*(mp.x-so.x)+(mp.y-so.y)*(mp.y-so.y));
					const float dr=std::abs(dm-ringRad[a]); if (dr<best){best=dr;hovAxis=a;hovMode=1;} }
			}
			if (m_gizmoDragAxis >= 0) { hovAxis = m_gizmoDragAxis; hovMode = m_gizmoDragMode; }
		}

		for (int a = 0; a < 3; ++a)
		{
			const bool axHot   = (hovAxis == a && hovMode == 0);
			const bool ringHot = (hovAxis == a && hovMode == 1);
			if (seOk[a])
			{
				dl->AddLine(so, se[a], axHot ? hot : cols[a], axHot ? 5.0f : 3.0f);
				dl->AddCircleFilled(se[a], axHot ? 7.0f : 5.0f, axHot ? hot : cols[a]);
			}
			if (ringOk[a] && ringRad[a] > 3.0f && ringRad[a] < 4000.0f)
				dl->AddCircle(so, ringRad[a], ringHot ? hot : cols[a], 48, ringHot ? 4.0f : 2.0f);
		}
		dl->AddCircleFilled(so, 4.0f, IM_COL32(255, 255, 255, 255)); // centre

		// Lecteur de valeurs : affiche la transform locale de la pièce active à
		// côté du gizmo (position + rotation), pour combler le « manque
		// d'information sur la valeur » pendant le drag (les anneaux qui tournent
		// ne montrent pas l'angle). L'axe en cours de manipulation est surligné.
		if (engine::editor::world::panels::BuildingEditorPanel* bp =
			m_worldEditorShell ? m_worldEditorShell->GetBuildingEditorPanel() : nullptr)
		{
			float pos[3], rot[3], sc = 1.0f;
			if (bp->ActivePartTransform(pos, rot, sc))
			{
				const int dragAxis = m_gizmoDragAxis;          // -1 si pas de drag
				const bool dragRot  = (m_gizmoDragMode == 1);
				char l1[96], l2[96];
				std::snprintf(l1, sizeof(l1), "Pos  X %.2f  Y %.2f  Z %.2f  (m)", pos[0], pos[1], pos[2]);
				std::snprintf(l2, sizeof(l2), "Rot  X %.0f  Y %.0f  Z %.0f  (deg)", rot[0], rot[1], rot[2]);
				const ImVec2 p1{ so.x + 14.0f, so.y + 10.0f };
				const ImVec2 p2{ so.x + 14.0f, so.y + 26.0f };
				// Fond semi-opaque pour lisibilité par-dessus la 3D.
				dl->AddRectFilled(ImVec2(p1.x - 4.0f, p1.y - 2.0f), ImVec2(p1.x + 230.0f, p2.y + 16.0f),
					IM_COL32(0, 0, 0, 150), 3.0f);
				// Surligne la ligne active (jaune) selon le mode de drag.
				const ImU32 hotTxt = IM_COL32(255, 230, 90, 255);
				const ImU32 white = IM_COL32(235, 235, 235, 255);
				dl->AddText(p1, (dragAxis >= 0 && !dragRot) ? hotTxt : white, l1);
				dl->AddText(p2, (dragAxis >= 0 &&  dragRot) ? hotTxt : white, l2);
			}
		}
#endif
	}

	bool Engine::UpdateEditorBuildingGizmoDrag(int mouseX, int mouseY)
	{
#if defined(_WIN32)
		if (!m_editorEnabled || !m_editorGizmoValid) return false;
		if (!m_worldEditorShell) return false;
		engine::editor::world::panels::BuildingEditorPanel* panel =
			m_worldEditorShell->GetBuildingEditorPanel();
		// Le drag n'agit que sur une pièce SÉLECTIONNÉE du brouillon (la pièce en
		// cours d'ajout reste pilotée par les champs du panneau).
		if (!panel || panel->SelectedDraft() < 0) { m_gizmoDragAxis = -1; return false; }

		const uint32_t readIdx = m_renderReadIndex.load(std::memory_order_acquire);
		const engine::math::Mat4& vp = m_renderStates[readIdx].viewProjMatrix;
		const ImGuiIO& io = ImGui::GetIO();
		const float W = io.DisplaySize.x, H = io.DisplaySize.y;
		if (W <= 0.0f || H <= 0.0f) return false;

		auto project = [&](const engine::math::Vec3& w, float& sx, float& sy) -> bool {
			const float* M = vp.m;
			const float cx = M[0]*w.x + M[4]*w.y + M[8]*w.z  + M[12];
			const float cy = M[1]*w.x + M[5]*w.y + M[9]*w.z  + M[13];
			const float cw = M[3]*w.x + M[7]*w.y + M[11]*w.z + M[15];
			if (cw <= 1e-4f) return false;
			sx = (cx / cw * 0.5f + 0.5f) * W;
			sy = (cy / cw * 0.5f + 0.5f) * H;
			return true;
		};

		float axisLen, ringR; GizmoHandleSizes(axisLen, ringR); // identique au dessin
		const engine::math::Vec3 dirs[3] = { {1,0,0}, {0,1,0}, {0,0,1} };
		const engine::math::Vec3 perps[3] = { {0,1,0}, {0,0,1}, {1,0,0} };
		const engine::math::Vec3 o = m_editorGizmoPos;

		float ox, oy;
		if (!project(o, ox, oy)) { m_gizmoDragAxis = -1; return false; }

		// Géométrie écran courante des handles (recalculée chaque frame).
		float tipX[3], tipY[3]; bool tipOk[3];
		float ringRad[3]; bool ringOk[3];
		for (int a = 0; a < 3; ++a)
		{
			const engine::math::Vec3 tip{ o.x + dirs[a].x*axisLen, o.y + dirs[a].y*axisLen, o.z + dirs[a].z*axisLen };
			tipOk[a] = project(tip, tipX[a], tipY[a]);
			float spx, spy;
			const engine::math::Vec3 rp{ o.x + perps[a].x*ringR, o.y + perps[a].y*ringR, o.z + perps[a].z*ringR };
			ringOk[a] = project(rp, spx, spy);
			ringRad[a] = ringOk[a] ? std::sqrt((spx-ox)*(spx-ox) + (spy-oy)*(spy-oy)) : 0.0f;
		}

		const float mx = static_cast<float>(mouseX), my = static_cast<float>(mouseY);

		// Distance point → segment [a,b] en écran.
		auto distToSeg = [](float px, float py, float ax, float ay, float bx, float by) -> float {
			const float vx = bx-ax, vy = by-ay;
			const float wx = px-ax, wy = py-ay;
			const float len2 = vx*vx + vy*vy;
			float t = (len2 > 1e-6f) ? (wx*vx + wy*vy) / len2 : 0.0f;
			t = std::clamp(t, 0.0f, 1.0f);
			const float cx = ax + t*vx, cy = ay + t*vy;
			const float dx = px-cx, dy = py-cy;
			return std::sqrt(dx*dx + dy*dy);
		};

		const bool pressed  = m_input.WasMousePressed(engine::platform::MouseButton::Left);
		const bool downNow  = m_input.IsMouseDown(engine::platform::MouseButton::Left);
		const bool released = m_input.WasMouseReleased(engine::platform::MouseButton::Left);

		// --- Saisie d'un handle au clic -----------------------------------
		if (m_gizmoDragAxis < 0 && pressed)
		{
			constexpr float kPick = 9.0f; // tolérance écran (pixels)
			int bestAxis = -1, bestMode = 0;
			float bestDist = kPick;
			for (int a = 0; a < 3; ++a)
			{
				if (tipOk[a])
				{
					const float dseg = distToSeg(mx, my, ox, oy, tipX[a], tipY[a]);
					// Évite de saisir un axe quasi vu de bout (segment trop court).
					const float segLen = std::sqrt((tipX[a]-ox)*(tipX[a]-ox) + (tipY[a]-oy)*(tipY[a]-oy));
					if (segLen > 12.0f && dseg < bestDist) { bestDist = dseg; bestAxis = a; bestMode = 0; }
				}
				if (ringOk[a] && ringRad[a] > 12.0f)
				{
					const float dmouse = std::sqrt((mx-ox)*(mx-ox) + (my-oy)*(my-oy));
					const float dring = std::abs(dmouse - ringRad[a]);
					if (dring < bestDist) { bestDist = dring; bestAxis = a; bestMode = 1; }
				}
			}
			if (bestAxis >= 0)
			{
				// Un drag = une étape d'annulation : on capture l'état AVANT de
				// commencer à déplacer/tourner (les mutations seront silencieuses).
				panel->PushUndoSnapshot();
				m_gizmoDragAxis = bestAxis;
				m_gizmoDragMode = bestMode;
				m_gizmoDragLastX = mx; m_gizmoDragLastY = my;
				m_gizmoDragLastAngle = std::atan2(my - oy, mx - ox);
				return true; // capture : pas de sélection de pièce sur ce clic
			}
		}

		// --- Application pendant le drag -----------------------------------
		if (m_gizmoDragAxis >= 0 && downNow)
		{
			const int a = m_gizmoDragAxis;
			if (m_gizmoDragMode == 0) // translation
			{
				// Direction écran de l'axe + mètres-monde par pixel le long de
				// l'axe (la poignée à axisLen mètres se projette à |tip-o|).
				float axSx = tipX[a]-ox, axSy = tipY[a]-oy;
				const float segLen = std::sqrt(axSx*axSx + axSy*axSy);
				if (segLen > 1e-3f)
				{
					axSx /= segLen; axSy /= segLen;
					const float worldPerPix = axisLen / segLen;
					const float dScreen = (mx - m_gizmoDragLastX)*axSx + (my - m_gizmoDragLastY)*axSy;
					const float d = dScreen * worldPerPix; // mètres monde le long de l'axe a
					// Monde → local : inverse(RotateY(groupYaw)) / échelle groupe.
					constexpr float kDeg2Rad = 3.14159265f / 180.f;
					const float c = std::cos(m_editorPreviewYaw * kDeg2Rad);
					const float s = std::sin(m_editorPreviewYaw * kDeg2Rad);
					const float invScale = (m_editorPreviewScale > 1e-4f) ? 1.0f/m_editorPreviewScale : 1.0f;
					float lx = 0, ly = 0, lz = 0;
					if (a == 0)      { lx = c*d*invScale;  lz = s*d*invScale; }
					else if (a == 1) { ly = d*invScale; }
					else             { lx = -s*d*invScale; lz = c*d*invScale; }
					// Silencieux : on ne reconstruit PAS le mesh à chaque frame (fuite
					// GPU) ; le gizmo suit via la donnée, le mesh est rebâti au release.
					panel->TranslateSelectedSilent(lx, ly, lz);
				}
			}
			else // rotation
			{
				const float angNow = std::atan2(my - oy, mx - ox);
				float dAng = angNow - m_gizmoDragLastAngle;
				// Normalise dans [-pi, pi] (passage ±180°).
				while (dAng >  3.14159265f) dAng -= 6.28318530f;
				while (dAng < -3.14159265f) dAng += 6.28318530f;
				m_gizmoDragLastAngle = angNow;
				panel->AddRotationSelectedSilent(a, dAng * (180.0f / 3.14159265f));
			}
			m_gizmoDragLastX = mx; m_gizmoDragLastY = my;

			// Rafraîchit le MESH périodiquement pendant le drag (~1 frame sur 6,
			// soit ~10 Hz) pour un retour visuel : les données sont déjà à jour
			// (mutateurs silencieux), on demande juste un rebuild ponctuel. Throttlé
			// car BuildPropFromMeshMatrix ne libère pas les meshes GPU (un rebuild
			// par frame saturerait). Le rebuild final a lieu au relâchement.
			if (++m_gizmoDragRefreshTick >= 6)
			{
				m_gizmoDragRefreshTick = 0;
				panel->MarkPreviewDirty();
			}

			// Recalcule la cible du gizmo CHAQUE frame depuis la pièce mise à jour,
			// pour que les cercles suivent la souris en temps réel même si le mesh
			// n'est reconstruit qu'à intervalles (throttle ci-dessus).
			float local[3];
			if (panel->ActivePartLocalPos(local))
			{
				constexpr float kDeg2Rad = 3.14159265f / 180.f;
				const float gY = m_terrainCollider.GroundHeightAt(m_editorPreviewOriginX, m_editorPreviewOriginZ);
				const float c = std::cos(m_editorPreviewYaw * kDeg2Rad);
				const float s = std::sin(m_editorPreviewYaw * kDeg2Rad);
				const float sx = m_editorPreviewScale * local[0];
				const float sy = m_editorPreviewScale * local[1];
				const float sz = m_editorPreviewScale * local[2];
				m_editorGizmoPos = engine::math::Vec3{
					m_editorPreviewOriginX + (c*sx + s*sz),
					gY + sy,
					m_editorPreviewOriginZ + (-s*sx + c*sz) };
			}
			return true;
		}

		// Fin du drag : reconstruire le mesh UNE fois à la position finale (les
		// mutations pendant le drag étaient silencieuses pour éviter la fuite GPU).
		if (released)
		{
			const bool wasDragging = (m_gizmoDragAxis >= 0);
			m_gizmoDragAxis = -1;
			if (wasDragging) { panel->MarkPreviewDirty(); return true; }
		}
		return m_gizmoDragAxis >= 0;
#else
		(void)mouseX; (void)mouseY; return false;
#endif
	}

	void Engine::SceneGizmoHandleSizes(const engine::math::Vec3& pos, float& axisLen, float& ringR) const
	{
		const uint32_t readIdx = m_renderReadIndex.load(std::memory_order_acquire);
		const engine::render::Camera& cam = m_renderStates[readIdx].camera;
		const float dx = cam.position.x - pos.x;
		const float dy = cam.position.y - pos.y;
		const float dz = cam.position.z - pos.z;
		const float dist = std::sqrt(dx*dx + dy*dy + dz*dz);
		// ~constant à l'écran : longueur d'axe proportionnelle à la distance
		// (mêmes coefficients que le gizmo bâtiment, cf. GizmoHandleSizes).
		axisLen = std::clamp(dist * 0.09f, 0.5f, 10.0f);
		ringR   = axisLen * 0.8f;
	}

	void Engine::DrawEditorSceneGizmo()
	{
#if defined(_WIN32)
		if (!m_editorEnabled || !m_worldEditorShell || !m_worldEditorShell->IsInitialized()) return;
		// Le gizmo bâtiment garde la main en mode édition bâtiment (deux gizmos
		// superposés seraient illisibles et se disputeraient les clics).
		if (engine::editor::world::panels::BuildingEditorPanel* bp =
			m_worldEditorShell->GetBuildingEditorPanel(); bp && bp->EditModeActive()) return;
		const engine::editor::scene::EditorSelection& sel = m_worldEditorShell->GetSelection();
		if (!sel.HasSelection()) return;
		const engine::editor::scene::EditorSceneModel& model = m_worldEditorShell->GetSceneModel();
		const engine::editor::scene::SceneEntity* primary = model.Find(sel.Current());
		if (primary == nullptr || !primary->hasTransform) return;

		const uint32_t readIdx = m_renderReadIndex.load(std::memory_order_acquire);
		const engine::math::Mat4& vp = m_renderStates[readIdx].viewProjMatrix;
		const ImGuiIO& io = ImGui::GetIO();
		const float W = io.DisplaySize.x, H = io.DisplaySize.y;
		if (W <= 0.0f || H <= 0.0f) return;

		// Projection monde -> écran (pixels) via la viewProj courante (même
		// convention que le gizmo bâtiment : viewProj col-major, pas de Y-flip).
		auto project = [&](const engine::math::Vec3& w, ImVec2& out) -> bool {
			const float* M = vp.m;
			const float cx = M[0]*w.x + M[4]*w.y + M[8]*w.z  + M[12];
			const float cy = M[1]*w.x + M[5]*w.y + M[9]*w.z  + M[13];
			const float cw = M[3]*w.x + M[7]*w.y + M[11]*w.z + M[15];
			if (cw <= 1e-4f) return false; // derrière la caméra
			out.x = (cx / cw * 0.5f + 0.5f) * W;
			out.y = (cy / cw * 0.5f + 0.5f) * H;
			return true;
		};

		ImDrawList* dl = ImGui::GetBackgroundDrawList();

		// Marqueurs de multi-sélection : un anneau magenta par entité (double
		// épaisseur sur la primaire) pour VOIR ce qui sera affecté par le geste.
		const ImU32 colSel = IM_COL32(230, 80, 230, 220);
		for (const engine::editor::scene::EntityId& id : sel.Items())
		{
			const engine::editor::scene::SceneEntity* e = model.Find(id);
			if (e == nullptr || !e->hasTransform) continue;
			ImVec2 p;
			if (project(e->transform.position, p))
				dl->AddCircle(p, (id == sel.Current()) ? 11.0f : 7.0f, colSel, 24,
					(id == sel.Current()) ? 3.0f : 1.5f);
		}

		const engine::math::Vec3 o = primary->transform.position;
		ImVec2 so;
		if (!project(o, so)) return;

		float axisLen, ringR; SceneGizmoHandleSizes(o, axisLen, ringR);
		const ImU32 colX = IM_COL32(235, 70, 70, 255);  // X rouge
		const ImU32 colY = IM_COL32(70, 210, 70, 255);  // Y vert
		const ImU32 colZ = IM_COL32(80, 130, 255, 255); // Z bleu
		const ImU32 hot  = IM_COL32(255, 240, 130, 255);// survol/actif : jaune vif
		const ImU32 cols[3] = { colX, colY, colZ };
		const engine::math::Vec3 dirs[3] = { {1,0,0}, {0,1,0}, {0,0,1} };
		const int mode = static_cast<int>(m_worldEditorShell->GetSceneGizmoMode());
		const bool dragging = (m_sceneGizmoDragAxis >= 0);

		if (mode == 0) // Translation : 3 axes monde
		{
			auto distToSeg = [](const ImVec2& p, const ImVec2& a, const ImVec2& b) -> float {
				const float vx=b.x-a.x, vy=b.y-a.y, wx=p.x-a.x, wy=p.y-a.y;
				const float len2=vx*vx+vy*vy; float t=(len2>1e-6f)?(wx*vx+wy*vy)/len2:0.0f;
				t=std::clamp(t,0.0f,1.0f); const float cx=a.x+t*vx, cy=a.y+t*vy;
				return std::sqrt((p.x-cx)*(p.x-cx)+(p.y-cy)*(p.y-cy));
			};
			int hovAxis = dragging ? m_sceneGizmoDragAxis : -1;
			for (int a = 0; a < 3; ++a)
			{
				const engine::math::Vec3 tip{ o.x + dirs[a].x*axisLen, o.y + dirs[a].y*axisLen, o.z + dirs[a].z*axisLen };
				ImVec2 se;
				if (!project(tip, se)) continue;
				if (!dragging)
				{
					const float sl = std::sqrt((se.x-so.x)*(se.x-so.x) + (se.y-so.y)*(se.y-so.y));
					if (sl > 12.0f && distToSeg(io.MousePos, so, se) < 9.0f) hovAxis = a;
				}
				const bool axHot = (hovAxis == a);
				dl->AddLine(so, se, axHot ? hot : cols[a], axHot ? 5.0f : 3.0f);
				dl->AddCircleFilled(se, axHot ? 7.0f : 5.0f, axHot ? hot : cols[a]);
			}
		}
		else if (mode == 1) // Rotation yaw : un anneau autour de Y
		{
			ImVec2 rp;
			const engine::math::Vec3 rw{ o.x + ringR, o.y, o.z };
			if (project(rw, rp))
			{
				const float rad = std::sqrt((rp.x-so.x)*(rp.x-so.x) + (rp.y-so.y)*(rp.y-so.y));
				if (rad > 3.0f && rad < 4000.0f)
				{
					bool ringHot = dragging;
					if (!dragging)
					{
						const float dm = std::sqrt((io.MousePos.x-so.x)*(io.MousePos.x-so.x)
							+ (io.MousePos.y-so.y)*(io.MousePos.y-so.y));
						ringHot = std::abs(dm - rad) < 9.0f;
					}
					dl->AddCircle(so, rad, ringHot ? hot : colY, 48, ringHot ? 4.0f : 2.5f);
				}
			}
		}
		else // Échelle uniforme : poignée carrée au centre (drag vertical)
		{
			bool sqHot = dragging;
			if (!dragging)
			{
				sqHot = std::abs(io.MousePos.x - so.x) < 14.0f && std::abs(io.MousePos.y - so.y) < 14.0f;
			}
			dl->AddRectFilled(ImVec2(so.x - 9.0f, so.y - 9.0f), ImVec2(so.x + 9.0f, so.y + 9.0f),
				sqHot ? hot : IM_COL32(210, 210, 210, 255), 2.0f);
		}
		dl->AddCircleFilled(so, 4.0f, IM_COL32(255, 255, 255, 255)); // centre

		// Lecteur de valeurs : transform de l'entité primaire + taille de la
		// sélection (le drag n'affiche pas ses valeurs autrement).
		{
			char l1[112], l2[112];
			std::snprintf(l1, sizeof(l1), "Pos  X %.2f  Y %.2f  Z %.2f  (m)",
				primary->transform.position.x, primary->transform.position.y, primary->transform.position.z);
			std::snprintf(l2, sizeof(l2), "Yaw  %.0f deg   Echelle  %.2f   (%d selectionnee(s))",
				primary->transform.eulerDeg.y, primary->transform.uniformScale,
				static_cast<int>(sel.Count()));
			const ImVec2 p1{ so.x + 14.0f, so.y + 10.0f };
			const ImVec2 p2{ so.x + 14.0f, so.y + 26.0f };
			dl->AddRectFilled(ImVec2(p1.x - 4.0f, p1.y - 2.0f), ImVec2(p1.x + 290.0f, p2.y + 16.0f),
				IM_COL32(0, 0, 0, 150), 3.0f);
			const ImU32 white = IM_COL32(235, 235, 235, 255);
			const ImU32 hotTxt = IM_COL32(255, 230, 90, 255);
			dl->AddText(p1, (dragging && m_sceneGizmoDragMode == 0) ? hotTxt : white, l1);
			dl->AddText(p2, (dragging && m_sceneGizmoDragMode != 0) ? hotTxt : white, l2);
		}
#endif
	}

	bool Engine::UpdateEditorSceneGizmoDrag(int mouseX, int mouseY)
	{
#if defined(_WIN32)
		if (!m_editorEnabled || !m_worldEditorShell || !m_worldEditorShell->IsInitialized()) return false;
		if (engine::editor::world::panels::BuildingEditorPanel* bp =
			m_worldEditorShell->GetBuildingEditorPanel(); bp && bp->EditModeActive())
		{
			m_sceneGizmoDragAxis = -1;
			return false;
		}
		const engine::editor::scene::EditorSelection& sel = m_worldEditorShell->GetSelection();
		const engine::editor::scene::EditorSceneModel& model = m_worldEditorShell->GetSceneModel();
		const engine::editor::scene::SceneEntity* primary =
			sel.HasSelection() ? model.Find(sel.Current()) : nullptr;
		if (primary == nullptr || !primary->hasTransform)
		{
			m_sceneGizmoDragAxis = -1;
			return false;
		}
		const engine::editor::world::WorldEditorShell::TransformWriter& writer =
			m_worldEditorShell->GetTransformWriter();
		if (!writer) return false;

		const uint32_t readIdx = m_renderReadIndex.load(std::memory_order_acquire);
		const engine::math::Mat4& vp = m_renderStates[readIdx].viewProjMatrix;
		const ImGuiIO& io = ImGui::GetIO();
		const float W = io.DisplaySize.x, H = io.DisplaySize.y;
		if (W <= 0.0f || H <= 0.0f) return false;

		auto project = [&](const engine::math::Vec3& w, float& sx, float& sy) -> bool {
			const float* M = vp.m;
			const float cx = M[0]*w.x + M[4]*w.y + M[8]*w.z  + M[12];
			const float cy = M[1]*w.x + M[5]*w.y + M[9]*w.z  + M[13];
			const float cw = M[3]*w.x + M[7]*w.y + M[11]*w.z + M[15];
			if (cw <= 1e-4f) return false;
			sx = (cx / cw * 0.5f + 0.5f) * W;
			sy = (cy / cw * 0.5f + 0.5f) * H;
			return true;
		};

		const engine::math::Vec3 o = primary->transform.position;
		float ox, oy;
		if (!project(o, ox, oy)) { m_sceneGizmoDragAxis = -1; return false; }
		float axisLen, ringR; SceneGizmoHandleSizes(o, axisLen, ringR);
		const engine::math::Vec3 dirs[3] = { {1,0,0}, {0,1,0}, {0,0,1} };
		const int mode = static_cast<int>(m_worldEditorShell->GetSceneGizmoMode());
		const float mx = static_cast<float>(mouseX), my = static_cast<float>(mouseY);

		const bool pressed  = m_input.WasMousePressed(engine::platform::MouseButton::Left);
		const bool downNow  = m_input.IsMouseDown(engine::platform::MouseButton::Left);
		const bool released = m_input.WasMouseReleased(engine::platform::MouseButton::Left);

		// --- Saisie d'un handle au clic (selon le mode courant) --------------
		if (m_sceneGizmoDragAxis < 0 && pressed)
		{
			int grabbedAxis = -1;
			if (mode == 0) // translation : un des 3 axes
			{
				auto distToSeg = [](float px, float py, float ax, float ay, float bx, float by) -> float {
					const float vx = bx-ax, vy = by-ay, wx = px-ax, wy = py-ay;
					const float len2 = vx*vx + vy*vy;
					float t = (len2 > 1e-6f) ? (wx*vx + wy*vy) / len2 : 0.0f;
					t = std::clamp(t, 0.0f, 1.0f);
					const float cx = ax + t*vx, cy = ay + t*vy;
					return std::sqrt((px-cx)*(px-cx) + (py-cy)*(py-cy));
				};
				float bestDist = 9.0f; // tolérance écran (pixels)
				for (int a = 0; a < 3; ++a)
				{
					const engine::math::Vec3 tip{ o.x + dirs[a].x*axisLen, o.y + dirs[a].y*axisLen, o.z + dirs[a].z*axisLen };
					float tx, ty;
					if (!project(tip, tx, ty)) continue;
					const float segLen = std::sqrt((tx-ox)*(tx-ox) + (ty-oy)*(ty-oy));
					if (segLen <= 12.0f) continue; // axe quasi vu de bout
					const float d = distToSeg(mx, my, ox, oy, tx, ty);
					if (d < bestDist) { bestDist = d; grabbedAxis = a; }
				}
			}
			else if (mode == 1) // rotation yaw : l'anneau
			{
				float rx, ry;
				const engine::math::Vec3 rw{ o.x + ringR, o.y, o.z };
				if (project(rw, rx, ry))
				{
					const float rad = std::sqrt((rx-ox)*(rx-ox) + (ry-oy)*(ry-oy));
					const float dm = std::sqrt((mx-ox)*(mx-ox) + (my-oy)*(my-oy));
					if (rad > 12.0f && std::abs(dm - rad) < 9.0f) grabbedAxis = 0;
				}
			}
			else // échelle : la poignée centrale
			{
				if (std::abs(mx - ox) < 14.0f && std::abs(my - oy) < 14.0f) grabbedAxis = 0;
			}

			if (grabbedAxis >= 0)
			{
				// Capture des transforms de départ de TOUTES les entités
				// éditables de la sélection (ancien état des commandes d'undo).
				m_sceneGizmoDragStart.clear();
				for (const engine::editor::scene::EntityId& id : sel.Items())
				{
					const engine::editor::scene::SceneEntity* e = model.Find(id);
					if (e != nullptr && e->hasTransform)
						m_sceneGizmoDragStart.emplace_back(id, e->transform);
				}
				if (m_sceneGizmoDragStart.empty()) return false;
				m_sceneGizmoDragAxis = grabbedAxis;
				m_sceneGizmoDragMode = mode;
				m_sceneGizmoDragLastX = mx;
				m_sceneGizmoDragLastY = my;
				m_sceneGizmoDragLastAngle = std::atan2(my - oy, mx - ox);
				return true; // capture : pas de picking/outil sur ce clic
			}
		}

		// --- Application pendant le drag (écriture directe, undo au release) --
		if (m_sceneGizmoDragAxis >= 0 && downNow)
		{
			if (m_sceneGizmoDragMode == 0) // translation le long de l'axe monde
			{
				const int a = m_sceneGizmoDragAxis;
				const engine::math::Vec3 tip{ o.x + dirs[a].x*axisLen, o.y + dirs[a].y*axisLen, o.z + dirs[a].z*axisLen };
				float tx, ty;
				if (project(tip, tx, ty))
				{
					float axSx = tx - ox, axSy = ty - oy;
					const float segLen = std::sqrt(axSx*axSx + axSy*axSy);
					if (segLen > 1e-3f)
					{
						axSx /= segLen; axSy /= segLen;
						const float worldPerPix = axisLen / segLen;
						const float dScreen = (mx - m_sceneGizmoDragLastX)*axSx + (my - m_sceneGizmoDragLastY)*axSy;
						const float d = dScreen * worldPerPix; // mètres monde le long de l'axe a
						for (const auto& [id, startT] : m_sceneGizmoDragStart)
						{
							const engine::editor::scene::SceneEntity* e = model.Find(id);
							if (e == nullptr) continue;
							engine::editor::scene::EntityTransform t = e->transform;
							t.position.x += dirs[a].x * d;
							t.position.y += dirs[a].y * d;
							t.position.z += dirs[a].z * d;
							writer(id, t);
						}
					}
				}
			}
			else if (m_sceneGizmoDragMode == 1) // rotation yaw autour du pivot de chaque entité
			{
				const float angNow = std::atan2(my - oy, mx - ox);
				float dAng = angNow - m_sceneGizmoDragLastAngle;
				while (dAng >  3.14159265f) dAng -= 6.28318530f;
				while (dAng < -3.14159265f) dAng += 6.28318530f;
				m_sceneGizmoDragLastAngle = angNow;
				const float dDeg = dAng * (180.0f / 3.14159265f);
				for (const auto& [id, startT] : m_sceneGizmoDragStart)
				{
					const engine::editor::scene::SceneEntity* e = model.Find(id);
					if (e == nullptr) continue;
					engine::editor::scene::EntityTransform t = e->transform;
					t.eulerDeg.y += dDeg;
					writer(id, t);
				}
			}
			else // échelle uniforme (drag vertical : haut = agrandir)
			{
				const float factor = 1.0f + (m_sceneGizmoDragLastY - my) * 0.01f;
				for (const auto& [id, startT] : m_sceneGizmoDragStart)
				{
					const engine::editor::scene::SceneEntity* e = model.Find(id);
					if (e == nullptr) continue;
					engine::editor::scene::EntityTransform t = e->transform;
					t.uniformScale = std::clamp(t.uniformScale * factor, 0.01f, 1000.0f);
					writer(id, t);
				}
			}
			m_sceneGizmoDragLastX = mx;
			m_sceneGizmoDragLastY = my;
			return true;
		}

		// --- Fin du drag : pousser UNE étape d'annulation pour tout le geste --
		if (released && m_sceneGizmoDragAxis >= 0)
		{
			auto sameTransform = [](const engine::editor::scene::EntityTransform& a,
				const engine::editor::scene::EntityTransform& b) -> bool
			{
				return a.position.x == b.position.x && a.position.y == b.position.y
					&& a.position.z == b.position.z
					&& a.eulerDeg.x == b.eulerDeg.x && a.eulerDeg.y == b.eulerDeg.y
					&& a.eulerDeg.z == b.eulerDeg.z
					&& a.uniformScale == b.uniformScale;
			};
			const char* label = (m_sceneGizmoDragMode == 0) ? "Gizmo : déplacer"
				: (m_sceneGizmoDragMode == 1) ? "Gizmo : tourner" : "Gizmo : échelle";
			// Toujours envelopper dans une CompositeCommand (mergeKey 0) : deux
			// drags successifs de la même entité ne doivent PAS fusionner en une
			// seule étape d'annulation (contrairement au slider de l'Inspector).
			auto composite = std::make_unique<engine::editor::world::CompositeCommand>(label);
			for (const auto& [id, startT] : m_sceneGizmoDragStart)
			{
				const engine::editor::scene::SceneEntity* e = model.Find(id);
				if (e == nullptr || sameTransform(startT, e->transform)) continue;
				composite->AddChild(std::make_unique<engine::editor::world::SetEntityTransformCommand>(
					id, startT, e->transform, writer));
			}
			if (!composite->Empty())
			{
				m_worldEditorShell->MutableCommandStack().Push(std::move(composite));
			}
			m_sceneGizmoDragAxis = -1;
			m_sceneGizmoDragStart.clear();
			return true;
		}
		return m_sceneGizmoDragAxis >= 0;
#else
		(void)mouseX; (void)mouseY; return false;
#endif
	}

	engine::render::TextureHandle Engine::SolidColorTexture(uint8_t r, uint8_t g, uint8_t b)
	{
		const uint32_t key = (static_cast<uint32_t>(r) << 16) | (static_cast<uint32_t>(g) << 8) | b;
		auto it = m_solidColorTextures.find(key);
		if (it != m_solidColorTextures.end()) return it->second;
		const uint8_t rgba[4] = { r, g, b, 255 };
		engine::render::TextureHandle h =
			m_assetRegistry.CreateTextureFromMemory(rgba, 1, 1, /*useSrgb*/ true);
		m_solidColorTextures[key] = h;
		return h;
	}

	void Engine::BuildPropFromMeshMatrix(const std::string& meshPath,
		const engine::math::Mat4& worldM, int interactableIndex,
		bool solid, float collisionRadius)
	{
		if (!m_pipeline) return;
		auto& materialCache = m_pipeline->GetMaterialDescriptorCache();
		if (!materialCache.IsValid()) return;
		const std::string contentRoot = m_cfg.GetString("paths.content", "game/data");

		const std::string full = contentRoot + "/" + meshPath;
		auto cpu = engine::render::staticmesh::StaticMeshLoader::LoadCpuOnlyForTests(full);
		if (!cpu || cpu->vertices.empty() || cpu->submeshes.empty())
		{
			LOG_WARN(Render, "[Buildings] mesh load FAIL '{}'", full);
			return;
		}
		std::string meshDir;
		{
			const auto slash = meshPath.find_last_of('/');
			if (slash != std::string::npos) meshDir = meshPath.substr(0, slash + 1);
		}
		std::unordered_map<std::string, std::vector<uint32_t>> idxByMat;
		std::unordered_map<std::string, const engine::render::staticmesh::StaticSubMesh*> repByMat;
		for (const auto& sub : cpu->submeshes)
		{
			std::vector<uint32_t>& v = idxByMat[sub.materialName];
			v.insert(v.end(), cpu->indices.begin() + sub.firstIndex,
			         cpu->indices.begin() + sub.firstIndex + sub.indexCount);
			if (repByMat.find(sub.materialName) == repByMat.end()) repByMat[sub.materialName] = &sub;
		}

		PropRenderable prop;
		// Cuisson de worldM dans les sommets (même contournement que BuildPropFromMesh :
		// le buffer d'instance GPU partagé n'autorise pas de matrice par-prop), mais
		// SANS le lift au sol : le Y de la matrice est autoritaire (pièce empilable).
		float minY = 1e30f, maxY = -1e30f;
		float minX = 1e30f, maxX = -1e30f, minZ = 1e30f, maxZ = -1e30f;
		{
			const float* M = worldM.m; // column-major : M[col*4+row]
			for (auto& v : cpu->vertices)
			{
				const float px = v.pos[0], py = v.pos[1], pz = v.pos[2];
				v.pos[0] = M[0]*px + M[4]*py + M[8]*pz  + M[12];
				v.pos[1] = M[1]*px + M[5]*py + M[9]*pz  + M[13];
				v.pos[2] = M[2]*px + M[6]*py + M[10]*pz + M[14];
				if (v.pos[1] < minY) minY = v.pos[1];
				if (v.pos[1] > maxY) maxY = v.pos[1];
				if (v.pos[0] < minX) minX = v.pos[0];
				if (v.pos[0] > maxX) maxX = v.pos[0];
				if (v.pos[2] < minZ) minZ = v.pos[2];
				if (v.pos[2] > maxZ) maxZ = v.pos[2];
				const float nx = v.normal[0], ny = v.normal[1], nz = v.normal[2];
				float rnx = M[0]*nx + M[4]*ny + M[8]*nz;
				float rny = M[1]*nx + M[5]*ny + M[9]*nz;
				float rnz = M[2]*nx + M[6]*ny + M[10]*nz;
				const float nlen = std::sqrt(rnx*rnx + rny*rny + rnz*rnz);
				if (nlen > 1e-6f) { rnx /= nlen; rny /= nlen; rnz /= nlen; }
				v.normal[0] = rnx; v.normal[1] = rny; v.normal[2] = rnz;
			}
		}
		prop.modelMatrix = engine::math::Mat4::Identity(); // sommets déjà en espace monde

		// Centre XZ = milieu de l'empreinte des sommets bakés (pour collision + impostor).
		const float cx = 0.5f * (minX + maxX);
		const float cz = 0.5f * (minZ + maxZ);
		float radiusAuto = 0.0f;
		for (const auto& v : cpu->vertices)
		{
			const float ddx = v.pos[0] - cx, ddz = v.pos[2] - cz;
			const float r = std::sqrt(ddx*ddx + ddz*ddz);
			if (r > radiusAuto) radiusAuto = r;
		}

		prop.meshPath = meshPath;
		{
			const float halfHeight = 0.5f * (maxY - minY);
			prop.impostorCenter = engine::math::Vec3{ cx, minY + halfHeight, cz };
			prop.impostorRadius = std::max(radiusAuto, halfHeight);
			if (prop.impostorRadius < 0.05f) prop.impostorRadius = 0.5f;
		}

		// Anti-occlusion caméra — sphère englobante monde (AABB des sommets bakés,
		// déjà en espace monde car modelMatrix = identité). Sert d'occulteur dans
		// CameraOcclusionFade. Si l'empreinte est dégénérée, le rayon reste positif.
		{
			prop.occluderCenter = engine::math::Vec3{
				0.5f * (minX + maxX), 0.5f * (minY + maxY), 0.5f * (minZ + maxZ) };
			const engine::math::Vec3 ext{ maxX - minX, maxY - minY, maxZ - minZ };
			prop.occluderRadius =
				0.5f * std::sqrt(ext.x * ext.x + ext.y * ext.y + ext.z * ext.z);
		}

		for (const auto& kv : idxByMat)
		{
			const std::string& matName = kv.first;
			const std::vector<uint32_t>& idxs = kv.second;
			if (idxs.empty()) continue;
			engine::render::MeshHandle mh = m_assetRegistry.CreateMeshFromData(
				cpu->vertices.data(), static_cast<uint32_t>(cpu->vertices.size()),
				idxs.data(), static_cast<uint32_t>(idxs.size()));
			if (!mh.IsValid()) continue;

			uint32_t matIdx = materialCache.GetDefaultMaterialIndex();
			uint32_t hlIdx  = matIdx;
			auto cached = m_trimMatCache.find(matName);
			if (cached != m_trimMatCache.end())
			{
				matIdx = cached->second.first;
				hlIdx  = cached->second.second;
			}
			else
			{
				const engine::render::staticmesh::StaticSubMesh* rep = repByMat[matName];
				if (rep && !rep->baseColorUri.empty())
				{
					const engine::render::TextureHandle bc =
						m_assetRegistry.LoadTexture(meshDir + rep->baseColorUri, /*useSrgb*/ true);
					if (bc.IsValid())
					{
						engine::render::Material mat{};
						mat.baseColor = bc;
						if (!rep->normalUri.empty())
						{
							const engine::render::TextureHandle n = m_assetRegistry.LoadTexture(meshDir + rep->normalUri, false);
							if (n.IsValid()) mat.normal = n;
						}
						if (!rep->ormUri.empty())
						{
							const engine::render::TextureHandle o = m_assetRegistry.LoadTexture(meshDir + rep->ormUri, false);
							if (o.IsValid()) mat.orm = o;
						}
						if (rep->alphaCutout)
							mat.flags = engine::render::MaterialFlags::AlphaCutout;
						matIdx = materialCache.CreateMaterial(m_vkDeviceContext.GetDevice(), mat);
						mat.flags = static_cast<engine::render::MaterialFlags>(
							static_cast<uint32_t>(mat.flags) | static_cast<uint32_t>(engine::render::MaterialFlags::Highlight));
						hlIdx = materialCache.CreateMaterial(m_vkDeviceContext.GetDevice(), mat);
					}
				}
				else
				{
					// Pièces de structure (murs, sols, coins…) : le glTF du projet
					// ne référence aucune texture, mais a GARDÉ le nom du matériau
					// (MI_Plaster, MI_WoodTrim, MI_Brick…). On retrouve la texture du
					// kit « Medieval Village » (copiée dans meshes/props/) par ce nom.
					// Priorité au NOM DU MATÉRIAU (sinon un mur « Wall_Plaster_WoodGrid »
					// donnerait du plâtre à sa partie bois) ; le chemin ne sert qu'à
					// distinguer la brique générique (rouge / irrégulière).
					auto matHas  = [&](const char* s) { return matName.find(s)  != std::string::npos; };
					auto pathHas = [&](const char* s) { return meshPath.find(s) != std::string::npos; };
					const char* baseTex = nullptr; const char* nrmTex = nullptr; const char* ormTex = nullptr;
					if (matHas("Plaster"))            { baseTex = "T_Plaster_BaseColor.png";     nrmTex = "T_Plaster_Normal.png";     ormTex = "T_Plaster_ORM.png"; }
					else if (matHas("Wood"))          { baseTex = "T_WoodTrim_BaseColor.png";    nrmTex = "T_WoodTrim_Normal.png";    ormTex = "T_WoodTrim_ORM.png"; }
					else if (matHas("UnevenBrick"))   { baseTex = "T_UnevenBrick_BaseColor.png"; nrmTex = "T_UnevenBrick_Normal.png"; }
					else if (matHas("RedBrick"))      { baseTex = "T_RedBrick_BaseColor.png";    nrmTex = "T_Brick_Normal.png"; }
					else if (matHas("Rock"))          { baseTex = "T_RockTrim_BaseColor.png";    nrmTex = "T_RockTrim_Normal.png";    ormTex = "T_RockTrim_ORM.png"; }
					else if (matHas("RoundTile"))     { baseTex = "T_RoundTiles_BaseColor.png";  nrmTex = "T_RoundTiles_Normal.png"; }
					else if (matHas("Metal"))         { baseTex = "T_Trim_Metal_BaseColor.png";  nrmTex = "T_Trim_Metal_Normal.png";  ormTex = "T_Trim_Metal_ORM.png"; }
					else if (matHas("Brick"))         {
						// Matériau « MI_Brick » générique : on affine via le nom du mesh.
						if      (pathHas("RedBrick"))    { baseTex = "T_RedBrick_BaseColor.png";    nrmTex = "T_Brick_Normal.png"; }
						else if (pathHas("UnevenBrick")) { baseTex = "T_UnevenBrick_BaseColor.png"; nrmTex = "T_UnevenBrick_Normal.png"; }
						else                             { baseTex = "T_Brick_BaseColor.png";       nrmTex = "T_Brick_Normal.png"; }
					}
					else if (pathHas("RoundTiles"))   { baseTex = "T_RoundTiles_BaseColor.png";  nrmTex = "T_RoundTiles_Normal.png"; }

					engine::render::TextureHandle bc;
					if (baseTex) bc = m_assetRegistry.LoadTexture(meshDir + baseTex, /*useSrgb*/ true);
					if (bc.IsValid())
					{
						engine::render::Material mat{};
						mat.baseColor = bc;
						if (nrmTex) { const auto n = m_assetRegistry.LoadTexture(meshDir + nrmTex, false); if (n.IsValid()) mat.normal = n; }
						if (ormTex) { const auto o = m_assetRegistry.LoadTexture(meshDir + ormTex, false); if (o.IsValid()) mat.orm = o; }
						matIdx = materialCache.CreateMaterial(m_vkDeviceContext.GetDevice(), mat);
						hlIdx = matIdx;
					}
					else
					{
						// Repli (matériau non mappé OU texture absente) : couleur plate
						// plutôt que blanc. Et on LOGGE le matériau pour pouvoir le
						// mapper précisément ensuite (diagnostic des pièces blanches).
						uint8_t cr = 200, cg = 195, cb = 185;
						if (matHas("Plaster"))                         { cr = 232; cg = 224; cb = 203; }
						else if (matHas("RedBrick") || pathHas("RedBrick")) { cr = 165; cg =  86; cb =  72; }
						else if (matHas("Brick") || pathHas("Brick"))  { cr = 170; cg = 120; cb =  96; }
						else if (pathHas("WoodDark"))                  { cr = 102; cg =  72; cb =  48; }
						else if (pathHas("WoodLight"))                 { cr = 170; cg = 130; cb =  88; }
						else if (matHas("Wood"))                       { cr = 140; cg = 100; cb =  62; }
						else if (matHas("Metal"))                      { cr = 120; cg = 122; cb = 128; }
						else if (matHas("Glass") || matHas("Window"))  { cr = 205; cg = 222; cb = 232; } // verre pâle
						engine::render::Material mat{};
						mat.baseColor = SolidColorTexture(cr, cg, cb);
						matIdx = materialCache.CreateMaterial(m_vkDeviceContext.GetDevice(), mat);
						hlIdx = matIdx;
						LOG_WARN(Render, "[Buildings] materiau sans texture '{}' (mesh '{}', baseTex tente='{}') -> couleur plate ({},{},{})",
							matName, meshPath, baseTex ? baseTex : "(aucun)", cr, cg, cb);
					}
				}
				m_trimMatCache[matName] = { matIdx, hlIdx };
			}

			PropPart part;
			part.mesh = mh;
			part.materialIndex = matIdx;
			part.highlightMaterialIndex = hlIdx;
			prop.parts.push_back(part);
		}

		prop.interactableIndex = interactableIndex;
		prop.worldPos = engine::math::Vec3{ cx, minY, cz };
		if (!prop.parts.empty())
		{
			if (solid)
			{
				float radius = collisionRadius > 0.0f ? collisionRadius : radiusAuto;
				if (radius < 0.05f) radius = 0.5f;
				// Pièces de bâtiment : une « door » devient un passage franchissable,
				// un « escalier » une surface gravissable (cf. PropCylinder). Détection
				// par le nom du mesh, insensible à la casse. Les murs restent pleins.
				std::string meshLower = meshPath;
				for (char& ch : meshLower)
				{
					if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
				}
				// Basename du mesh (sans dossier ni extension) pour le catalogue.
				std::string meshBase = meshPath;
				{
					const auto sl = meshBase.find_last_of("/\\");
					if (sl != std::string::npos) meshBase = meshBase.substr(sl + 1);
					const auto dot = meshBase.find_last_of('.');
					if (dot != std::string::npos) meshBase = meshBase.substr(0, dot);
				}

				const auto* piece = m_buildingCollisionCatalog.Lookup(meshBase);
				const bool isStair = (meshLower.find("escalier") != std::string::npos)
					|| (meshLower.find("stair") != std::string::npos);
				if (piece == nullptr)
				{
					// Fallback : comportement actuel (cylindre englobant), rétro-compatible.
					// Pièce de bâtiment = MUR (barrière latérale pure, pas de dessus
					// marchable), SAUF un escalier qui doit garder son dessus pour être
					// gravi. Évite le « vol » : la sonde anti-encastrement ne peut plus
					// remonter le perso au sommet du gros cylindre englobant de la pièce.
					engine::gameplay::PropCylinder cyl{ cx, cz, radius, minY, maxY };
					cyl.passable = meshLower.find("door") != std::string::npos;
					cyl.stair = isStair;
					cyl.wall = !cyl.stair;
					m_worldCollider.AddCylinder(cyl);
				}
				else if (!piece->passable)
				{
					// Boîtes du catalogue : transformer chaque boîte LOCALE par la
					// matrice monde de la pièce (worldM, column-major M[col*4+row]).
					// Colonnes 3x3 = axes + échelle.
					const float* M = worldM.m;
					const engine::math::Vec3 colX{ M[0], M[1], M[2] };
					const engine::math::Vec3 colY{ M[4], M[5], M[6] };
					const engine::math::Vec3 colZ{ M[8], M[9], M[10] };
					const float sX = std::sqrt(colX.x*colX.x + colX.y*colX.y + colX.z*colX.z);
					const float sY = std::sqrt(colY.x*colY.x + colY.y*colY.y + colY.z*colY.z);
					const float sZ = std::sqrt(colZ.x*colZ.x + colZ.y*colZ.y + colZ.z*colZ.z);
					for (const auto& lb : piece->boxes)
					{
						// Centre local -> monde.
						const float wx = M[0]*lb.cx + M[4]*lb.cy + M[8]*lb.cz  + M[12];
						const float wy = M[1]*lb.cx + M[5]*lb.cy + M[9]*lb.cz  + M[13];
						const float wz = M[2]*lb.cx + M[6]*lb.cy + M[10]*lb.cz + M[14];
						engine::gameplay::PropBox box;
						box.cx = wx; box.cz = wz;
						box.halfX = lb.hx * sX; box.halfZ = lb.hz * sZ;
						box.axisX = (sX > 1e-6f) ? engine::math::Vec3{ colX.x/sX, 0.0f, colX.z/sX } : engine::math::Vec3{ 1, 0, 0 };
						box.axisZ = (sZ > 1e-6f) ? engine::math::Vec3{ colZ.x/sZ, 0.0f, colZ.z/sZ } : engine::math::Vec3{ 0, 0, 1 };
						box.loY = wy - lb.hy * sY; box.hiY = wy + lb.hy * sY;
						box.stair = isStair;
						// Roadmap-5 (2026-07-19) — dessus marchable pour les
						// sols (tag JSON "walkable") et les marches d'escalier ;
						// les murs restent des barrières latérales pures.
						box.walkableTop = piece->walkable || isStair;
						box.wall = !box.walkableTop;
						m_worldCollider.AddBox(box);
					}
				}
				// piece->passable : on n'ajoute rien (battant franchissable).
			}
			m_props.push_back(std::move(prop));
		}
	}

	bool Engine::EnsureImpostorAtlas(const std::string& meshPath)
	{
		if (!m_impostorEnabled || meshPath.empty()) return false;
		auto it = m_impostorAtlases.find(meshPath);
		if (it != m_impostorAtlases.end())
			return it->second.IsValid();

		// Chemin de l'atlas : à côté du .gltf, même nom, extension `.mipo`.
		// Ex. meshes/props/DeadTree_2.gltf -> <content>/meshes/props/DeadTree_2.mipo.
		const std::string contentRoot = m_cfg.GetString("paths.content", "game/data");
		std::string base = meshPath;
		const auto dot = base.find_last_of('.');
		if (dot != std::string::npos) base = base.substr(0, dot);
		const std::string mipoPath = contentRoot + "/" + base + ".mipo";

		engine::render::ImpostorAsset asset;
		std::string err;
		const bool ok = asset.LoadFromFile(mipoPath, m_assetRegistry, err);
		if (!ok)
		{
			// Absence d'atlas = cas normal (tous les meshes n'en ont pas) -> Debug, pas Warn.
			LOG_DEBUG(Render, "[Impostor] pas d'atlas pour '{}' ({})", meshPath, err);
		}
		// On insère même un asset invalide pour mémoriser l'échec (évite de retenter
		// le chargement disque à chaque frame/prop partageant ce mesh).
		auto [insIt, inserted] = m_impostorAtlases.emplace(meshPath, std::move(asset));
		(void)inserted;
		return insIt->second.IsValid();
	}

	void Engine::RecordPropsGeometry(VkCommandBuffer cmd, engine::render::Registry& reg,
	                                 const engine::RenderState& rs)
	{
		if (m_props.empty() || !m_pipeline) return;
		auto& geom = m_pipeline->GetGeometryPass();
		// loadOp=LOAD requis pour se superposer au GBuffer (terrain + avatar) sans clear.
		if (!geom.HasLoadPass()) return;
		auto& materialCache = m_pipeline->GetMaterialDescriptorCache();
		// Culling de distance des props de DECOR. Le profiler (M18.1) montre que la
		// passe Geometry GPU (overdraw de feuillages a 6,2 Mpx) est le poste de cout
		// n1 en foret dense : au-dela de ce rayon, on ne dessine plus le decor lointain
		// VISIBLE. Distance lue a CHAQUE frame depuis config.json (reglage a chaud, sans
		// recompilation) ; repli 80 m si la cle est absente (sinon le correctif perf
		// serait silencieusement desactive sur une config plus ancienne). <= 0 =>
		// desactive. Les INTERACTIBLES (coffre, caisses, PNJ) sont toujours dessines.
		const engine::math::Vec3 camPos = rs.camera.position;
		const float cullDist = static_cast<float>(m_cfg.GetDouble("world.props.cull_distance_m", 80.0));
		const float cullDist2 = cullDist * cullDist;
		const bool cullEnabled = cullDist > 0.0f;

		// M45.5 — bascule mesh -> impostor. GATED world.impostor.enabled : par défaut
		// false => `impostorActive` reste false et le chemin ci-dessous est STRICTEMENT
		// l'historique (aucun changement de rendu). Quand actif : un prop de DÉCOR
		// (interactableIndex<0) dont la distance XZ ∈ [impostorDist, cullDist] ET qui a
		// un atlas chargé est rendu en impostor au lieu de son mesh ; en deçà
		// d'impostorDist, mesh normal (inchangé).
		const bool impostorPassValid = m_pipeline->GetImpostorPass().IsValid();
		const bool impostorActive = m_impostorEnabled && impostorPassValid;
		// Seuil de bascule mesh -> impostor : centralisé dans LodConfig (M45.5b),
		// plus de duplication avec world.impostor.distance_m. Gated : si inactif,
		// D = +inf => aucune des branches impostor ne s'active (historique strict).
		const float impostorDist = impostorActive
			? m_lodConfig.GetImpostorDistanceMax() : 1e30f;
		// Largeur de la bande de cross-fade (m) juste après le seuil : le mesh et
		// l'impostor coexistent, l'impostor monte en dither 0->1 (anti-popping).
		const float impostorBand = impostorActive
			? static_cast<float>(m_cfg.GetDouble("world.impostor.fade_band_m", 10.0)) : 0.0f;
		const float impostorDist2 = impostorDist * impostorDist;
		// Borne haute de la bande de fondu (au-delà : impostor seul, fadeAlpha=1).
		const float impostorFadeEnd = impostorDist + impostorBand;
		// Instances d'impostors accumulées, groupées par chemin de mesh (= clé atlas).
		std::unordered_map<std::string, std::vector<engine::render::ImpostorInstance>> impostorBatches;

		int drawn = 0, culled = 0, impostored = 0;
		// Boucle indexée : propIndex sert de clé FadeFor (anti-occlusion caméra). Cet
		// index DOIT correspondre à l'id d'occulteur poussé dans la boucle Update
		// (même conteneur m_props, même ordre → même frame).
		for (std::size_t propIndex = 0; propIndex < m_props.size(); ++propIndex)
		{
			const auto& prop = m_props[propIndex];
			const float dxp = prop.worldPos.x - camPos.x;
			const float dzp = prop.worldPos.z - camPos.z;
			const float dist2 = dxp * dxp + dzp * dzp;

			if (cullEnabled && prop.interactableIndex < 0)
			{
				if (dist2 > cullDist2)
					{ ++culled; continue; }  // decor trop loin -> non dessine
			}

			// Cross-fade mesh <-> impostor (M45.5b, décor uniquement, atlas dispo).
			// Trois régimes selon la distance XZ `dist` :
			//   dist < D                : mesh seul (drawImpostorOnly=false, pas d'impostor).
			//   D <= dist < D+band      : mesh PLEIN + impostor en dither montant (fadeAlpha
			//                             = smoothstep(D, D+band, dist)) -> aucun pop.
			//   dist >= D+band          : impostor SEUL (fadeAlpha=1), pas de mesh.
			// Gated : si impostorActive est false, D = +inf => on n'entre jamais ici.
			bool drawImpostorOnly = false;
			if (impostorActive && prop.interactableIndex < 0 && dist2 >= impostorDist2)
			{
				if (EnsureImpostorAtlas(prop.meshPath))
				{
					const float dist = std::sqrt(dist2);
					// smoothstep(D, D+band, dist) : 0 au seuil, 1 en fin de bande.
					float fade = 1.0f;
					if (impostorBand > 0.0f && dist < impostorFadeEnd)
					{
						const float t = std::clamp((dist - impostorDist) / impostorBand, 0.0f, 1.0f);
						fade = t * t * (3.0f - 2.0f * t); // smoothstep
					}
					engine::render::ImpostorInstance inst;
					inst.worldPos[0] = prop.impostorCenter.x;
					inst.worldPos[1] = prop.impostorCenter.y;
					inst.worldPos[2] = prop.impostorCenter.z;
					inst.radius      = prop.impostorRadius;
					inst.fadeAlpha   = fade;
					impostorBatches[prop.meshPath].push_back(inst);
					++impostored;
					// Hors de la bande de fondu : l'impostor REMPLACE le mesh (pas de mesh).
					// Dans la bande : on dessine AUSSI le mesh par-dessous (drawImpostorOnly
					// reste false) pour que l'impostor monte en fondu sans pop.
					if (dist >= impostorFadeEnd)
						drawImpostorOnly = true;
				}
				// Pas d'atlas pour ce mesh -> fallback mesh normal (dessin plus bas).
			}
			if (drawImpostorOnly)
				continue; // NE PAS dessiner le mesh : l'impostor le remplace entièrement.

			++drawn;
			// Surbrillance (chantier C) : si ce prop est l'interactible a portee, on
			// dessine ses parties avec la variante de materiau Highlight (teinte).
			const bool highlight =
				(prop.interactableIndex >= 0 && prop.interactableIndex == m_interactableInRange);
			// Fondu d'occlusion de ce prop (1.0 = opaque si non occulteur / inconnu).
			const float occFade =
				m_cameraOcclusionFade.FadeFor(static_cast<std::uint32_t>(propIndex));
			for (const auto& part : prop.parts)
			{
				engine::render::MeshAsset* mesh = part.mesh.Get();
				if (mesh == nullptr) continue;
				geom.Record(
					m_vkDeviceContext.GetDevice(), cmd, reg, m_vkSwapchain.GetExtent(),
					m_fgGBufferAId, m_fgGBufferBId, m_fgGBufferCId, m_fgGBufferVelocityId, m_fgDepthId,
					rs.prevViewProjMatrix.m, rs.viewProjMatrix.m, mesh,
					0u,
					materialCache.GetDescriptorSet(),
					prop.modelMatrix.m,
					highlight ? part.highlightMaterialIndex : part.materialIndex,
					true,
					occFade);
			}
		}

		// M45.5 — dessine les impostors accumulés DANS le GBuffer, via le render pass
		// loadOp=LOAD de la GeometryPass (RecordTerrainChunkBatch ouvre exactement ce
		// render pass + framebuffer + viewport/scissor). Un RecordInstances par atlas
		// distinct (le descriptor de l'ImpostorPass est réécrit à chaque appel).
		if (impostorActive && !impostorBatches.empty())
		{
			auto& impostorPass = m_pipeline->GetImpostorPass();
			geom.RecordTerrainChunkBatch(
				m_vkDeviceContext.GetDevice(), cmd, reg, m_vkSwapchain.GetExtent(),
				m_fgGBufferAId, m_fgGBufferBId, m_fgGBufferCId, m_fgGBufferVelocityId, m_fgDepthId,
				[this, &impostorPass, &impostorBatches, &rs](VkCommandBuffer innerCmd) {
					const float camPos3[3] = {
						rs.camera.position.x, rs.camera.position.y, rs.camera.position.z };
					// Échelle de parallax single-step (frag v2) ; gated par enabled.
					const float parallaxScale = static_cast<float>(
						m_cfg.GetDouble("world.impostor.parallax_scale", 0.08));
					for (auto& kv : impostorBatches)
					{
						auto atlasIt = m_impostorAtlases.find(kv.first);
						if (atlasIt == m_impostorAtlases.end() || !atlasIt->second.IsValid())
							continue;
						const engine::render::ImpostorAsset& atlas = atlasIt->second;
						engine::render::TextureAsset* albedo = atlas.Albedo().Get();
						engine::render::TextureAsset* normal = atlas.Normal().Get();
						engine::render::TextureAsset* orm    = atlas.Orm().Get();
						if (albedo == nullptr || normal == nullptr || orm == nullptr
							|| albedo->view == VK_NULL_HANDLE || normal->view == VK_NULL_HANDLE
							|| orm->view == VK_NULL_HANDLE)
							continue;
						const VkSampler samp = impostorPass.GetSampler();
						impostorPass.RecordInstances(
							m_vkDeviceContext.GetDevice(), innerCmd, m_vkSwapchain.GetExtent(),
							kv.second.data(), static_cast<uint32_t>(kv.second.size()),
							albedo->view, samp, normal->view, samp, orm->view, samp,
							rs.viewProjMatrix.m, rs.prevViewProjMatrix.m, camPos3,
							atlas.Info().viewsPerAxis, atlas.Info().tileSize, parallaxScale);
					}
				});
		}

		// Diag throttle (1/60 frames) : visible uniquement en log.level=Debug.
		if ((m_currentFrame % 60) == 0)
			LOG_DEBUG(Render, "[Props] cull_dist={:.0f}m dessines={} impostors={} coupes={} (total={})",
			          cullDist, drawn, impostored, culled, static_cast<int>(m_props.size()));
	}

	void Engine::RecordRemoteAvatars(VkCommandBuffer cmd, engine::render::Registry& reg,
	                                 const engine::RenderState& rs)
	{
		(void)reg; // reg n'est plus utilisé depuis le passage à SkinnedRenderer (TD.7).
		if (!m_pipeline) return;
		const std::vector<engine::client::UIRemoteEntity>& remotes = m_uiModelBinding.GetModel().remoteEntities;
		if (remotes.empty()) return;
		// TD.7 — bascule du placeholder (GeometryPass, mesh cube statique) au rendu skinné
		// (SkinnedRenderer, mesh humanoïde + animation dérivée de la vélocité serveur). Les
		// pré-requis : (1) SkinnedRenderer initialisé + ses meshes humains chargés ;
		// (2) GeometryPass dispose d'un load pass (on partage le render pass pour superposer
		// au G-buffer terrain + props + avatar local sans clear).
		static bool diagLoggedReason = false;
		if (!m_skinnedRenderer.IsValid())
		{
			if (!diagLoggedReason)
			{
				LOG_WARN(Render, "[RecordRemoteAvatars] SKIP : SkinnedRenderer non initialise (remotes={})", remotes.size());
				diagLoggedReason = true;
			}
			return;
		}
		auto& geom = m_pipeline->GetGeometryPass();
		if (!geom.HasLoadPass())
		{
			if (!diagLoggedReason)
			{
				LOG_WARN(Render, "[RecordRemoteAvatars] SKIP : GeometryPass sans load pass (remotes={})", remotes.size());
				diagLoggedReason = true;
			}
			return;
		}
		auto& materialCache = m_pipeline->GetMaterialDescriptorCache();
		const float nowSec = EngineNowSec();
		// TD.7 — seuils dérivation Idle/Walk depuis la vélocité serveur. Le seuil bas est
		// volontairement laxiste (0.1 m/s) pour éviter de flicker entre Idle et Walk autour
		// du stationnement (déplacements d'inertie / sub-tick). Au-delà, on joue Walk en
		// boucle. Pas encore de distinction Walk/Run/Sprint côté wire — TD.7 V1 suffit.
		constexpr float kIdleSpeedThresholdMps = 0.1f;
		static bool diagLoggedSuccess = false;
		for (const engine::client::UIRemoteEntity& re : remotes)
		{
			// remoteEntities contient TOUTES les entités distantes (joueurs + mobs + lootbags).
			// Combat SP1 : les mobs (archetypeId != 0) réutilisent un mesh de race
			// existant déclaré par le catalogue (cf. CreatureCatalog) ; seuls les loot
			// bags (les deux ids à 0) gardent leur pipeline dédié (pas de mesh humanoïde).
			// Attention budget : chaque mob = 1 draw skinné dans le ring SSBO partagé
			// (kFrameSlots=32, cf. SkinnedRenderer.h) — au-delà de ~16 draws skinnés
			// par frame le ring se réécrit pendant une frame en vol (limite connue,
			// audit 2026-06-10) ; zone actuelle = 1 mob, marge OK.
			std::string meshRace;
			std::string gender;
			float meshScale = 1.0f;
			if (re.playerClientId != 0u)
			{
				// TD.6 — genre du personnage propagé par le snapshot. Fallback "male" si vide
				// (master sans migration 0067 ou avatar legacy). Récupère le mesh correspondant
				// dans le registre m_raceMeshes (clé "humains|male" / "humains|female").
				meshRace = "humains";
				gender = (re.gender == "male" || re.gender == "female") ? re.gender : std::string{"male"};
			}
			else if (re.archetypeId >= engine::server::kGatheringNodeArchetypeBase)
			{
				// Métiers SP1 — node de récolte : pas de mesh (label flottant seul).
				continue;
			}
			else if (re.archetypeId != 0u)
			{
				// Mob mort en attente de despawn : pas de rendu (kEntityStateDead).
				if ((re.stateFlags & 1u) != 0u)
					continue;
				const engine::client::CreatureAppearance* appearance = m_creatureCatalog.Find(re.archetypeId);
				meshRace = (appearance != nullptr && !appearance->meshKey.empty())
					? appearance->meshKey
					: std::string{"humains"};
				gender = "male";
				meshScale = (appearance != nullptr) ? appearance->scale : 1.0f;
			}
			else
			{
				continue; // loot bag : pipeline dédié, pas de mesh humanoïde
			}
			engine::render::skinned::SkinnedMesh* remoteMesh = GetRaceMesh(meshRace, gender);
			if (remoteMesh == nullptr && re.archetypeId != 0u)
			{
				// Fallback mob : la race du catalogue n'est pas chargée dans
				// m_raceMeshes (asset absent) → on retombe sur le mesh humain.
				remoteMesh = GetRaceMesh("humains", "male");
			}
			if (remoteMesh == nullptr)
			{
				// Aucun mesh chargé pour cette combo race+genre → on saute. Pas de fallback
				// vers le cube placeholder ici : si le mesh humain n'est pas chargé, l'avatar
				// local non plus ne s'afficherait pas correctement.
				continue;
			}
			// TD.3 : position lissée (interpolation, cf. UpdateGameplayNet) ; repli sur la
			// position snapshot brute si pas encore d'état lissé (graceful).
			float px = re.positionX, py = re.positionY, pz = re.positionZ, yaw = re.yawRadians;
			const auto sit = m_remoteSmoothed.find(re.entityId);
			if (sit != m_remoteSmoothed.end() && sit->second.valid)
			{
				px = sit->second.x; py = sit->second.y; pz = sit->second.z; yaw = sit->second.yaw;
			}
			// Combat SP1 fix — mobs : le serveur réplique le Y BRUT du spawner
			// (souvent 0.0, jamais collé au terrain — pas de heightfield serveur).
			// Avec l'offset -0.9 « centre → pieds » ci-dessous, le mesh se dessinait
			// SOUS le terrain (sanglier invisible, plaque flottante). Snap visuel au
			// sol client : ResolveRemoteDisplayCenterY renvoie sol + 0.9 pour les
			// non-joueurs, que le -0.9 ramène exactement au niveau des pieds.
			py = ResolveRemoteDisplayCenterY(re.playerClientId != 0u, py, px, pz);
			// TD.7 — état d'animation par avatar distant. Crée l'entrée à la première frame
			// où on voit cet entityId. L'horloge utilisée pour Sample() / Play() est la même
			// (nowSec) pour tous les avatars : crossfade et boucle restent en phase.
			RemoteAvatarAnim& anim = m_remoteAnims[re.entityId];
			// TD.8 — clip dérivé de l'état d'animation RÉPLIQUÉ (emote/roulade/run/sprint/saut/…),
			// plus seulement Idle/Walk dérivé de la vélocité. C'est ce qui rend /dance et la
			// roulade visibles aux autres joueurs.
			const float speedXZ = std::sqrt(re.velocityX * re.velocityX + re.velocityZ * re.velocityZ);
			AvatarLocomotionState effState = FromWireAnimState(re.animationState);
			// Repli : état Idle reçu mais l'avatar glisse encore (latence de propagation de
			// l'état) → on joue Walk pour éviter une pose Idle qui dérape.
			if (effState == AvatarLocomotionState::Idle && speedXZ >= kIdleSpeedThresholdMps)
				effState = AvatarLocomotionState::Walk;
			// L'état Emote ne désigne pas un clip unique : plusieurs emotes existent
			// (cf. kEmotes : /dance, /sit, /talk, /torch, /kneel, /sittalk, /push) mais le wire
			// ne porte pas le rôle précis (1 octet d'état seulement). V1 : on joue l'emote
			// primaire "Dance" pour TOUTES les emotes distantes — /dance est donc exact, les
			// autres s'animent visuellement comme Dance (mieux que l'ancien Idle/Walk figé).
			// Réplication du rôle d'emote exact = travail ultérieur (porter le rôle sur le wire).
			const char* desiredClipName = (effState == AvatarLocomotionState::Emote)
				? "Dance"
				: StateToClipName(effState);
			if (anim.lastClipName != desiredClipName)
			{
				const engine::render::skinned::AnimationClip* clip = remoteMesh->FindClip(desiredClipName);
				if (clip != nullptr)
				{
					anim.crossfade.Play(*clip, /*loops=*/ ClipLoops(effState), nowSec);
					anim.lastClipName = desiredClipName;
				}
				// Si le clip n'existe pas sur ce mesh (asset manquant), on garde l'anim
				// précédente (graceful) — pas de switch, pas de crash.
			}
			// Sample → globals → finals : même chaîne que l'avatar local (cf. ligne ~4723).
			auto locals  = anim.crossfade.Sample(remoteMesh->skeleton, nowSec);
			auto globals = engine::render::skinned::AnimationSampler::ComputeGlobalMatrices(
				remoteMesh->skeleton, locals);
			auto finals  = engine::render::skinned::AnimationSampler::ComputeFinalMatrices(
				remoteMesh->skeleton, globals);
			// Pieds au sol : le serveur réplique la position « centre capsule » comme pour
			// l'avatar local (feetPos = ccPos.y - 0.9). Même offset ici pour la cohérence.
			// Combat SP1 : échelle uniforme d'archétype (1.0 pour les joueurs) appliquée
			// entre la rotation et l'importTransform — Mat4 n'a pas de helper Scale,
			// on construit la diagonale (column-major) à la main.
			engine::math::Mat4 scaleMat;
			scaleMat.m[0] = meshScale;
			scaleMat.m[5] = meshScale;
			scaleMat.m[10] = meshScale;
			const engine::math::Mat4 model =
				engine::math::Mat4::Translate(engine::math::Vec3{ px, py - 0.9f, pz }) *
				engine::math::Mat4::RotateY(yaw) *
				scaleMat *
				(remoteMesh->importTransform);
			// Matériau : on prend le même matériau habit (m_avatarMaterialId) que l'avatar
			// local. Pas encore de skin par-personnage côté serveur — toute la flotte distante
			// partage le même habit pour V1. Idem pour la peau (m_avatarBodyMaterialId{Male,Female}).
			const uint32_t outfitMaterialIndex = m_avatarMaterialId;
			const uint32_t skinMaterialIndex = (gender == "female")
				? m_avatarBodyMaterialIdFemale
				: m_avatarBodyMaterialIdMale;
			// Per-submesh materials : skin pour les sous-maillages dont le nom matche un body
			// material name (cf. Engine::BuildAvatarSubmeshMaterialIndices pour la logique).
			// Pour V1, on passe un vecteur vide → SkinnedRenderer dessine en un seul draw avec
			// outfitMaterialIndex. C'est cohérent avec l'avatar local pré-mat-mat (régression
			// acceptée pour V1, sera amélioré quand les matériaux par submesh seront décorrélés
			// de l'état local).
			const std::vector<uint32_t> submeshMaterialIndices;
			m_skinnedRenderer.Record(
				m_vkDeviceContext.GetDevice(), cmd,
				m_vkSwapchain.GetExtent(),
				geom.GetRenderPassLoad(),
				VK_NULL_HANDLE,
				rs.prevViewProjMatrix.m, rs.viewProjMatrix.m,
				*remoteMesh,
				finals,
				materialCache.GetDescriptorSet(),
				model.m,
				outfitMaterialIndex,
				submeshMaterialIndices,
				skinMaterialIndex,
				m_avatarSkinDepthBiasConstant,
				m_avatarSkinDepthBiasSlope);
			if (!diagLoggedSuccess)
			{
				LOG_INFO(Render,
					"[RecordRemoteAvatars] OK : rendu skinne demarre (remotes={}, premier entity_id={}, gender='{}', clip='{}', mesh.bones={}, skin_mat={})",
					remotes.size(), re.entityId, gender, desiredClipName,
					remoteMesh->skeleton.bones.size(), skinMaterialIndex);
				diagLoggedSuccess = true;
			}
		}
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
		// SP2 Task 5 — Relayout journal/tracker (QuestUiPresenter). SetViewportSize
		// est un no-op silencieux (log + false) si le presenter n'est pas initialise
		// (echec Init au boot), donc pas besoin d'un garde IsInitialized ici.
		(void)m_questUi.SetViewportSize(static_cast<uint32_t>(std::max(1, w)), static_cast<uint32_t>(std::max(1, h)));
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

	void Engine::ToggleInGamePauseMenu()
	{
		m_inGamePauseMenuVisible = !m_inGamePauseMenuVisible;
		LOG_INFO(Core, "[InGamePauseMenu] toggled visible={}", m_inGamePauseMenuVisible);
	}

	void Engine::ReloadKeybindsFromDisk()
	{
		// Même mécanisme qu'au boot (ApplyUserSettingsOverrides) : Config::LoadFromFile
		// merge (override) les clés du fichier par-dessus m_cfg. Re-charger keybinds.json
		// réécrit donc controls.keybind.* dans m_cfg -> les binds remappés via l'écran
		// d'options prennent effet à la frame suivante (BuildMoveInput lit m_cfg).
		// Fichier absent/malformé -> LoadFromFile renvoie false : on garde les binds
		// courants (jamais de corruption).
		if (m_cfg.LoadFromFile("keybinds.json"))
			LOG_INFO(Core, "[Options] keybinds.json rechargé en jeu (binds clavier live)");
	}

	void Engine::RequestLogoutToLoginScreen()
	{
		LOG_INFO(Core, "[InGamePauseMenu] logout requested -> Login screen");
		// 1) Coupe la connexion gameplay UDP + presenters in-game.
		if (m_gameplayNetInitialized)
		{
			ShutdownGameplayNet();
		}
		// 2) Reset auth UI : flowComplete repasse a false, phase Login. Le presenter
		//    relancera MasterShardClientFlow au prochain clic Se connecter.
		m_authUi.RequestReturnToLogin();
		// 3) Cache le menu pause (pour qu'il ne reste pas visible sur l'ecran auth).
		m_inGamePauseMenuVisible = false;
		// 4) Oublie le character_id memorise (sinon SavePositionAsync continuerait
		//    a essayer d'envoyer la position d'un perso pour lequel la session est
		//    fermee).
		m_currentCharacterId = 0;
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

		// Combat SP2 — présentateurs combat (non bloquants : un échec d'Init
		// désactive juste le HUD combat, le gameplay réseau reste fonctionnel).
		if (!m_combatHud.Init())
		{
			LOG_WARN(Core, "[GameplayNet] CombatHudPresenter Init FAILED — HUD combat désactivé");
		}
		if (!m_advancedCombat.Init())
		{
			LOG_WARN(Core, "[GameplayNet] AdvancedCombatPresenter Init FAILED — panneau combat désactivé");
		}
		// Combat SP3 — BuffBar (auras du joueur / de la cible).
		if (!m_buffBar.Init())
		{
			LOG_WARN(Core, "[GameplayNet] BuffBarPresenter Init FAILED — BuffBar désactivée");
		}
		// Combat SP4 — FX d'auras (halo aux pieds des entités sous aura).
		if (!m_auraFx.Init())
		{
			LOG_WARN(Core, "[GameplayNet] AuraFXSystem Init FAILED — FX d'auras désactivés");
		}
		// Groupes SP1 — cadres de groupe (M32.2).
		if (!m_partyHud.Init())
		{
			LOG_WARN(Core, "[GameplayNet] PartyHudPresenter Init FAILED — cadres de groupe désactivés");
		}
		// Métiers SP1 — barre de récolte + panneau d'artisanat.
		if (!m_harvestBar.Init())
		{
			LOG_WARN(Core, "[GameplayNet] HarvestCastBarPresenter Init FAILED — barre de récolte désactivée");
		}
		if (!m_craftingUi.Init())
		{
			LOG_WARN(Core, "[GameplayNet] CraftingUiPresenter Init FAILED — panneau d'artisanat désactivé");
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
		// Combat SP2 — layout pixel du HUD combat (cadre cible, log, cooldowns).
		if (!m_combatHud.SetViewportSize(vw, vh))
		{
			LOG_WARN(Core, "[GameplayNet] CombatHudPresenter viewport FAILED — using fallback layout");
		}
		// Combat SP3 — layout pixel de la BuffBar.
		if (!m_buffBar.SetViewportSize(vw, vh))
		{
			LOG_WARN(Core, "[GameplayNet] BuffBarPresenter viewport FAILED — using fallback layout");
		}
		// Groupes SP1 — layout pixel des cadres de groupe.
		if (!m_partyHud.SetViewportSize(vw, vh))
		{
			LOG_WARN(Core, "[GameplayNet] PartyHudPresenter viewport FAILED — using fallback layout");
		}
		// Métiers SP1 — layouts récolte + artisanat.
		if (!m_harvestBar.SetViewportSize(vw, vh))
		{
			LOG_WARN(Core, "[GameplayNet] HarvestCastBarPresenter viewport FAILED — using fallback layout");
		}
		if (!m_craftingUi.SetViewportSize(vw, vh))
		{
			LOG_WARN(Core, "[GameplayNet] CraftingUiPresenter viewport FAILED — using fallback layout");
		}

		m_uiObserverHandle = m_uiModelBinding.AddObserver(
			[this](const engine::client::UIModel& model, uint32_t changeMask)
			{
				(void)m_shopUi.ApplyModel(model, changeMask);
				(void)m_auctionUi.ApplyModel(model, changeMask);
				(void)m_invUi.ApplyModel(model, changeMask);
				// Combat SP2 — les présentateurs combat consomment UIModelChangeCombat
				// (combat log, cadre cible, DPS meter) et Stats (PV joueur).
				(void)m_combatHud.ApplyModel(model, changeMask);
				m_advancedCombat.ApplyModel(model, changeMask);
				// Groupes SP1 — cadres de groupe (UIModelChangeParty).
				(void)m_partyHud.ApplyModel(model, changeMask);
				// Métiers SP1 — récolte (UIModelChangeHarvest) + artisanat (Crafting).
				(void)m_harvestBar.ApplyModel(model, changeMask);
				(void)m_craftingUi.ApplyModel(model, changeMask);
				// SP2 Task 5 — journal + tracker (QuestUiPresenter reconstruit sa vue
				// depuis model.quests a chaque changement). Le panneau donneur, lui,
				// lit directement model.giverList depuis QuestImGuiRenderer (pas besoin
				// de passer par le presenter).
				(void)m_questUi.ApplyModel(model, changeMask);
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
		// Phase 3.7.5 — character_key élargi à uint64. La config stocke un int64_t signé ;
		// on bit-cast pour préserver la valeur uint64 quand le bit 63 serait positionné
		// (reinterpret par bits, pas conversion arithmétique).
		const int64_t charKeyCfg = m_cfg.GetInt("client.gameplay_udp.character_key", 1);
		const uint64_t charKey = (charKeyCfg <= 0)
			? static_cast<uint64_t>(1u)
			: static_cast<uint64_t>(charKeyCfg);
		(void)m_gameplayUdp.SendHello(reqTick, reqSnap, charKey);

		m_gameplayNetInitialized = true;
		LOG_INFO(Core,
			"[GameplayNet] Init OK (host={}, port={}, vendor_target='{}', auction_target='{}')",
			host,
			port,
			m_gameplayVendorTalkTarget,
			m_gameplayAuctionTalkTarget);

		// SP2 Task 5 — Panneau donneur (QuestImGuiRenderer) : boutons Accepter/
		// Terminer. Contrairement au callback d'action du dialogue (qui cible
		// m_currentDialogueNpcTargetId), le panneau donneur cible
		// giverList.npcTargetId : il n'apparaît qu'après une réponse
		// QuestGiverList (donc npcTargetId est forcément à jour dans ce contexte).
		if (m_questImGui)
		{
			// Callback partagé Accepter/Rendre (panneau donneur ET boutons injectés
			// dans le dialogue, PR-B) : même logique, npcTargetId = celui du dernier
			// QuestGiverList (à jour dans ce contexte).
			auto giverActionCb = [this](const std::string& questId, uint8_t role)
			{
				if (!m_gameplayNetInitialized)
					return;
				const uint32_t gameplayClientId = m_gameplayUdp.ServerClientId();
				const std::string& npcTargetId = m_uiModelBinding.GetModel().giverList.npcTargetId;
				if (role == 0)
					(void)m_gameplayUdp.SendQuestAcceptRequest(gameplayClientId, questId, npcTargetId);
				else
					(void)m_gameplayUdp.SendQuestTurnInRequest(gameplayClientId, questId, npcTargetId);
			};
			m_questImGui->SetGiverActionCallback(giverActionCb);
			// PR-B — même callback pour les boutons Accepter/Rendre injectés dans la
			// fenêtre de dialogue (acceptation « dans la conversation »).
			if (m_dialogueImGui)
				m_dialogueImGui->SetGiverActionCallback(giverActionCb);
		}
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

		// Combat SP2/SP3/SP4 + Groupes/Métiers SP1 — symétrie d'Init.
		m_craftingUi.Shutdown();
		m_harvestBar.Shutdown();
		m_craftingVisible = false;
		m_partyHud.Shutdown();
		m_selectedAllyEntityId = 0;
		m_auraFx.Shutdown();
		m_buffBar.Shutdown();
		m_spellCooldownUiUntilSec.clear();
		m_advancedCombat.Shutdown();
		m_combatHud.Shutdown();
		m_advancedCombatVisible = false;
		m_attackSendCooldownSec = 0.0f;
		m_outOfRangeHintSec = 0.0f;
		m_rmbClickCandidate = false;
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

	// Refactor B2 (ST1) — Consomme et applique les commandes de réglages stagées par
	// l'écran d'options (AuthUiPresenter). Idempotent : appelable chaque frame.
	// Extrait verbatim de l'ancien bloc gardé par authGateActive dans la boucle de rendu,
	// pour que l'application des options tourne aussi in-game (écran d'options réutilisé).
	void Engine::ApplyConsumedSettingsCommands(bool authGateActive)
	{
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
			m_cfg.SetValue("render.resolution_width", static_cast<int64_t>(videoCmd.resolutionWidth));
			m_cfg.SetValue("render.resolution_height", static_cast<int64_t>(videoCmd.resolutionHeight));
			m_cfg.SetValue("render.quality_preset", static_cast<int64_t>(videoCmd.qualityPreset));
			m_cfg.SetValue("render.fov", static_cast<double>(videoCmd.fovDegrees));
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
			int cw = 0, ch = 0;
			m_window.GetClientSize(cw, ch);
			const bool resChanged = (videoCmd.resolutionWidth > 0 && videoCmd.resolutionHeight > 0
				&& (videoCmd.resolutionWidth != cw || videoCmd.resolutionHeight != ch));
			if (resChanged)
			{
				m_swapchainResizeRequested = true;
				LOG_INFO(Core, "[Options] Resolution persisted {}x{} (fenêtre actuelle {}x{} ; redimensionnement natif à brancher)",
					videoCmd.resolutionWidth, videoCmd.resolutionHeight, cw, ch);
			}
			if (!fullscreenChanged && !vsyncChanged && !resChanged)
			{
				LOG_INFO(Core, "[Options] Video apply requested but window flags unchanged (qualité / FOV enregistrés dans la config)");
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
			// Garde-fou réseau (ST1, étape 4) : on ne (ré)initialise / n'arrête le réseau
			// gameplay QUE pendant l'auth. En jeu (session active), appliquer un réglage
			// "game settings" ne doit PAS couper la session UDP de façon intempestive via
			// ShutdownGameplayNet (ni rouvrir un socket via InitGameplayNet hors handshake).
			// La config est tout de même persistée ci-dessus ; le changement effectif de
			// l'état réseau in-game sera traité par un chemin dédié dans une sous-tâche
			// ultérieure du refactor B2.
			if (authGateActive && gameplayWasEnabled != gameCmd.gameplayUdpEnabled)
			{
				if (gameCmd.gameplayUdpEnabled)
					InitGameplayNet();
				else
					ShutdownGameplayNet();
			}
			else if (gameplayWasEnabled != gameCmd.gameplayUdpEnabled)
			{
				LOG_INFO(Core, "[Options] Game: changement gameplay_udp.enabled={} persisté en config mais état réseau inchangé (hors auth, session préservée)",
					gameCmd.gameplayUdpEnabled);
			}
			LOG_INFO(Core, "[Options] Game applied (gameplay_udp={}, allow_insecure_dev={}, timeout_ms={})",
				gameCmd.gameplayUdpEnabled, gameCmd.allowInsecureDev, gameCmd.authTimeoutMs);
		}
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

		// Combat SP2 — avance les timers des présentateurs combat (cooldowns HUD,
		// fenêtre DPS) et le throttle local d'envoi d'attaque.
		(void)m_combatHud.Tick(deltaSeconds);
		m_advancedCombat.Tick(deltaSeconds);
		// Métiers SP1 — progression des cast bars récolte/artisanat.
		(void)m_harvestBar.Tick(deltaSeconds);
		(void)m_craftingUi.Tick(deltaSeconds);

		// Correction SP1 — position imposée par le serveur (respawn, rejet
		// anti-triche, téléport) : téléporte le CharacterController AVANT
		// l'envoi du prochain Input — le mouvement client-autoritaire reprend
		// depuis la position imposée au lieu d'écraser la téléportation serveur
		// (c'était le bug du respawn SP2 : « Réapparaître » soignait sur place).
		{
			const engine::client::UIForcedPosition& forced = m_uiModelBinding.GetModel().forcedPosition;
			if (forced.pending)
			{
				// Validation v12 — collage au sol : le Y des points de réapparition
				// (data) ou du serveur (sans heightfield) peut être SOUS le terrain
				// local → le joueur réapparaissait sous la carte. On garantit au
				// minimum sol + 0.9 (centre capsule) ; un Y data plus haut (tour,
				// étage) reste respecté.
				const float forcedGroundY =
					m_terrain.SampleHeightAtWorldXZ(forced.x, forced.z) + 0.9f;
				const engine::math::Vec3 forcedPos{
					forced.x, std::max(forced.y, forcedGroundY), forced.z };
				(void)m_characterController.Init(forcedPos);
				m_orbitalCameraController.SetTargetPosition(forcedPos);
				m_avatarYaw = forced.yawRadians;
				LOG_INFO(Core,
					"[Engine] Position imposée par le serveur appliquée (pos=({:.1f},{:.1f},{:.1f}), reason={})",
					forced.x, forced.y, forced.z, forced.reason);
				// Validation v12 — un ForcePosition « respawn » prouve la
				// résurrection : on ferme l'écran de mort même si l'événement
				// de résurrection se perdait (UDP) ou si le shardd déployé ne
				// l'émettait pas encore (ceinture-bretelles).
				if (forced.reason == engine::server::kForcePositionReasonRespawn)
				{
					m_uiModelBinding.MarkLocalPlayerResurrected();
				}
				m_uiModelBinding.ClearForcedPosition();
			}
		}
		if (m_attackSendCooldownSec > 0.0f)
		{
			m_attackSendCooldownSec -= deltaSeconds;
		}
		// Validation v12 — fait expirer l'indication « Hors de portee ».
		if (m_outOfRangeHintSec > 0.0f)
		{
			m_outOfRangeHintSec -= deltaSeconds;
		}

		// TC.2 — émet la position + orientation de l'avatar local au shard, à la cadence
		// client.gameplay_udp.request_tick_hz (défaut 20 Hz), avec une séquence monotone.
		// C'est ce qui permet aux autres joueurs de voir bouger ce client (réplication AoI).
		{
			const int64_t hzCfg = m_cfg.GetInt("client.gameplay_udp.request_tick_hz", 20);
			const float hz = static_cast<float>(std::clamp<int64_t>(hzCfg, 1, 120));
			m_gameplayInputAccumSec += deltaSeconds;
			if (m_gameplayInputAccumSec >= (1.0f / hz))
			{
				m_gameplayInputAccumSec = 0.0f;
				const engine::math::Vec3 avatarPos = m_characterController.GetPosition();
				// TD.8 — propage l'état d'animation local pour que les autres joueurs voient
				// emotes/roulades/run/sprint/etc. (pas seulement Idle/Walk dérivé de la vélocité).
				(void)m_gameplayUdp.SendInput(clientId, ++m_gameplayInputSeq,
					avatarPos.x, avatarPos.y, avatarPos.z, m_avatarYaw,
					ToWireAnimState(m_avatarLocoState));
			}
		}

		// TD.3 — lissage exponentiel des positions des avatars distants vers la cible
		// snapshot (~10 Hz) pour un rendu fluide entre snapshots. RecordRemoteAvatars lit
		// ces valeurs (et retombe sur la position snapshot brute si une entité manque ici).
		{
			const std::vector<engine::client::UIRemoteEntity>& remotes = m_uiModelBinding.GetModel().remoteEntities;
			const float k = std::clamp(deltaSeconds * 12.0f, 0.0f, 1.0f); // ~0.1-0.2 s pour converger
			for (const engine::client::UIRemoteEntity& re : remotes)
			{
				RemoteAvatarSmoothed& s = m_remoteSmoothed[re.entityId];
				if (!s.valid)
				{
					s.x = re.positionX; s.y = re.positionY; s.z = re.positionZ; s.yaw = re.yawRadians; s.valid = true;
				}
				else
				{
					s.x += (re.positionX - s.x) * k;
					s.y += (re.positionY - s.y) * k;
					s.z += (re.positionZ - s.z) * k;
					s.yaw += (re.yawRadians - s.yaw) * k;
				}
			}
			// Purge des entités sorties d'AoI (borne la map ; erase renvoie l'itérateur suivant).
			for (auto it = m_remoteSmoothed.begin(); it != m_remoteSmoothed.end(); )
			{
				bool present = false;
				for (const engine::client::UIRemoteEntity& re : remotes)
					if (re.entityId == it->first) { present = true; break; }
				if (present) ++it; else it = m_remoteSmoothed.erase(it);
			}
			// TD.7 — purge symétrique des états d'animation. Si un avatar sort d'AoI ou se
			// déconnecte, on libère son AnimationCrossfade (sinon m_remoteAnims grossit sans
			// borne pour les entités qu'on a vues une fois).
			for (auto it = m_remoteAnims.begin(); it != m_remoteAnims.end(); )
			{
				bool present = false;
				for (const engine::client::UIRemoteEntity& re : remotes)
					if (re.entityId == it->first) { present = true; break; }
				if (present) ++it; else it = m_remoteAnims.erase(it);
			}
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

	std::vector<uint32_t> Engine::LoadTerrainSpirvWords(const char* relativeSpvPath)
	{
		std::vector<uint8_t> bytes = engine::platform::FileSystem::ReadAllBytesContent(m_cfg, relativeSpvPath);
		if (bytes.size() % 4u == 0u && !bytes.empty())
		{
			std::vector<uint32_t> out(bytes.size() / 4u);
			std::memcpy(out.data(), bytes.data(), bytes.size());
			return out;
		}
		LOG_WARN(Render, "[Terrain] Shader SPIR-V manquant ou invalide: {}", relativeSpvPath);
		return {};
	}

#if defined(_WIN32)
	void Engine::RebuildWorldEditorTerrainGpu()
	{
		if (!m_worldEditorExe || !m_vkDeviceContext.IsValid() || !m_worldEditorSession)
		{
			return;
		}
		VkDevice device = m_vkDeviceContext.GetDevice();
		VkPhysicalDevice physDev = m_vkDeviceContext.GetPhysicalDevice();
		vkDeviceWaitIdle(device);
		m_worldEditorTerrainTools.Shutdown();
		if (m_terrain.IsValid())
		{
			m_terrain.Destroy(device);
		}

		const std::string& hmRel = m_worldEditorSession->Doc().heightmapContentRelativePath;
		if (hmRel.empty())
		{
			return;
		}

		auto loadFn = [this](const char* p) { return LoadTerrainSpirvWords(p); };
		std::optional<float> worldSizeOverride;
		const engine::editor::WorldMapEditDocument& wed = m_worldEditorSession->Doc();
		if (wed.hasTerrainWorldSizeM && wed.terrainWorldSizeM > 0.0)
		{
			worldSizeOverride = static_cast<float>(wed.terrainWorldSizeM);
		}
		const std::string& splatRel = wed.splatmapContentRelativePath;
		const std::string& grassRel = wed.grassMaskContentRelativePath;
		const bool ok = m_terrain.Init(
			device,
			physDev,
			m_cfg,
			hmRel,
			splatRel,
			grassRel,
			"",
			{},
			VK_FORMAT_R8G8B8A8_SRGB,
			VK_FORMAT_A2B10G10R10_UNORM_PACK32,
			VK_FORMAT_R8G8B8A8_UNORM,
			VK_FORMAT_R16G16_SFLOAT,
			VK_FORMAT_D32_SFLOAT,
			m_vkDeviceContext.GetGraphicsQueue(),
			m_vkDeviceContext.GetGraphicsQueueFamilyIndex(),
			loadFn,
			worldSizeOverride);
		if (!ok)
		{
			LOG_WARN(Render,
				"[WorldEditor] TerrainRenderer::Init failed for \"{}\" — fichier introuvable sous paths.content ? "
				"Lancer l’éditeur avec le cwd à la racine du dépôt (config.json + game/data), ou rebuild avec la détection cwd (world_editor_main).",
				hmRel);
			return;
		}
		if (!m_worldEditorTerrainTools.Init(
				&m_terrain.GetMutableHeightmapData(),
				&m_terrain.GetSplatting(),
				&m_terrain.GetMutableGrassMaskData(),
				m_terrain.GetTerrainOriginX(),
				m_terrain.GetTerrainOriginZ(),
				m_terrain.GetTerrainWorldSize(),
				m_terrain.GetHeightScale()))
		{
			LOG_WARN(Render, "[WorldEditor] TerrainEditingTools::Init failed");
		}
		m_terrain.InvalidateFramebufferCache(device);

		// M100.46+ — Pont TerrainDocument → HeightmapData GPU. Le callback
		// est appelé synchrone par OnCommit (thread main) — il ne fait que
		// set un flag atomique pour différer la sync au prochain tick (où
		// l'on a accès à VkDeviceContext sans race). Re-set à chaque
		// Rebuild pour ne pas perdre l'observer si Shell ou Tools sont
		// reset.
		if (m_worldEditorShell && m_worldEditorShell->IsInitialized())
		{
			m_worldEditorShell->MutableTerrainDocument().SetOnChunkChanged(
				[this](engine::world::GlobalChunkCoord) {
					m_worldEditorTerrainNeedsSync.store(true, std::memory_order_release);
				});
		}

		// Detection "aucune texture utilisateur assignee" -> pousse le flag fallback
		// orange au TerrainRenderer (via push-constant). Des qu'au moins une couche
		// splat recoit un mapping texture (refs[i] non vide), le flag retombe a
		// false et le rendu normal reprend.
		// Garde stricte sur m_worldEditorExe : le client jeu ne doit jamais lever ce
		// flag (regression visuelle).
		bool noUserTextures = false;
		if (m_worldEditorExe && m_worldEditorSession)
		{
			noUserTextures = true;
			const auto& refs = m_worldEditorSession->Doc().splatLayerTextureRefs;
			for (const std::string& r : refs)
			{
				if (!r.empty()) { noUserTextures = false; break; }
			}
		}
		m_terrain.SetNoUserTexturesFallback(noUserTextures);

		// World Editor : desactive le frustum cull. Diagnostic (cf. PR #427) :
		// avec heightmap 256x256 + world_size override 10000m, le ratio
		// vertStepWorld/patchSize ne correspond pas a la matrice viewProj
		// utilisee par Frustum::ExtractFromMatrix, qui rejette TOUS les patches
		// meme quand la camera est pile au centre. Bug pre-existant (Gribb-
		// Hartmann avec convention Vulkan Z[0,1] + Y inverse). En attendant la
		// correction de l'extraction frustum, on desactive le cull cote World
		// Editor (225 patches max -> aucun impact perf). Le client jeu garde
		// le cull actif (defaut).
		m_terrain.SetFrustumCullEnabled(false);

		// Repositionne la camera au centre du terrain qu'on vient de charger pour
		// que l'utilisateur voie immediatement le sol apres "Creer une nouvelle
		// carte" ou "Charger la carte selectionnee". Sans ce reset, la camera peut
		// se retrouver hors champ ou sous le sol selon la heightmap chargee.
		// On utilise m_worldEditorExe (et non m_editorMode qui peut etre null si
		// EditorMode::Init a echoue) comme garde du reset.
		if (m_worldEditorExe)
		{
			const float ox = m_terrain.GetTerrainOriginX();
			const float oz = m_terrain.GetTerrainOriginZ();
			const float hs = m_terrain.GetHeightScale();

			// CRITIQUE : la zone REELLEMENT maillee (`actualExtX/Z`) peut etre
			// nettement plus petite que `GetTerrainWorldSize()` (cf.
			// `GetActualRenderedExtentX/Z`). Pour une heightmap 256x256 avec
			// world_size=10000, on couvre 2344 m au lieu de 10000 m. Si on
			// place la camera au centre du `world_size`, elle se retrouve hors
			// des patches et le frustum cull rejette TOUT (terrain invisible).
			// On vise donc le centre des patches reels.
			const float actualExtX = m_terrain.GetActualRenderedExtentX();
			const float actualExtZ = m_terrain.GetActualRenderedExtentZ();
			const float maxExt     = std::max(actualExtX, actualExtZ);

			const float centerX     = ox + actualExtX * 0.5f;
			const float centerZ     = oz + actualExtZ * 0.5f;
			const float midGroundY  = hs * 0.5f;            // hauteur moyenne attendue du sol

			// Cadrage ADAPTATIF : on place la camera en recul (+Z) et en hauteur
			// PROPORTIONNELS a l'etendue REELLE du terrain, orientee pour viser
			// exactement son centre. L'ancienne version (altitude fixe +80 m,
			// pitch fixe ~20deg) cadrait les grands terrains mais laissait une
			// PETITE carte neuve (ex. 60 m) entierement SOUS le bas de l'ecran :
			// le terrain etait bien rendu (Record diag kept=225) mais hors champ,
			// d'ou le ressenti "rien ne s'affiche apres Creer une nouvelle carte".
			// On calcule la distance qui fait tenir l'etendue verticale du
			// terrain dans le FOV (+ marge), avec un garde-fou bas pour les
			// micro-terrains. La camera vise le centre : avec la base camera
			// (forward = (0, -sin(pitch), -cos(pitch)) pour yaw=0), poser la
			// camera a `centre + (0, dist*sin(pitch), dist*cos(pitch))` garantit
			// que l'axe optique passe pile par le centre du sol.
			constexpr float kPi      = 3.14159265358979323846f;
			constexpr float fovYDeg  = 70.0f;
			constexpr float pitchDown = 0.70f;              // ~40deg : vue d'ensemble plongeante
			const float halfFovY     = (fovYDeg * 0.5f) * (kPi / 180.0f);
			const float fitDist      = (std::max(maxExt, 1.0f) * 0.5f * 1.3f) / std::tan(halfFovY);
			const float dist         = std::max(fitDist, 8.0f); // garde-fou micro-terrain

			engine::render::Camera reset;
			reset.position.x = centerX;
			reset.position.y = midGroundY + dist * std::sin(pitchDown);
			reset.position.z = centerZ + dist * std::cos(pitchDown);
			reset.yaw        = 0.0f;
			reset.pitch      = pitchDown;
			reset.fovYDeg    = fovYDeg;
			reset.aspect     = static_cast<float>(std::max(1, m_width)) / static_cast<float>(std::max(1, m_height));
			reset.nearZ      = 0.1f;
			reset.farZ       = std::max(5000.0f, (dist + maxExt) * 2.0f); // visibilite des bords
			m_renderStates[0].camera = reset;
			m_renderStates[1].camera = reset;
			LOG_INFO(Render,
				"[WorldEditor] Camera reset: pos=({:.1f},{:.1f},{:.1f}) farZ={:.0f} actualExt=({:.0f}x{:.0f}) origin=({:.0f},{:.0f})",
				reset.position.x, reset.position.y, reset.position.z, reset.farZ,
				actualExtX, actualExtZ, ox, oz);
		}
	}

	void Engine::SyncWorldEditorHeightmapFromDocument()
	{
#if defined(_WIN32)
		// Garde stricte : seul l'éditeur monde a un TerrainDocument actif
		// (le Shell). Le client jeu ne passe jamais ici.
		if (!m_worldEditorExe || !m_worldEditorShell || !m_worldEditorShell->IsInitialized())
			return;
		if (!m_worldEditorTerrainTools.IsValid()) return;
		if (!m_terrain.IsValid()) return;

		auto& terrainDoc = m_worldEditorShell->MutableTerrainDocument();
		auto& hm         = m_terrain.GetMutableHeightmapData();
		if (hm.width == 0 || hm.height == 0 || hm.heights.empty()) return;

		const float heightScale = m_terrain.GetHeightScale();
		if (heightScale <= 0.0f) return;

		// Map terrain world coords → heightmap pixel coords.
		// La heightmap couvre `terrainWorldSize` mètres avec `width-1` mailles
		// horizontales. Origine en (terrainOriginX, terrainOriginZ).
		const float originX     = m_terrain.GetTerrainOriginX();
		const float originZ     = m_terrain.GetTerrainOriginZ();
		const float worldSize   = m_terrain.GetTerrainWorldSize();
		if (worldSize <= 0.0f) return;
		const uint32_t hmW      = hm.width;
		const uint32_t hmH      = hm.height;
		const float pixelsPerMX = static_cast<float>(hmW - 1u) / worldSize;
		const float pixelsPerMZ = static_cast<float>(hmH - 1u) / worldSize;

		constexpr uint32_t kChunkRes =
			static_cast<uint32_t>(engine::world::terrain::kTerrainResolution);
		constexpr float kCell =
			engine::world::terrain::kTerrainCellSizeMeters;

		// Itère **tous les chunks chargés** du document (pas seulement 2×2),
		// car les zone presets s'appliquent sur 10 km (toute la zone) et
		// peuvent charger n'importe quelle paire (cx, cz). Pour chaque
		// cellule (ix, iz), on calcule sa position monde → pixel heightmap.
		// Conversion float (m) → uint16 normalise par heightScale.
		uint32_t touchedChunks = 0u;
		uint64_t modifiedCells = 0u;
		terrainDoc.ForEachLoadedChunk(
			[&](engine::world::GlobalChunkCoord coord,
			    const std::shared_ptr<engine::world::terrain::TerrainChunk>& chunk)
			{
				if (!chunk) return;
				++touchedChunks;
				const float chunkOriginX = static_cast<float>(coord.x) * (kChunkRes - 1u) * kCell;
				const float chunkOriginZ = static_cast<float>(coord.z) * (kChunkRes - 1u) * kCell;
				for (uint32_t iz = 0; iz < kChunkRes; ++iz)
				{
					for (uint32_t ix = 0; ix < kChunkRes; ++ix)
					{
						const float worldX = originX + chunkOriginX + static_cast<float>(ix) * kCell;
						const float worldZ = originZ + chunkOriginZ + static_cast<float>(iz) * kCell;
						const float fx = (worldX - originX) * pixelsPerMX;
						const float fz = (worldZ - originZ) * pixelsPerMZ;
						if (fx < 0.0f || fz < 0.0f) continue;
						const uint32_t px = static_cast<uint32_t>(fx + 0.5f);
						const uint32_t pz = static_cast<uint32_t>(fz + 0.5f);
						if (px >= hmW || pz >= hmH) continue;
						const float heightMeters = chunk->heights[iz * kChunkRes + ix];
						const float norm = std::clamp(heightMeters / heightScale, 0.0f, 1.0f);
						const uint16_t u16 = static_cast<uint16_t>(norm * 65535.0f + 0.5f);
						hm.heights[pz * hmW + px] = u16;
						++modifiedCells;
					}
				}
			});
		LOG_INFO(EditorWorld,
			"[Engine] SyncHeightmapFromDocument: {} chunks visited, {} cells written, hm={}x{}, heightScale={:.1f}m",
			touchedChunks, modifiedCells, hmW, hmH, heightScale);
		if (touchedChunks == 0u) return;

		// Pousse au GPU. FlushHeightmap re-upload la heightmap CPU complète
		// vers l'image GPU R16 (single staging buffer + one-time command
		// buffer). Coût ~10-30 ms sur 1025² uint16 (~2 MB).
		const auto& hmGpu = m_terrain.GetHeightmapGpu();
		(void)m_worldEditorTerrainTools.FlushHeightmap(
			m_vkDeviceContext.GetDevice(),
			m_vkDeviceContext.GetPhysicalDevice(),
			m_vkDeviceContext.GetGraphicsQueue(),
			m_vkDeviceContext.GetGraphicsQueueFamilyIndex(),
			hmGpu.image);
#endif
	}

	void Engine::ProcessSplatRefsDirty()
	{
		if (!m_worldEditorExe || !m_worldEditorSession) return;
		if (!m_texturePreviewCache || !m_texturePreviewCache->IsReady()) return;
		if (!m_worldEditorSession->ConsumeSplatRefsDirty()) return;
		if (!m_terrain.IsValid()) return;

		engine::render::terrain::TerrainSplatting& splatting = m_terrain.GetSplatting();

		// Pour chaque layer : pousser le buffer CPU adequat dans TerrainSplatting.
		const auto& refs = m_worldEditorSession->Doc().splatLayerTextureRefs;
		for (uint32_t layer = 0; layer < engine::render::terrain::kSplatLayerCount; ++layer)
		{
			const std::vector<uint8_t>* rgba = nullptr;
			if (!refs[layer].empty())
			{
				// Force la decode/upload de la vignette si pas deja fait
				// (cree l'entree dans le cache si absente).
				m_texturePreviewCache->GetTexrThumb(refs[layer]);
				rgba = m_texturePreviewCache->GetCpuRgba256(refs[layer]);
			}
			if (rgba == nullptr)
			{
				// Fallback procedurale : assure que l'entree procedurale existe.
				m_texturePreviewCache->GetProceduralThumb(layer);
				rgba = m_texturePreviewCache->GetCpuRgba256(
					"procedural:" + std::to_string(layer));
			}
			if (rgba != nullptr)
			{
				splatting.SetLayerCpuRgba256(layer, *rgba);
			}
		}

		if (!splatting.RebuildAlbedoArrayFromCpuLayers(
				m_vkDeviceContext.GetDevice(), m_vkDeviceContext.GetPhysicalDevice(),
				m_vkDeviceContext.GetGraphicsQueue(),
				m_vkDeviceContext.GetGraphicsQueueFamilyIndex()))
		{
			LOG_ERROR(Render, "[Engine] ProcessSplatRefsDirty: splat array rebuild failed");
		}
	}
#endif

	// =================================================================
	// M100 — Task 12 : ressources caméra (set 0) du TerrainChunkPipeline.
	// =================================================================

	bool Engine::CreateTerrainChunkCameraResources(std::string& outError)
	{
		VkDevice device = m_vkDeviceContext.GetDevice();
		VkPhysicalDevice physDev = m_vkDeviceContext.GetPhysicalDevice();
		if (device == VK_NULL_HANDLE || physDev == VK_NULL_HANDLE)
		{
			outError = "VkDeviceContext non initialisé";
			return false;
		}

		// 1. Set layout : 1 UBO pour CameraUBO { mat4 viewProj; }.
		VkDescriptorSetLayoutBinding binding{};
		binding.binding         = 0u;
		binding.descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		binding.descriptorCount = 1u;
		binding.stageFlags      = VK_SHADER_STAGE_VERTEX_BIT;
		VkDescriptorSetLayoutCreateInfo lci{};
		lci.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		lci.bindingCount = 1u;
		lci.pBindings    = &binding;
		if (vkCreateDescriptorSetLayout(device, &lci, nullptr, &m_terrainChunkCameraSetLayout) != VK_SUCCESS)
		{
			outError = "vkCreateDescriptorSetLayout (camera) failed";
			return false;
		}

		// 2. Pool : 1 set, 1 UBO.
		VkDescriptorPoolSize poolSize{};
		poolSize.type            = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		poolSize.descriptorCount = 1u;
		VkDescriptorPoolCreateInfo pci{};
		pci.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		pci.maxSets       = 1u;
		pci.poolSizeCount = 1u;
		pci.pPoolSizes    = &poolSize;
		if (vkCreateDescriptorPool(device, &pci, nullptr, &m_terrainChunkCameraPool) != VK_SUCCESS)
		{
			outError = "vkCreateDescriptorPool (camera) failed";
			DestroyTerrainChunkCameraResources();
			return false;
		}

		// 3. Buffer host-visible 64 octets (mat4 viewProj std140).
		constexpr VkDeviceSize kUboSize = 64u;
		VkBufferCreateInfo bci{};
		bci.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		bci.size        = kUboSize;
		bci.usage       = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
		bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		if (vkCreateBuffer(device, &bci, nullptr, &m_terrainChunkCameraUbo) != VK_SUCCESS)
		{
			outError = "vkCreateBuffer (camera UBO) failed";
			DestroyTerrainChunkCameraResources();
			return false;
		}
		VkMemoryRequirements req{};
		vkGetBufferMemoryRequirements(device, m_terrainChunkCameraUbo, &req);

		// Cherche un memory type host-visible + host-coherent.
		VkPhysicalDeviceMemoryProperties memProps{};
		vkGetPhysicalDeviceMemoryProperties(physDev, &memProps);
		uint32_t memType = UINT32_MAX;
		const VkMemoryPropertyFlags wanted = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
		for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i)
		{
			if ((req.memoryTypeBits & (1u << i)) != 0u
				&& (memProps.memoryTypes[i].propertyFlags & wanted) == wanted)
			{
				memType = i;
				break;
			}
		}
		if (memType == UINT32_MAX)
		{
			outError = "Aucun memory type host-visible+coherent trouvé pour camera UBO";
			DestroyTerrainChunkCameraResources();
			return false;
		}
		VkMemoryAllocateInfo mai{};
		mai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		mai.allocationSize  = req.size;
		mai.memoryTypeIndex = memType;
		if (vkAllocateMemory(device, &mai, nullptr, &m_terrainChunkCameraUboMem) != VK_SUCCESS)
		{
			outError = "vkAllocateMemory (camera UBO) failed";
			DestroyTerrainChunkCameraResources();
			return false;
		}
		if (vkBindBufferMemory(device, m_terrainChunkCameraUbo, m_terrainChunkCameraUboMem, 0) != VK_SUCCESS)
		{
			outError = "vkBindBufferMemory (camera UBO) failed";
			DestroyTerrainChunkCameraResources();
			return false;
		}
		if (vkMapMemory(device, m_terrainChunkCameraUboMem, 0, kUboSize, 0, &m_terrainChunkCameraUboMapped) != VK_SUCCESS)
		{
			outError = "vkMapMemory (camera UBO) failed";
			m_terrainChunkCameraUboMapped = nullptr;
			DestroyTerrainChunkCameraResources();
			return false;
		}

		// 4. Allocation du descriptor set + écriture initiale.
		VkDescriptorSetAllocateInfo dsai{};
		dsai.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		dsai.descriptorPool     = m_terrainChunkCameraPool;
		dsai.descriptorSetCount = 1u;
		dsai.pSetLayouts        = &m_terrainChunkCameraSetLayout;
		if (vkAllocateDescriptorSets(device, &dsai, &m_terrainChunkCameraSet) != VK_SUCCESS)
		{
			outError = "vkAllocateDescriptorSets (camera) failed";
			DestroyTerrainChunkCameraResources();
			return false;
		}
		VkDescriptorBufferInfo bufInfo{};
		bufInfo.buffer = m_terrainChunkCameraUbo;
		bufInfo.offset = 0u;
		bufInfo.range  = kUboSize;
		VkWriteDescriptorSet write{};
		write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		write.dstSet          = m_terrainChunkCameraSet;
		write.dstBinding      = 0u;
		write.descriptorCount = 1u;
		write.descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		write.pBufferInfo     = &bufInfo;
		vkUpdateDescriptorSets(device, 1u, &write, 0, nullptr);
		return true;
	}

	void Engine::DestroyTerrainChunkCameraResources()
	{
		VkDevice device = m_vkDeviceContext.GetDevice();
		if (device == VK_NULL_HANDLE) return;
		// Note : le descriptor set est libéré implicitement par vkDestroyDescriptorPool.
		m_terrainChunkCameraSet = VK_NULL_HANDLE;
		if (m_terrainChunkCameraUboMapped != nullptr && m_terrainChunkCameraUboMem != VK_NULL_HANDLE)
		{
			vkUnmapMemory(device, m_terrainChunkCameraUboMem);
			m_terrainChunkCameraUboMapped = nullptr;
		}
		if (m_terrainChunkCameraUbo != VK_NULL_HANDLE)
		{
			vkDestroyBuffer(device, m_terrainChunkCameraUbo, nullptr);
			m_terrainChunkCameraUbo = VK_NULL_HANDLE;
		}
		if (m_terrainChunkCameraUboMem != VK_NULL_HANDLE)
		{
			vkFreeMemory(device, m_terrainChunkCameraUboMem, nullptr);
			m_terrainChunkCameraUboMem = VK_NULL_HANDLE;
		}
		if (m_terrainChunkCameraPool != VK_NULL_HANDLE)
		{
			vkDestroyDescriptorPool(device, m_terrainChunkCameraPool, nullptr);
			m_terrainChunkCameraPool = VK_NULL_HANDLE;
		}
		if (m_terrainChunkCameraSetLayout != VK_NULL_HANDLE)
		{
			vkDestroyDescriptorSetLayout(device, m_terrainChunkCameraSetLayout, nullptr);
			m_terrainChunkCameraSetLayout = VK_NULL_HANDLE;
		}
	}

	void Engine::UpdateTerrainChunkCameraUbo(const float* viewProjMat4)
	{
		if (m_terrainChunkCameraUboMapped == nullptr || viewProjMat4 == nullptr)
			return;
		std::memcpy(m_terrainChunkCameraUboMapped, viewProjMat4, 64u);
		// Memory host-coherent : pas de flush nécessaire, la GPU verra l'update
		// avant le prochain submit.
	}

} // namespace engine
