#pragma once

#include "src/client/render/skinned/AnimationClip.h"
#include "src/client/render/skinned/Skeleton.h"
#include "src/client/render/skinned/SkinnedMeshLoader.h"

#include <vulkan/vulkan_core.h>

#include <cstdint>
#include <string>
#include <vector>

namespace engine::render::skinned
{

/// Mesh skinné prêt à dessiner : données CPU (squelette + clips) + buffers Vulkan
/// (vertex + index) appartenant à la struct (RAII via Destroy()).
///
/// Note : la struct utilise host-visible + coherent memory pour les buffers
/// (chemin simple, pas de staging buffer ni de transfer queue). Suffisant pour
/// le scope A (un seul avatar statique). Une future PR pourra basculer vers
/// device-local + staging si la perf devient un sujet.
struct SkinnedMesh
{
    /// Squelette extrait du glTF (hiérarchie + bind pose + inverse-bind matrices).
    Skeleton skeleton;
    /// Liste des clips d'animation chargés depuis le même glTF.
    std::vector<AnimationClip> clips;

    /// Transform d'import appliquée au model matrix de l'avatar (innermost) pour
    /// corriger échelle/orientation à l'import (ex. corps UE5 : Z-up + cm vs le
    /// moteur Y-up + m). Identité par défaut (meshes Mixamo inchangés). Réglée
    /// depuis races.json (importScale / importRotXDeg) au chargement.
    engine::math::Mat4 importTransform = engine::math::Mat4::Identity();

    /// Vertex buffer GPU (contient des SkinnedVertex, stride 56 octets).
    VkBuffer vertexBuffer = VK_NULL_HANDLE;
    /// Mémoire backing du vertex buffer (host-visible + coherent).
    VkDeviceMemory vertexMemory = VK_NULL_HANDLE;

    /// Index buffer GPU (uint32, primitive triangle list).
    VkBuffer indexBuffer = VK_NULL_HANDLE;
    /// Mémoire backing de l'index buffer (host-visible + coherent).
    VkDeviceMemory indexMemory = VK_NULL_HANDLE;

    /// Nombre d'indices dans indexBuffer (passé à vkCmdDrawIndexed).
    uint32_t indexCount = 0;

    /// Uploade les données CPU (vertices + indices) vers le GPU et copie
    /// skeleton + clips dans cette struct. Utilise host-visible + coherent
    /// memory : pas de staging buffer, pas de transfer queue requise.
    ///
    /// \param device         Logical device Vulkan valide.
    /// \param physicalDevice Physical device associé (pour query memory types).
    /// \param cpu            Données CPU à uploader (typiquement issues de
    ///                       SkinnedMeshLoader::LoadCpuOnlyForTests).
    /// \return true si tout a réussi, false en cas d'échec Vulkan (les
    ///         ressources partiellement allouées sont nettoyées).
    ///
    /// Effet de bord : alloue 2 VkBuffer + 2 VkDeviceMemory côté driver.
    /// L'appelant doit appeler Destroy() avant la destruction du device.
    bool Upload(VkDevice device, VkPhysicalDevice physicalDevice, const SkinnedMeshCpuData& cpu);

    /// Libère les ressources GPU (buffers + memory). Idempotent : appels
    /// multiples sans effet après le premier (les handles sont remis à
    /// VK_NULL_HANDLE).
    ///
    /// \param device Logical device qui a servi à allouer les ressources.
    /// Effet de bord : invalide vertexBuffer/Memory + indexBuffer/Memory.
    void Destroy(VkDevice device);

    /// Recherche linéaire d'un clip par son nom exact (case-sensitive).
    /// \return Pointeur vers le clip stocké dans `clips`, ou nullptr si absent.
    ///         Le pointeur est valide tant que `clips` n'est pas modifié.
    const AnimationClip* FindClip(const std::string& name) const;
};

}  // namespace engine::render::skinned
