/// Tests unitaires pour WorldEditorSession.
///
/// Cible actuelle :
///  - WorldEditorSession_DefaultMapHasEmptyTextureRefs : verifie qu'apres
///    ActionNewMap, le document a son tableau splatLayerTextureRefs
///    entierement vide (aucune texture utilisateur). Cette propriete est la
///    condition de bascule du fallback orange World Editor (terrain.frag,
///    push-constant noUserTextures), donc tout futur changement
///    d'ActionNewMap qui pre-remplirait ces refs casserait silencieusement le
///    fallback. Ce test garde la propriete cote document.
///
/// Pas de dependance Vulkan : ActionNewMap n'utilise que
/// engine::platform::FileSystem (E/S disque) et la couche de serialisation
/// terrain (heightmap r16h / splat slap / grass grms / JSON), donc le test
/// tourne en CI sans GPU.

#include "src/shared/core/Config.h"
#include "src/world_editor/WorldEditorSession.h"
#include "src/world_editor/WorldMapEditDocument.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <random>
#include <string>

namespace
{
    int g_failed = 0;

    #define REQUIRE(cond) do { \
        if (!(cond)) { \
            std::fprintf(stderr, "[FAIL] %s:%d  %s\n", __FILE__, __LINE__, #cond); \
            ++g_failed; \
        } \
    } while (0)

    /// Construit un repertoire temporaire unique sous \c temp_directory_path.
    /// Retourne le chemin (cree). N'echoue pas silencieusement : abort si la
    /// creation echoue (test invalide sinon).
    std::filesystem::path MakeTempContentDir()
    {
        std::random_device rd;
        std::mt19937_64 rng(rd());
        const std::filesystem::path base = std::filesystem::temp_directory_path()
            / ("lcdlln_world_editor_test_" + std::to_string(rng()));
        std::error_code ec;
        std::filesystem::create_directories(base, ec);
        if (ec)
        {
            std::fprintf(stderr, "[FATAL] cannot create temp dir %s: %s\n",
                base.string().c_str(), ec.message().c_str());
            std::abort();
        }
        return base;
    }

    /// Cree une carte par defaut via ActionNewMap et verifie que toutes les
    /// references textures de couches splat (`splatLayerTextureRefs`) sont
    /// vides. C'est la garde de bascule du fallback orange World Editor.
    void Test_DefaultMapHasEmptyTextureRefs()
    {
        const std::filesystem::path tmp = MakeTempContentDir();

        engine::core::Config cfg;
        cfg.SetValue("paths.content", engine::core::Config::Value{ tmp.string() });

        engine::editor::WorldEditorSession session;
        // Renseigne le zoneId requis par ActionNewMap (tampon UI).
        const char* kZoneId = "test_default_zone";
        std::memcpy(session.BufZoneId().data(), kZoneId, std::strlen(kZoneId) + 1u);

        const bool ok = session.ActionNewMap(cfg);
        REQUIRE(ok);

        const auto& refs = session.Doc().splatLayerTextureRefs;
        for (const std::string& r : refs)
        {
            REQUIRE(r.empty());
        }

        // Nettoyage best-effort (on ne fait pas echouer le test si rm echoue).
        std::error_code ec;
        std::filesystem::remove_all(tmp, ec);
    }
}

int main()
{
    Test_DefaultMapHasEmptyTextureRefs();

    if (g_failed == 0)
    {
        std::printf("[PASS] WorldEditorSessionTests\n");
        return 0;
    }
    std::printf("[FAIL] WorldEditorSessionTests: %d failure(s)\n", g_failed);
    return 1;
}
