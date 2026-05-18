#include "src/client/render/skinned/AnimationClip.h"

#include <cmath>
#include <cstdio>

using engine::math::Quat;
using engine::math::Vec3;
using engine::render::skinned::AnimationClip;
using engine::render::skinned::BoneTracks;
using engine::render::skinned::InterpolateKeyframes;
using engine::render::skinned::Keyframe;

namespace
{
    int g_failed = 0;
    #define REQUIRE(cond) do { \
        if (!(cond)) { std::fprintf(stderr, "[FAIL] %s:%d %s\n", __FILE__, __LINE__, #cond); ++g_failed; } \
    } while (0)

    bool Approx(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }

    void Test_InterpolateVec3_BetweenTwoKeys()
    {
        std::vector<Keyframe<Vec3>> kf = {
            {0.0f, Vec3{0.0f, 0.0f, 0.0f}},
            {1.0f, Vec3{10.0f, 20.0f, 30.0f}}
        };
        Vec3 r = InterpolateKeyframes(kf, 0.5f, Vec3{0.0f, 0.0f, 0.0f});
        REQUIRE(Approx(r.x, 5.0f));
        REQUIRE(Approx(r.y, 10.0f));
        REQUIRE(Approx(r.z, 15.0f));
    }

    void Test_InterpolateVec3_BeforeFirstKey_ClampsToFirst()
    {
        std::vector<Keyframe<Vec3>> kf = {
            {1.0f, Vec3{10.0f, 0.0f, 0.0f}},
            {2.0f, Vec3{20.0f, 0.0f, 0.0f}}
        };
        Vec3 r = InterpolateKeyframes(kf, 0.5f, Vec3{99.0f, 99.0f, 99.0f});
        REQUIRE(Approx(r.x, 10.0f));
    }

    void Test_InterpolateVec3_AfterLastKey_ClampsToLast()
    {
        std::vector<Keyframe<Vec3>> kf = {
            {0.0f, Vec3{0.0f, 0.0f, 0.0f}},
            {1.0f, Vec3{10.0f, 0.0f, 0.0f}}
        };
        Vec3 r = InterpolateKeyframes(kf, 5.0f, Vec3{99.0f, 99.0f, 99.0f});
        REQUIRE(Approx(r.x, 10.0f));
    }

    void Test_InterpolateVec3_Empty_ReturnsFallback()
    {
        std::vector<Keyframe<Vec3>> kf;
        Vec3 r = InterpolateKeyframes(kf, 0.5f, Vec3{42.0f, 43.0f, 44.0f});
        REQUIRE(Approx(r.x, 42.0f));
        REQUIRE(Approx(r.y, 43.0f));
        REQUIRE(Approx(r.z, 44.0f));
    }

    void Test_InterpolateQuat_UsesSlerp()
    {
        std::vector<Keyframe<Quat>> kf = {
            {0.0f, Quat::Identity()},
            {1.0f, Quat::FromAxisAngle(Vec3{0,1,0}, 1.5707963f)}
        };
        Quat r = InterpolateKeyframes(kf, 0.5f, Quat::Identity());
        // 45 degrees around Y
        REQUIRE(Approx(r.y, std::sin(0.39269908f)));
        REQUIRE(Approx(r.w, std::cos(0.39269908f)));
    }

    void Test_AnimationClip_Construction()
    {
        AnimationClip clip;
        clip.name = "Walk";
        clip.duration = 1.5f;
        clip.tracks.resize(3);
        REQUIRE(clip.name == "Walk");
        REQUIRE(Approx(clip.duration, 1.5f));
        REQUIRE(clip.tracks.size() == 3);
    }
}

int main()
{
    Test_InterpolateVec3_BetweenTwoKeys();
    Test_InterpolateVec3_BeforeFirstKey_ClampsToFirst();
    Test_InterpolateVec3_AfterLastKey_ClampsToLast();
    Test_InterpolateVec3_Empty_ReturnsFallback();
    Test_InterpolateQuat_UsesSlerp();
    Test_AnimationClip_Construction();
    return g_failed == 0 ? 0 : 1;
}
