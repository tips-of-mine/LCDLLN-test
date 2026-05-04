#pragma once

#include <cstdint>
#include <string>
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

    /// Decode un fichier .texr (magic TEXR + RGBA8) ou un PNG/JPG (via stb_image).
    /// Le format .texr est defini dans engine/render/AssetRegistry.cpp :
    ///   bytes 0..3 : magic 'TEXR' (0x52584554 LE)
    ///   bytes 4..7 : width (uint32 LE)
    ///   bytes 8..11: height (uint32 LE)
    ///   bytes 12..15: sRGB flag (uint32 LE, 0 = lineaire, !=0 = sRGB)
    ///   bytes 16.. : width*height*4 octets RGBA8
    ///
    /// \param absolutePath Chemin absolu sur disque (string UTF-8). Le caller
    ///   est responsable de resoudre les chemins content-relatifs en absolus
    ///   via Config::ResolveContentPath.
    /// \param outRgba Buffer de sortie (width * height * 4 octets RGBA8).
    /// \param outWidth Largeur du buffer decode.
    /// \param outHeight Hauteur du buffer decode.
    /// \return true si succes. false si fichier introuvable, magic invalide,
    ///   buffer trop petit, ou erreur stb_image. LOG_ERROR emis. outRgba/outWidth/outHeight remis a zero.
    bool LoadTexrFile(const std::string& absolutePath,
                      std::vector<uint8_t>& outRgba,
                      uint32_t& outWidth, uint32_t& outHeight);
} // namespace engine::editor
