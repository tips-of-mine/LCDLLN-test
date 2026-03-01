#pragma once

/**
 * @file ParticleSystem.h
 * @brief Particle component: emitter params, CPU pool, update. M17.2.
 */

#include <array>
#include <cstdint>
#include <string>

namespace engine::render {

/** @brief Emitter parameters: rate (per second), lifetime, velocity (data-driven). */
struct ParticleEmitterDef {
    float rate = 100.f;
    float lifetimeMin = 0.3f;
    float lifetimeMax = 0.8f;
    float velocity[3] = {0.f, 5.f, 0.f};
    float size = 0.5f;
    float color[4] = {1.f, 0.6f, 0.1f, 0.9f};
};

/** @brief Single particle state (CPU pool entry). */
struct Particle {
    float position[3] = {0.f, 0.f, 0.f};
    float velocity[3] = {0.f, 0.f, 0.f};
    float life = 0.f;
    float size = 0.5f;
    float color[4] = {1.f, 1.f, 1.f, 1.f};
};

/** @brief Maximum number of active particles in one pool. */
constexpr uint32_t kMaxParticles = 4096u;

/**
 * @brief CPU-side particle pool: fixed array, spawn from emitter params, update (life, position).
 * Optional: sort by distance (squared) for back-to-front draw.
 */
class ParticlePool {
public:
    ParticlePool() = default;

    /** @brief Updates all particles (life -= dt, position += velocity * dt); removes dead; spawns new from def at origin. */
    void Update(float dt, const ParticleEmitterDef& def, const float spawnPosition[3]);

    /** @brief Returns pointer to live particles (count = GetCount()). Order may be back-to-front if SortByDistanceToCamera was called. */
    const Particle* GetParticles() const { return m_particles.data(); }
    /** @brief Non-const pointer for upload to GPU (same order as GetParticles()). */
    Particle* GetParticlesWrite() { return m_particles.data(); }
    uint32_t GetCount() const { return m_count; }

    /** @brief Optional: sort particles by distance to camera (squared) back-to-front for correct transparency. */
    void SortByDistanceToCamera(const float cameraPosition[3]);

    /** @brief Resets pool (e.g. for one-shot explosion). */
    void Reset() { m_count = 0; }

private:
    std::vector<Particle> m_particles;
    uint32_t m_count = 0;
    float m_spawnAccum = 0.f;
};

/**
 * @brief Loads emitter definition from JSON. Path relative to content root.
 * @param path Full path (contentRoot + "/" + relativePath).
 * @param out Filled on success.
 * @return true if parsed successfully.
 */
bool LoadEmitterJson(const std::string& path, ParticleEmitterDef& out);

} // namespace engine::render
