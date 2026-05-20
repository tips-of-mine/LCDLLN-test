#include "src/client/render/skinned/AnimationCrossfade.h"
#include "src/client/render/skinned/AnimationClip.h"
#include "src/client/render/skinned/Skeleton.h"

#include <cmath>
#include <cstdio>

using engine::math::Mat4;
using engine::math::Quat;
using engine::math::Vec3;
using engine::render::skinned::AnimationClip;
using engine::render::skinned::AnimationCrossfade;
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
        s.bones.push_back(Bone{"root", -1, Mat4::Identity(), Mat4::Identity()});
        return s;
    }

    AnimationClip MakeStaticTranslateClip(const std::string& name, float x)
    {
        AnimationClip c;
        c.name = name;
        c.duration = 1.0f;
        c.tracks.resize(1);
        c.tracks[0].translation = {{0.0f, Vec3{x, 0.0f, 0.0f}}, {1.0f, Vec3{x, 0.0f, 0.0f}}};
        return c;
    }

    void Test_NoCrossfade_PlaysCurrentClipOnly()
    {
        Skeleton skel = MakeOneBoneSkel();
        AnimationClip a = MakeStaticTranslateClip("A", 10.0f);
        AnimationCrossfade cf;
        cf.Play(a, /*loops=*/ true, /*now=*/ 0.0f);

        auto poses = cf.Sample(skel, /*now=*/ 0.5f);
        REQUIRE(poses.size() == 1);
        REQUIRE(Approx(poses[0].m[12], 10.0f));
    }

    void Test_Crossfade_AtAlphaZero_ReturnsOldPose()
    {
        Skeleton skel = MakeOneBoneSkel();
        AnimationClip a = MakeStaticTranslateClip("A", 10.0f);
        AnimationClip b = MakeStaticTranslateClip("B", 20.0f);
        AnimationCrossfade cf;
        cf.Play(a, true, 0.0f);
        cf.Play(b, true, 1.0f);

        auto poses = cf.Sample(skel, 1.0f);
        REQUIRE(Approx(poses[0].m[12], 10.0f));
    }

    void Test_Crossfade_AtAlphaOne_ReturnsNewPose()
    {
        Skeleton skel = MakeOneBoneSkel();
        AnimationClip a = MakeStaticTranslateClip("A", 10.0f);
        AnimationClip b = MakeStaticTranslateClip("B", 20.0f);
        AnimationCrossfade cf;
        cf.Play(a, true, 0.0f);
        cf.Play(b, true, 1.0f);

        auto poses = cf.Sample(skel, 1.0f + 0.15f + 0.01f);
        REQUIRE(Approx(poses[0].m[12], 20.0f));
    }

    void Test_Crossfade_AtAlphaHalf_Midway()
    {
        Skeleton skel = MakeOneBoneSkel();
        AnimationClip a = MakeStaticTranslateClip("A", 10.0f);
        AnimationClip b = MakeStaticTranslateClip("B", 20.0f);
        AnimationCrossfade cf;
        cf.Play(a, true, 0.0f);
        cf.Play(b, true, 1.0f);

        auto poses = cf.Sample(skel, 1.0f + 0.075f);
        REQUIRE(Approx(poses[0].m[12], 15.0f, 1e-3f));
    }

    void Test_OneShot_Clamped_AtClipEnd()
    {
        Skeleton skel = MakeOneBoneSkel();
        AnimationClip jump = MakeStaticTranslateClip("Jump", 5.0f);
        AnimationCrossfade cf;
        cf.Play(jump, /*loops=*/ false, /*now=*/ 0.0f);

        auto poses = cf.Sample(skel, 1.5f);
        REQUIRE(Approx(poses[0].m[12], 5.0f));
    }
}

int main()
{
    Test_NoCrossfade_PlaysCurrentClipOnly();
    Test_Crossfade_AtAlphaZero_ReturnsOldPose();
    Test_Crossfade_AtAlphaOne_ReturnsNewPose();
    Test_Crossfade_AtAlphaHalf_Midway();
    Test_OneShot_Clamped_AtClipEnd();
    return g_failed == 0 ? 0 : 1;
}
