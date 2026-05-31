#pragma once

// Chargeur de mesh glTF STATIQUE (sans squelette) pour les props (chantier B).
// Pendant non-skinné de SkinnedMeshLoader : 93/94 props n'ont pas d'armature et
// ne peuvent donc pas passer par SkinnedMeshLoader (qui exige JOINTS_0/WEIGHTS_0).
//
// Cette première étape (incrément 1) ne fait que la lecture CPU (cgltf) :
// géométrie + sous-maillages + chemins de textures par matériau. L'upload GPU et
// le rendu (via GeometryPass) viennent dans les incréments suivants.

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace engine::render::staticmesh
{

/// Sommet statique (position + normale + UV + couleur de sommet). Pas de bones/weights.
/// `color` = COLOR_0 glTF (RGBA, défaut blanc opaque). Utilisé comme albedo pour les
/// props « nature » colorés par vertex color (cf. MaterialFlags::VertexColorAlbedo).
/// Stride = 48 octets (12 + 12 + 8 + 16), aligné sur kMeshVertexStride côté GPU.
struct StaticVertex
{
    float pos[3]    = { 0.f, 0.f, 0.f };
    float normal[3] = { 0.f, 0.f, 1.f };
    float uv[2]     = { 0.f, 0.f };
    float color[4]  = { 1.f, 1.f, 1.f, 1.f };
};

/// Plage d'indices d'un sous-maillage + nom et textures du matériau glTF associé.
/// Les chemins de textures sont les `uri` glTF (relatifs au .gltf), vides si absents.
struct StaticSubMesh
{
    uint32_t    firstIndex = 0;   ///< Offset (en indices) du début de la plage.
    uint32_t    indexCount = 0;   ///< Nombre d'indices (multiple de 3).
    std::string materialName;     ///< Nom du matériau glTF (ex. "MI_Trim_Metal"). Vide si aucun.
    std::string baseColorUri;     ///< URI baseColor (sRGB). Vide si absent.
    std::string normalUri;        ///< URI normal map (linéaire). Vide si absent.
    std::string ormUri;           ///< URI ORM / metallic-roughness (linéaire). Vide si absent.
};

/// Données CPU d'un mesh statique : sommets + indices triangulaires + sous-maillages.
/// Toutes les primitives de tous les meshes du fichier sont fusionnées (un seul
/// vertex/index buffer), comme SkinnedMeshLoader.
struct StaticMeshCpuData
{
    std::vector<StaticVertex> vertices;
    std::vector<uint32_t>     indices;
    std::vector<StaticSubMesh> submeshes;
    float localMinY = 0.0f;  ///< Y min des sommets (espace local du mesh).
    float localMaxY = 0.0f;  ///< Y max des sommets (espace local du mesh).
};

class StaticMeshLoader
{
public:
    /// Parse un .gltf/.glb statique via cgltf et renvoie ses données CPU.
    /// \return std::nullopt si le fichier est introuvable/illisible, ou s'il ne
    ///         contient aucune primitive avec attribut POSITION.
    /// N'alloue aucune ressource GPU (lecture CPU pure ; testable hors device).
    static std::optional<StaticMeshCpuData> LoadCpuOnlyForTests(const std::string& path);
};

}  // namespace engine::render::staticmesh
