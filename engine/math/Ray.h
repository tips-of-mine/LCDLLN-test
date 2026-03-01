#pragma once

/**
 * @file Ray.h
 * @brief Ray casting helpers for editor selection (M12.1).
 *
 * ScreenPointToRay: build world-space ray from screen coordinates.
 * RayAABB: intersect ray with axis-aligned box, return hit distance.
 */

namespace engine::math {

/**
 * @brief Builds a world-space ray from a screen point using view and projection matrices.
 *
 * @param screenX       X in pixels (0 = left).
 * @param screenY       Y in pixels (0 = top).
 * @param viewportW     Viewport width in pixels.
 * @param viewportH     Viewport height in pixels.
 * @param viewColMajor  View matrix (column-major 4x4).
 * @param projColMajor  Projection matrix (column-major 4x4).
 * @param outOrigin     Output ray origin (world space).
 * @param outDir        Output ray direction (world space, not normalized).
 */
void ScreenPointToRay(float screenX, float screenY,
                     float viewportW, float viewportH,
                     const float viewColMajor[16],
                     const float projColMajor[16],
                     float outOrigin[3], float outDir[3]);

/**
 * @brief Intersects a ray with an AABB. Returns true if hit and writes hit distance to outT.
 *
 * @param origin  Ray origin (world space).
 * @param dir     Ray direction (world space; can be unnormalized).
 * @param aabbMin AABB minimum corner.
 * @param aabbMax AABB maximum corner.
 * @param outT    Hit distance along ray (>= 0). Undefined if no hit.
 * @return        true if ray hits the box, false otherwise.
 */
bool RayAABB(const float origin[3], const float dir[3],
            const float aabbMin[3], const float aabbMax[3],
            float* outT);

/**
 * @brief Intersects a ray with a sphere. Returns true if hit and writes hit distance to outT.
 *
 * @param origin  Ray origin (world space).
 * @param dir     Ray direction (world space; can be unnormalized).
 * @param center Sphere center (world space).
 * @param radius  Sphere radius.
 * @param outT    Hit distance along ray (>= 0). Undefined if no hit.
 * @return        true if ray hits the sphere, false otherwise.
 */
bool RaySphere(const float origin[3], const float dir[3],
               const float center[3], float radius,
               float* outT);

/**
 * @brief Projects a world-space point to screen coordinates (for debug draw).
 *
 * @param worldX     World X.
 * @param worldY     World Y.
 * @param worldZ     World Z.
 * @param viewCol    View matrix (column-major 16 floats).
 * @param projCol    Projection matrix (column-major 16 floats).
 * @param viewportW  Viewport width in pixels.
 * @param viewportH  Viewport height in pixels.
 * @param outScreenX Output screen X (0 = left).
 * @param outScreenY Output screen Y (0 = top). Returns false if behind camera.
 */
bool WorldToScreen(float worldX, float worldY, float worldZ,
                   const float viewCol[16], const float projCol[16],
                   float viewportW, float viewportH,
                   float* outScreenX, float* outScreenY);

} // namespace engine::math
