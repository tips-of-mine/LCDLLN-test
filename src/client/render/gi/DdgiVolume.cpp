#include "src/client/render/gi/DdgiVolume.h"
#include "src/shared/core/Log.h"

#include <vulkan/vulkan.h>

namespace engine::render::gi
{
	// ---------------------------------------------------------------------------
	// Indexation des sondes (CPU pur — aucune dépendance Vulkan dans ces méthodes)
	// ---------------------------------------------------------------------------

	uint32_t DdgiVolume::ProbeCount() const
	{
		return m_config.counts[0] * m_config.counts[1] * m_config.counts[2];
	}

	uint32_t DdgiVolume::ProbeIndex(uint32_t ix, uint32_t iy, uint32_t iz) const
	{
		// X le plus rapide, puis Y, puis Z.
		return ix + iy * m_config.counts[0] + iz * m_config.counts[0] * m_config.counts[1];
	}

	void DdgiVolume::GridCoord(uint32_t index, uint32_t& ix, uint32_t& iy, uint32_t& iz) const
	{
		const uint32_t cx = m_config.counts[0];
		const uint32_t cy = m_config.counts[1];
		const uint32_t plane = cx * cy; // sondes par étage XY
		iz = (plane != 0u) ? (index / plane) : 0u;
		const uint32_t rem = (plane != 0u) ? (index % plane) : 0u;
		iy = (cx != 0u) ? (rem / cx) : 0u;
		ix = (cx != 0u) ? (rem % cx) : 0u;
	}

	void DdgiVolume::ProbeWorldPos(uint32_t ix, uint32_t iy, uint32_t iz, float& x, float& y, float& z) const
	{
		x = m_config.origin[0] + static_cast<float>(ix) * m_config.spacing[0];
		y = m_config.origin[1] + static_cast<float>(iy) * m_config.spacing[1];
		z = m_config.origin[2] + static_cast<float>(iz) * m_config.spacing[2];
	}

	// ---------------------------------------------------------------------------
	// Layout d'atlas (CPU pur) — cf. section PACKING D'ATLAS dans le header.
	// ---------------------------------------------------------------------------

	void DdgiVolume::ProbeAtlasTileOrigin(uint32_t probeIndex, uint32_t tileSize, uint32_t& px, uint32_t& py) const
	{
		uint32_t ix = 0u, iy = 0u, iz = 0u;
		GridCoord(probeIndex, ix, iy, iz);
		// Plan XZ déplié horizontalement : col = ix + iz * counts[0] ; row = iy.
		const uint32_t col = ix + iz * m_config.counts[0];
		const uint32_t row = iy;
		px = col * tileSize;
		py = row * tileSize;
	}

	// ---------------------------------------------------------------------------
	// Ressources GPU (gated — NON appelées par défaut)
	//
	// Coût VRAM estimé par zone (config par défaut 8x8x4 = 256 sondes) :
	//   - Irradiance : atlas (8*4)*(8+2) x (8)*(8+2) = 320 x 80 px, R16G16B16A16_SFLOAT
	//     (8 octets/px) => 320*80*8 ≈ 200 Kio.
	//   - Visibilité : atlas (8*4)*(16+2) x (8)*(16+2) = 576 x 144 px, R16G16_SFLOAT
	//     (4 octets/px) => 576*144*4 ≈ 324 Kio.
	//   Total ≈ 0,5 Mio par zone à la résolution par défaut. Croît linéairement avec
	//   ProbeCount et quadratiquement avec la résolution octaédrique par sonde.
	// ---------------------------------------------------------------------------

	bool DdgiVolume::CreateImage(VkDevice device, VkPhysicalDevice phys,
		uint32_t width, uint32_t height, VkFormat format,
		VkImage& outImage, VkDeviceMemory& outMemory, std::string& err) const
	{
		VkImageCreateInfo imageInfo{};
		imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		imageInfo.imageType = VK_IMAGE_TYPE_2D;
		imageInfo.format = format;
		imageInfo.extent = { width, height, 1u };
		imageInfo.mipLevels = 1u;
		imageInfo.arrayLayers = 1u;
		imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
		imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
		imageInfo.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
		imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

		if (vkCreateImage(device, &imageInfo, nullptr, &outImage) != VK_SUCCESS)
		{
			err = "vkCreateImage a echoue";
			return false;
		}

		VkMemoryRequirements memoryRequirements{};
		vkGetImageMemoryRequirements(device, outImage, &memoryRequirements);

		VkPhysicalDeviceMemoryProperties memoryProperties{};
		vkGetPhysicalDeviceMemoryProperties(phys, &memoryProperties);

		uint32_t memoryTypeIndex = UINT32_MAX;
		for (uint32_t i = 0; i < memoryProperties.memoryTypeCount; ++i)
		{
			if ((memoryRequirements.memoryTypeBits & (1u << i)) != 0
				&& (memoryProperties.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) != 0)
			{
				memoryTypeIndex = i;
				break;
			}
		}

		if (memoryTypeIndex == UINT32_MAX)
		{
			err = "aucun type de memoire device-local compatible";
			vkDestroyImage(device, outImage, nullptr);
			outImage = VK_NULL_HANDLE;
			return false;
		}

		VkMemoryAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		allocInfo.allocationSize = memoryRequirements.size;
		allocInfo.memoryTypeIndex = memoryTypeIndex;
		if (vkAllocateMemory(device, &allocInfo, nullptr, &outMemory) != VK_SUCCESS)
		{
			err = "vkAllocateMemory a echoue";
			vkDestroyImage(device, outImage, nullptr);
			outImage = VK_NULL_HANDLE;
			return false;
		}

		if (vkBindImageMemory(device, outImage, outMemory, 0) != VK_SUCCESS)
		{
			err = "vkBindImageMemory a echoue";
			vkFreeMemory(device, outMemory, nullptr);
			vkDestroyImage(device, outImage, nullptr);
			outMemory = VK_NULL_HANDLE;
			outImage = VK_NULL_HANDLE;
			return false;
		}

		return true;
	}

