/**
 * @file Ray.cpp
 * @brief Ray casting: screen to ray and ray-AABB intersection (M12.1).
 */

#include "Ray.h"

#include <cstring>

namespace engine::math {

namespace {

/** @brief 4x4 matrix multiply C = A * B (column-major). */
void Mat4Mul(const float a[16], const float b[16], float c[16]) {
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            c[col * 4 + row] = a[0 * 4 + row] * b[col * 4 + 0] +
                               a[1 * 4 + row] * b[col * 4 + 1] +
                               a[2 * 4 + row] * b[col * 4 + 2] +
                               a[3 * 4 + row] * b[col * 4 + 3];
        }
    }
}

/** @brief 4x4 matrix inverse (column-major). Returns false if singular. */
bool Mat4Inverse(const float m[16], float out[16]) {
    float inv[16];
    inv[0] = m[5]*m[10]*m[15] - m[5]*m[11]*m[14] - m[9]*m[6]*m[15] + m[9]*m[7]*m[14] + m[13]*m[6]*m[11] - m[13]*m[7]*m[10];
    inv[4] = -m[4]*m[10]*m[15] + m[4]*m[11]*m[14] + m[8]*m[6]*m[15] - m[8]*m[7]*m[14] - m[12]*m[6]*m[11] + m[12]*m[7]*m[10];
    inv[8] = m[4]*m[9]*m[15] - m[4]*m[11]*m[13] - m[8]*m[5]*m[15] + m[8]*m[7]*m[13] + m[12]*m[5]*m[11] - m[12]*m[7]*m[9];
    inv[12] = -m[4]*m[9]*m[14] + m[4]*m[10]*m[13] + m[8]*m[5]*m[14] - m[8]*m[6]*m[13] - m[12]*m[5]*m[10] + m[12]*m[6]*m[9];
    inv[1] = -m[1]*m[10]*m[15] + m[1]*m[11]*m[14] + m[9]*m[2]*m[15] - m[9]*m[3]*m[14] - m[13]*m[2]*m[11] + m[13]*m[3]*m[10];
    inv[5] = m[0]*m[10]*m[15] - m[0]*m[11]*m[14] - m[8]*m[2]*m[15] + m[8]*m[3]*m[14] + m[12]*m[2]*m[11] - m[12]*m[3]*m[10];
    inv[9] = -m[0]*m[9]*m[15] + m[0]*m[11]*m[13] + m[8]*m[1]*m[15] - m[8]*m[3]*m[13] - m[12]*m[1]*m[11] + m[12]*m[3]*m[9];
    inv[13] = m[0]*m[9]*m[14] - m[0]*m[10]*m[13] - m[8]*m[1]*m[14] + m[8]*m[2]*m[13] + m[12]*m[1]*m[10] - m[12]*m[2]*m[9];
    inv[2] = m[1]*m[6]*m[15] - m[1]*m[7]*m[14] - m[5]*m[2]*m[15] + m[5]*m[3]*m[14] + m[13]*m[2]*m[7] - m[13]*m[3]*m[6];
    inv[6] = -m[0]*m[6]*m[15] + m[0]*m[7]*m[14] + m[4]*m[2]*m[15] - m[4]*m[3]*m[14] - m[12]*m[2]*m[7] + m[12]*m[3]*m[6];
    inv[10] = m[0]*m[5]*m[15] - m[0]*m[7]*m[13] - m[4]*m[1]*m[15] + m[4]*m[3]*m[13] + m[12]*m[1]*m[7] - m[12]*m[3]*m[5];
    inv[14] = -m[0]*m[5]*m[14] + m[0]*m[6]*m[13] + m[4]*m[1]*m[14] - m[4]*m[2]*m[13] - m[12]*m[1]*m[6] + m[12]*m[2]*m[5];
    inv[3] = -m[1]*m[6]*m[11] + m[1]*m[7]*m[10] + m[5]*m[2]*m[11] - m[5]*m[3]*m[10] - m[9]*m[2]*m[7] + m[9]*m[3]*m[6];
    inv[7] = m[0]*m[6]*m[11] - m[0]*m[7]*m[10] - m[4]*m[2]*m[11] + m[4]*m[3]*m[10] + m[8]*m[2]*m[7] - m[8]*m[3]*m[6];
    inv[11] = -m[0]*m[5]*m[11] + m[0]*m[7]*m[9] + m[4]*m[1]*m[11] - m[4]*m[3]*m[9] - m[8]*m[1]*m[7] + m[8]*m[3]*m[5];
    inv[15] = m[0]*m[5]*m[10] - m[0]*m[6]*m[9] - m[4]*m[1]*m[10] + m[4]*m[2]*m[9] + m[8]*m[1]*m[6] - m[8]*m[2]*m[5];
    float det = m[0]*inv[0] + m[1]*inv[4] + m[2]*inv[8] + m[3]*inv[12];
    if (det < 1e-10f && det > -1e-10f)
        return false;
    det = 1.0f / det;
    for (int i = 0; i < 16; ++i)
        out[i] = inv[i] * det;
    return true;
}

