#pragma once

/**
 * @file DrawItem.h
 * @brief GPU draw item: mesh/material/transform/bounds for indirect culling (M18.2).
 */

#include <cstdint>

namespace engine::world {
struct ZoneChunkInstance;
}

namespace engine::render {

/**
 * @brief Draw item struct matching GPU layout for compute frustum culling.
 *
 * Layout: meshId (uint), materialId (uint), transform[16] (column-major),
 * boundsMin[3], boundsMax[3]. Padding for vec4 alignment.
 */
struct DrawItemGpu {
    uint32_t meshId    = 0u;
    uint32_t materialId = 0u;
    float    transform[16] = {};
    float    boundsMin[3]  = {0.f, 0.f, 0.f};
    float    boundsMax[3]  = {0.f, 0.f, 0.f};
};

/** @brief Size in bytes of DrawItemGpu for buffer strides. */
constexpr size_t kDrawItemGpuSize = sizeof(DrawItemGpu);

/**
 * @brief Builds a GPU draw item from a zone chunk instance.
 *
 * Derives AABB in world space from transform (unit box -1..1 per axis).
 *
 * @param inst  Zone chunk instance (transform, assetId, flags).
 * @param out   Output draw item to fill.
 */
void BuildDrawItemFromInstance(const ::engine::world::ZoneChunkInstance& inst, DrawItemGpu& out);

} // namespace engine::render
