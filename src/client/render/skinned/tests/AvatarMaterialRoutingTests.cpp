// Tests for BuildSubmeshMaterialIndices — pure logic, no Vulkan.

#include "src/client/render/skinned/AvatarMaterialRouting.h"

#include <cstdio>
#include <string>
#include <vector>

using engine::render::skinned::BuildSubmeshMaterialIndices;
using engine::render::skinned::SkinnedSubMesh;

namespace
{
    int g_failed = 0;
    #define REQUIRE(cond) do { \
        if (!(cond)) { std::fprintf(stderr, "[FAIL] %s:%d %s\n", __FILE__, __LINE__, #cond); ++g_failed; } \
    } while (0)

    SkinnedSubMesh Sub(const char* name)
    {
        SkinnedSubMesh s;
        s.firstIndex = 0;
        s.indexCount = 3;
        s.materialName = name;
        return s;
    }

    // Le sous-maillage peau reçoit l'id peau, l'habit reçoit l'id habit.
    void Test_BodyNameMatched_GetsBodyId()
    {
        std::vector<SkinnedSubMesh> subs = { Sub("MI_Ranger"), Sub("MI_Regular_Male") };
        auto out = BuildSubmeshMaterialIndices(subs, { "MI_Regular_Male", "MI_Regular_Female" }, 42u, 7u);
        REQUIRE(out.size() == 2);
        REQUIRE(out[0] == 7u);   // habit
        REQUIRE(out[1] == 42u);  // peau
    }

    // Aucun nom ne matche -> tout habit (cas du bug #2 si les noms ne matchent pas).
    void Test_NoBodyMatch_AllOutfit()
    {
        std::vector<SkinnedSubMesh> subs = { Sub("Alpha_Body"), Sub("Alpha_Joints") };
        auto out = BuildSubmeshMaterialIndices(subs, { "MI_Regular_Male" }, 42u, 7u);
        REQUIRE(out.size() == 2);
        REQUIRE(out[0] == 7u);
        REQUIRE(out[1] == 7u);
    }

    // bodyMaterialId == 0 -> vide (l'appelant fera un mono-draw habit).
    void Test_BodyIdZero_ReturnsEmpty()
    {
        std::vector<SkinnedSubMesh> subs = { Sub("MI_Regular_Male") };
        auto out = BuildSubmeshMaterialIndices(subs, { "MI_Regular_Male" }, 0u, 7u);
        REQUIRE(out.empty());
    }

    // submeshes vide -> vide.
    void Test_EmptySubmeshes_ReturnsEmpty()
    {
        std::vector<SkinnedSubMesh> empty;
        auto out = BuildSubmeshMaterialIndices(empty, { "MI_Regular_Male" }, 42u, 7u);
        REQUIRE(out.empty());
    }

    // Espaces parasites autour du nom -> matche quand même.
    void Test_TrimWhitespace_StillMatches()
    {
        std::vector<SkinnedSubMesh> subs = { Sub("  MI_Regular_Female  ") };
        auto out = BuildSubmeshMaterialIndices(subs, { "MI_Regular_Female" }, 42u, 7u);
        REQUIRE(out.size() == 1);
        REQUIRE(out[0] == 42u);
    }
}  // namespace

int main()
{
    Test_BodyNameMatched_GetsBodyId();
    Test_NoBodyMatch_AllOutfit();
    Test_BodyIdZero_ReturnsEmpty();
    Test_EmptySubmeshes_ReturnsEmpty();
    Test_TrimWhitespace_StillMatches();
    return g_failed == 0 ? 0 : 1;
}
