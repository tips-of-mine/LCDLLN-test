// Tests for StaticMeshLoader — CPU-only path against real prop .gltf fixtures.

#include "src/client/render/static_mesh/StaticMeshLoader.h"

#include <cstdio>
#include <string>

using engine::render::staticmesh::StaticMeshCpuData;
using engine::render::staticmesh::StaticMeshLoader;

namespace
{
    int g_failed = 0;
    #define REQUIRE(cond) do { \
        if (!(cond)) { std::fprintf(stderr, "[FAIL] %s:%d %s\n", __FILE__, __LINE__, #cond); ++g_failed; } \
    } while (0)

    // Un prop STATIQUE (sans armature) se charge : géométrie non vide, triangles valides.
    void Test_LoadStaticBarrel()
    {
        auto result = StaticMeshLoader::LoadCpuOnlyForTests("game/data/meshes/props/Barrel.gltf");
        REQUIRE(result.has_value());
        if (!result.has_value()) return;
        const StaticMeshCpuData& m = *result;
        REQUIRE(!m.vertices.empty());
        REQUIRE(!m.indices.empty());
        REQUIRE(m.indices.size() % 3 == 0);  // triangles
        REQUIRE(!m.submeshes.empty());
        // Toutes les plages de sous-maillage sont dans les bornes de l'index buffer.
        for (const auto& s : m.submeshes)
        {
            REQUIRE(s.indexCount % 3 == 0);
            REQUIRE(static_cast<size_t>(s.firstIndex) + s.indexCount <= m.indices.size());
        }
        // Tous les indices référencent un sommet existant.
        for (uint32_t idx : m.indices)
            REQUIRE(idx < m.vertices.size());
    }

    // Le coffre (riggé) se charge AUSSI par le loader statique (on ignore le skin) :
    // valide l'extraction des noms de matériaux trim + des URI de textures.
    void Test_LoadChestExtractsTrimMaterials()
    {
        auto result = StaticMeshLoader::LoadCpuOnlyForTests("game/data/meshes/props/Chest_Wood.gltf");
        REQUIRE(result.has_value());
        if (!result.has_value()) return;
        const StaticMeshCpuData& m = *result;
        REQUIRE(!m.submeshes.empty());
        bool foundNamedMaterial = false;
        bool foundBaseColorUri = false;
        for (const auto& s : m.submeshes)
        {
            if (!s.materialName.empty()) foundNamedMaterial = true;
            if (!s.baseColorUri.empty()) foundBaseColorUri = true;
        }
        REQUIRE(foundNamedMaterial);   // ex. "MI_Trim_Furniture" / "MI_Trim_Metal"
        REQUIRE(foundBaseColorUri);    // ex. "T_Trim_Furniture_BaseColor.png"
    }

    // Chemin inexistant -> std::nullopt (pas de crash).
    void Test_LoadMissingFile_ReturnsNullopt()
    {
        auto result = StaticMeshLoader::LoadCpuOnlyForTests("game/data/meshes/props/does_not_exist.gltf");
        REQUIRE(!result.has_value());
    }
}  // namespace

int main()
{
    Test_LoadStaticBarrel();
    Test_LoadChestExtractsTrimMaterials();
    Test_LoadMissingFile_ReturnsNullopt();
    return g_failed == 0 ? 0 : 1;
}
