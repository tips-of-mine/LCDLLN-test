#pragma once

/**
 * @file Frustum.h
 * @brief View frustum: 6 planes and AABB test.
 *
 * Ticket: M03.0 — Camera & Frustum Culling MVP.
 *
 * Planes extracted from view-projection matrix (normalized).
 * TestAABB(min, max) returns true if the box is inside or intersecting the frustum.
 */

namespace engine::math {

/**
 * @brief One plane: nx*x + ny*y + nz*z + d = 0 (normal points inward).
 */
struct Plane {
    float nx = 0.0f, ny = 0.0f, nz = 0.0f, d = 0.0f;
};

/**
 * @brief Six frustum planes (left, right, top, bottom, near, far).
 */
struct Frustum {
    Plane planes[6];
};

/**
 * @brief Extracts the 6 frustum planes from a column-major view-projection matrix.
 *
 * Planes are normalized. Order: left, right, top, bottom, near, far.
 *
 * @param viewProj  Column-major 4x4 matrix (16 floats).
 * @param out       Frustum to fill.
 */
void ExtractFromMatrix(const float viewProj[16], Frustum& out);

/**
 * @brief Returns true if the AABB [min, max] is inside or intersecting the frustum.
 *
 * @param f    Frustum (from ExtractFromMatrix).
 * @param min  AABB minimum corner (world space).
 * @param max  AABB maximum corner (world space).
 * @return     true if visible (inside or intersect), false if fully outside.
 */
bool TestAABB(const Frustum& f, const float min[3], const float max[3]);

} // namespace engine::math
