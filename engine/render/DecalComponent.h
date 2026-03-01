#pragma once

/**
 * @file DecalComponent.h
 * @brief Decal component: volume (cube), material (albedo), lifetime + fade. M17.3.
 */

#include <cstdint>
#include <vector>

namespace engine::render {

/** @brief Single decal instance: axis-aligned volume (position + halfExtents), lifetime and fade. */
struct DecalInstance {
    float position[3] = {0.f, 0.f, 0.f};
    float halfExtents[3] = {0.5f, 0.5f, 0.5f};
    float lifetime = 5.f;
    float lifetimeMax = 5.f;
};

/**
 * @brief Decal list: add decals, update (lifetime -= dt, remove expired), optional distance cull.
 * Used by the decal pass to draw decals into GBufferA.
 */
class DecalList {
public:
    DecalList() = default;

    /** @brief Adds a decal at world position with half-extents and lifetime in seconds. */
    void Add(const float position[3], const float halfExtents[3], float lifetimeSeconds);

    /** @brief Updates all decals (lifetime -= dt), removes expired. Optionally culls by distance to camera. */
    void Update(float dt, const float cameraPosition[3] = nullptr, float cullDistance = -1.f);

    /** @brief Returns decals to render (valid until next Update). */
    const std::vector<DecalInstance>& GetDecals() const { return m_decals; }

private:
    std::vector<DecalInstance> m_decals;
};

} // namespace engine::render
