#pragma once

/**
 * @file Csm.h
 * @brief Cascaded Shadow Maps: split distances, light view-proj matrices, stabilization.
 *
 * Ticket: M04.1 — CSM: splits + matrices + stabilization.
 *
 * Practical split (λ=0.7), 4 cascades. Snap ortho to worldUnitsPerTexel for stability.
 * All matrices column-major (Vulkan). Light direction normalized; backoff in world units.
 */

#include <cstdint>

namespace engine::render {

/** Number of shadow cascades. */
constexpr int kCsmCascadeCount = 4;

/** Practical split blend factor (0=linear, 1=logarithmic). */
constexpr float kCsmLambda = 0.7f;

/**
 * @brief Uniform buffer layout for CSM: 4 lightViewProj matrices + 4 split depths.
 *
 * Matrices are column-major (16 floats each). Split depths are view-space cascade far planes.
 * Total size 4*64 + 4*4 = 272 bytes. Pad to 256-aligned if required by UBO.
 */
struct CsmUniform {
    float lightViewProj[kCsmCascadeCount][16];
    float splitDepths[kCsmCascadeCount];
};

/**
 * @brief Computes practical split depths for 4 cascades.
 *
 * splitDepths[i] = far plane of cascade i in camera view space (positive depth).
 * splitDepths[0]..[3] are the four cascade far bounds; cascade i uses [splitDepths[i-1], splitDepths[i]]
 * with splitDepths[-1] = cameraNear.
 *
 * @param cameraNear Camera near plane (positive).
 * @param cameraFar  Camera far plane (positive).
 * @param outSplits  Output array of 4 floats (cascade far planes).
 */
void CsmComputeSplitDepths(float cameraNear,
                            float cameraFar,
                            float outSplits[kCsmCascadeCount]);

/**
 * @brief Computes the 8 world-space corners of a frustum slice (view-space near/far).
 *
 * Uses invView to transform from view space. View space: camera at origin, -Z forward.
 *
 * @param invView    Inverse of camera view matrix (column-major).
 * @param fovY       Camera vertical FOV in radians.
 * @param aspect     Camera aspect ratio (width/height).
 * @param splitNear  Near plane depth of the slice (positive).
 * @param splitFar   Far plane depth of the slice (positive).
 * @param outCorners Output 8 corners: [0..3] near face, [4..7] far face (x,y,z per corner).
 */
void CsmFrustumSliceCorners(const float invView[16],
                             float fovY,
                             float aspect,
                             float splitNear,
                             float splitFar,
                             float outCorners[8][3]);

/**
 * @brief Builds light view matrix (world to light space), looking toward -lightDir.
 *
 * Eye = center - lightDir * backoff. Up vector chosen to avoid singularity.
 *
 * @param lightDir Normalized light direction (e.g. toward sun).
 * @param center   World-space center of the cascade (e.g. centroid of 8 corners).
 * @param backoff  Distance along lightDir to place the "camera" (world units).
 * @param outView  Column-major 4x4 output.
 */
void CsmBuildLightView(const float lightDir[3],
                       const float center[3],
                       float backoff,
                       float outView[16]);

/**
 * @brief Builds Vulkan orthographic projection (Z [0, 1], column-major).
 *
 * Maps [left,right] x [bottom,top] x [near,far] to NDC.
 *
 * @param left   Light-space left.
 * @param right  Light-space right.
 * @param bottom Light-space bottom.
 * @param top    Light-space top.
 * @param nearZ  Light-space near (smaller z).
 * @param farZ   Light-space far (larger z).
 * @param outProj Column-major 4x4 output.
 */
void CsmBuildOrthoProj(float left, float right, float bottom, float top,
                       float nearZ, float farZ,
                       float outProj[16]);

/**
 * @brief Stabilizes ortho bounds by snapping center to worldUnitsPerTexel grid.
 *
 * Keeps the same size; snaps the center so shadow map texels align and reduce shimmering.
 *
 * @param worldUnitsPerTexel Texel size in world units (e.g. (right-left)/shadowMapSize).
 * @param minBounds          [in] Light-space AABB min; [out] snapped min.
 * @param maxBounds          [in] Light-space AABB max; [out] snapped max.
 */
void CsmStabilizeOrtho(float worldUnitsPerTexel,
                       float minBounds[3],
                       float maxBounds[3]);

/**
 * @brief Fills the cascades uniform: split depths + lightViewProj per cascade.
 *
 * Uses camera view (column-major), camera near/far, FOV, aspect, normalized light direction,
 * shadow map size (for stabilization), and optional backoff (default from cascade size).
 *
 * @param view          Camera view matrix (column-major).
 * @param cameraNear    Camera near plane.
 * @param cameraFar     Camera far plane.
 * @param fovY          Camera vertical FOV (radians).
 * @param aspect        Camera aspect ratio.
 * @param lightDir      Normalized light direction (3 floats).
 * @param shadowMapSize Shadow map resolution (e.g. 1024) for worldUnitsPerTexel.
 * @param out           Cascades uniform to fill.
 */
void CsmComputeCascades(const float view[16],
                        float cameraNear,
                        float cameraFar,
                        float fovY,
                        float aspect,
                        const float lightDir[3],
                        uint32_t shadowMapSize,
                        CsmUniform& out);

} // namespace engine::render
