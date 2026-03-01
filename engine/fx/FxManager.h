#pragma once

/**
 * @file FxManager.h
 * @brief FX manager: spawn particle/decal/sound from definitions; event->FX mapping; client-side only (M17.1).
 */

#include "engine/fx/FxDef.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace engine::fx {

/** @brief One spawn request (for testing / future playback). Deterministic enough. */
struct FxSpawnRequest {
    std::string fxId;
    FxType type = FxType::Particle;
    float position[3] = {0.f, 0.f, 0.f};
    float scale = 1.f;
    float duration = 1.f;
};

/**
 * @brief Manages FX definitions and event->FX mapping. Load fx.json; spawn by id; trigger from events.
 * Do not run on server (FX is client-side only).
 */
class FxManager {
public:
    FxManager() = default;

    /**
     * @brief Loads definitions and mapping from contentRoot/relativePath (e.g. "fx/fx.json").
     * @param contentRoot Content root (paths.content).
     * @param relativePath Relative path to fx.json.
     * @return true if loaded (or file missing with fallback).
     */
    bool Load(const std::string& contentRoot, const std::string& relativePath);

    /** @brief Spawns particle FX by id; position and overrides optional. */
    void SpawnParticle(const std::string& fxId, float x, float y, float z, float scale = -1.f, float duration = -1.f);

    /** @brief Spawns decal FX by id. */
    void SpawnDecal(const std::string& fxId, float x, float y, float z, float scale = -1.f, float duration = -1.f);

    /** @brief Spawns sound FX by id. */
    void SpawnSound(const std::string& fxId, float x, float y, float z, float volume = -1.f);

    /**
     * @brief Called when CombatEvent is received (client-side). Looks up "CombatEvent" -> "hit" or "kill" and spawns configured FX.
     * @param targetDead true for kill, false for hit.
     * @param positionXyz Optional position [3] for spawn; can be nullptr to use (0,0,0).
     */
    void OnCombatEvent(bool targetDead, const float* positionXyz = nullptr);

    /** @brief Returns the last N spawn requests (for testing). */
    const std::vector<FxSpawnRequest>& GetLastSpawnRequests() const { return m_lastSpawns; }

    /** @brief Clears last spawn list. */
    void ClearLastSpawns() { m_lastSpawns.clear(); }

private:
    void Spawn(const std::string& fxId, FxType type, float x, float y, float z, float scaleOrVolume, float duration, bool isSound);
    const FxDefinition* GetDef(const std::string& id) const;

    std::unordered_map<std::string, FxDefinition> m_definitionsById;
    EventFxMapping m_mapping;
    std::vector<FxSpawnRequest> m_lastSpawns;
};

} // namespace engine::fx
