// Tests for SkinnedMeshLoader — exercises CPU-only path against the real y_bot.glb fixture.

#include "src/client/render/skinned/SkinnedMeshLoader.h"

#include <cstdint>
#include <cstdio>
#include <string>

using engine::render::skinned::AnimationClip;
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

    /// CHAR-MODEL.25 / migration UE5 — charge le corps modulaire UE5
    /// (Male_Ranger : 9 meshes / 10 primitives) et vérifie que TOUS les
    /// sous-meshes sont fusionnés (pas seulement le premier), avec indices
    /// rebasés et indices d'os remappés sur le squelette canonique.
    void Test_LoadModularUe5Body_MergesAllSubmeshes()
    {
        auto result = SkinnedMeshLoader::LoadCpuOnlyForTests(
            "game/data/models/characters/humains/Male_Ranger/Male_Ranger.glb");
        REQUIRE(result.has_value());
        if (!result.has_value()) return;
        const SkinnedMeshCpuData& cpu = *result;

        // Squelette UE5 (root, pelvis, spine_01.., thigh_l.., doigts) = 65 os.
        REQUIRE(cpu.skeleton.bones.size() == 65);

        // Fusion des 10 primitives -> ~24873 sommets / ~80946 indices. L'ancien
        // loader (1er sous-mesh seulement) en aurait chargé une petite fraction.
        REQUIRE(cpu.vertices.size() > 20000);
        REQUIRE(cpu.indices.size() > 60000);
        REQUIRE(cpu.indices.size() % 3 == 0);

        // Indices rebasés : tous pointent dans le buffer fusionné.
        for (uint32_t idx : cpu.indices) {
            REQUIRE(idx < cpu.vertices.size());
        }
        // Indices d'os remappés : tous dans les bornes du squelette.
        const uint16_t nbones = static_cast<uint16_t>(cpu.skeleton.bones.size());
        bool boneOob = false;
        for (const auto& v : cpu.vertices) {
            for (int k = 0; k < 4; ++k) {
                if (v.boneIndices[k] >= nbones) { boneOob = true; break; }
            }
            if (boneOob) break;
        }
        REQUIRE(!boneOob);
    }

    /// Migration UE5 — la library d'animation UE5 (45 takes) doit se retargeter
    /// sur le squelette du corps UE5 (mêmes noms d'os) : tous les clips reviennent
    /// et animent réellement des os du corps.
    void Test_RetargetUalLibraryOntoUe5Body()
    {
        auto body = SkinnedMeshLoader::LoadCpuOnlyForTests(
            "game/data/models/characters/humains/Male_Ranger/Male_Ranger.glb");
        REQUIRE(body.has_value());
        if (!body.has_value()) return;

        auto clips = SkinnedMeshLoader::LoadClipsAnimOnly(
            "game/data/models/animations/humanoid_base/Humanoid_Base_Standard/"
            "Humanoid_Base_Standard.glb",
            body->skeleton);

        // 45 takes dans la library ; le retarget par nom d'os doit tous les ramener.
        REQUIRE(clips.size() >= 40);

        const int pelvisIdx = body->skeleton.FindBoneIndex("pelvis");
        REQUIRE(pelvisIdx >= 0);

        const AnimationClip* idle = nullptr;
        for (const auto& c : clips) {
            if (c.name.find("Idle_Loop") != std::string::npos) { idle = &c; break; }
        }
        REQUIRE(idle != nullptr);
        if (idle && pelvisIdx >= 0) {
            REQUIRE(idle->duration > 0.0f);
            REQUIRE(static_cast<size_t>(pelvisIdx) < idle->tracks.size());
            const auto& trk = idle->tracks[pelvisIdx];
            REQUIRE(!trk.rotation.empty() || !trk.translation.empty());
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
    Test_LoadModularUe5Body_MergesAllSubmeshes();
    Test_RetargetUalLibraryOntoUe5Body();
    Test_LoadMissingFile_ReturnsNullopt();
    return g_failed == 0 ? 0 : 1;
}
