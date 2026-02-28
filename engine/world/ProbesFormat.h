#pragma once

/**
 * @file ProbesFormat.h
 * @brief Binary format for IBL probes (probes.bin) and zone atmosphere (M11.4).
 *
 * MVP: one global zone probe + extents; runtime loads probes so lighting can use global probe or fallback sky.
 */

#include <cstdint>
#include <string>
#include <vector>

namespace engine::world {

/** @brief probes.bin file magic "PROB". */
constexpr uint32_t kProbesBinMagic = 0x424F5250u;

/** @brief probes.bin format version. */
constexpr uint32_t kProbesBinVersion = 1u;

/**
 * @brief One IBL probe: position, radius, optional intensity (params).
 */
struct ZoneProbe {
    float position[3] = { 0.f, 0.f, 0.f };
    float radius = 1000.f;
    float intensity = 1.f;
};

/**
 * @brief Reads probes.bin from path. Returns true on success.
 */
bool ReadProbesBin(const std::string& path, std::vector<ZoneProbe>& out);

/**
 * @brief Minimal zone atmosphere (sky/horizon). Optional runtime use.
 */
struct ZoneAtmosphere {
    float skyColor[3] = { 0.53f, 0.81f, 1.0f };
    float horizonExponent = 1.f;
};

/**
 * @brief Reads zone_atmosphere.json from path. Returns true on success.
 */
bool ReadZoneAtmosphere(const std::string& path, ZoneAtmosphere& out);

} // namespace engine::world
