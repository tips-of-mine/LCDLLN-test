// Tests CharacterController — focalises sur le "ground snap" qui corrige le
// tremblement du mesh en mouvement (la position du CC pilote a la fois le mesh
// et la camera ; si elle fait une dent de scie verticale a chaque pas, toute la
// vue tremble). Le collider stub reproduit fidelement la semantique de
// TerrainCollider::SweepCapsule (detection uniquement sur traversee descendante)
// pour exercer l'interaction reelle.

#include "src/client/gameplay/CharacterController.h"

#include <cmath>
#include <cstdio>
#include <functional>

using engine::gameplay::CharacterController;
using engine::gameplay::IWorldCollider;
using engine::gameplay::MoveInput;
using engine::math::Vec3;

namespace
{
    int g_failed = 0;
    #define REQUIRE(cond) do { \
        if (!(cond)) { std::fprintf(stderr, "[FAIL] %s:%d %s\n", __FILE__, __LINE__, #cond); ++g_failed; } \
    } while (0)

    bool Approx(float a, float b, float eps) { return std::fabs(a - b) <= eps; }

    // Collider analytique : sol = heightFn(x,z). Reproduit a l'identique la
    // logique de TerrainCollider::SweepCapsule (seuil = groundHeight + halfHeight,
    // hit uniquement quand le centre traverse le seuil VERS LE BAS).
    struct AnalyticGroundCollider final : IWorldCollider
    {
        std::function<float(float, float)> heightFn;

        bool SweepCapsule(const Capsule& capsule,
                          const Vec3& startCenter,
                          const Vec3& endCenter,
                          SweepHit& outHit) const override
        {
            outHit.hit = false;
            outHit.fraction = 1.0f;
            outHit.normal = Vec3{0.0f, 1.0f, 0.0f};

            const float halfHeight = capsule.height * 0.5f;
            const float startY = startCenter.y;
            const float endY = endCenter.y;
            const float endGround = heightFn(endCenter.x, endCenter.z);
            const float endThreshold = endGround + halfHeight;

            if (endY < endThreshold && startY >= endThreshold)
            {
                const float deltaY = endY - startY;
                if (std::fabs(deltaY) > 1e-6f)
                {
                    float t = (endThreshold - startY) / deltaY;
                    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
                    outHit.fraction = t;
                }
                else
                {
                    outHit.fraction = 0.0f;
                }
                outHit.hit = true;
            }
            return outHit.hit;
        }
    };

    constexpr float kDt = 1.0f / 60.0f;
    constexpr float kHalfHeight = 0.9f;  // capsule par defaut : height 1.8 / 2.

    // Le perso doit rester colle au sol (pas de tremblement) en marchant sur une
    // pente douce. Sans le ground snap, le Y ferait une dent de scie
    // (penetration en montee -> rattrapage gravite) et ce test echouerait.
    void Test_GroundSnap_FollowsSlope_NoSawtooth()
    {
        AnalyticGroundCollider world;
        const float slope = 0.1f;  // 10% : sol = 0.1 * x
        world.heightFn = [slope](float x, float) { return slope * x; };

        CharacterController cc;
        REQUIRE(cc.Init(Vec3{0.0f, world.heightFn(0.0f, 0.0f) + kHalfHeight, 0.0f}));

        MoveInput in{};
        in.moveDirXZ = Vec3{1.0f, 0.0f, 0.0f};  // marche en +X (montee).

        float maxErr = 0.0f;
        for (int frame = 0; frame < 240; ++frame)
        {
            cc.Update(kDt, in, world);
            const Vec3 p = cc.GetPosition();
            const float expectedY = world.heightFn(p.x, p.z) + kHalfHeight;
            // On laisse quelques frames de mise en regime (acceleration initiale).
            if (frame >= 10)
            {
                const float err = std::fabs(p.y - expectedY);
                if (err > maxErr) maxErr = err;
            }
        }
        // Colle au sol a moins d'1 cm pres : aucune dent de scie.
        REQUIRE(maxErr < 0.01f);
        REQUIRE(cc.IsGrounded());
        // A bien avance en X.
        REQUIRE(cc.GetPosition().x > 1.0f);
    }

    void Test_GroundSnap_FlatGround_Stable()
    {
        AnalyticGroundCollider world;
        world.heightFn = [](float, float) { return 5.0f; };

        CharacterController cc;
        REQUIRE(cc.Init(Vec3{0.0f, 5.0f + kHalfHeight, 0.0f}));

        MoveInput in{};
        in.moveDirXZ = Vec3{1.0f, 0.0f, 0.0f};

        for (int frame = 0; frame < 120; ++frame)
        {
            cc.Update(kDt, in, world);
            const Vec3 p = cc.GetPosition();
            if (frame >= 5)
                REQUIRE(Approx(p.y, 5.0f + kHalfHeight, 0.005f));
        }
        REQUIRE(cc.IsGrounded());
    }

    // Le snap ne doit pas annuler un saut : apres un jump, le centre s'eleve
    // nettement au-dessus de sol + halfHeight.
    void Test_GroundSnap_DoesNotCancelJump()
    {
        AnalyticGroundCollider world;
        world.heightFn = [](float, float) { return 0.0f; };

        CharacterController cc;
        REQUIRE(cc.Init(Vec3{0.0f, kHalfHeight, 0.0f}));

        // Frame 1 : pose au sol pour etablir grounded.
        MoveInput idle{};
        cc.Update(kDt, idle, world);
        REQUIRE(cc.IsGrounded());

        // Frame 2 : saut.
        MoveInput jump{};
        jump.jumpPressed = true;
        cc.Update(kDt, jump, world);

        // Quelques frames de montee.
        MoveInput air{};
        float maxY = cc.GetPosition().y;
        for (int frame = 0; frame < 10; ++frame)
        {
            cc.Update(kDt, air, world);
            maxY = std::fmax(maxY, cc.GetPosition().y);
        }
        REQUIRE(maxY > kHalfHeight + 0.3f);  // a bien quitte le sol.
    }

    // Marcher au-dela d'un bord (chute > maxStep) ne doit PAS telesnaper le perso
    // vers le bas : il doit tomber progressivement (gravite), pas se coller
    // instantanement au sol bas.
    void Test_GroundSnap_DropTallerThanMaxStep_Falls()
    {
        AnalyticGroundCollider world;
        // Plateau a y=0 pour x < 1, puis falaise a y=-5 (chute 5 m >> maxStep 0.3).
        world.heightFn = [](float x, float) { return x < 1.0f ? 0.0f : -5.0f; };

        CharacterController cc;
        REQUIRE(cc.Init(Vec3{0.0f, kHalfHeight, 0.0f}));

        MoveInput in{};
        in.moveDirXZ = Vec3{1.0f, 0.0f, 0.0f};

        bool sawAirborneAboveCliff = false;
        for (int frame = 0; frame < 60; ++frame)
        {
            cc.Update(kDt, in, world);
            const Vec3 p = cc.GetPosition();
            if (p.x >= 1.0f && p.y > -5.0f + kHalfHeight + 0.5f)
            {
                // Au-dessus du sol bas : preuve qu'on n'a pas teleporte en bas.
                sawAirborneAboveCliff = true;
            }
        }
        REQUIRE(sawAirborneAboveCliff);
    }
}

int main()
{
    Test_GroundSnap_FollowsSlope_NoSawtooth();
    Test_GroundSnap_FlatGround_Stable();
    Test_GroundSnap_DoesNotCancelJump();
    Test_GroundSnap_DropTallerThanMaxStep_Falls();
    return g_failed == 0 ? 0 : 1;
}
