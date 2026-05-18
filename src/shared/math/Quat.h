#pragma once

#include "src/shared/math/Math.h"

namespace engine::math
{

/// Quaternion unitaire (rotation 3D). Convention (x, y, z, w) avec w = scalaire.
/// Utilisé pour les rotations d'animation (Mixamo stocke les rotations comme
/// quaternions), pour interpolation entre keyframes (Slerp arc court), et
/// pour composer avec Mat4 via ToMat4().
///
/// Vit dans shared/math/ car les rotations peuvent être nécessaires côté
/// serveur (sync orientation) et côté éditeur (gizmos).
struct Quat
{
    float x = 0.0f, y = 0.0f, z = 0.0f, w = 1.0f;

    /// Quaternion identité (rotation nulle).
    static Quat Identity() { return Quat{0.0f, 0.0f, 0.0f, 1.0f}; }

    /// Construit un quaternion depuis axe + angle.
    /// \param axis Axe de rotation. Supposé normalisé (sinon le quaternion résultant ne le sera pas).
    /// \param radians Angle de rotation en radians.
    static Quat FromAxisAngle(const Vec3& axis, float radians);

    /// Interpolation sphérique linéaire (slerp) entre a et b à paramètre t ∈ [0, 1].
    /// Prend l'arc court (négation de b si dot < 0). Bascule vers nlerp + renormalisation
    /// quand les quaternions sont quasi-colinéaires (dot > 0.9995) pour éviter une
    /// division par zéro dans sin(θ₀).
    static Quat Slerp(const Quat& a, const Quat& b, float t);

    /// Composition de rotations (Hamilton product). (a * b) applique d'abord b puis a.
    Quat operator*(const Quat& rhs) const;

    /// Convertit le quaternion en matrice 4x4 (column-major, translation nulle).
    /// Compatible avec le Mat4 de Math.h (Mat4 * Mat4 et conventions Vulkan).
    Mat4 ToMat4() const;
};

}  // namespace engine::math
