#include "engine/editor/TexturePreviewCache.h"

#include <algorithm>
#include <cstring>

namespace engine::editor
{
    namespace
    {
        /// Crop centre rectangulaire vers carre. Renvoie le pointeur vers le
        /// pixel (0,0) du sous-rectangle carre + son cote.
        void CropToSquare(const uint8_t* src, uint32_t srcW, uint32_t srcH,
                          const uint8_t*& outSrc, uint32_t& outSize, uint32_t& outRowStrideBytes)
        {
            outRowStrideBytes = srcW * 4u;
            if (srcW == srcH)
            {
                outSrc = src;
                outSize = srcW;
                return;
            }
            const uint32_t side = std::min(srcW, srcH);
            const uint32_t offX = (srcW - side) / 2u;
            const uint32_t offY = (srcH - side) / 2u;
            outSrc = src + (offY * srcW + offX) * 4u;
            outSize = side;
        }
    } // namespace

    bool ResampleRgba8Box(const uint8_t* src, uint32_t srcW, uint32_t srcH,
                          uint32_t dstSize, std::vector<uint8_t>& outRgba)
    {
        if (src == nullptr || srcW == 0 || srcH == 0 || dstSize < 4u || dstSize > 4096u)
        {
            outRgba.clear();
            return false;
        }

        const uint8_t* sqSrc = nullptr;
        uint32_t sqSize = 0;
        uint32_t srcStride = 0;
        CropToSquare(src, srcW, srcH, sqSrc, sqSize, srcStride);

        outRgba.assign(static_cast<size_t>(dstSize) * dstSize * 4u, 0u);

        // Box filter direct (pas de mipmap chain) : pour chaque pixel dst,
        // moyenner tous les pixels src couverts. Cout O(dstSize^2 * (sqSize/dstSize)^2).
        // Pour 1024->256 : 256^2 * 16 = ~1M reads, ~3ms en debug, <1ms en release.
        for (uint32_t dy = 0; dy < dstSize; ++dy)
        {
            const uint32_t y0 = (dy * sqSize) / dstSize;
            const uint32_t y1 = ((dy + 1u) * sqSize) / dstSize;
            const uint32_t ySpan = std::max(1u, y1 - y0);
            for (uint32_t dx = 0; dx < dstSize; ++dx)
            {
                const uint32_t x0 = (dx * sqSize) / dstSize;
                const uint32_t x1 = ((dx + 1u) * sqSize) / dstSize;
                const uint32_t xSpan = std::max(1u, x1 - x0);
                uint32_t accR = 0, accG = 0, accB = 0, accA = 0;
                const uint32_t count = xSpan * ySpan;
                for (uint32_t sy = y0; sy < y0 + ySpan; ++sy)
                {
                    const uint8_t* row = sqSrc + sy * srcStride;
                    for (uint32_t sx = x0; sx < x0 + xSpan; ++sx)
                    {
                        const uint8_t* p = row + sx * 4u;
                        accR += p[0]; accG += p[1]; accB += p[2]; accA += p[3];
                    }
                }
                uint8_t* dst = outRgba.data() + (dy * dstSize + dx) * 4u;
                dst[0] = static_cast<uint8_t>(accR / count);
                dst[1] = static_cast<uint8_t>(accG / count);
                dst[2] = static_cast<uint8_t>(accB / count);
                dst[3] = static_cast<uint8_t>(accA / count);
            }
        }
        return true;
    }
} // namespace engine::editor