/** @brief Transform point by 4x4 (column-major); result in out[3]. */
void Mat4TransformPoint(const float m[16], const float p[3], float out[3]) {
    float w = m[3]*p[0] + m[7]*p[1] + m[11]*p[2] + m[15];
    if (w < 1e-10f && w > -1e-10f) w = 1.0f;
    float iw = 1.0f / w;
    out[0] = (m[0]*p[0] + m[4]*p[1] + m[8]*p[2] + m[12]) * iw;
    out[1] = (m[1]*p[0] + m[5]*p[1] + m[9]*p[2] + m[13]) * iw;
    out[2] = (m[2]*p[0] + m[6]*p[1] + m[10]*p[2] + m[14]) * iw;
}
} // namespace

void ScreenPointToRay(float screenX, float screenY,
                     float viewportW, float viewportH,
                     const float viewColMajor[16],
                     const float projColMajor[16],
                     float outOrigin[3], float outDir[3]) {
    float ndcX = (screenX / viewportW) * 2.0f - 1.0f;
    float ndcY = 1.0f - (screenY / viewportH) * 2.0f;
    float viewProj[16];
    Mat4Mul(viewColMajor, projColMajor, viewProj);
    float invViewProj[16];
    if (!Mat4Inverse(viewProj, invViewProj)) {
        outOrigin[0] = outOrigin[1] = outOrigin[2] = 0.0f;
        outDir[0] = 0.0f; outDir[1] = 0.0f; outDir[2] = -1.0f;
        return;
    }
    float nearPt[3] = { ndcX, ndcY, 0.0f };
    float farPt[3]  = { ndcX, ndcY, 1.0f };
    float nearWorld[3], farWorld[3];
    Mat4TransformPoint(invViewProj, nearPt, nearWorld);
    Mat4TransformPoint(invViewProj, farPt, farWorld);
    outOrigin[0] = nearWorld[0]; outOrigin[1] = nearWorld[1]; outOrigin[2] = nearWorld[2];
    outDir[0] = farWorld[0] - nearWorld[0];
    outDir[1] = farWorld[1] - nearWorld[1];
    outDir[2] = farWorld[2] - nearWorld[2];
}

bool RayAABB(const float origin[3], const float dir[3],
            const float aabbMin[3], const float aabbMax[3],
            float* outT) {
    float tMin = -1e30f;
    float tMax = 1e30f;
    for (int i = 0; i < 3; ++i) {
        if (dir[i] > 1e-10f || dir[i] < -1e-10f) {
            float t1 = (aabbMin[i] - origin[i]) / dir[i];
            float t2 = (aabbMax[i] - origin[i]) / dir[i];
            if (t1 > t2) { float t = t1; t1 = t2; t2 = t; }
            if (t1 > tMin) tMin = t1;
            if (t2 < tMax) tMax = t2;
        } else {
            if (origin[i] < aabbMin[i] || origin[i] > aabbMax[i])
                return false;
        }
    }
    if (tMin > tMax || tMax < 0.0f)
        return false;
    float t = (tMin >= 0.0f) ? tMin : tMax;
    if (t < 0.0f)
        return false;
    *outT = t;
    return true;
}

} // namespace engine::math
