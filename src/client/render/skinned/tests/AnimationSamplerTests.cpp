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
}

int main()
{
    Test_SamplePose_AtMidTime_HalfRotation();
    return g_failed == 0 ? 0 : 1;
}
