/**
 * @file Csm.cpp
 * @brief CSM: practical splits, frustum corners, light view/ortho, stabilization.
 */

#include "engine/render/Csm.h"

#include <cmath>
#include <cstring>

namespace engine::render {

namespace {

/** Inverts a 4x4 column-major matrix. */
void Invert4x4(const float m[16], float out[16]) {
    float inv[16];
    inv[0]  =  m[5]*m[10]*m[15] - m[5]*m[11]*m[14] - m[9]*m[6]*m[15] + m[9]*m[7]*m[14] + m[13]*m[6]*m[11] - m[13]*m[7]*m[10];
    inv[4]  = -m[4]*m[10]*m[15] + m[4]*m[11]*m[14] + m[8]*m[6]*m[15] - m[8]*m[7]*m[14] - m[12]*m[6]*m[11] + m[12]*m[7]*m[10];
    inv[8]  =  m[4]*m[9]*m[15]  - m[4]*m[11]*m[13] - m[8]*m[5]*m[15] + m[8]*m[7]*m[13] + m[12]*m[5]*m[11] - m[12]*m[7]*m[9];
    inv[12] = -m[4]*m[9]*m[14]  + m[4]*m[10]*m[13] + m[8]*m[5]*m[14] - m[8]*m[6]*m[13] - m[12]*m[5]*m[10] + m[12]*m[6]*m[9];
    inv[1]  = -m[1]*m[10]*m[15] + m[1]*m[11]*m[14] + m[9]*m[2]*m[15] - m[9]*m[3]*m[14] - m[13]*m[2]*m[11] + m[13]*m[3]*m[10];
    inv[5]  =  m[0]*m[10]*m[15] - m[0]*m[11]*m[14] - m[8]*m[2]*m[15] + m[8]*m[3]*m[14] + m[12]*m[2]*m[11] - m[12]*m[3]*m[10];
    inv[9]  = -m[0]*m[9]*m[15]  + m[0]*m[11]*m[13] + m[8]*m[1]*m[15] - m[8]*m[3]*m[13] - m[12]*m[1]*m[11] + m[12]*m[3]*m[9];
    inv[13] =  m[0]*m[9]*m[14]   - m[0]*m[10]*m[13] - m[8]*m[1]*m[14] + m[8]*m[2]*m[13] + m[12]*m[1]*m[10] - m[12]*m[2]*m[9];
    inv[2]  =  m[1]*m[6]*m[15]   - m[1]*m[7]*m[14]  - m[5]*m[2]*m[15] + m[5]*m[3]*m[14] + m[13]*m[2]*m[7]  - m[13]*m[3]*m[6];
    inv[6]  = -m[0]*m[6]*m[15]   + m[0]*m[7]*m[14]  + m[4]*m[2]*m[15] - m[4]*m[3]*m[14] - m[12]*m[2]*m[7]  + m[12]*m[3]*m[6];
    inv[10] =  m[0]*m[5]*m[15]   - m[0]*m[7]*m[13]  - m[4]*m[1]*m[15] + m[4]*m[3]*m[13] + m[12]*m[1]*m[7]  - m[12]*m[3]*m[5];
    inv[14] = -m[0]*m[5]*m[14]   + m[0]*m[6]*m[13]  + m[4]*m[1]*m[14] - m[4]*m[2]*m[13] - m[12]*m[1]*m[6]  + m[12]*m[2]*m[5];
    inv[3]  = -m[1]*m[6]*m[11]   + m[1]*m[7]*m[10]  + m[5]*m[2]*m[11] - m[5]*m[3]*m[10] - m[9]*m[2]*m[7]   + m[9]*m[3]*m[6];
    inv[7]  =  m[0]*m[6]*m[11]   - m[0]*m[7]*m[10]  - m[4]*m[2]*m[11] + m[4]*m[3]*m[10] + m[8]*m[2]*m[7]   - m[8]*m[3]*m[6];
    inv[11] = -m[0]*m[5]*m[11]   + m[0]*m[7]*m[9]   + m[4]*m[1]*m[11] - m[4]*m[3]*m[9]  - m[8]*m[1]*m[7]   + m[8]*m[3]*m[5];
    inv[15] =  m[0]*m[5]*m[10]   - m[0]*m[6]*m[9]   - m[4]*m[1]*m[10] + m[4]*m[2]*m[9]  + m[8]*m[1]*m[6]   - m[8]*m[2]*m[5];
    float det = m[0]*inv[0] + m[1]*inv[4] + m[2]*inv[8] + m[3]*inv[12];
    if (std::abs(det) < 1e-8f) {
        std::memset(out, 0, 16 * sizeof(float));
        return;
    }
    det = 1.0f / det;
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c)
            out[c * 4 + r] = inv[r * 4 + c] * det;
}

/** Column-major matrix * vector. */
void Mat4MulPoint(const float m[16], const float p[3], float out[3]) {
    float w = m[3]*p[0] + m[7]*p[1] + m[11]*p[2] + m[15];
    if (std::abs(w) < 1e-8f) w = 1.0f;
    float iw = 1.0f / w;
    out[0] = (m[0]*p[0] + m[4]*p[1] + m[8]*p[2] + m[12]) * iw;
    out[1] = (m[1]*p[0] + m[5]*p[1] + m[9]*p[2] + m[13]) * iw;
    out[2] = (m[2]*p[0] + m[6]*p[1] + m[10]*p[2] + m[14]) * iw;
}

/** Column-major: transform point (no w). */
void Mat4TransformPoint(const float m[16], const float p[3], float out[3]) {
    out[0] = m[0]*p[0] + m[4]*p[1] + m[8]*p[2] + m[12];
    out[1] = m[1]*p[0] + m[5]*p[1] + m[9]*p[2] + m[13];
    out[2] = m[2]*p[0] + m[6]*p[1] + m[10]*p[2] + m[14];
}

/** Column-major A * B. */
void Mat4Mul(const float a[16], const float b[16], float out[16]) {
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            out[col*4+row] = a[0*4+row]*b[col*4+0] + a[1*4+row]*b[col*4+1] +
                            a[2*4+row]*b[col*4+2] + a[3*4+row]*b[col*4+3];
        }
    }
}

} // namespace

