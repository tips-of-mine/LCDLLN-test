/**
 * @file DecalComponent.cpp
 * @brief Decal list: add, update (lifetime, distance cull). M17.3.
 */

#include "engine/render/DecalComponent.h"

#include <cmath>

namespace engine::render {

void DecalList::Add(const float position[3], const float halfExtents[3], float lifetimeSeconds) {
    DecalInstance d;
    d.position[0] = position[0];
    d.position[1] = position[1];
    d.position[2] = position[2];
    d.halfExtents[0] = halfExtents[0];
    d.halfExtents[1] = halfExtents[1];
    d.halfExtents[2] = halfExtents[2];
    d.lifetime = lifetimeSeconds;
    d.lifetimeMax = lifetimeSeconds;
    m_decals.push_back(d);
}

void DecalList::Update(float dt, const float cameraPosition[3], float cullDistance) {
    for (size_t i = 0; i < m_decals.size(); ) {
        DecalInstance& d = m_decals[i];
        d.lifetime -= dt;
        if (d.lifetime <= 0.f) {
            if (i + 1 < m_decals.size())
                d = m_decals.back();
            m_decals.pop_back();
            continue;
        }
        if (cameraPosition != nullptr && cullDistance > 0.f) {
            float dx = d.position[0] - cameraPosition[0];
            float dy = d.position[1] - cameraPosition[1];
            float dz = d.position[2] - cameraPosition[2];
            float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
            if (dist > cullDistance) {
                if (i + 1 < m_decals.size())
                    d = m_decals.back();
                m_decals.pop_back();
                continue;
            }
        }
        ++i;
    }
}

} // namespace engine::render
