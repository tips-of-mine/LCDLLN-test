// src/client/world/surface/tests/SurfaceTableTests.cpp
#include "src/client/world/surface/SurfaceTable.h"

#include <cmath>
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
    using engine::world::surface::SurfaceTable;

    bool ApproxEq(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }

    /// Écrit un fichier JSON temporaire avec les 13 entrées de la spec ticket.
    std::filesystem::path WriteFixtureFull()
    {
        auto p = std::filesystem::temp_directory_path() / "surface_table_full.json";
        std::ofstream f(p);
        f << R"({
  "version": 1,
  "surfaces": [
    { "type": "Dirt",         "baseSpeed": 1.00, "audioStep": "step_dirt",          "visualTag": "dust_sprint" },
    { "type": "Grass",        "baseSpeed": 0.95, "audioStep": "step_grass",         "visualTag": "grass_bend" },
    { "type": "Mud",          "baseSpeed": 0.55, "audioStep": "step_mud_squelch",   "visualTag": "splash_mud" },
    { "type": "Sand",         "baseSpeed": 0.70, "audioStep": "step_sand",          "visualTag": "footprint_decal_fade" },
    { "type": "Rock",         "baseSpeed": 1.05, "audioStep": "step_rock",          "visualTag": "" },
    { "type": "Snow",         "baseSpeed": 0.50, "audioStep": "step_snow_crunch",   "visualTag": "footprint_decal_persistent" },
    { "type": "ShallowWater", "baseSpeed": 0.40, "audioStep": "step_water",         "visualTag": "splash_water" },
    { "type": "DeepWater",    "baseSpeed": 0.25, "audioStep": "swim_stroke",        "visualTag": "swim_mode" },
    { "type": "LavaCooled",   "baseSpeed": 0.85, "audioStep": "step_rock",          "visualTag": "heat_emission" },
    { "type": "WheatField",   "baseSpeed": 0.85, "audioStep": "step_wheat_rustle",  "visualTag": "wheat_part" },
    { "type": "CornField",    "baseSpeed": 0.80, "audioStep": "step_corn_rustle",   "visualTag": "corn_part" },
    { "type": "Road",         "baseSpeed": 1.10, "audioStep": "step_gravel",        "visualTag": "" },
    { "type": "Bridge",       "baseSpeed": 1.10, "audioStep": "step_wood",          "visualTag": "" }
  ]
})";
        return p;
    }

    void Test_LoadFromJson_FixtureHas13Entries()
    {
        auto path = WriteFixtureFull();
        SurfaceTable table;
        std::string err;
        REQUIRE(table.LoadFromJson(path, err));
        REQUIRE(err.empty());
        REQUIRE(table.IsLoaded());
        std::filesystem::remove(path);
    }

    void Test_LoadFromJson_DirtBaseSpeedIs1p0()
    {
        auto path = WriteFixtureFull();
        SurfaceTable table; std::string err;
        REQUIRE(table.LoadFromJson(path, err));
        REQUIRE(ApproxEq(table.Get(SurfaceType::Dirt).baseSpeed, 1.00f));
        std::filesystem::remove(path);
    }

    void Test_LoadFromJson_SnowBaseSpeedIs0p5()
    {
        auto path = WriteFixtureFull();
        SurfaceTable table; std::string err;
        REQUIRE(table.LoadFromJson(path, err));
        REQUIRE(ApproxEq(table.Get(SurfaceType::Snow).baseSpeed, 0.50f));
        std::filesystem::remove(path);
    }

    void Test_LoadFromJson_AudioStepNonEmpty()
    {
        auto path = WriteFixtureFull();
        SurfaceTable table; std::string err;
        REQUIRE(table.LoadFromJson(path, err));
        for (int i = 0; i < static_cast<int>(SurfaceType::_Count); ++i)
        {
            const auto& e = table.Get(static_cast<SurfaceType>(i));
            REQUIRE(!e.audioStep.empty());
        }
        std::filesystem::remove(path);
    }

    void Test_LoadFromJson_MalformedJson_Fails()
    {
        auto p = std::filesystem::temp_directory_path() / "surface_table_bad.json";
        std::ofstream f(p);
        f << R"({ "version": 1, "surfaces": [ { "type": "Di)";  // tronqué
        f.close();

        SurfaceTable table; std::string err;
        REQUIRE(!table.LoadFromJson(p, err));
        REQUIRE(!err.empty());
        REQUIRE(!table.IsLoaded());
        std::filesystem::remove(p);
    }

    void Test_LoadFromJson_MissingEntry_Fails()
    {
        auto p = std::filesystem::temp_directory_path() / "surface_table_short.json";
        std::ofstream f(p);
        // Seulement 12 entrées : il manque "Bridge"
        f << R"({
  "version": 1,
  "surfaces": [
    { "type": "Dirt",       "baseSpeed": 1.0, "audioStep": "a", "visualTag": "" },
    { "type": "Grass",      "baseSpeed": 1.0, "audioStep": "a", "visualTag": "" },
    { "type": "Mud",        "baseSpeed": 1.0, "audioStep": "a", "visualTag": "" },
    { "type": "Sand",       "baseSpeed": 1.0, "audioStep": "a", "visualTag": "" },
    { "type": "Rock",       "baseSpeed": 1.0, "audioStep": "a", "visualTag": "" },
    { "type": "Snow",       "baseSpeed": 1.0, "audioStep": "a", "visualTag": "" },
    { "type": "ShallowWater","baseSpeed": 1.0, "audioStep": "a", "visualTag": "" },
    { "type": "DeepWater",  "baseSpeed": 1.0, "audioStep": "a", "visualTag": "" },
    { "type": "LavaCooled", "baseSpeed": 1.0, "audioStep": "a", "visualTag": "" },
    { "type": "WheatField", "baseSpeed": 1.0, "audioStep": "a", "visualTag": "" },
    { "type": "CornField",  "baseSpeed": 1.0, "audioStep": "a", "visualTag": "" },
    { "type": "Road",       "baseSpeed": 1.0, "audioStep": "a", "visualTag": "" }
  ]
})";
        f.close();

        SurfaceTable table; std::string err;
        REQUIRE(!table.LoadFromJson(p, err));
        REQUIRE(err.find("13") != std::string::npos);  // message mentionne "13 entries expected"
        std::filesystem::remove(p);
    }
}

int main()
{
    Test_LoadFromJson_FixtureHas13Entries();
    Test_LoadFromJson_DirtBaseSpeedIs1p0();
    Test_LoadFromJson_SnowBaseSpeedIs0p5();
    Test_LoadFromJson_AudioStepNonEmpty();
    Test_LoadFromJson_MalformedJson_Fails();
    Test_LoadFromJson_MissingEntry_Fails();
    return g_failed;
}
