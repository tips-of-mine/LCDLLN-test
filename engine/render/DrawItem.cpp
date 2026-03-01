/**
 * @file DrawItem.cpp
 * @brief Build DrawItem from zone instance; derive AABB from transform (M18.2).
 */

#include "engine/render/DrawItem.h"
#include "engine/world/ZoneBuildFormat.h"

#include <cstring>
#include <algorithm>

namespace engine::render {

namespace {

void transformPoint(const float m[16], float x, float y, float z, float out[3]) {
    out[0] = m[0] * x + m[4] * y + m[8] * z + m[12];
    out[1] = m[1] * x + m[5] * y + m[9] * z + m[13];
    out[2] = m[2] * x + m[6] * y + m[10] * z + m[14];
}

} // namespace

void BuildDrawItemFromInstance(const ::engine::world::ZoneChunkInstance& inst, DrawItemGpu& out) {
    out.meshId     = 0u;
    out.materialId = inst.assetId;
    std::memcpy(out.transform, inst.transform, sizeof(out.transform));

    /* Unit box -1..1 per axis; transform corners and take AABB. */
    float corners[8][3];
    const float sgn[8][3] = {
        {-1.f, -1.f, -1.f}, { 1.f, -1.f, -1.f}, {-1.f,  1.f, -1.f}, { 1.f,  1.f, -1.f},
        {-1.f, -1.f,  1.f}, { 1.f, -1.f,  1.f}, {-1.f,  1.f,  1.f}, { 1.f,  1.f,  1.f},
    };
    for (int i = 0; i < 8; ++i)
        transformPoint(inst.transform, sgn[i][0], sgn[i][1], sgn[i][2], corners[i]);

    out.boundsMin[0] = out.boundsMax[0] = corners[0][0];
    out.boundsMin[1] = out.boundsMax[1] = corners[0][1];
    out.boundsMin[2] = out.boundsMax[2] = corners[0][2];
    for (int i = 1; i < 8; ++i) {
        out.boundsMin[0] = std::min(out.boundsMin[0], corners[i][0]);
        out.boundsMin[1] = std::min(out.boundsMin[1], corners[i][1]);
        out.boundsMin[2] = std::min(out.boundsMin[2], corners[i][2]);
        out.boundsMax[0] = std::max(out.boundsMax[0], corners[i][0]);
        out.boundsMax[1] = std::max(out.boundsMax[1], corners[i][1]);
        out.boundsMax[2] = std::max(out.boundsMax[2], corners[i][2]);
    }
}

} // namespace engine::render
