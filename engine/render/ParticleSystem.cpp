/**
 * @file ParticleSystem.cpp
 * @brief Particle pool update, spawn, sort; emitter JSON load. M17.2.
 */

#include "engine/render/ParticleSystem.h"
#include "engine/core/Log.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <random>

namespace engine::render {

namespace {

float RandFloat(float a, float b) {
    float t = static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX);
    return a + t * (b - a);
}

} // namespace

void ParticlePool::Update(float dt, const ParticleEmitterDef& def, const float spawnPosition[3]) {
    if (m_particles.size() < kMaxParticles)
        m_particles.resize(kMaxParticles);
    uint32_t write = 0;
    for (uint32_t i = 0; i < m_count; ++i) {
        Particle& p = m_particles[i];
        p.life -= dt;
        if (p.life > 0.f) {
            p.position[0] += p.velocity[0] * dt;
            p.position[1] += p.velocity[1] * dt;
            p.position[2] += p.velocity[2] * dt;
            if (write != i)
                m_particles[write] = p;
            ++write;
        }
    }
    m_count = write;

    m_spawnAccum += def.rate * dt;
    while (m_spawnAccum >= 1.f && m_count < kMaxParticles) {
        Particle p;
        p.position[0] = spawnPosition[0];
        p.position[1] = spawnPosition[1];
        p.position[2] = spawnPosition[2];
        p.velocity[0] = def.velocity[0];
        p.velocity[1] = def.velocity[1];
        p.velocity[2] = def.velocity[2];
        p.life = RandFloat(def.lifetimeMin, def.lifetimeMax);
        p.size = def.size;
        p.color[0] = def.color[0];
        p.color[1] = def.color[1];
        p.color[2] = def.color[2];
        p.color[3] = def.color[3];
        m_particles[m_count] = p;
        ++m_count;
        m_spawnAccum -= 1.f;
    }
    m_spawnAccum = std::min(m_spawnAccum, 1.f);
}

void ParticlePool::SortByDistanceToCamera(const float cameraPosition[3]) {
    if (m_count <= 1) return;
    std::sort(m_particles.begin(), m_particles.begin() + m_count,
        [&cameraPosition](const Particle& a, const Particle& b) {
            float dxa = a.position[0] - cameraPosition[0];
            float dya = a.position[1] - cameraPosition[1];
            float dza = a.position[2] - cameraPosition[2];
            float dxb = b.position[0] - cameraPosition[0];
            float dyb = b.position[1] - cameraPosition[1];
            float dzb = b.position[2] - cameraPosition[2];
            float distSqA = dxa * dxa + dya * dya + dza * dza;
            float distSqB = dxb * dxb + dyb * dyb + dzb * dzb;
            return distSqA > distSqB;
        });
}

bool LoadEmitterJson(const std::string& path, ParticleEmitterDef& out) {
    std::ifstream f(path);
    if (!f.is_open()) {
        LOG_INFO(Render, "ParticleSystem: emitter json not found at {}", path);
        return false;
    }
    try {
        nlohmann::json j = nlohmann::json::parse(f);
        out.rate = j.value("rate", 100.0);
        out.lifetimeMin = j.value("lifetimeMin", 0.3);
        out.lifetimeMax = j.value("lifetimeMax", 0.8);
        if (j.contains("velocity") && j["velocity"].is_array() && j["velocity"].size() >= 3) {
            out.velocity[0] = j["velocity"][0].get<float>();
            out.velocity[1] = j["velocity"][1].get<float>();
            out.velocity[2] = j["velocity"][2].get<float>();
        }
        out.size = j.value("size", 0.5);
        if (j.contains("color") && j["color"].is_array() && j["color"].size() >= 4) {
            out.color[0] = j["color"][0].get<float>();
            out.color[1] = j["color"][1].get<float>();
            out.color[2] = j["color"][2].get<float>();
            out.color[3] = j["color"][3].get<float>();
        }
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR(Render, "ParticleSystem: parse error at {}: {}", path, e.what());
        return false;
    }
}

} // namespace engine::render
