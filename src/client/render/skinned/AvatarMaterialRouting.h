#pragma once

#include "src/client/render/skinned/SkinnedMeshLoader.h"  // SkinnedSubMesh

#include <cstdint>
#include <string>
#include <vector>

namespace engine::render::skinned
{
/// Construit la table d'index de matériau par sous-maillage de l'avatar.
///
/// Chaque sous-maillage dont le `materialName` (après suppression des espaces de
/// début/fin) figure dans `bodyMaterialNames` reçoit `bodyMaterialId` (la PEAU) ;
/// tous les autres reçoivent `outfitMaterialId` (l'HABIT). Le matching est
/// sensible à la casse (noms glTF), insensible aux espaces parasites.
///
/// \param submeshes        Sous-maillages du mesh (parallèle au résultat).
/// \param bodyMaterialNames Noms de matériaux glTF considérés comme peau.
/// \param bodyMaterialId    Id matériau peau (selon le genre). 0 = aucun.
/// \param outfitMaterialId  Id matériau habit (défaut).
/// \return Vecteur parallèle à `submeshes`. **VIDE** si `bodyMaterialId == 0`
///         ou si `submeshes` est vide — l'appelant retombe alors sur le
///         mono-draw habit (comportement historique).
///
/// Fonction pure : aucune dépendance Vulkan, aucun effet de bord.
std::vector<uint32_t> BuildSubmeshMaterialIndices(
    const std::vector<SkinnedSubMesh>& submeshes,
    const std::vector<std::string>&    bodyMaterialNames,
    uint32_t                           bodyMaterialId,
    uint32_t                           outfitMaterialId);
}  // namespace engine::render::skinned
