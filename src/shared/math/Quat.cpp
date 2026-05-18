#include "src/shared/math/Quat.h"

#include <cmath>

namespace engine::math
{

/// Construit un quaternion depuis axe + angle.
/// Effet : x,y,z = axis * sin(θ/2), w = cos(θ/2). Si axis n'est pas normalisé,
/// le quaternion résultant ne sera pas non plus unitaire.
Quat Quat::FromAxisAngle(const Vec3& axis, float radians)
{
    const float h = 0.5f * radians;
    const float s = std::sin(h);
    return Quat{axis.x * s, axis.y * s, axis.z * s, std::cos(h)};
}

/// Hamilton product : (this * r). Compose les rotations de droite à gauche
/// (r d'abord, puis this). Préserve la norme si this et r sont unitaires.
Quat Quat::operator*(const Quat& r) const
{
    return Quat{
        w * r.x + x * r.w + y * r.z - z * r.y,
        w * r.y - x * r.z + y * r.w + z * r.x,
        w * r.z + x * r.y - y * r.x + z * r.w,
        w * r.w - x * r.x - y * r.y - z * r.z
    };
}

/// Slerp avec arc court + fallback nlerp.
/// 1. Si dot(a,b) < 0 → on négativise b pour prendre la rotation la plus courte
///    (q et -q représentent la même rotation).
/// 2. Si dot > 0.9995 → angles très petits, sin(θ₀) ≈ 0 → on utilise un lerp
///    composante-par-composante puis on renormalise (nlerp).
/// 3. Sinon, slerp standard : a*sin((1-t)θ₀)/sin(θ₀) + b*sin(tθ₀)/sin(θ₀),
///    formulé ici via cos(tθ₀) - dot*sin(tθ₀)/sin(θ₀) pour le coefficient sur a.
Quat Quat::Slerp(const Quat& a, const Quat& b, float t)
{
    float dot = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;

    Quat bAdj = b;
    if (dot < 0.0f)
    {
        bAdj.x = -b.x; bAdj.y = -b.y; bAdj.z = -b.z; bAdj.w = -b.w;
        dot = -dot;
    }

    if (dot > 0.9995f)
    {
        Quat r{
            a.x + t * (bAdj.x - a.x),
            a.y + t * (bAdj.y - a.y),
            a.z + t * (bAdj.z - a.z),
            a.w + t * (bAdj.w - a.w)
        };
        const float len = std::sqrt(r.x * r.x + r.y * r.y + r.z * r.z + r.w * r.w);
        if (len > 0.0f) { r.x /= len; r.y /= len; r.z /= len; r.w /= len; }
        return r;
    }

    const float theta0 = std::acos(dot);
    const float theta = theta0 * t;
    const float sinTheta = std::sin(theta);
    const float sinTheta0 = std::sin(theta0);

    const float s0 = std::cos(theta) - dot * sinTheta / sinTheta0;
    const float s1 = sinTheta / sinTheta0;

    return Quat{
        s0 * a.x + s1 * bAdj.x,
        s0 * a.y + s1 * bAdj.y,
        s0 * a.z + s1 * bAdj.z,
        s0 * a.w + s1 * bAdj.w
    };
}

/// Conversion quaternion → matrice 4x4 column-major (compatible Mat4 de Math.h).
/// Translation = identité. Suppose un quaternion unitaire ; sinon la matrice
/// résultante contiendra un facteur d'échelle parasite (||q||²).
Mat4 Quat::ToMat4() const
{
    const float xx = x * x, yy = y * y, zz = z * z;
    const float xy = x * y, xz = x * z, yz = y * z;
    const float wx = w * x, wy = w * y, wz = w * z;

    Mat4 m;
    m.m[0] = 1.0f - 2.0f * (yy + zz);
    m.m[1] = 2.0f * (xy + wz);
    m.m[2] = 2.0f * (xz - wy);
    m.m[3] = 0.0f;

    m.m[4] = 2.0f * (xy - wz);
    m.m[5] = 1.0f - 2.0f * (xx + zz);
    m.m[6] = 2.0f * (yz + wx);
    m.m[7] = 0.0f;

    m.m[8]  = 2.0f * (xz + wy);
    m.m[9]  = 2.0f * (yz - wx);
    m.m[10] = 1.0f - 2.0f * (xx + yy);
    m.m[11] = 0.0f;

    m.m[12] = 0.0f; m.m[13] = 0.0f; m.m[14] = 0.0f; m.m[15] = 1.0f;
    return m;
}

}  // namespace engine::math