	VkImageView DdgiVolume::CreateView(VkDevice device, VkImage image, VkFormat format) const
	{
		VkImageViewCreateInfo viewInfo{};
		viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		viewInfo.image = image;
		viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
		viewInfo.format = format;
		viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		viewInfo.subresourceRange.baseMipLevel = 0u;
		viewInfo.subresourceRange.levelCount = 1u;
		viewInfo.subresourceRange.baseArrayLayer = 0u;
		viewInfo.subresourceRange.layerCount = 1u;

		VkImageView view = VK_NULL_HANDLE;
		if (vkCreateImageView(device, &viewInfo, nullptr, &view) != VK_SUCCESS)
			return VK_NULL_HANDLE;
		return view;
	}

	bool DdgiVolume::Allocate(VkDevice device, VkPhysicalDevice phys, void* /*vmaAllocator*/, std::string& err)
	{
		// vmaAllocator est volontairement ignoré : on miroite le pattern d'image
		// persistante de HiZPyramidPass (vkCreateImage + vkAllocateMemory device-local),
		// autonome et sans dépendance à l'allocateur VMA du moteur.
		if (device == VK_NULL_HANDLE || phys == VK_NULL_HANDLE)
		{
			err = "device ou physical device invalide";
			return false;
		}
		if (m_allocated)
		{
			err = "deja alloue";
			return false;
		}

		const uint32_t irrW = IrradianceAtlasWidth();
		const uint32_t irrH = IrradianceAtlasHeight();
		const uint32_t visW = VisibilityAtlasWidth();
		const uint32_t visH = VisibilityAtlasHeight();

		if (irrW == 0u || irrH == 0u || visW == 0u || visH == 0u)
		{
			err = "dimensions d'atlas nulles (config invalide)";
			return false;
		}

		if (!CreateImage(device, phys, irrW, irrH, VK_FORMAT_R16G16B16A16_SFLOAT,
			m_irradianceImage, m_irradianceMemory, err))
		{
			LOG_ERROR(Render, "[DdgiVolume] Allocation irradiance FAILED: {}", err);
			Destroy(device, nullptr);
			return false;
		}

		if (!CreateImage(device, phys, visW, visH, VK_FORMAT_R16G16_SFLOAT,
			m_visibilityImage, m_visibilityMemory, err))
		{
			LOG_ERROR(Render, "[DdgiVolume] Allocation visibilite FAILED: {}", err);
			Destroy(device, nullptr);
			return false;
		}

		m_irradianceView = CreateView(device, m_irradianceImage, VK_FORMAT_R16G16B16A16_SFLOAT);
		if (m_irradianceView == VK_NULL_HANDLE)
		{
			err = "creation vue irradiance a echoue";
			LOG_ERROR(Render, "[DdgiVolume] {}", err);
			Destroy(device, nullptr);
			return false;
		}

		m_visibilityView = CreateView(device, m_visibilityImage, VK_FORMAT_R16G16_SFLOAT);
		if (m_visibilityView == VK_NULL_HANDLE)
		{
			err = "creation vue visibilite a echoue";
			LOG_ERROR(Render, "[DdgiVolume] {}", err);
			Destroy(device, nullptr);
			return false;
		}

		m_allocated = true;

		// VRAM estimée (8 octets/px irradiance RGBA16F, 4 octets/px visibilité RG16F).
		const uint64_t irrBytes = static_cast<uint64_t>(irrW) * irrH * 8ull;
		const uint64_t visBytes = static_cast<uint64_t>(visW) * visH * 4ull;
		const double totalKib = static_cast<double>(irrBytes + visBytes) / 1024.0;
		LOG_INFO(Render,
			"[DdgiVolume] Alloue : {} sondes, irradiance {}x{} (RGBA16F), visibilite {}x{} (RG16F), VRAM estimee ~{:.1f} Kio",
			ProbeCount(), irrW, irrH, visW, visH, totalKib);
		return true;
	}

	void DdgiVolume::Destroy(VkDevice device, void* /*vmaAllocator*/)
	{
		if (device != VK_NULL_HANDLE)
		{
			if (m_irradianceView != VK_NULL_HANDLE)
				vkDestroyImageView(device, m_irradianceView, nullptr);
			if (m_visibilityView != VK_NULL_HANDLE)
				vkDestroyImageView(device, m_visibilityView, nullptr);
			if (m_irradianceImage != VK_NULL_HANDLE)
				vkDestroyImage(device, m_irradianceImage, nullptr);
			if (m_visibilityImage != VK_NULL_HANDLE)
				vkDestroyImage(device, m_visibilityImage, nullptr);
			if (m_irradianceMemory != VK_NULL_HANDLE)
				vkFreeMemory(device, m_irradianceMemory, nullptr);
			if (m_visibilityMemory != VK_NULL_HANDLE)
				vkFreeMemory(device, m_visibilityMemory, nullptr);
		}

		m_irradianceView = VK_NULL_HANDLE;
		m_visibilityView = VK_NULL_HANDLE;
		m_irradianceImage = VK_NULL_HANDLE;
		m_visibilityImage = VK_NULL_HANDLE;
		m_irradianceMemory = VK_NULL_HANDLE;
		m_visibilityMemory = VK_NULL_HANDLE;
		m_allocated = false;
	}
} // namespace engine::render::gi
