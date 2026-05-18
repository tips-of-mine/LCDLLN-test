#include "src/client/render/skinned/Skeleton.h"

#include <cstdio>

using engine::render::skinned::Bone;
using engine::render::skinned::Skeleton;

namespace
{
    int g_failed = 0;
    #define REQUIRE(cond) do { \
        if (!(cond)) { std::fprintf(stderr, "[FAIL] %s:%d %s\n", __FILE__, __LINE__, #cond); ++g_failed; } \
    } while (0)

    Skeleton MakeThreeBoneChain()
    {
        Skeleton s;
        s.bones.push_back(Bone{"root",       -1, engine::math::Mat4{}, engine::math::Mat4{}});
        s.bones.push_back(Bone{"upperArm",    0, engine::math::Mat4{}, engine::math::Mat4{}});
        s.bones.push_back(Bone{"foreArm",     1, engine::math::Mat4{}, engine::math::Mat4{}});
        return s;
    }

    void Test_EmptySkeleton_HasZeroBones()
    {
        Skeleton s;
        REQUIRE(s.bones.empty());
        REQUIRE(s.FindBoneIndex("anything") == -1);
    }

    void Test_FindBoneIndex_ReturnsCorrectIndex()
    {
        Skeleton s = MakeThreeBoneChain();
        REQUIRE(s.FindBoneIndex("root") == 0);
        REQUIRE(s.FindBoneIndex("upperArm") == 1);
        REQUIRE(s.FindBoneIndex("foreArm") == 2);
        REQUIRE(s.FindBoneIndex("unknown") == -1);
    }

    void Test_BonesAreOrderedParentBeforeChild()
    {
        Skeleton s = MakeThreeBoneChain();
        for (size_t i = 0; i < s.bones.size(); ++i) {
            REQUIRE(s.bones[i].parentIndex < static_cast<int>(i));
        }
    }
}

int main()
{
    Test_EmptySkeleton_HasZeroBones();
    Test_FindBoneIndex_ReturnsCorrectIndex();
    Test_BonesAreOrderedParentBeforeChild();
    return g_failed == 0 ? 0 : 1;
}
