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

    // --- B.1 fix audit §1/§3 : root motion lock XZ ---

    Mat4 MakeTranslationMat(float x, float y, float z)
    {
        Mat4 m = Mat4::Identity();
        m.m[12] = x; m.m[13] = y; m.m[14] = z;
        return m;
    }

    // Squelette racine + enfant. La racine porte une bind pose translatee
    // (bindX/bindZ) pour verifier que le lock revient bien a la BIND et pas a 0.
    Skeleton MakeRootChildSkel(float bindX, float bindZ)
    {
        Skeleton s;
        s.bones.push_back(Bone{"Hips", -1, MakeTranslationMat(bindX, 0.0f, bindZ), Mat4::Identity()});
        s.bones.push_back(Bone{"Child", 0, Mat4::Identity(), Mat4::Identity()});
        return s;
    }

    AnimationClip MakeXYZClip(const std::string& name, const Vec3& root, const Vec3& child)
    {
        AnimationClip c;
        c.name = name;
        c.duration = 1.0f;
        c.tracks.resize(2);
        c.tracks[0].translation = {{0.0f, root}, {1.0f, root}};
        c.tracks[1].translation = {{0.0f, child}, {1.0f, child}};
        return c;
    }

    void Test_RootMotionLock_Default_AppliesRootMotion()
    {
        // Par defaut (lock off), la translation root est appliquee telle quelle.
        Skeleton skel = MakeRootChildSkel(/*bindX=*/0.0f, /*bindZ=*/0.0f);
        AnimationClip a = MakeXYZClip("A", Vec3{7.0f, 3.0f, 9.0f}, Vec3{1.0f, 0.0f, 2.0f});
        AnimationCrossfade cf;
        cf.Play(a, true, 0.0f);

        auto poses = cf.Sample(skel, 0.5f);
        REQUIRE(!cf.IsRootMotionLockXZ());
        REQUIRE(Approx(poses[0].m[12], 7.0f));
        REQUIRE(Approx(poses[0].m[14], 9.0f));
    }

    void Test_RootMotionLock_LocksRootXZ_KeepsY()
    {
        // Lock on : X/Z root forces a la bind (1.5 / -2.5), Y conserve (3.0).
        Skeleton skel = MakeRootChildSkel(/*bindX=*/1.5f, /*bindZ=*/-2.5f);
        AnimationClip a = MakeXYZClip("A", Vec3{7.0f, 3.0f, 9.0f}, Vec3{1.0f, 0.0f, 2.0f});
        AnimationCrossfade cf;
        cf.SetRootMotionLockXZ(true);
        cf.Play(a, true, 0.0f);

        auto poses = cf.Sample(skel, 0.5f);
        REQUIRE(cf.IsRootMotionLockXZ());
        REQUIRE(Approx(poses[0].m[12], 1.5f));   // X -> bind
        REQUIRE(Approx(poses[0].m[13], 3.0f));   // Y -> conserve (bob)
        REQUIRE(Approx(poses[0].m[14], -2.5f));  // Z -> bind
    }

    void Test_RootMotionLock_ChildBoneUnaffected()
    {
        // Lock on : un bone non-racine garde sa translation d'animation complete.
        Skeleton skel = MakeRootChildSkel(0.0f, 0.0f);
        AnimationClip a = MakeXYZClip("A", Vec3{7.0f, 3.0f, 9.0f}, Vec3{5.0f, 6.0f, 8.0f});
        AnimationCrossfade cf;
        cf.SetRootMotionLockXZ(true);
        cf.Play(a, true, 0.0f);

        auto poses = cf.Sample(skel, 0.5f);
        REQUIRE(Approx(poses[1].m[12], 5.0f));
        REQUIRE(Approx(poses[1].m[13], 6.0f));
        REQUIRE(Approx(poses[1].m[14], 8.0f));
    }

    void Test_RootMotionLock_AppliesDuringCrossfade()
    {
        // Le lock s'applique aussi sur le chemin de blend (crossfade actif).
        Skeleton skel = MakeRootChildSkel(0.0f, 0.0f);
        AnimationClip a = MakeXYZClip("A", Vec3{10.0f, 2.0f, 10.0f}, Vec3{0.0f, 0.0f, 0.0f});
        AnimationClip b = MakeXYZClip("B", Vec3{20.0f, 4.0f, 20.0f}, Vec3{0.0f, 0.0f, 0.0f});
        AnimationCrossfade cf;
        cf.SetRootMotionLockXZ(true);
        cf.Play(a, true, 0.0f);
        cf.Play(b, true, 1.0f);

        auto poses = cf.Sample(skel, 1.0f + 0.075f);  // alpha ~ 0.5
        REQUIRE(Approx(poses[0].m[12], 0.0f));        // X locke a la bind (0)
        REQUIRE(Approx(poses[0].m[14], 0.0f));        // Z locke a la bind (0)
        REQUIRE(Approx(poses[0].m[13], 3.0f, 1e-3f)); // Y lerp(2,4,0.5) conserve
    }
}

int main()
{
    Test_NoCrossfade_PlaysCurrentClipOnly();
    Test_Crossfade_AtAlphaZero_ReturnsOldPose();
    Test_Crossfade_AtAlphaOne_ReturnsNewPose();
    Test_Crossfade_AtAlphaHalf_Midway();
    Test_OneShot_Clamped_AtClipEnd();
    Test_RootMotionLock_Default_AppliesRootMotion();
    Test_RootMotionLock_LocksRootXZ_KeepsY();
    Test_RootMotionLock_ChildBoneUnaffected();
    Test_RootMotionLock_AppliesDuringCrossfade();
    return g_failed == 0 ? 0 : 1;
}
