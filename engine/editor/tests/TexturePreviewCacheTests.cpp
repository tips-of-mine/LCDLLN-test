/// Tests unitaires pour les fonctions CPU pures de TexturePreviewCache :
/// - ResampleRgba8Box (crop + box filter)
/// - GenerateProceduralAlbedoLayer (deterministe)
/// Pas de Vulkan ici : ces tests doivent tourner en CI sans GPU.

#include "engine/editor/TexturePreviewCache.h"
#include "engine/render/terrain/TerrainSplatting.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

namespace
{
    int g_failed = 0;

    #define REQUIRE(cond) do { \
        if (!(cond)) { \
            std::fprintf(stderr, "[FAIL] %s:%d  %s\n", __FILE__, __LINE__, #cond); \
            ++g_failed; \
        } \
    } while (0)

    /// 256x256 rouge uni -> 64x64 rouge uni (preserve la couleur).
    void Test_ResampleDownsampleSquare()
    {
        std::vector<uint8_t> red(256u * 256u * 4u);
        for (size_t p = 0; p < 256u * 256u; ++p)
        {
            red[p * 4u + 0] = 200u;
            red[p * 4u + 1] = 50u;
            red[p * 4u + 2] = 30u;
            red[p * 4u + 3] = 255u;
        }
        std::vector<uint8_t> out;
        REQUIRE(engine::editor::ResampleRgba8Box(red.data(), 256u, 256u, 64u, out));
        REQUIRE(out.size() == 64u * 64u * 4u);
        for (size_t p = 0; p < 64u * 64u; ++p)
        {
            REQUIRE(out[p * 4u + 0] == 200u);
            REQUIRE(out[p * 4u + 1] == 50u);
            REQUIRE(out[p * 4u + 2] == 30u);
            REQUIRE(out[p * 4u + 3] == 255u);
        }
    }

    /// 1024x512 -> 256x256 : crop centre. Carre central [256..768] = rouge,
    /// hors-zone = bleu. Apres resample, attendu = rouge uniforme.
    void Test_ResampleNonSquareCropsCenter()
    {
        const uint32_t W = 1024u, H = 512u;
        std::vector<uint8_t> img(W * H * 4u);
        for (uint32_t y = 0; y < H; ++y)
        {
            for (uint32_t x = 0; x < W; ++x)
            {
                const bool inSquare = (x >= 256u && x < 768u);
                uint8_t* p = img.data() + (y * W + x) * 4u;
                if (inSquare)
                {
                    p[0] = 200u; p[1] = 50u; p[2] = 30u; p[3] = 255u;
                }
                else
                {
                    p[0] = 30u; p[1] = 60u; p[2] = 200u; p[3] = 255u;
                }
            }
        }
        std::vector<uint8_t> out;
        REQUIRE(engine::editor::ResampleRgba8Box(img.data(), W, H, 256u, out));
        for (size_t p = 0; p < 256u * 256u; ++p)
        {
            REQUIRE(out[p * 4u + 0] == 200u);
            REQUIRE(out[p * 4u + 1] == 50u);
            REQUIRE(out[p * 4u + 2] == 30u);
        }
    }

    /// GenerateProceduralAlbedoLayer doit etre deterministe.
    void Test_GenerateProceduralDeterminism()
    {
        std::vector<uint8_t> a, b, c;
        REQUIRE(engine::render::terrain::GenerateProceduralAlbedoLayer(64u, 0u, a));
        REQUIRE(engine::render::terrain::GenerateProceduralAlbedoLayer(64u, 0u, b));
        REQUIRE(engine::render::terrain::GenerateProceduralAlbedoLayer(64u, 1u, c));
        REQUIRE(a.size() == 64u * 64u * 4u);
        REQUIRE(a == b);
        REQUIRE(a != c);
    }

    void Test_GenerateProceduralInvalidParams()
    {
        std::vector<uint8_t> out;
        REQUIRE(!engine::render::terrain::GenerateProceduralAlbedoLayer(64u, 4u, out));
        REQUIRE(!engine::render::terrain::GenerateProceduralAlbedoLayer(2u, 0u, out));
        REQUIRE(!engine::render::terrain::GenerateProceduralAlbedoLayer(8192u, 0u, out));
    }

    void Test_ResampleInvalidParams()
    {
        std::vector<uint8_t> out;
        REQUIRE(!engine::editor::ResampleRgba8Box(nullptr, 64u, 64u, 32u, out));
        std::vector<uint8_t> img(64u * 64u * 4u, 0u);
        REQUIRE(!engine::editor::ResampleRgba8Box(img.data(), 0u, 64u, 32u, out));
        REQUIRE(!engine::editor::ResampleRgba8Box(img.data(), 64u, 64u, 2u, out));
        REQUIRE(!engine::editor::ResampleRgba8Box(img.data(), 64u, 64u, 8192u, out));
    }

    /// Ecrit un .texr 4x4 RGBA8 dans tmp et le decode via LoadTexrFile.
    void Test_LoadTexrRoundTrip()
    {
        const std::filesystem::path tmpPath =
            std::filesystem::temp_directory_path() / "lcdlln_texr_test.texr";
        std::vector<uint8_t> file;
        const uint32_t magic = 0x52584554u;
        const uint32_t w = 4u, h = 4u, srgb = 1u;
        file.resize(16u + w * h * 4u);
        std::memcpy(file.data() + 0,  &magic, 4);
        std::memcpy(file.data() + 4,  &w,     4);
        std::memcpy(file.data() + 8,  &h,     4);
        std::memcpy(file.data() + 12, &srgb,  4);
        for (size_t p = 0; p < w * h; ++p)
        {
            file[16u + p * 4u + 0] = 100u;
            file[16u + p * 4u + 1] = 150u;
            file[16u + p * 4u + 2] = 200u;
            file[16u + p * 4u + 3] = 255u;
        }
        {
            std::ofstream f(tmpPath, std::ios::binary);
            f.write(reinterpret_cast<const char*>(file.data()),
                    static_cast<long long>(file.size()));
        }

        std::vector<uint8_t> out;
        uint32_t outW = 0, outH = 0;
        REQUIRE(engine::editor::LoadTexrFile(tmpPath.string(), out, outW, outH));
        REQUIRE(outW == 4u);
        REQUIRE(outH == 4u);
        REQUIRE(out.size() == 64u);
        REQUIRE(out[0] == 100u && out[1] == 150u && out[2] == 200u && out[3] == 255u);

        std::filesystem::remove(tmpPath);
    }
} // namespace

int main()
{
    Test_ResampleDownsampleSquare();
    Test_ResampleNonSquareCropsCenter();
    Test_GenerateProceduralDeterminism();
    Test_GenerateProceduralInvalidParams();
    Test_ResampleInvalidParams();
    Test_LoadTexrRoundTrip();
    if (g_failed == 0)
    {
        std::fprintf(stdout, "[OK] all texture_preview_cache_tests passed\n");
        return 0;
    }
    std::fprintf(stderr, "[FAIL] %d assertions failed\n", g_failed);
    return 1;
}
