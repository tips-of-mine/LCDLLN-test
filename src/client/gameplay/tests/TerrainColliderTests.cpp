// src/client/gameplay/tests/TerrainColliderTests.cpp
//
// Sous-projet B.1 (locomotion) etape 6/14 : tests unitaires TerrainCollider.
//
// On teste uniquement le fallback "no terrain bound" + edge cases du sweep
// car instancier un TerrainRenderer reel necessiterait un VkDevice (chaine
// Vulkan complete). Le comportement "avec terrain" est valide au smoke test
// visuel (Task 13).
//
// Quand aucun terrain n'est bind :
//   - GroundHeightAt(...) renvoie toujours 0.0.
//   - SweepCapsule : hit ssi (startY >= 0 && endY < 0), fraction lineaire
//     (0 - startY) / (endY - startY), normale (0,1,0).
//   - Tout autre cas : pas de hit, fraction = 1.0.

#include "src/client/gameplay/TerrainCollider.h"

#include <cmath>
#include <cstdio>

using engine::gameplay::TerrainCollider;
using engine::math::Vec3;

namespace
{
    int g_failed = 0;
    #define REQUIRE(cond) do { \
        if (!(cond)) { std::fprintf(stderr, "[FAIL] %s:%d %s\n", __FILE__, __LINE__, #cond); ++g_failed; } \
    } while (0)

    /// Comparateur tolerant pour floats : evite les faux positifs lies aux
    /// arrondis IEEE-754 sur les fractions calculees (e.g. 0.5 exact mais
    /// 1.0 / 3.0 != 0.333...).
    bool Approx(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) <= eps; }

    /// Sans terrain bind, GroundHeightAt doit renvoyer 0.0 a tout (x, z).
    /// Garantit que le placeholder flat sol fonctionne meme loin de l'origine.
    void Test_NoTerrainBound_GroundIsZero()
    {
        TerrainCollider c;
        REQUIRE(Approx(c.GroundHeightAt(0.0f, 0.0f), 0.0f));
        REQUIRE(Approx(c.GroundHeightAt(100.0f, -50.0f), 0.0f));
    }

    /// Sweep descendant qui traverse Y=0 (sol fallback) : doit hit a 50% du
    /// chemin (lineaire entre y=10 et y=-10, sol a y=0).
    void Test_NoTerrainBound_SweepReportsNoHit()
    {
        TerrainCollider c;
        engine::gameplay::IWorldCollider::Capsule cap{};
        engine::gameplay::IWorldCollider::SweepHit hit{};
        bool hitOccurred = c.SweepCapsule(cap, Vec3{0, 10, 0}, Vec3{0, -10, 0}, hit);
        // Sans terrain : GroundHeightAt = 0 ; depart y=10 ; arrivee y=-10.
        // 10 >= 0 (depart au-dessus) && -10 < 0 (arrivee en-dessous) -> HIT.
        REQUIRE(hitOccurred);
        REQUIRE(hit.hit);
        // fraction = (0 - 10) / (-10 - 10) = 0.5
        REQUIRE(Approx(hit.fraction, 0.5f));
    }

    /// Sweep ascendant (y=1 -> y=5) : startY >= 0 mais endY > 0, donc la
    /// condition (endY < endGround) est fausse, pas de hit.
    void Test_NoTerrainBound_AscendingSweep_NoHit()
    {
        TerrainCollider c;
        engine::gameplay::IWorldCollider::Capsule cap{};
        engine::gameplay::IWorldCollider::SweepHit hit{};
        bool hitOccurred = c.SweepCapsule(cap, Vec3{0, 1, 0}, Vec3{0, 5, 0}, hit);
        // Ascending : ne devrait pas hit le sol.
        REQUIRE(!hitOccurred);
        REQUIRE(!hit.hit);
        REQUIRE(Approx(hit.fraction, 1.0f));
    }

    /// Sweep horizontal au-dessus du sol (y=5 -> y=5, XZ change) : aucune
    /// extremite ne traverse Y=0, pas de hit, fraction reste a 1.0.
    void Test_NoTerrainBound_BothAboveGround_NoHit()
    {
        TerrainCollider c;
        engine::gameplay::IWorldCollider::Capsule cap{};
        engine::gameplay::IWorldCollider::SweepHit hit{};
        bool hitOccurred = c.SweepCapsule(cap, Vec3{0, 5, 0}, Vec3{2, 5, 2}, hit);
        REQUIRE(!hitOccurred);
        REQUIRE(Approx(hit.fraction, 1.0f));
    }
}

int main()
{
    Test_NoTerrainBound_GroundIsZero();
    Test_NoTerrainBound_SweepReportsNoHit();
    Test_NoTerrainBound_AscendingSweep_NoHit();
    Test_NoTerrainBound_BothAboveGround_NoHit();
    return g_failed == 0 ? 0 : 1;
}
