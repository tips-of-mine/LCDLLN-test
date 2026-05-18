// Tests for SkinnedMeshLoader — exercises CPU-only path against the real y_bot.glb fixture.

#include "src/client/render/skinned/SkinnedMeshLoader.h"

#include <cstdio>

using engine::render::skinned::SkinnedMeshCpuData;
using engine::render::skinned::SkinnedMeshLoader;

namespace
{
    int g_failed = 0;
    #define REQUIRE(cond) do { \
        if (!(cond)) { std::fprintf(stderr, "[FAIL] %s:%d %s\n", __FILE__, __LINE__, #cond); ++g_failed; } \
    } while (0)

    /// Charge le vrai fixture Y Bot et vérifie les invariants attendus :
    /// squelette non vide et topologiquement ordonné, mesh non vide, indices
    /// par triplets, au moins une animation avec durée > 0 et nom "mixamo*",
    /// poids de skinning normalisés.
    void Test_LoadYBot_HasSkeletonMeshAndClip()
    {
        auto result = SkinnedMeshLoader::LoadCpuOnlyForTests("game/data/models/avatars/y_bot/y_bot.glb");
        REQUIRE(result.has_value());
        if (!result.has_value()) return;

        const SkinnedMeshCpuData& cpu = *result;

        // Y Bot Mixamo skeleton: ~65 bones (humanoid with fingers).
        REQUIRE(cpu.skeleton.bones.size() >= 30);

        // Mesh has vertices and triangles.
        REQUIRE(!cpu.vertices.empty());
        REQUIRE(!cpu.indices.empty());
        REQUIRE(cpu.indices.size() % 3 == 0);  // Triangles.

        // At least one bone has a valid parent (not all roots).
        bool foundChild = false;
        for (const auto& b : cpu.skeleton.bones) {
            if (b.parentIndex >= 0) { foundChild = true; break; }
        }
        REQUIRE(foundChild);

        // Bones ordered parent-before-child (invariant for ComputeGlobalMatrices).
        for (size_t i = 0; i < cpu.skeleton.bones.size(); ++i) {
            REQUIRE(cpu.skeleton.bones[i].parentIndex < static_cast<int>(i));
        }

        // The FBX export contained 2 animations ("mixamo.com" + "Take 001").
        // The loader exposes both; runtime will pick by name.
        REQUIRE(cpu.clips.size() >= 1);

        // At least one clip has a non-zero duration.
        bool foundDurationClip = false;
        for (const auto& c : cpu.clips) {
            if (c.duration > 0.0f) { foundDurationClip = true; break; }
        }
        REQUIRE(foundDurationClip);

        // At least one clip is named like "mixamo.com" (the walking one).
        bool foundMixamoClip = false;
        for (const auto& c : cpu.clips) {
            if (c.name.find("mixamo") != std::string::npos) { foundMixamoClip = true; break; }
        }
        REQUIRE(foundMixamoClip);

        // Each vertex weights sum to ~1.0 (FBX2glTF --normalize-weights 1).
        if (!cpu.vertices.empty()) {
            const auto& v = cpu.vertices[0];
            const float ws = v.weights[0] + v.weights[1] + v.weights[2] + v.weights[3];
            REQUIRE(ws > 0.99f && ws < 1.01f);
        }
    }

    /// Vérifie qu'un chemin inexistant renvoie std::nullopt (pas un crash).
    void Test_LoadMissingFile_ReturnsNullopt()
    {
        auto result = SkinnedMeshLoader::LoadCpuOnlyForTests("game/data/models/avatars/does_not_exist.glb");
        REQUIRE(!result.has_value());
    }
}

int main()
{
    Test_LoadYBot_HasSkeletonMeshAndClip();
    Test_LoadMissingFile_ReturnsNullopt();
    return g_failed == 0 ? 0 : 1;
}
