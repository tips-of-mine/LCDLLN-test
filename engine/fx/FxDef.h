#pragma once

/**
 * @file FxDef.h
 * @brief FX definitions and event->FX mapping schema (fx.json). M17.1.
 */

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace engine::fx {

/** @brief FX type: particle, decal, sound. */
enum class FxType : uint8_t {
    Particle = 0,
    Decal = 1,
    Sound = 2,
};

/** @brief One FX definition: id, type, optional asset path, scale, duration (deterministic enough). */
struct FxDefinition {
    std::string id;
    FxType type = FxType::Particle;
    std::string asset;
    float scale = 1.f;
    float duration = 1.f;
};

/**
 * @brief Event->FX mapping: eventType (e.g. "CombatEvent") -> subtype (e.g. "hit", "kill") -> fx id.
 * Loaded from fx.json "mapping" section.
 */
using EventFxMapping = std::map<std::string, std::map<std::string, std::string>>;

/**
 * @brief Loads FX definitions and event->FX mapping from a single JSON file (schema fx.json).
 * Path is content-relative; caller resolves via contentRoot + relativePath.
 * @param path Full path to fx.json (e.g. contentRoot + "/fx/fx.json").
 * @param outDefs Filled with "definitions" array entries.
 * @param outMapping Filled with "mapping" object (eventType -> subtype -> fxId).
 * @return true if file read and parsed; false on missing file or parse error (outDefs/outMapping may be partial).
 */
bool LoadFxJson(const std::string& path, std::vector<FxDefinition>& outDefs, EventFxMapping& outMapping);

} // namespace engine::fx
