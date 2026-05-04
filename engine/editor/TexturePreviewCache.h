#pragma once

#include <cstdint>
#include <vector>

namespace engine::editor
{
    /// Resample un buffer RGBA8 a une nouvelle resolution carree par box filter
    /// separable (passe horizontale + verticale). Si le source n'est pas carre,
    /// crop centre vers carre avant resample (pas de stretch, pas de letterbox).
    /// \param src Buffer source RGBA8, srcW * srcH * 4 octets.
    /// \param srcW Largeur source en pixels (>=1).
    /// \param srcH Hauteur source en pixels (>=1).
    /// \param dstSize Cote du carre de sortie en pixels (>=4, <=4096).
    /// \param outRgba Sortie : dstSize * dstSize * 4 octets.
    /// \return true si succes ; false sur params invalides.
    bool ResampleRgba8Box(const uint8_t* src, uint32_t srcW, uint32_t srcH,
                          uint32_t dstSize, std::vector<uint8_t>& outRgba);
} // namespace engine::editor
