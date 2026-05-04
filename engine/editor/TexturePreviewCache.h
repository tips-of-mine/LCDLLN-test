#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Forward decl Vulkan + ImGui (evite #include vulkan.h dans le .h pour
// reduire le compile time des consommateurs).
struct VkDevice_T;         typedef VkDevice_T*         VkDevice;
struct VkPhysicalDevice_T; typedef VkPhysicalDevice_T* VkPhysicalDevice;
struct VkQueue_T;          typedef VkQueue_T*          VkQueue;
struct VkSampler_T;        typedef VkSampler_T*        VkSampler;
struct VkDescriptorPool_T; typedef VkDescriptorPool_T* VkDescriptorPool;
struct VkImage_T;          typedef VkImage_T*          VkImage;
struct VkImageView_T;      typedef VkImageView_T*      VkImageView;
struct VkDeviceMemory_T;   typedef VkDeviceMemory_T*   VkDeviceMemory;
struct VkDescriptorSet_T;  typedef VkDescriptorSet_T*  VkDescriptorSet;
typedef void* ImTextureID;

namespace engine::editor
{
    /// Resample un buffer RGBA8 a une nouvelle resolution carree par box filter
    /// separable (passe horizontale + verticale). Si le source n'est pas carre,
    /// crop centre vers carre avant resample (pas de stretch, pas de letterbox).
    /// \param src Buffer source RGBA8, srcW * srcH * 4 octets.
    /// \param srcW Largeur source en pixels (>=1).
    /// \param srcH Hauteur source en pixels (>=1).
    /// \param dstSize Cote du carre de sortie en pixels (>=4, <=4096).
    /// \param outRgba Sortie : dstSize * dstSize * 4 octets.
    /// \return true si succes ; false sur params invalides.
    bool ResampleRgba8Box(const uint8_t* src, uint32_t srcW, uint32_t srcH,
                          uint32_t dstSize, std::vector<uint8_t>& outRgba);

    /// Decode un fichier .texr (magic TEXR + RGBA8) ou un PNG/JPG (via stb_image).
    /// Le format .texr est defini dans engine/render/AssetRegistry.cpp :
    ///   bytes 0..3 : magic 'TEXR' (0x52584554 LE)
    ///   bytes 4..7 : width (uint32 LE)
    ///   bytes 8..11: height (uint32 LE)
    ///   bytes 12..15: sRGB flag (uint32 LE, 0 = lineaire, !=0 = sRGB)
    ///   bytes 16.. : width*height*4 octets RGBA8
    ///
    /// \param absolutePath Chemin absolu sur disque (string UTF-8). Le caller
    ///   est responsable de resoudre les chemins content-relatifs en absolus
    ///   via Config::ResolveContentPath.
    /// \param outRgba Buffer de sortie (width * height * 4 octets RGBA8).
    /// \param outWidth Largeur du buffer decode.
    /// \param outHeight Hauteur du buffer decode.
    /// \return true si succes. false si fichier introuvable, magic invalide,
    ///   buffer trop petit, ou erreur stb_image. LOG_ERROR emis. outRgba/outWidth/outHeight remis a zero.
    bool LoadTexrFile(const std::string& absolutePath,
                      std::vector<uint8_t>& outRgba,
                      uint32_t& outWidth, uint32_t& outHeight);

    /// Cache lazy de textures decodes + uploadees a 256x256 RGBA8 pour rendu
    /// dans ImGui (vignettes editeur monde) et reupload du splat array terrain.
    /// Possede par Engine, vit le temps du device Vulkan.
    /// Win32 uniquement : sur autres plateformes, Init/Get* sont no-op et
    /// IsReady reste false.
    class TexturePreviewCache
    {
    public:
        TexturePreviewCache() = default;
        TexturePreviewCache(const TexturePreviewCache&) = delete;
        TexturePreviewCache& operator=(const TexturePreviewCache&) = delete;
        ~TexturePreviewCache();

        /// Initialise sampler + descriptor pool. A appeler apres l'init du
        /// device Vulkan et apres ImGui_ImplVulkan_Init.
        /// \param contentDir Repertoire absolu pointant vers <content>/ (pour
        ///   resoudre les chemins .texr content-relatifs lors des futurs
        ///   GetTexrThumb).
        /// \return true si succes. false sur autre plateforme que Win32 ou si
        ///   les handles Vulkan sont invalides.
        bool Init(VkDevice device, VkPhysicalDevice physDev,
                  VkQueue queue, uint32_t queueFamilyIndex,
                  const std::string& contentDir);

        /// Detruit toutes les ressources Vulkan possedees. Idempotent.
        void Shutdown();

        /// True si Init a reussi et Shutdown n'a pas ete appele.
        bool IsReady() const { return m_ready; }

    private:
        bool             m_ready        = false;
        VkDevice         m_device       = nullptr;
        VkPhysicalDevice m_physDev      = nullptr;
        VkQueue          m_queue        = nullptr;
        uint32_t         m_queueFamily  = 0;
        std::string      m_contentDir;

        VkSampler        m_sampler = nullptr;
        VkDescriptorPool m_pool    = nullptr;
    };

} // namespace engine::editor
