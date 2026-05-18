#include "src/shared/math/Math.h"

#include <cmath>
#include <cstdio>

using engine::math::Mat4;
using engine::math::Vec3;

namespace
{
    int g_failed = 0;
    #define REQUIRE(cond) do { \
        if (!(cond)) { std::fprintf(stderr, "[FAIL] %s:%d %s\n", __FILE__, __LINE__, #cond); ++g_failed; } \
    } while (0)

    bool Approx(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }

    void Test_Identity_IsIdentity()
    {
        Mat4 m = Mat4::Identity();
        for (int col = 0; col < 4; ++col)
            for (int row = 0; row < 4; ++row)
                REQUIRE(Approx(m.m[col * 4 + row], (col == row) ? 1.0f : 0.0f));
    }

    void Test_Translate_PutsTranslationInLastColumn()
    {
        Mat4 m = Mat4::Translate(Vec3{5.0f, 6.0f, 7.0f});
        REQUIRE(Approx(m.m[12], 5.0f));  // col 3, row 0
        REQUIRE(Approx(m.m[13], 6.0f));  // col 3, row 1
        REQUIRE(Approx(m.m[14], 7.0f));  // col 3, row 2
        REQUIRE(Approx(m.m[15], 1.0f));
        REQUIRE(Approx(m.m[0], 1.0f));   // diag stays identity
        REQUIRE(Approx(m.m[5], 1.0f));
        REQUIRE(Approx(m.m[10], 1.0f));
    }

    void Test_RotateY_Pi_FlipsXAndZ()
    {
        Mat4 m = Mat4::RotateY(3.14159265f);
        // Y axis stays.
        REQUIRE(Approx(m.m[5], 1.0f));
        // X column inverted, Z column inverted.
        REQUIRE(Approx(m.m[0], -1.0f, 1e-3f));   // cos(π) = -1
        REQUIRE(Approx(m.m[10], -1.0f, 1e-3f));  // cos(π) = -1
        // Off-diagonals sin/-sin (≈0 at π).
        REQUIRE(Approx(m.m[2], 0.0f, 1e-3f));    // sin(π) ≈ 0
        REQUIRE(Approx(m.m[8], 0.0f, 1e-3f));    // -sin(π) ≈ 0
    }

    void Test_RotateY_HalfPi_RotatesXTowardMinusZ()
    {
        Mat4 m = Mat4::RotateY(1.5707963f);  // 90°
        // Standard column-major right-hand: x basis becomes (0, 0, -1)
        REQUIRE(Approx(m.m[0], 0.0f, 1e-3f));
        REQUIRE(Approx(m.m[2], -1.0f, 1e-3f));
        // z basis becomes (1, 0, 0)
        REQUIRE(Approx(m.m[8], 1.0f, 1e-3f));
        REQUIRE(Approx(m.m[10], 0.0f, 1e-3f));
    }

    void Test_TranslateThenRotateY_Composes()
    {
        Mat4 t = Mat4::Translate(Vec3{10.0f, 0.0f, 0.0f});
        Mat4 r = Mat4::RotateY(1.5707963f);
        // T * R : first rotate then translate (column-vector convention).
        Mat4 tr = t * r;
        // Translation column unchanged by post-multiplied rotation : still (10, 0, 0).
        REQUIRE(Approx(tr.m[12], 10.0f));
        REQUIRE(Approx(tr.m[13], 0.0f));
        REQUIRE(Approx(tr.m[14], 0.0f));
        // Rotation part preserved.
        REQUIRE(Approx(tr.m[0], 0.0f, 1e-3f));
        REQUIRE(Approx(tr.m[2], -1.0f, 1e-3f));
    }
}

int main()
{
    Test_Identity_IsIdentity();
    Test_Translate_PutsTranslationInLastColumn();
    Test_RotateY_Pi_FlipsXAndZ();
    Test_RotateY_HalfPi_RotatesXTowardMinusZ();
    Test_TranslateThenRotateY_Composes();
    return g_failed == 0 ? 0 : 1;
}
