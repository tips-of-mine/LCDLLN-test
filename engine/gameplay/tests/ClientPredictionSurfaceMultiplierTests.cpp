// engine/gameplay/tests/ClientPredictionSurfaceMultiplierTests.cpp
#include "engine/gameplay/ClientPrediction.h"

#include <cmath>
#include <cstdio>

namespace
{
    int g_failed = 0;

    #define REQUIRE(cond) do { \
        if (!(cond)) { \
            std::fprintf(stderr, "[FAIL] %s:%d  %s\n", __FILE__, __LINE__, #cond); \
            ++g_failed; \
        } \
    } while (0)

    using engine::gameplay::ClientPredictionSystem;

    bool ApproxEq(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) <= eps; }

    void Test_SetSurfaceSpeedMultiplier_Default_Is1p0()
    {
        ClientPredictionSystem sys;
        // Default avant tout Set = 1.0
        REQUIRE(ApproxEq(sys.GetSurfaceSpeedMultiplier(), 1.0f));
    }

    void Test_SetSurfaceSpeedMultiplier_0p5_HalvesSpeed()
    {
        ClientPredictionSystem sys;
        sys.SetSurfaceSpeedMultiplier(0.5f);
        REQUIRE(ApproxEq(sys.GetSurfaceSpeedMultiplier(), 0.5f));
        // Le ratio 2× du critère M100.11 : Snow (0.5) vs Dirt (1.0) → 0.5×.
    }

    void Test_SetSurfaceSpeedMultiplier_Clamp()
    {
        ClientPredictionSystem sys;
        sys.SetSurfaceSpeedMultiplier(-1.0f);
        REQUIRE(ApproxEq(sys.GetSurfaceSpeedMultiplier(), 0.1f));   // clamp bas
        sys.SetSurfaceSpeedMultiplier(99.0f);
        REQUIRE(ApproxEq(sys.GetSurfaceSpeedMultiplier(), 5.0f));   // clamp haut
    }
}

int main()
{
    Test_SetSurfaceSpeedMultiplier_Default_Is1p0();
    Test_SetSurfaceSpeedMultiplier_0p5_HalvesSpeed();
    Test_SetSurfaceSpeedMultiplier_Clamp();
    return g_failed;
}
