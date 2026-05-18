// Tests for engine::math::Quat — construction, identity, multiply, slerp.

#include "src/shared/math/Quat.h"

#include <cmath>
#include <cstdio>

using engine::math::Quat;
using engine::math::Vec3;

namespace
{
    int g_failed = 0;

    #define REQUIRE(cond) do { \
        if (!(cond)) { \
            std::fprintf(stderr, "[FAIL] %s:%d  %s\n", __FILE__, __LINE__, #cond); \
            ++g_failed; \
        } \
    } while (0)

    bool Approx(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }

    void Test_IdentityQuat_HasZeroXYZ_OneW()
    {
        Quat q = Quat::Identity();
        REQUIRE(Approx(q.x, 0.0f));
        REQUIRE(Approx(q.y, 0.0f));
        REQUIRE(Approx(q.z, 0.0f));
        REQUIRE(Approx(q.w, 1.0f));
    }

    void Test_FromAxisAngle_HalfPiAroundY_IsExpected()
    {
        Quat q = Quat::FromAxisAngle(Vec3{0.0f, 1.0f, 0.0f}, 1.5707963f); // 90 deg
        REQUIRE(Approx(q.x, 0.0f));
        REQUIRE(Approx(q.y, 0.70710677f));
        REQUIRE(Approx(q.z, 0.0f));
        REQUIRE(Approx(q.w, 0.70710677f));
    }

    void Test_Multiply_IdentityIsNeutral()
    {
        Quat a = Quat::FromAxisAngle(Vec3{0.0f, 1.0f, 0.0f}, 0.5f);
        Quat r = a * Quat::Identity();
        REQUIRE(Approx(r.x, a.x));
        REQUIRE(Approx(r.y, a.y));
        REQUIRE(Approx(r.z, a.z));
        REQUIRE(Approx(r.w, a.w));
    }

    void Test_Slerp_AtZero_ReturnsA()
    {
        Quat a = Quat::Identity();
        Quat b = Quat::FromAxisAngle(Vec3{0.0f, 1.0f, 0.0f}, 1.5707963f);
        Quat r = Quat::Slerp(a, b, 0.0f);
        REQUIRE(Approx(r.w, 1.0f));
    }

    void Test_Slerp_AtOne_ReturnsB()
    {
        Quat a = Quat::Identity();
        Quat b = Quat::FromAxisAngle(Vec3{0.0f, 1.0f, 0.0f}, 1.5707963f);
        Quat r = Quat::Slerp(a, b, 1.0f);
        REQUIRE(Approx(r.y, 0.70710677f));
        REQUIRE(Approx(r.w, 0.70710677f));
    }

    void Test_Slerp_AtHalf_IsMidwayRotation()
    {
        Quat a = Quat::Identity();
        Quat b = Quat::FromAxisAngle(Vec3{0.0f, 1.0f, 0.0f}, 1.5707963f); // 90 deg
        Quat r = Quat::Slerp(a, b, 0.5f);
        // Half of 90deg = 45deg → y = sin(22.5deg), w = cos(22.5deg)
        REQUIRE(Approx(r.y, std::sin(0.39269908f)));
        REQUIRE(Approx(r.w, std::cos(0.39269908f)));
    }
}

int main()
{
    Test_IdentityQuat_HasZeroXYZ_OneW();
    Test_FromAxisAngle_HalfPiAroundY_IsExpected();
    Test_Multiply_IdentityIsNeutral();
    Test_Slerp_AtZero_ReturnsA();
    Test_Slerp_AtOne_ReturnsB();
    Test_Slerp_AtHalf_IsMidwayRotation();
    return g_failed == 0 ? 0 : 1;
}
