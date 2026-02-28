/**
 * @file Frustum.cpp
 * @brief Frustum plane extraction and AABB test.
 */

#include "engine/math/Frustum.h"

#include <cmath>

namespace engine::math {

namespace {

float length3(float x, float y, float z) {
    return std::sqrt(x * x + y * y + z * z);
}

void normalize(Plane& p) {
    const float len = length3(p.nx, p.ny, p.nz);
    if (len > 1e-6f) {
        const float inv = 1.0f / len;
        p.nx *= inv;
        p.ny *= inv;
        p.nz *= inv;
        p.d  *= inv;
    }
}

} // namespace

void ExtractFromMatrix(const float viewProj[16], Frustum& out) {
    const float* m = viewProj;

    out.planes[0].nx = m[3] + m[0];
    out.planes[0].ny = m[7] + m[4];
    out.planes[0].nz = m[11] + m[8];
    out.planes[0].d  = m[15] + m[12];
    normalize(out.planes[0]);

    out.planes[1].nx = m[3] - m[0];
    out.planes[1].ny = m[7] - m[4];
    out.planes[1].nz = m[11] - m[8];
    out.planes[1].d  = m[15] - m[12];
    normalize(out.planes[1]);

    out.planes[2].nx = m[3] - m[1];
    out.planes[2].ny = m[7] - m[5];
    out.planes[2].nz = m[11] - m[9];
    out.planes[2].d  = m[15] - m[13];
    normalize(out.planes[2]);

    out.planes[3].nx = m[3] + m[1];
    out.planes[3].ny = m[7] + m[5];
    out.planes[3].nz = m[11] + m[9];
    out.planes[3].d  = m[15] + m[13];
    normalize(out.planes[3]);

    out.planes[4].nx = m[3] + m[2];
    out.planes[4].ny = m[7] + m[6];
    out.planes[4].nz = m[11] + m[10];
    out.planes[4].d  = m[15] + m[14];
    normalize(out.planes[4]);

    out.planes[5].nx = m[3] - m[2];
    out.planes[5].ny = m[7] - m[6];
    out.planes[5].nz = m[11] - m[10];
    out.planes[5].d  = m[15] - m[14];
    normalize(out.planes[5]);
}

bool TestAABB(const Frustum& f, const float min[3], const float max[3]) {
    for (const Plane& p : f.planes) {
        float nx = p.nx, ny = p.ny, nz = p.nz, d = p.d;
        const float px = (nx >= 0.0f) ? max[0] : min[0];
        const float py = (ny >= 0.0f) ? max[1] : min[1];
        const float pz = (nz >= 0.0f) ? max[2] : min[2];
        if (nx * px + ny * py + nz * pz + d < 0.0f) {
            return false;
        }
    }
    return true;
}

} // namespace engine::math
