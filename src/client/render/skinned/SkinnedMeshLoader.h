#pragma once

#include "src/client/render/skinned/AnimationClip.h"
#include "src/client/render/skinned/Skeleton.h"

#include <vulkan/vulkan_core.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace engine::render::skinned
{

/// Sommet du mesh skinné tel qu'utilisé par le pipeline de skinning.
///
/// Layout strict (stride = 56 octets) — doit rester aligné avec :
///   - le VkVertexInputBindingDescription et les VkVertexInputAttributeDescription
///     que Task 11 ajoutera (SkinnedMesh.cpp / SkinnedPipeline).
///   - les inputs du vertex shader (location 0..4).
///
/// Champs :
///   - pos[3]          : position en espace local du mesh (mètres).
///   - normal[3]       : normale en espace local (déjà normalisée à l'export).
///   - uv[2]           : coordonnées UV0 (glTF TEXCOORD_0).
///   - boneIndices[4]  : 4 indices de bones (uint16) dans Skeleton::bones.
///                        glTF stocke JOINTS_0 comme u8 ou u16 ; on lit en uint
///                        puis on caste — Mixamo a < 256 bones donc safe.
///   - weights[4]      : 4 poids normalisés (somme = 1, FBX2glTF --normalize-weights 1).
struct SkinnedVertex
{
    float pos[3];
    float normal[3];
    float uv[2];
    uint16_t boneIndices[4];
    float weights[4];
};
// Stride = 56 octets : pos(12) + normal(12) + uv(8) + boneIndices(8) + weights(16).
static_assert(sizeof(SkinnedVertex) == 56, "SkinnedVertex stride must be 56");

/// Données CPU d'un mesh skinné chargé depuis un .glb. Pas de ressource GPU
/// (buffers Vulkan) ici — c'est la responsabilité de Task 11 (SkinnedMesh).
///
/// Composé de :
///   - skeleton : hiérarchie de bones + bind pose + inverse-bind matrices.
///   - vertices : tableau plat de SkinnedVertex (taille = nombre de sommets).
///   - indices  : indices triangulaires uint32 (taille = nombre de triangles * 3).
///   - clips    : toutes les animations exposées par le .glb (vide possible).
struct SkinnedMeshCpuData
{
    Skeleton skeleton;
    std::vector<SkinnedVertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<AnimationClip> clips;
};

/// Forward declaration : SkinnedMesh est défini dans SkinnedMesh.h qui inclut
/// CE fichier. On ne peut donc PAS inclure SkinnedMesh.h ici (cycle), d'où la
/// déclaration anticipée — le .cpp qui implémente Load() inclura le header
/// complet pour pouvoir construire/uploader la struct.
struct SkinnedMesh;

/// Chargeur de mesh skinné via cgltf.
///
/// Deux entrées :
///   - LoadCpuOnlyForTests : parse seulement (utilisé par les tests unitaires
///     qui n'ont pas accès à un VkDevice).
///   - Load                : parse + upload GPU en un seul appel (chemin de
///     production utilisé par le runtime client).
class SkinnedMeshLoader
{
public:
    /// Charge un fichier .glb (ou .gltf + binaires) entièrement côté CPU.
    ///
    /// \param path Chemin vers le .glb. Résolu relativement au cwd du process
    ///             (les tests CTest tournent avec WORKING_DIRECTORY = CMAKE_SOURCE_DIR,
    ///              donc un chemin "game/data/..." marche).
    /// \return SkinnedMeshCpuData rempli, ou std::nullopt si :
    ///         - le fichier n'existe pas / parse cgltf échoue,
    ///         - aucun skin n'est présent dans le glTF,
    ///         - aucun primitive du mesh n'a les attributs POSITION + JOINTS_0 + WEIGHTS_0.
    /// Émet un spdlog::warn dans chacun de ces cas pour faciliter le diagnostic.
    static std::optional<SkinnedMeshCpuData> LoadCpuOnlyForTests(const std::string& path);

    /// Chargement complet : parse le glTF puis uploade les buffers sur le GPU.
    ///
    /// \param device         Logical device Vulkan valide (utilisé pour les
    ///                       allocations VkBuffer / VkDeviceMemory).
    /// \param physicalDevice Physical device associé (pour query memory types).
    /// \param path           Chemin vers le .glb (voir LoadCpuOnlyForTests).
    /// \return SkinnedMesh prêt à dessiner, ou std::nullopt si le parse OU
    ///         l'upload GPU échoue. L'appelant prend la propriété du mesh
    ///         retourné et doit appeler Destroy(device) avant de détruire le
    ///         device.
    /// Effet de bord : alloue 2 VkBuffer + 2 VkDeviceMemory en cas de succès.
    static std::optional<SkinnedMesh> Load(VkDevice device, VkPhysicalDevice physicalDevice,
                                            const std::string& path);
};

}  // namespace engine::render::skinned
