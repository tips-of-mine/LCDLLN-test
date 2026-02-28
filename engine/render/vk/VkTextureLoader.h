#pragma once

/**
 * @file VkTextureLoader.h
 * @brief Load textures from content path (stb_image) + GPU upload + cache by path.
 *
 * Ticket: M03.3 — Materials: BaseColor/Normal/ORM + loader.
 */

#include <vulkan/vulkan.h>

#include <string>
#include <string_view>
#include <unordered_map>

namespace engine::render::vk {

/**
 * @brief Loads texture images from content-relative paths, uploads to GPU, caches by path.
 *
 * Paths resolved via Config paths.content. sRGB/linear flag per load.
 */
class VkTextureLoader {
public:
    VkTextureLoader() = default;

    ~VkTextureLoader();

    VkTextureLoader(const VkTextureLoader&) = delete;
    VkTextureLoader& operator=(const VkTextureLoader&) = delete;

    /**
     * @brief Initialises loader with device and upload queue/pool.
     *
     * @param physicalDevice Physical device.
     * @param device         Logical device.
     * @param queue          Queue for transfer submit.
     * @param commandPool    Command pool for one-time uploads.
     */
    [[nodiscard]] bool Init(VkPhysicalDevice physicalDevice,
                            VkDevice device,
                            VkQueue queue,
                            VkCommandPool commandPool);

    /**
     * @brief Loads texture from relative path; returns cached view if already loaded.
     *
     * @param relativePath Path relative to content root (e.g. "textures/brick_albedo.png").
     * @param useSrgb      true for BaseColor (sRGB), false for Normal/ORM (linear).
     * @return             VkImageView or VK_NULL_HANDLE on failure.
     */
    [[nodiscard]] VkImageView Load(std::string_view relativePath, bool useSrgb);

    /**
     * @brief Loads an HDR cubemap from 6 faces under a base path (M05.2).
     *
     * Loads basePath/posx.hdr, negx.hdr, posy.hdr, negy.hdr, posz.hdr, negz.hdr
     * via stb_image HDR. Creates a single VkImage with 6 layers (cube compatible)
     * and returns a cube image view. Paths relative to content root.
     *
     * @param basePath Base path relative to content (e.g. "env" for env/posx.hdr etc.).
     * @return         VkImageView (VK_IMAGE_VIEW_TYPE_CUBE) or VK_NULL_HANDLE on failure.
     */
    [[nodiscard]] VkImageView LoadCubemapHDR(std::string_view basePath);

    /**
     * @brief Creates a 1x1 per-face cubemap (for irradiance fallback when env not loaded). Cached as ":default_cube".
     *
     * @return VkImageView (VK_IMAGE_VIEW_TYPE_CUBE) or VK_NULL_HANDLE.
     */
    [[nodiscard]] VkImageView CreateDefaultCubemap();

    /**
     * @brief Creates a 1x1 pixel texture (for default/fallback). Not cached.
     *
     * @param r       Red (0-255).
     * @param g       Green (0-255).
     * @param b       Blue (0-255).
     * @param useSrgb true for sRGB format.
     * @return        VkImageView or VK_NULL_HANDLE. Caller must track and destroy (or store in cache).
     */
    [[nodiscard]] VkImageView CreateDefaultTexture(uint8_t r, uint8_t g, uint8_t b, bool useSrgb);

    /**
     * @brief Releases all loaded textures and shuts down.
     */
    void Shutdown();

private:
    struct LoadedTex {
        VkImage        image  = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView    view   = VK_NULL_HANDLE;
    };
    [[nodiscard]] VkImageView LoadInternal(const std::string& key, std::string_view relativePath, bool useSrgb);
    [[nodiscard]] VkImageView LoadCubemapHDRInternal(const std::string& key, std::string_view basePath);
    [[nodiscard]] VkImageView CreateDefaultCubemapInternal();
    [[nodiscard]] VkImageView Create1x1AndCache(uint8_t r, uint8_t g, uint8_t b, bool useSrgb);
    void DestroyTexture(LoadedTex& tex);

    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    VkDevice         m_device         = VK_NULL_HANDLE;
    VkQueue          m_queue          = VK_NULL_HANDLE;
    VkCommandPool    m_commandPool    = VK_NULL_HANDLE;
    std::unordered_map<std::string, LoadedTex> m_cache;
};

} // namespace engine::render::vk
