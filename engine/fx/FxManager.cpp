/**
 * @file FxManager.cpp
 * @brief FX manager: load definitions/mapping, spawn by id, OnCombatEvent (M17.1).
 */

#include "engine/fx/FxManager.h"

namespace engine::fx {

bool FxManager::Load(const std::string& contentRoot, const std::string& relativePath) {
    std::string path = contentRoot.empty() ? relativePath : (contentRoot + "/" + relativePath);
    std::vector<FxDefinition> defs;
    EventFxMapping mapping;
    if (!LoadFxJson(path, defs, mapping)) {
        m_definitionsById.clear();
        m_mapping.clear();
        return false;
    }
    m_definitionsById.clear();
    for (const auto& d : defs)
        m_definitionsById[d.id] = d;
    m_mapping = std::move(mapping);
    return true;
}

const FxDefinition* FxManager::GetDef(const std::string& id) const {
    auto it = m_definitionsById.find(id);
    return it != m_definitionsById.end() ? &it->second : nullptr;
}

void FxManager::Spawn(const std::string& fxId, FxType type, float x, float y, float z,
                      float scaleOrVolume, float duration, bool isSound) {
    const FxDefinition* def = GetDef(fxId);
    float scale = (scaleOrVolume >= 0.f) ? scaleOrVolume : (def ? def->scale : 1.f);
    float dur = (duration >= 0.f) ? duration : (def ? def->duration : 1.f);
    FxSpawnRequest req;
    req.fxId = fxId;
    req.type = type;
    req.position[0] = x;
    req.position[1] = y;
    req.position[2] = z;
    req.scale = scale;
    req.duration = dur;
    m_lastSpawns.push_back(req);
}

void FxManager::SpawnParticle(const std::string& fxId, float x, float y, float z, float scale, float duration) {
    if (fxId.empty()) return;
    if (!GetDef(fxId)) return;
    Spawn(fxId, FxType::Particle, x, y, z, scale, duration, false);
}

void FxManager::SpawnDecal(const std::string& fxId, float x, float y, float z, float scale, float duration) {
    if (fxId.empty()) return;
    if (!GetDef(fxId)) return;
    Spawn(fxId, FxType::Decal, x, y, z, scale, duration, false);
}

void FxManager::SpawnSound(const std::string& fxId, float x, float y, float z, float volume) {
    if (fxId.empty()) return;
    if (!GetDef(fxId)) return;
    Spawn(fxId, FxType::Sound, x, y, z, volume >= 0.f ? volume : 1.f, 0.f, true);
}

void FxManager::OnCombatEvent(bool targetDead, const float* positionXyz) {
    auto it = m_mapping.find("CombatEvent");
    if (it == m_mapping.end()) return;
    const std::string subtype = targetDead ? "kill" : "hit";
    auto it2 = it->second.find(subtype);
    if (it2 == it->second.end()) return;
    const std::string& fxId = it2->second;
    if (fxId.empty()) return;
    const FxDefinition* def = GetDef(fxId);
    if (!def) return;
    float x = 0.f, y = 0.f, z = 0.f;
    if (positionXyz) {
        x = positionXyz[0];
        y = positionXyz[1];
        z = positionXyz[2];
    }
    switch (def->type) {
        case FxType::Particle:
            SpawnParticle(fxId, x, y, z, def->scale, def->duration);
            break;
        case FxType::Decal:
            SpawnDecal(fxId, x, y, z, def->scale, def->duration);
            break;
        case FxType::Sound:
            SpawnSound(fxId, x, y, z, 1.f);
            break;
    }
}

} // namespace engine::fx
