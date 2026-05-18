#include "src/client/render/skinned/AnimationSampler.h"
#include "src/client/render/skinned/AnimationClip.h"
#include "src/client/render/skinned/Skeleton.h"

#include <cmath>
#include <cstdio>

using engine::math::Mat4;
using engine::math::Quat;
using engine::math::Vec3;
using engine::render::skinned::AnimationClip;
using engine::render::skinned::AnimationSampler;
using engine::render::skinned::Bone;
using engine::render::skinned::BoneTracks;
using engine::render::skinned::Skeleton;

namespace
{
    int g_failed = 0;
    #define REQUIRE(cond) do { \
        if (!(cond)) { std::fprintf(stderr, "[FAIL] %s:%d %s\n", __FILE__, __LINE__, #cond); ++g_failed; } \
    } while (0)

    bool Approx(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }

    Skeleton MakeOneBoneSkel()
    {
        Skeleton s;
        s.bones.push_back(Bone{"root", -1, Mat4{}, Mat4{}});
        return s;
    }

    AnimationClip MakeRotateY90Clip()
    {
        AnimationClip clip;
        clip.name = "wave";
        clip.duration = 1.0f;
        clip.tracks.resize(1);
        clip.tracks[0].rotation = {
            {0.0f, Quat::Identity()},
            {1.0f, Quat::FromAxisAngle(Vec3{0,1,0}, 1.5707963f)}
        };
        return clip;
    }

    void Test_SamplePose_AtMidTime_HalfRotation()
    {
        Skeleton skel = MakeOneBoneSkel();
        AnimationClip clip = MakeRotateY90Clip();
        std::vector<Mat4> locals = AnimationSampler::SamplePose(skel, clip, 0.5f);
        REQUIRE(locals.size() == 1);
        // 45 degrees rotation around Y -> cos(45)=sin(45)=0.7071 on diagonal of rotation
        REQUIRE(Approx(locals[0].m[0],  0.70710677f, 1e-3f)); // column 0, x component
        REQUIRE(Approx(locals[0].m[10], 0.70710677f, 1e-3f)); // column 2, z component
    }

    void Test_ComputeGlobalMatrices_TwoBoneChain_AppliesParent()
    {
        Skeleton skel;
        skel.bones.push_back(Bone{"root", -1, Mat4{}, Mat4{}});
        skel.bones.push_back(Bone{"child", 0, Mat4{}, Mat4{}});

        // root local = translate (10, 0, 0), child local = identity.
        Mat4 rootLocal;
        rootLocal.m[12] = 10.0f;
        Mat4 childLocal; // identity (Mat4 default ctor)

        std::vector<Mat4> locals = {rootLocal, childLocal};
        std::vector<Mat4> globals = AnimationSampler::ComputeGlobalMatrices(skel, locals);

        REQUIRE(globals.size() == 2);
        // root global == root local (parent == -1)
        REQUIRE(Approx(globals[0].m[12], 10.0f));
        // child global == root global * child local == translate(10,0,0) * identity == translate(10,0,0)
        REQUIRE(Approx(globals[1].m[12], 10.0f));
    }

    void Test_ComputeFinalMatrices_BindPose_GivesIdentity()
    {
        // Property: if the bones are sampled as their bindLocal AND inverseBindGlobal is correctly set,
        // the final matrices should all be identity (i.e., the mesh is in bind pose, undeformed).
        Skeleton skel;
        Mat4 rootBindLocal;
        rootBindLocal.m[12] = 5.0f; // translate(5,0,0)
        Mat4 rootInvBindGlobal;
        rootInvBindGlobal.m[12] = -5.0f; // inverse of translate(5,0,0) is translate(-5,0,0)
        skel.bones.push_back(Bone{"root", -1, rootBindLocal, rootInvBindGlobal});

        std::vector<Mat4> locals = {rootBindLocal};
        std::vector<Mat4> globals = AnimationSampler::ComputeGlobalMatrices(skel, locals);
        std::vector<Mat4> finals = AnimationSampler::ComputeFinalMatrices(skel, globals);

        REQUIRE(finals.size() == 1);
        // Diagonal stays identity (no rotation)
        REQUIRE(Approx(finals[0].m[0], 1.0f));
        REQUIRE(Approx(finals[0].m[5], 1.0f));
        REQUIRE(Approx(finals[0].m[10], 1.0f));
        // Translation cancels out: globals[0] = translate(5,0,0), finals[0] = translate(5,0,0) * translate(-5,0,0) = identity
        REQUIRE(Approx(finals[0].m[12], 0.0f));
    }
}

int main()
{
    Test_SamplePose_AtMidTime_HalfRotation();
    Test_ComputeGlobalMatrices_TwoBoneChain_AppliesParent();
    Test_ComputeFinalMatrices_BindPose_GivesIdentity();
    return g_failed == 0 ? 0 : 1;
}
