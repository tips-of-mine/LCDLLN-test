#pragma once

/**
 * @file Halton.h
 * @brief Halton low-discrepancy sequence (base 2/3) for TAA jitter.
 *
 * Ticket: M07.1 — TAA: jitter Halton + matrices prev/curr.
 */

#include <cstdint>

namespace engine::render {

/** Number of Halton samples in the cycle (N=8..16). */
constexpr uint32_t kHaltonSequenceSize = 16u;

/**
 * @brief Returns the Halton value for given base and index (radical inverse).
 *
 * @param base  Prime base (2 or 3).
 * @param index Sample index in [0, kHaltonSequenceSize). Uses index+1 internally so output is in (0, 1).
 * @return      Value in (0, 1).
 */
float Halton(uint32_t base, uint32_t index);

/**
 * @brief Returns 2D Halton sample (base 2 for x, base 3 for y).
 *
 * @param index Sample index in [0, kHaltonSequenceSize).
 * @param outX  Output x in (0, 1).
 * @param outY  Output y in (0, 1).
 */
void Halton2D(uint32_t index, float& outX, float& outY);

} // namespace engine::render
