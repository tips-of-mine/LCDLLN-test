// Engine — initialisation graphique et de la scène au démarrage (ancien gros
// bloc du constructeur Engine::Engine) : GLFW / fenêtre de surface, instance,
// device et swapchain Vulkan, pipeline différé, ressources (terrain, eau,
// avatar, ciel, nuages…) et passes du FrameGraph.
//
// Code déplacé tel quel (aucun changement de comportement). La documentation
// est dans Engine.h.

#include "src/client/app/Engine.h"
#include "src/client/app/EngineHelpers.h"

#include "src/shared/platform/FileSystem.h"
#include "src/client/render/clouds/CloudWeatherMapper.h"
#include "src/client/render/DeferredPipeline.h"
#include "src/client/render/terrain/TerrainRenderSelection.h"

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
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#if defined(_WIN32)
#include "imgui.h"
#endif

namespace engine
{
	using namespace engine::helpers; // utilitaires internes partagés (EngineHelpers.h)

	// Utilitaires déplacés d'Engine.cpp avec le démarrage graphique (seul utilisateur).
	namespace
	{
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

	// Ancien bloc du constructeur — voir la documentation dans Engine.h.
	void Engine::InitGraphicsAndScene()
	{
		LOG_INFO(Core, "[Boot] glfwInit OK");
		bool surfaceReady = false;
#if defined(_WIN32)
		surfaceReady = true;
#else
		// Build Linux = fumée (fenêtre cachée 1×1). Variable d'environnement
		// LCDLLN_LINUX_VISIBLE_WINDOW=1 : vraie fenêtre 1280×720 (tests de rendu
		// sous Xvfb + lavapipe, captures d'écran en CI ou en local).
		const char* visibleWindowEnv = std::getenv("LCDLLN_LINUX_VISIBLE_WINDOW");
		const bool visibleWindow = visibleWindowEnv != nullptr && visibleWindowEnv[0] == '1';
		glfwWindowHint(GLFW_VISIBLE, visibleWindow ? GLFW_TRUE : GLFW_FALSE);
		glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
		m_glfwWindowForVk = visibleWindow
			? glfwCreateWindow(1280, 720, "LCDLLN", nullptr, nullptr)
			: glfwCreateWindow(1, 1, "VkSurface", nullptr, nullptr);
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
										// SND.1 — musique de l'écran de connexion (Ogg Vorbis, boucle exacte).
										constexpr const char* kDefaultMenuMusic = "audio/music/menu_lune_noire.ogg";
										std::string menuMusicRel = m_cfg.GetString("audio.menu_music_path", "");
										if (menuMusicRel.empty()
											|| !engine::platform::FileSystem::Exists(engine::platform::FileSystem::ResolveContentPath(m_cfg, menuMusicRel)))
										{
											// Clé absente, ou config.json local qui pointe encore vers une piste
											// retirée (ancien MP3) : la piste livrée par défaut prend le relais.
											if (!menuMusicRel.empty() && menuMusicRel != kDefaultMenuMusic)
												LOG_WARN(Core, "[Boot] audio.menu_music_path '{}' introuvable : repli sur {}", menuMusicRel, kDefaultMenuMusic);
											menuMusicRel = kDefaultMenuMusic;
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
											return SampleGroundHeight(worldX, worldZ);
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
								InitWaterPassResources(loadSpirv, pipelineOk);

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
									// Monde « Royaumes » en cellules : pas de terrain unique, les terrains
									// des cellules sont chargés par le streaming (EngineCells.cpp).
									if (pipelineOk && !m_worldEditorExe && !m_cellWorld)
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

									InitAuthGlyphAndLogoPasses(loadSpirv);

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
									InitSkinnedAvatarRenderer(loadSpirv);

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
									InitPlayerSpawnPlacement();

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
											RecordGeometryFramePass(cmd, reg);
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
											RecordLightingFramePass(cmd, reg);
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
												RecordWaterFramePass(cmd, reg);
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
												RecordVolumetricFogFramePass(cmd, reg);
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
												RecordCloudsFramePass(cmd, reg);
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
											RecordTaaFramePass(cmd, reg);
										});

