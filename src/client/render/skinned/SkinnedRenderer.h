#pragma once

#include "src/client/render/skinned/SkinnedMesh.h"
#include "src/shared/math/Math.h"

#include <vulkan/vulkan_core.h>

#include <cstdint>
#include <vector>

namespace engine::render::skinned
{

/// Pipeline Vulkan dédié au rendu d'un mesh skinné (un avatar humanoïde glTF).
///
/// Conçu comme un miroir de `engine::render::GeometryPass` (mêmes G-buffer
/// outputs A/B/C/Velocity + depth, même push-constant layout, même fragment
/// shader `gbuffer_geometry.frag.spv`), avec ces deltas spécifiques au skinning :
///
/// 1. Vertex input étendu : 2 bindings, 9 attributes — ajoute boneIdx@loc7
///    (uint16x4) et weights@loc8 (vec4) en plus des pos/normal/uv + 4 lignes
///    de la mat4 d'instance par-objet.
/// 2. frontFace = `VK_FRONT_FACE_COUNTER_CLOCKWISE` (la spec glTF impose le
///    winding CCW). NE PAS confondre avec GeometryPass qui utilise CW
///    intentionnellement pour son mesh placeholder CW. Cf.
///    `CLAUDE.md` section "Convention winding / face culling".
/// 3. 2 descriptor sets : set 0 = matériau (layout fourni par l'appelant,
///    identique à GeometryPass), set 1 = bone SSBO (créé ici par Init).
/// 4. Bone SSBO : 1 VkBuffer host-visible + coherent dimensionné pour
///    `maxBonesPerSkeleton * sizeof(mat4)` (~16 KB pour 256 bones). Rewritten
///    chaque Record (Task 14).
/// 5. Model instance buffer : 1 VkBuffer 64 octets (1 mat4 = matrice modèle
///    de l'avatar), réécrit chaque Record.
///
/// Le render pass utilise `loadOp = LOAD` (pas CLEAR) sur les 4 color
/// attachments et le depth — l'avatar est dessiné après le terrain qui a
/// déjà rempli le G-buffer.
///
/// Cycle de vie typique :
///   - Init(...)   à la création du renderer (1 fois par device).
///   - Record(...) chaque frame (Task 14).
///   - Destroy()   avant la destruction du device.
class SkinnedRenderer
{
public:
    SkinnedRenderer() = default;
    SkinnedRenderer(const SkinnedRenderer&) = delete;
    SkinnedRenderer& operator=(const SkinnedRenderer&) = delete;

    /// Initialise le pipeline skinné : render pass (loadOp=LOAD), pipeline
    /// layout (set 0 matériau + set 1 bone SSBO + push constants 144 octets),
    /// graphics pipeline, descriptor pool / set bone, et les 2 VkBuffer
    /// host-visible (bone SSBO + model instance).
    ///
    /// \param device                Logical device Vulkan valide.
    /// \param physicalDevice        Physical device associé (memory types).
    /// \param formatA               Format G-buffer attachment 0 (albedo).
    /// \param formatB               Format G-buffer attachment 1 (normal).
    /// \param formatC               Format G-buffer attachment 2 (ORM).
    /// \param formatVelocity        Format G-buffer attachment 3 (velocity).
    /// \param depthFormat           Format du depth attachment.
    /// \param vertSpirv             Pointeur vers le SPIR-V du vertex shader
    ///                              skinné (skinned.vert.spv). Doit rester
    ///                              valide jusqu'à Destroy() (mais en pratique
    ///                              copié dans VkShaderModule donc utilisé
    ///                              uniquement pendant l'Init).
    /// \param vertWordCount         Nombre de uint32_t dans vertSpirv.
    /// \param fragSpirv             Pointeur vers gbuffer_geometry.frag.spv —
    ///                              réutilisé tel quel (mêmes outputs G-buffer).
    /// \param fragWordCount         Nombre de uint32_t dans fragSpirv.
    /// \param materialLayout        Descriptor set layout du matériau (set 0).
    ///                              Doit être identique à celui de GeometryPass.
    /// \param maxBonesPerSkeleton   Nombre maximal de bones supporté (typiquement
    ///                              256 pour Mixamo). Dimensionne le bone SSBO.
    /// \return true si tout a réussi, false si une étape Vulkan a échoué
    ///         (le renderer reste invalide ; appeler Destroy() est inutile mais
    ///         safe).
    ///
    /// Effet de bord : alloue 1 VkRenderPass + 1 VkPipelineLayout + 1 VkPipeline
    /// + 1 VkDescriptorSetLayout + 1 VkDescriptorPool + 1 VkDescriptorSet
    /// + 2 VkBuffer + 2 VkDeviceMemory côté driver.
    bool Init(VkDevice device,
              VkPhysicalDevice physicalDevice,
              VkFormat formatA, VkFormat formatB, VkFormat formatC,
              VkFormat formatVelocity, VkFormat depthFormat,
              const uint32_t* vertSpirv, size_t vertWordCount,
              const uint32_t* fragSpirv, size_t fragWordCount,
              VkDescriptorSetLayout materialLayout,
              uint32_t maxBonesPerSkeleton);

