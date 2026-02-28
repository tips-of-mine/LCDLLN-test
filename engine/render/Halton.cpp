/**
 * @file Halton.cpp
 * @brief Halton (2/3) sequence for subpixel jitter.
 *
 * Ticket: M07.1 — TAA: jitter Halton + matrices prev/curr.
 */

#include "engine/render/Halton.h"

namespace engine::render {

float Halton(uint32_t base, uint32_t index) {
    if (base == 0u) return 0.0f;
    uint32_t n = index + 1u;
    float result = 0.0f;
    float invBase = 1.0f / static_cast<float>(base);
    float f = invBase;
    while (n > 0u) {
        result += static_cast<float>(n % base) * f;
        n /= base;
        f *= invBase;
    }
    return result;
}

void Halton2D(uint32_t index, float& outX, float& outY) {
    outX = Halton(2u, index);
    outY = Halton(3u, index);
}

} // namespace engine::render