void CsmComputeSplitDepths(float cameraNear, float cameraFar, float outSplits[kCsmCascadeCount]) {
    const float ratio = cameraFar / cameraNear;
    const int N = kCsmCascadeCount;
    for (int i = 0; i < N; ++i) {
        float clip = (float)(i + 1) / (float)N;
        float logSplit = cameraNear * std::pow(ratio, clip);
        float linearSplit = cameraNear + (cameraFar - cameraNear) * clip;
        outSplits[i] = std::lerp(linearSplit, logSplit, kCsmLambda);
    }
}

void CsmFrustumSliceCorners(const float invView[16],
                            float fovY,
                            float aspect,
                            float splitNear,
                            float splitFar,
                            float outCorners[8][3]) {
    const float tanHalfFov = std::tan(fovY * 0.5f);
    const float ny = tanHalfFov * splitNear;
    const float nx = ny * aspect;
    const float fy = tanHalfFov * splitFar;
    const float fx = fy * aspect;
    float vs[8][3] = {
        {-nx, -ny, -splitNear}, { nx, -ny, -splitNear}, { nx,  ny, -splitNear}, {-nx,  ny, -splitNear},
        {-fx, -fy, -splitFar }, { fx, -fy, -splitFar }, { fx,  fy, -splitFar }, {-fx,  fy, -splitFar },
    };
    for (int i = 0; i < 8; ++i)
        Mat4TransformPoint(invView, vs[i], outCorners[i]);
}

void CsmBuildLightView(const float lightDir[3], const float center[3], float backoff, float outView[16]) {
    float eye[3] = {
        center[0] - lightDir[0] * backoff,
        center[1] - lightDir[1] * backoff,
        center[2] - lightDir[2] * backoff,
    };
    float fwd[3] = { -lightDir[0], -lightDir[1], -lightDir[2] };
    const float upW[3] = { 0.0f, 1.0f, 0.0f };
    float dot = upW[0]*fwd[0] + upW[1]*fwd[1] + upW[2]*fwd[2];
    float up[3];
    if (std::abs(dot) > 0.99f) {
        up[0] = 0.0f; up[1] = 0.0f; up[2] = 1.0f;
    } else {
        up[0] = upW[0]; up[1] = upW[1]; up[2] = upW[2];
    }
    float z[3] = { fwd[0], fwd[1], fwd[2] };
    float lenZ = std::sqrt(z[0]*z[0] + z[1]*z[1] + z[2]*z[2]);
    if (lenZ > 1e-6f) { z[0]/=lenZ; z[1]/=lenZ; z[2]/=lenZ; }
    float x[3] = {
        up[1]*z[2] - up[2]*z[1],
        up[2]*z[0] - up[0]*z[2],
        up[0]*z[1] - up[1]*z[0],
    };
    float lenX = std::sqrt(x[0]*x[0] + x[1]*x[1] + x[2]*x[2]);
    if (lenX > 1e-6f) { x[0]/=lenX; x[1]/=lenX; x[2]/=lenX; }
    float y[3] = {
        z[1]*x[2] - z[2]*x[1],
        z[2]*x[0] - z[0]*x[2],
        z[0]*x[1] - z[1]*x[0],
    };
    outView[0] = x[0]; outView[4] = y[0]; outView[8]  = z[0]; outView[12] = 0.0f;
    outView[1] = x[1]; outView[5] = y[1]; outView[9]  = z[1]; outView[13] = 0.0f;
    outView[2] = x[2]; outView[6] = y[2]; outView[10] = z[2]; outView[14] = 0.0f;
    outView[3] = 0.0f; outView[7] = 0.0f; outView[11] = 0.0f; outView[15] = 1.0f;
    outView[12] = -(x[0]*eye[0] + x[1]*eye[1] + x[2]*eye[2]);
    outView[13] = -(y[0]*eye[0] + y[1]*eye[1] + y[2]*eye[2]);
    outView[14] = -(z[0]*eye[0] + z[1]*eye[1] + z[2]*eye[2]);
}