    /// Enregistre la draw call pour un avatar skinné. Stub vide ici —
    /// l'implémentation arrive en Task 14.
    ///
    /// \param device              Logical device (pour map/unmap éventuels).
    /// \param cmd                 Command buffer en cours d'enregistrement.
    /// \param extent              Taille du viewport (largeur, hauteur).
    /// \param renderPass          Render pass actif (souvent
    ///                            `GetRenderPass()` mais peut être un autre
    ///                            render-pass compatible).
    /// \param framebuffer         Framebuffer compatible (4 color + depth).
    /// \param prevViewProj        Matrice view*proj de la frame précédente
    ///                            (motion vectors). 16 floats column-major.
    /// \param viewProj            Matrice view*proj de la frame courante.
    /// \param mesh                Mesh skinné à dessiner (vertex/index buffers).
    /// \param finalBoneMatrices   Tableau des matrices finales de skinning
    ///                            (`bind * inverseBind * worldPose`), une par
    ///                            bone. Uploadé dans le SSBO chaque Record.
    /// \param materialDescriptorSet Descriptor set matériau à binder (set 0).
    ///                            Bindless : un seul set partagé, l'index
    ///                            matériau sélectionne la slot dans le shader.
    /// \param modelMatrixColumnMajor4x4 Matrice modèle 4x4 column-major
    ///                            (transform monde de l'avatar). 16 floats.
    /// \param materialIndex       Index matériau du push constant utilisé pour
    ///                            le chemin mono-draw (mesh.submeshes vide OU
    ///                            submeshMaterialIndices de taille incohérente).
    /// \param submeshMaterialIndices Index matériau bindless par sous-maillage,
    ///                            parallèle à `mesh.submeshes`. S'il est non
    ///                            vide et de même taille que `mesh.submeshes`,
    ///                            chaque sous-maillage est dessiné dans son
    ///                            propre draw call avec son index (rendu
    ///                            multi-matériaux : habit vs peau). Sinon on
    ///                            retombe sur un unique draw avec `materialIndex`.
    /// \param skinMaterialIndex   Index matériau considéré « peau » (corps), ou
    ///                            0 = aucun. Les sous-maillages dont l'index
    ///                            matériau == celui-ci reçoivent un depth bias
    ///                            (skinDepthBias*) pour passer DERRIÈRE l'habit
    ///                            coplanaire et éviter le z-fighting/flicker
    ///                            (« parait double »). 0 → aucun biais.
    /// \param skinDepthBiasConstant Facteur constant passé à vkCmdSetDepthBias
    ///                            pour les sous-maillages peau (0 = pas de biais).
    /// \param skinDepthBiasSlope  Facteur de pente passé à vkCmdSetDepthBias
    ///                            pour les sous-maillages peau.
    void Record(VkDevice device, VkCommandBuffer cmd,
                VkExtent2D extent,
                VkRenderPass renderPass, VkFramebuffer framebuffer,
                const float* prevViewProj, const float* viewProj,
                const SkinnedMesh& mesh,
                const std::vector<engine::math::Mat4>& finalBoneMatrices,
                VkDescriptorSet materialDescriptorSet,
                const float* modelMatrixColumnMajor4x4,
                uint32_t materialIndex,
                const std::vector<uint32_t>& submeshMaterialIndices = {},
                uint32_t skinMaterialIndex = 0u,
                float skinDepthBiasConstant = 0.0f,
                float skinDepthBiasSlope = 0.0f);

    /// Libère toutes les ressources Vulkan dans l'ordre inverse de leur
    /// création. Idempotent (safe à appeler plusieurs fois, ou après un Init
    /// qui a échoué — chaque handle est checké contre VK_NULL_HANDLE).
    void Destroy(VkDevice device);

    /// \return true si le pipeline graphique principal est valide
    ///         (renderer prêt à enregistrer des draw calls).
    bool IsValid() const { return m_pipeline != VK_NULL_HANDLE; }

    /// \return Render pass interne (4 color G-buffer + depth, loadOp=LOAD).
    ///         Utilisé par l'appelant pour créer un framebuffer compatible.
    VkRenderPass GetRenderPass() const { return m_renderPass; }

    /// Nombre de copies des buffers réécrits chaque frame (bone SSBO + model
    /// instance + descriptor set bone). Doit être >= au nombre maximal de Record
    /// par frame × FIF + marge. Sans cette duplication, réécrire un buffer
    /// host-coherent pendant que la frame précédente le lit encore provoque une
    /// course → l'avatar tremble / « parait double » (deux poses se mélangent).
    ///
    /// TD.7 — bump 3 → 32 pour le rendu skinné des avatars distants. Avec FIF=2
    /// et jusqu'à 14 avatars/frame (1 local + 13 distants), 32 slots laissent
    /// une marge confortable avant qu'un slot soit réutilisé par-dessus une
    /// écriture encore en flight côté GPU. Coût : 32 × (~16 KB bone SSBO + 64 B
    /// model) ≈ 512 KB host-visible GPU. Acceptable pour un client.
    static constexpr uint32_t kFrameSlots = 32u;

private:
    VkDevice m_device = VK_NULL_HANDLE;
    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;

    VkRenderPass m_renderPass = VK_NULL_HANDLE;
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_pipeline = VK_NULL_HANDLE;

    VkDescriptorSetLayout m_boneSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
    /// Un descriptor set bone par slot, pointant chacun sur son m_boneSsbo[slot].
    VkDescriptorSet m_boneDescriptorSet[kFrameSlots] = {};

    /// Bone SSBO host-visible/coherent, un par slot (anti-course FIF).
    VkBuffer m_boneSsbo[kFrameSlots] = {};
    VkDeviceMemory m_boneSsboMemory[kFrameSlots] = {};

    /// VkBuffer 64 octets : mat4 modèle de l'avatar, réécrit chaque Record, un
    /// par slot (anti-course FIF).
    VkBuffer m_modelInstanceBuffer[kFrameSlots] = {};
    VkDeviceMemory m_modelInstanceMemory[kFrameSlots] = {};

    /// Slot courant, avancé d'un cran à chaque Record (ring sur kFrameSlots).
    uint32_t m_frameSlot = 0u;

    uint32_t m_maxBones = 0;
};

}  // namespace engine::render::skinned
