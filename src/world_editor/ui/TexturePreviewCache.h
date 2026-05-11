#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
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
typedef unsigned long long ImTextureID;

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
    /// Le format .texr est defini dans src/client/render/AssetRegistry.cpp :
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

        /// Renvoie une ImTextureID utilisable par ImGui::Image() pour la
        /// vignette procedurale du layer (0=grass, 1=dirt, 2=rock, 3=snow).
        /// 1re demande : genere le bruit + cree image+view+descriptor ImGui.
        /// Suivantes : hit cache. Cle interne : "procedural:N".
        /// \return nullptr si Init non OK ou layer >= kSplatLayerCount.
        ImTextureID GetProceduralThumb(uint32_t layer);

        /// Renvoie l'ImTextureID d'une .texr content-relative (ex: "textures/sand.texr").
        /// 1re demande : decode (LoadTexrFile) -> resample 256x256 -> upload GPU
        /// (CreateEntry). Suivantes : hit cache. Cache negatif (decode failed) =
        /// nullptr renvoye jusqu'a Invalidate.
        /// \param contentRelPath Chemin relatif au content dir (configure via Init).
        /// \return nullptr si Init non OK, contentRelPath vide, fichier introuvable
        ///   ou corrompu.
        ImTextureID GetTexrThumb(const std::string& contentRelPath);

        /// Renvoie le buffer CPU 256x256 d'une key cachee (ou nullptr).
        /// Cles : "procedural:0".."procedural:3" pour les builtins,
        ///        "textures/<rel>" pour les .texr importees (Task 8).
        /// Utilise par Engine::ProcessSplatRefsDirty pour reuploader le splat
        /// array terrain.
        const std::vector<uint8_t>* GetCpuRgba256(const std::string& key) const;

        /// Marque une entree pour destruction differee. Le descriptor + ressources
        /// Vulkan seront detruits au prochain Tick(currentFrame >= invalidatedAt + framesInFlight).
        /// Reset aussi l'entree dans le cache negatif (permet de retenter le decode).
        /// Procedurales : invalider n'a pas de sens (octets fixes), mais aucun effet
        /// indesirable si appele.
        /// \param contentRelPath Chemin tel que passe a GetTexrThumb (ou
        ///   "procedural:N" pour les builtins).
        void Invalidate(const std::string& contentRelPath);

        /// A appeler chaque frame en main thread. Met a jour le compteur interne et
        /// detruit les entrees en pending depuis assez longtemps pour qu'aucune
        /// command buffer en vol ne les reference.
        /// \param currentFrameIndex Compteur de frame monotone (Engine::m_frameCounter).
        /// \param framesInFlight Nombre maximum de frames en vol simultanees (kMaxFramesInFlight).
        void Tick(uint64_t currentFrameIndex, uint32_t framesInFlight);

    private:
        bool             m_ready        = false;
        VkDevice         m_device       = nullptr;
        VkPhysicalDevice m_physDev      = nullptr;
        VkQueue          m_queue        = nullptr;
        uint32_t         m_queueFamily  = 0;
        std::string      m_contentDir;

        VkSampler        m_sampler = nullptr;
        // TODO M-XX : m_pool cree par Init mais non utilise pour l'instant
        // (ImGui_ImplVulkan_AddTexture alloue depuis son propre pool interne).
        // A repurposer ou supprimer quand le besoin sera clarifie.
        VkDescriptorPool m_pool    = nullptr;

        /// Une entree GPU cachee : buffer CPU + ressources Vulkan + descriptor ImGui.
        struct GpuPreview
        {
            std::vector<uint8_t> cpuRgba256;
            VkImage              image   = nullptr;
            VkImageView          view    = nullptr;
            VkDeviceMemory       memory  = nullptr;
            VkDescriptorSet      imguiDS = nullptr;
        };

        /// key -> preview. Cles cf. doc de GetCpuRgba256.
        std::unordered_map<std::string, GpuPreview> m_entries;

        /// Cles dont le decode a echoue : ne pas retenter avant Invalidate
        /// (evite spam logs). Reset par Invalidate (Task 9).
        std::unordered_set<std::string> m_negativeCache;

        /// Entree marquee Invalidate, en attente de destruction.
        struct PendingDelete
        {
            GpuPreview preview;
            uint64_t   frameIndex = 0; // frame ou Invalidate a ete appele
        };
        std::vector<PendingDelete> m_pendingDeletes;

        /// Frame du dernier Tick (sert d'horloge a Invalidate quand Tick n'a
        /// pas encore ete appele de la frame courante).
        uint64_t m_lastTickFrame = 0;

        /// Cle procedurale standardisee : "procedural:0".."procedural:3".
        static std::string ProceduralKey(uint32_t layer);

        /// Cree image+view+descriptor ImGui a partir d'un buffer 256x256 RGBA8.
        /// Stocke dans m_entries[key]. Renvoie l'ImTextureID ou nullptr sur echec.
        /// Win32 uniquement.
        ImTextureID CreateEntry(const std::string& key,
                                const std::vector<uint8_t>& rgba256);

        /// Detruit les ressources Vulkan + ImGui d'une entree (helper interne, Win32 only).
        void DestroyEntry(GpuPreview& p);
    };

} // namespace engine::editor