void CsmBuildOrthoProj(float left, float right, float bottom, float top,
                       float nearZ, float farZ, float outProj[16]) {
    std::memset(outProj, 0, 16 * sizeof(float));
    outProj[0]  = 2.0f / (right - left);
    outProj[5]  = -2.0f / (top - bottom);
    outProj[10] = 1.0f / (farZ - nearZ);
    outProj[12] = -(right + left) / (right - left);
    outProj[13] = -(top + bottom) / (top - bottom);
    outProj[14] = -nearZ / (farZ - nearZ);
    outProj[15] = 1.0f;
}

void CsmStabilizeOrtho(float worldUnitsPerTexel, float minBounds[3], float maxBounds[3]) {
    if (worldUnitsPerTexel <= 0.0f) return;
    float center[3] = {
        (minBounds[0] + maxBounds[0]) * 0.5f,
        (minBounds[1] + maxBounds[1]) * 0.5f,
        (minBounds[2] + maxBounds[2]) * 0.5f,
    };
    float half[3] = {
        (maxBounds[0] - minBounds[0]) * 0.5f,
        (maxBounds[1] - minBounds[1]) * 0.5f,
        (maxBounds[2] - minBounds[2]) * 0.5f,
    };
    center[0] = std::round(center[0] / worldUnitsPerTexel) * worldUnitsPerTexel;
    center[1] = std::round(center[1] / worldUnitsPerTexel) * worldUnitsPerTexel;
    minBounds[0] = center[0] - half[0]; maxBounds[0] = center[0] + half[0];
    minBounds[1] = center[1] - half[1]; maxBounds[1] = center[1] + half[1];
}

void CsmComputeCascades(const float view[16],
                        float cameraNear,
                        float cameraFar,
                        float fovY,
                        float aspect,
                        const float lightDir[3],
                        uint32_t shadowMapSize,
                        CsmUniform& out) {
    float invView[16];
    Invert4x4(view, invView);
    float splitDepths[kCsmCascadeCount];
    CsmComputeSplitDepths(cameraNear, cameraFar, splitDepths);
    for (int i = 0; i < kCsmCascadeCount; ++i)
        out.splitDepths[i] = splitDepths[i];

    float lightView[16];
    float ortho[16];
    float lightViewProj[16];

    for (int c = 0; c < kCsmCascadeCount; ++c) {
        float splitNear = (c == 0) ? cameraNear : splitDepths[c - 1];
        float splitFar  = splitDepths[c];
        float corners[8][3];
        CsmFrustumSliceCorners(invView, fovY, aspect, splitNear, splitFar, corners);

        float center[3] = { 0.0f, 0.0f, 0.0f };
        for (int i = 0; i < 8; ++i) {
            center[0] += corners[i][0]; center[1] += corners[i][1]; center[2] += corners[i][2];
        }
        center[0] /= 8.0f; center[1] /= 8.0f; center[2] /= 8.0f;

        float diag = 0.0f;
        for (int i = 0; i < 8; ++i) {
            float dx = corners[i][0] - center[0], dy = corners[i][1] - center[1], dz = corners[i][2] - center[2];
            diag = std::max(diag, std::sqrt(dx*dx + dy*dy + dz*dz));
        }
        float backoff = diag * 2.0f + 10.0f;
        CsmBuildLightView(lightDir, center, backoff, lightView);

        float minB[3] = { 1e9f, 1e9f, 1e9f };
        float maxB[3] = { -1e9f, -1e9f, -1e9f };
        for (int i = 0; i < 8; ++i) {
            float ls[3];
            Mat4TransformPoint(lightView, corners[i], ls);
            for (int j = 0; j < 3; ++j) {
                if (ls[j] < minB[j]) minB[j] = ls[j];
                if (ls[j] > maxB[j]) maxB[j] = ls[j];
            }
        }
        float sizeX = maxB[0] - minB[0];
        float sizeY = maxB[1] - minB[1];
        float worldUnitsPerTexel = std::max(sizeX, sizeY) / (float)shadowMapSize;
        CsmStabilizeOrtho(worldUnitsPerTexel, minB, maxB);

        CsmBuildOrthoProj(minB[0], maxB[0], minB[1], maxB[1], minB[2], maxB[2], ortho);
        Mat4Mul(ortho, lightView, lightViewProj);
        std::memcpy(out.lightViewProj[c], lightViewProj, 16 * sizeof(float));
    }
}

} // namespace engine::render