									m_frameGraph.addPass("CopyPresent",
										[this](engine::render::PassBuilder& b) {
											b.read(m_fgHistoryAId,      engine::render::ImageUsage::TransferSrc);
											b.read(m_fgHistoryBId,      engine::render::ImageUsage::TransferSrc);
											b.read(m_fgSceneColorLDRId, engine::render::ImageUsage::TransferSrc);
											b.write(m_fgBackbufferId,   engine::render::ImageUsage::TransferDst);
										},
										[this](VkCommandBuffer cmd, engine::render::Registry& reg) {
											RecordPresentFramePass(cmd, reg);
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

	// Sous-étape extraite de InitGraphicsAndScene — voir la documentation dans Engine.h.
	void Engine::InitWaterPassResources(const std::function<std::vector<uint32_t>(const char*)>& loadSpirv, bool& pipelineOk)
	{
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
	}

	// Sous-étape extraite de InitGraphicsAndScene — voir la documentation dans Engine.h.
	void Engine::InitAuthGlyphAndLogoPasses(const std::function<std::vector<uint32_t>(const char*)>& loadSpirv)
	{
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
	}

	// Sous-étape extraite de InitGraphicsAndScene — voir la documentation dans Engine.h.
	void Engine::InitSkinnedAvatarRenderer(const std::function<std::vector<uint32_t>(const char*)>& loadSpirv)
	{
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
					// FIX.10 — Perception du personnage (rayon des plaques).
					m_perceptionModel.Init(m_cfg);
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
	}

	// Sous-étape extraite de InitGraphicsAndScene — voir la documentation dans Engine.h.
	void Engine::InitPlayerSpawnPlacement()
	{
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
			// Monde « Royaumes » en cellules : hauteurs des cellules (CPU) et eau
			// de toutes les cellules générées, en coordonnées de rendu.
			if (m_cellWorld)
				InitCellWorldBoot();

			// Nage (v1) : eau-test procedurale. La zone demo est plate et sans
			// eau ; on pose un BASSIN DE TEST au-dessus du sol pour valider la
			// mecanique de nage (a remplacer par une vraie etendue d'eau via le
			// pipeline water.bin / level-design). Desactivable : world.test_water.enabled=false.
			// Nage : tenter d'abord une VRAIE eau (instances/water.bin via StreamCache) ;
			// repli sur l'eau-test procedurale si absente (zone demo plate, sans water.bin).
			if (m_cellWorld)
			{
				// Eau déjà posée par InitCellWorldBoot (cellules générées).
			}
			else if (auto realWater = m_streamCache.LoadWater(m_cfg, ""))
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
			// Monde en cellules : les interactibles viennent des cellules
			// (EnsureCellHeights), chargées plus bas.
			if (!m_cellWorld)
			{
				m_interactables.clear();
				AppendInteractables(m_cfg, 0.0, 0.0);
			}
			// Pas de cibles de test à l'origine dans le monde en cellules.
			if (m_interactables.empty() && !m_cellWorld)
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
			// Monde ouvert (chantier « Royaumes ») : décor instancié de la zone
			// (scatter.bin). Après le terrain (ancrage au sol) et le collisionneur.
			// Le catalogue de collision des pièces doit précéder le décor
			// instancié : ses murs/arches enregistrent leurs boîtes au chargement.
			LoadBuildingCollisionCatalog();
			if (m_cellWorld)
			{
				// Monde en cellules : première cellule générée chargée
				// synchronement (monde visible avant l'EnterWorld) ; le
				// streaming prend ensuite le relais autour du joueur.
				EnterCellWorldAt(m_cellBootGlobalX, m_cellBootGlobalZ);
			}
			else
			{
				LoadScatterWorld();
				// Bâtiments éditables (auberge…) depuis instances/.../buildings.bin,
				// rendus eux aussi en instancié (pièces partagées entre bâtiments).
				LoadBuildings();
			}
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
			// Monde en cellules : point d'arrivée du boot (repère de rendu).
			const float spawnX = m_cellWorld ? m_renderFrame.ToRenderX(m_cellBootGlobalX) : 0.0f;
			const float spawnZ = m_cellWorld ? m_renderFrame.ToRenderZ(m_cellBootGlobalZ) : 0.0f;
			const float spawnY = m_terrainCollider.GroundHeightAt(spawnX, spawnZ) + 0.9f;
			m_characterController.Init(engine::math::Vec3{ spawnX, spawnY, spawnZ });
			LOG_INFO(Render,
				"[Engine] B.1 CharacterController spawn=({:.2f}, {:.2f}, {:.2f}) capsule r={:.2f} h={:.2f}",
				spawnX, spawnY, spawnZ, ccCfg.capsule.radius, ccCfg.capsule.height);
		}
	}

	// Ancien corps de lambda du constructeur — voir la documentation dans Engine.h.
	void Engine::RecordPresentFramePass(VkCommandBuffer cmd, engine::render::Registry& reg)
	{
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
			const bool editorHubActive = HasEditorHubImGui() && m_editorEnabled && !m_worldEditorExe;
			if (m_imguiOverlay && m_imguiOverlay->IsReady()
				&& (m_worldEditorExe
					|| (m_authImGui && authVisualState.active
						&& m_cfg.GetBool("render.auth_ui.imgui.enabled", false))
					|| chatImguiActive
					|| editorHubActive)
				&& m_vkDeviceContext.SupportsDynamicRendering() && backbufferView != VK_NULL_HANDLE
				&& !presentSolidColorDebug)
			{
				worldEditorUiToPresent = m_imguiOverlay->RecordToBackbuffer(
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
	}
	// Ancien corps de lambda du constructeur — voir la documentation dans Engine.h.
	void Engine::RecordTaaFramePass(VkCommandBuffer cmd, engine::render::Registry& reg)
	{
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
	}
	// Ancien corps de lambda du constructeur — voir la documentation dans Engine.h.
	void Engine::RecordCloudsFramePass(VkCommandBuffer cmd, engine::render::Registry& reg)
	{
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
		// w = tuile (m) de la weather map 2D de couverture (chantier
		// weather map 2026-07-20) : fronts nuageux par position monde.
		// <= 1 = carte neutre (bascule de secours sans rebuild).
		pc.shadowParams[3] = static_cast<float>(m_cfg.GetDouble("render.clouds.weather_tile_m", 40000.0));

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
	}
	// Ancien corps de lambda du constructeur — voir la documentation dans Engine.h.
	void Engine::RecordVolumetricFogFramePass(VkCommandBuffer cmd, engine::render::Registry& reg)
	{
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
	}
	// Ancien corps de lambda du constructeur — voir la documentation dans Engine.h.
	void Engine::RecordWaterFramePass(VkCommandBuffer cmd, engine::render::Registry& reg)
	{
		const engine::world::water::WaterScene* scene = nullptr;
		bool editorWaterDirty = false; // seule la scène sert ici
		if (!(m_worldEditorExe && TryGetEditorWaterScene(scene, editorWaterDirty)) && m_clientWaterScene)
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

		// EAU.3 — l'eau reflète le vrai ciel (capturé + préfiltré,
		// suit l'heure et la météo) au lieu du cubemap uni de repli.
		if (m_pipeline && m_pipeline->GetSpecularPrefilterPass().IsValid())
			m_waterPass.SetSkyboxCube(m_pipeline->GetSpecularPrefilterPass().GetImageView(),
				m_pipeline->GetSpecularPrefilterPass().GetSampler());
		const uint32_t frameIdx = m_currentFrame % 2;
		m_waterPass.Record(m_vkDeviceContext.GetDevice(), cmd, reg, m_vkSwapchain.GetExtent(),
			m_fgSceneColorHDRId, m_fgDepthId, m_fgSceneColorHDRPostWaterId,
			m_waterMeshGpu, base, *scene, frameIdx);
	}
	// Ancien corps de lambda du constructeur — voir la documentation dans Engine.h.
	void Engine::RecordLightingFramePass(VkCommandBuffer cmd, engine::render::Registry& reg)
	{
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
		// v1.0.118 — zone d'ambiance locale (fond du gouffre, zone.json
		// world.ambiance_zones) : soleil presque éteint, ambiant réduit ;
		// les torches (point lights) éclairent alors le fond.
		if (rs.ambianceDarkness > 0.0f)
		{
			const float dk = std::clamp(rs.ambianceDarkness, 0.0f, 1.0f);
			const float sunK = 1.0f - 0.92f * dk;
			const float ambK = 1.0f - 0.75f * dk;
			for (int i = 0; i < 3; ++i)
			{
				lp.lightColor[i]   *= sunK;
				lp.ambientColor[i] *= ambK;
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
			// Chantier perspective aérienne 2026-07-20 — quand le ciel
			// analytique est actif (même clé que sky.frag), la teinte de
			// la perspective aérienne est évaluée par lighting.frag DANS
			// la direction du rayon (fond de ciel analytique) au lieu de
			// la couleur d'horizon legacy uniforme ci-dessus.
			lp.aerialSkyModel =
				m_cfg.GetBool("client.sky.analytic", true) ? 1.0f : 0.0f;
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
	}
	// Ancien corps de lambda du constructeur — voir la documentation dans Engine.h.
	void Engine::RecordGeometryFramePass(VkCommandBuffer cmd, engine::render::Registry& reg)
	{
		const uint32_t readIdx = m_renderReadIndex.load(std::memory_order_acquire);
		const engine::RenderState& rs = m_renderStates[readIdx];
		// Phase 0 (chantier C) : rendu exclusif. On ne dessine le terrain
		// heightmap legacy que si les chunks n'ont pas couvert la scène à la
		// frame précédente -> supprime le z-fighting du double terrain.
		// Monde « Royaumes » : terrains des cellules résidentes (le
		// premier efface le GBuffer) à la place du terrain unique.
		const bool terrainBeforeGeometry = engine::render::ShouldDrawLegacyTerrain(
			m_cellWorld ? HasCellTerrainToDraw() : m_terrain.IsValid(),
			m_pipeline->GetGeometryPass().HasLoadPass(),
			m_lastFrameChunkDrawCount);
		if (terrainBeforeGeometry && m_cellWorld)
		{
			(void)RecordCellTerrains(cmd, reg, rs);
		}
		else if (terrainBeforeGeometry)
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
		// Monde ouvert : décor + bâtiments instanciés (une passe, lots par mesh).
		RecordScatterGeometry(cmd, reg, rs);

		// TD.2 : dessine les avatars des joueurs distants (réplication AoI).
		RecordRemoteAvatars(cmd, reg, rs);
		// Coffre anime (ouverture/fermeture a l'interaction).
		RecordAnimatedChest(cmd, reg, rs);
		// v1.0.125 — bateaux des lignes maritimes.
		RecordBoats(cmd, reg, rs);

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
	}
} // namespace engine
