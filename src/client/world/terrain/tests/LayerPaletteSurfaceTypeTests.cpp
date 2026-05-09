// src/client/world/terrain/tests/LayerPaletteSurfaceTypeTests.cpp
#include "src/client/world/terrain/LayerPalette.h"
#include "src/client/world/surface/SurfaceType.h"

#include <cstdio>
#include <filesystem>
#include <fstream>

namespace
{
    int g_failed = 0;

    #define REQUIRE(cond) do { \
        if (!(cond)) { \
            std::fprintf(stderr, "[FAIL] %s:%d  %s\n", __FILE__, __LINE__, #cond); \
            ++g_failed; \
        } \
    } while (0)

    using engine::world::surface::SurfaceType;
    using engine::world::terrain::LayerPalette;
    using engine::world::terrain::LoadLayerPalette;

    /// 8 layers avec un mix de surfaceType : Dirt, Grass, Rock, Snow, ...
    std::filesystem::path WritePaletteFixture()
    {
        auto p = std::filesystem::temp_directory_path() / "layer_palette_surface_test.json";
        std::ofstream f(p);
        f << R"({
  "version": 1,
  "layers": [
    { "index": 0, "name": "dirt",  "albedo": "a0", "normal": "n0", "arm": "r0", "tilingMeters": 4.0, "surfaceType": "Dirt" },
    { "index": 1, "name": "grass", "albedo": "a1", "normal": "n1", "arm": "r1", "tilingMeters": 4.0, "surfaceType": "Grass" },
    { "index": 2, "name": "mud",   "albedo": "a2", "normal": "n2", "arm": "r2", "tilingMeters": 4.0, "surfaceType": "Mud" },
    { "index": 3, "name": "sand",  "albedo": "a3", "normal": "n3", "arm": "r3", "tilingMeters": 4.0, "surfaceType": "Sand" },
    { "index": 4, "name": "rock",  "albedo": "a4", "normal": "n4", "arm": "r4", "tilingMeters": 4.0, "surfaceType": "Rock" },
    { "index": 5, "name": "snow",  "albedo": "a5", "normal": "n5", "arm": "r5", "tilingMeters": 4.0, "surfaceType": "Snow" },
    { "index": 6, "name": "road",  "albedo": "a6", "normal": "n6", "arm": "r6", "tilingMeters": 4.0, "surfaceType": "Road" },
    { "index": 7, "name": "bridge","albedo": "a7", "normal": "n7", "arm": "r7", "tilingMeters": 4.0, "surfaceType": "Bridge" }
  ]
})";
        return p;
    }

    void Test_LoadLayerPalette_ParsesSurfaceTypeString()
    {
        auto path = WritePaletteFixture();
        LayerPalette pal; std::string err;
        REQUIRE(LoadLayerPalette(path, pal, err));
        REQUIRE(pal.layers[0].surfaceType == SurfaceType::Dirt);
        REQUIRE(pal.layers[5].surfaceType == SurfaceType::Snow);
        REQUIRE(pal.layers[5].surfaceTypeName == "Snow");
        std::filesystem::remove(path);
    }

    void Test_LoadLayerPalette_UnknownSurfaceType_FallsBackDirt()
    {
        auto p = std::filesystem::temp_directory_path() / "layer_palette_unknown.json";
        std::ofstream f(p);
        f << R"({
  "version": 1,
  "layers": [
    { "index": 0, "name": "x", "albedo": "a", "normal": "n", "arm": "r", "tilingMeters": 4.0, "surfaceType": "Foobar" },
    { "index": 1, "name": "x", "albedo": "a", "normal": "n", "arm": "r", "tilingMeters": 4.0, "surfaceType": "Dirt" },
    { "index": 2, "name": "x", "albedo": "a", "normal": "n", "arm": "r", "tilingMeters": 4.0, "surfaceType": "Dirt" },
    { "index": 3, "name": "x", "albedo": "a", "normal": "n", "arm": "r", "tilingMeters": 4.0, "surfaceType": "Dirt" },
    { "index": 4, "name": "x", "albedo": "a", "normal": "n", "arm": "r", "tilingMeters": 4.0, "surfaceType": "Dirt" },
    { "index": 5, "name": "x", "albedo": "a", "normal": "n", "arm": "r", "tilingMeters": 4.0, "surfaceType": "Dirt" },
    { "index": 6, "name": "x", "albedo": "a", "normal": "n", "arm": "r", "tilingMeters": 4.0, "surfaceType": "Dirt" },
    { "index": 7, "name": "x", "albedo": "a", "normal": "n", "arm": "r", "tilingMeters": 4.0, "surfaceType": "Dirt" }
  ]
})";
        f.close();

        LayerPalette pal; std::string err;
        REQUIRE(LoadLayerPalette(p, pal, err));      // load réussit (warn-level seulement)
        REQUIRE(pal.layers[0].surfaceType == SurfaceType::Dirt);  // fallback Dirt
        REQUIRE(pal.layers[0].surfaceTypeName == "Foobar");        // string brute conservée
        std::filesystem::remove(p);
    }

    void Test_GetSurfaceTypeForLayer_ValidIndex()
    {
        auto path = WritePaletteFixture();
        LayerPalette pal; std::string err;
        REQUIRE(LoadLayerPalette(path, pal, err));
        REQUIRE(pal.GetSurfaceTypeForLayer(0) == SurfaceType::Dirt);
        REQUIRE(pal.GetSurfaceTypeForLayer(4) == SurfaceType::Rock);
        REQUIRE(pal.GetSurfaceTypeForLayer(7) == SurfaceType::Bridge);
        std::filesystem::remove(path);
    }
}

int main()
{
    Test_LoadLayerPalette_ParsesSurfaceTypeString();
    Test_LoadLayerPalette_UnknownSurfaceType_FallsBackDirt();
    Test_GetSurfaceTypeForLayer_ValidIndex();
    return g_failed;
}
