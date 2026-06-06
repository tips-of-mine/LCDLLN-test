// src/client/world/surface/tests/SurfaceQueryServiceTests.cpp
#include "src/client/world/surface/SurfaceQueryService.h"
#include "src/client/world/surface/SurfaceTable.h"
#include "src/client/world/StreamCache.h"
#include "src/client/world/terrain/LayerPalette.h"
#include "src/client/world/terrain/SplatMap.h"
#include "src/shared/core/Config.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

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
    using engine::world::surface::SurfaceQueryService;
    using engine::world::surface::SurfaceQueryResult;
    using engine::world::StreamCache;
    using engine::world::terrain::LayerPalette;
    using engine::world::terrain::SplatMap;
    using engine::world::terrain::SaveSplatBin;
    using engine::world::terrain::kSplatResolution;
    using engine::world::terrain::kSplatLayerCount;

    /// Construit une LayerPalette test avec mapping fixé : layer 0=Dirt, 4=Rock,
    /// 5=Snow, et le reste à Dirt (les tests s'en moquent).
    LayerPalette MakeTestPalette()
    {
        LayerPalette pal;
        for (uint32_t i = 0; i < 8; ++i)
        {
            pal.layers[i].index = i;
            pal.layers[i].surfaceType = SurfaceType::Dirt;
        }
        pal.layers[0].surfaceType = SurfaceType::Dirt;
        pal.layers[4].surfaceType = SurfaceType::Rock;
        pal.layers[5].surfaceType = SurfaceType::Snow;
        return pal;
    }

    /// Insère une SplatMap uniforme (100% layer X) dans le cache pour le chunk (cx, cz).
    void InsertUniformSplatToCache(StreamCache& cache, int chunkX, int chunkZ, uint32_t layerIdx)
    {
        SplatMap splat = SplatMap::MakeUniform(layerIdx);
        std::vector<uint8_t> bytes;
        std::string err;
        REQUIRE(SaveSplatBin(splat, bytes, err));

        std::ostringstream ks;
        ks << "chunks/chunk_" << chunkX << "_" << chunkZ << "/splat.bin";
        cache.Insert(ks.str(), bytes);
    }

    /// Idem mais 50/50 entre layer A et B (poids 128/127, somme=255).
    void InsertTwoLayerSplat(StreamCache& cache, int chunkX, int chunkZ,
        uint32_t layerA, uint32_t layerB)
    {
        SplatMap splat;
        const size_t cellCount = static_cast<size_t>(kSplatResolution) * kSplatResolution;
        splat.weights.assign(cellCount * kSplatLayerCount, 0u);
        for (size_t cell = 0; cell < cellCount; ++cell)
        {
            splat.weights[cell * kSplatLayerCount + layerA] = 128u;
            splat.weights[cell * kSplatLayerCount + layerB] = 127u;
        }

        std::vector<uint8_t> bytes;
        std::string err;
        REQUIRE(SaveSplatBin(splat, bytes, err));

        std::ostringstream ks;
        ks << "chunks/chunk_" << chunkX << "_" << chunkZ << "/splat.bin";
        cache.Insert(ks.str(), bytes);
    }

    /// Construit un SurfaceTable + Config + StreamCache + LayerPalette dans un
    /// helper unique. Le `paths.content` pointe vers un dossier inexistant pour
    /// que le fallback disque échoue (le cache est l'unique source de splat).
    struct Fixture
    {
        SurfaceTable table;
        engine::core::Config cfg;
        StreamCache cache;
        LayerPalette palette;
        SurfaceQueryService svc;

        Fixture()
        {
            // SurfaceTable : on remplit minimalement via fixture JSON.
            auto p = std::filesystem::temp_directory_path() / "svc_test_table.json";
            std::ofstream f(p);
            f << R"({"version":1,"surfaces":[
                {"type":"Dirt","baseSpeed":1.00,"audioStep":"d","visualTag":""},
                {"type":"Grass","baseSpeed":0.95,"audioStep":"g","visualTag":""},
                {"type":"Mud","baseSpeed":0.55,"audioStep":"m","visualTag":""},
                {"type":"Sand","baseSpeed":0.70,"audioStep":"s","visualTag":""},
                {"type":"Rock","baseSpeed":1.05,"audioStep":"r","visualTag":""},
                {"type":"Snow","baseSpeed":0.50,"audioStep":"sn","visualTag":""},
                {"type":"ShallowWater","baseSpeed":0.40,"audioStep":"sw","visualTag":""},
                {"type":"DeepWater","baseSpeed":0.25,"audioStep":"dw","visualTag":""},
                {"type":"LavaCooled","baseSpeed":0.85,"audioStep":"l","visualTag":""},
                {"type":"WheatField","baseSpeed":0.85,"audioStep":"w","visualTag":""},
                {"type":"CornField","baseSpeed":0.80,"audioStep":"c","visualTag":""},
                {"type":"Road","baseSpeed":1.10,"audioStep":"ro","visualTag":""},
                {"type":"Bridge","baseSpeed":1.10,"audioStep":"b","visualTag":""}
            ]})";
            f.close();
            std::string err;
            REQUIRE(table.LoadFromJson(p, err));
            std::filesystem::remove(p);

            cfg.SetValue("paths.content", std::string("/nonexistent_dir_for_test"));
            cache.Init(cfg);
            palette = MakeTestPalette();
            REQUIRE(svc.Init(table, cache, cfg, palette));
        }
    };

    void Test_Query_SplatAbsent_FallbackDirt()
    {
        Fixture fx;
        // Pas d'Insert : LoadSplatMap → cache miss → disk miss → nullptr.
        engine::math::Vec3 pos{ 0.0f, 0.0f, 0.0f };
        SurfaceQueryResult r = fx.svc.Query(pos);
        REQUIRE(r.base == SurfaceType::Dirt);
        REQUIRE(r.modifiers.speedMultiplier == 1.0f);
    }

    void Test_Query_DominantLayerDirt_ReturnsDirt()
    {
        Fixture fx;
        // Chunk (0, 0) avec splat 100% layer 0 (Dirt dans la palette test).
        InsertUniformSplatToCache(fx.cache, 0, 0, 0);
        engine::math::Vec3 pos{ 1.0f, 0.0f, 1.0f };  // dans chunk (0,0)
        SurfaceQueryResult r = fx.svc.Query(pos);
        REQUIRE(r.base == SurfaceType::Dirt);
    }

    void Test_Query_DominantLayerRock_ReturnsRock()
    {
        Fixture fx;
        InsertUniformSplatToCache(fx.cache, 0, 0, 4);  // layer 4 = Rock
        engine::math::Vec3 pos{ 1.0f, 0.0f, 1.0f };
        SurfaceQueryResult r = fx.svc.Query(pos);
        REQUIRE(r.base == SurfaceType::Rock);
    }

    void Test_Query_DominantLayerSnow_ReturnsSnow()
    {
        Fixture fx;
        InsertUniformSplatToCache(fx.cache, 0, 0, 5);  // layer 5 = Snow
        engine::math::Vec3 pos{ 1.0f, 0.0f, 1.0f };
        SurfaceQueryResult r = fx.svc.Query(pos);
        REQUIRE(r.base == SurfaceType::Snow);
    }

    void Test_Query_TieBreaker_LowestIndex()
    {
        Fixture fx;
        // 50/50 entre layer 4 (Rock, 128) et layer 5 (Snow, 127).
        // argmax avec tie-break "plus petit index" → layer 4 = Rock.
        InsertTwoLayerSplat(fx.cache, 0, 0, 4, 5);
        engine::math::Vec3 pos{ 1.0f, 0.0f, 1.0f };
        SurfaceQueryResult r = fx.svc.Query(pos);
        REQUIRE(r.base == SurfaceType::Rock);  // layer 4 wins (lower index, equal weight)
    }

    void Test_Query_OutOfBoundsCell_FallbackDirt()
    {
        Fixture fx;
        InsertUniformSplatToCache(fx.cache, 0, 0, 5);  // chunk (0,0) Snow
        // worldPos très loin → chunk (1000, 1000) sans splat.
        engine::math::Vec3 pos{ 100000.0f, 0.0f, 100000.0f };
        SurfaceQueryResult r = fx.svc.Query(pos);
        REQUIRE(r.base == SurfaceType::Dirt);  // fallback
    }

    void Test_Query_ModifiersNeutralByDefault()
    {
        Fixture fx;
        InsertUniformSplatToCache(fx.cache, 0, 0, 5);  // Snow
        SurfaceQueryResult r = fx.svc.Query({ 1.0f, 0.0f, 1.0f });
        REQUIRE(r.modifiers.speedMultiplier == 1.0f);
        REQUIRE(r.modifiers.audioPitchShift == 1.0f);
        REQUIRE(!r.modifiers.slippery);
        REQUIRE(!r.modifiers.wet);
        REQUIRE(!r.modifiers.frozen);
        REQUIRE(!r.modifiers.seasonalSnow);
    }

    // Prouve que le service utilise la grille TERRAIN 256 m (et plus la grille
    // 500 m). worldPos.x = 300 → chunk (1,0) sur la grille 256, mais (0,0) sur
    // l'ancienne grille 500. On insère le splat UNIQUEMENT au chunk (1,0) :
    //  - grille 256 (correcte) → charge (1,0) → Rock.
    //  - grille 500 (bug) → chargerait (0,0) (vide) → fallback Dirt.
    // Ce test échouerait donc si on revenait à WorldToGlobalChunkCoord/ChunkBounds.
    void Test_Query_Uses256TerrainGrid()
    {
        Fixture fx;
        InsertUniformSplatToCache(fx.cache, 1, 0, 4);  // chunk (1,0) = Rock
        // x=300 ∈ [256,512) → chunk terrain (1,0) ; serait (0,0) en grille 500.
        engine::math::Vec3 pos{ 300.0f, 0.0f, 1.0f };
        SurfaceQueryResult r = fx.svc.Query(pos);
        REQUIRE(r.base == SurfaceType::Rock);
    }
}

int main()
{
    Test_Query_SplatAbsent_FallbackDirt();
    Test_Query_DominantLayerDirt_ReturnsDirt();
    Test_Query_DominantLayerRock_ReturnsRock();
    Test_Query_DominantLayerSnow_ReturnsSnow();
    Test_Query_TieBreaker_LowestIndex();
    Test_Query_OutOfBoundsCell_FallbackDirt();
    Test_Query_ModifiersNeutralByDefault();
    Test_Query_Uses256TerrainGrid();
    return g_failed;
}
