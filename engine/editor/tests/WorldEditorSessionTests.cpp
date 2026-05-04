/// Tests unitaires pour WorldEditorSession.
///
/// Verifie en particulier que la creation d'une carte par defaut
/// (`ActionNewMap`) produit bien des `splatLayerTextureRefs` toutes vides :
/// c'est cette condition qui pilote le fallback orange du shader terrain
/// dans le World Editor (cf. `terrain.frag`, push-constant `noUserTextures`).
/// Si quelqu'un casse cette invariance plus tard, le fallback orange ne
/// s'allumerait jamais sur les nouvelles cartes et le terrain redeviendrait
/// invisible / illisible apres "Creer une nouvelle carte".

#include "engine/core/Config.h"
#include "engine/editor/WorldEditorSession.h"
#include "engine/editor/WorldMapEditDocument.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
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

    /// Cree un repertoire temporaire unique pour ne pas polluer game/data
    /// (ActionNewMap ecrit height.r16h, splat.slap, grass.grms et le JSON).
    std::filesystem::path MakeUniqueTempContentDir()
    {
        const auto now = std::chrono::high_resolution_clock::now().time_since_epoch().count();
        std::random_device rd;
        std::mt19937_64 rng(static_cast<uint64_t>(now) ^ rd());
        const std::filesystem::path base = std::filesystem::temp_directory_path()
            / ("lcdlln_world_editor_session_test_" + std::to_string(rng()));
        std::filesystem::create_directories(base);
        return base;
    }

    /// Une carte fraichement creee n'a aucune texture utilisateur assignee :
    /// les 4 entrees de `splatLayerTextureRefs` doivent etre vides. Cette
    /// invariance pilote le fallback orange du shader terrain en mode editeur.
    void Test_DefaultMapHasEmptyTextureRefs()
    {
        const std::filesystem::path tmpContent = MakeUniqueTempContentDir();
        engine::core::Config cfg;
        cfg.SetValue("paths.content", std::string(tmpContent.string()));

        engine::editor::WorldEditorSession session;
        REQUIRE(session.ActionNewMap(cfg));

        const auto& refs = session.Doc().splatLayerTextureRefs;
        REQUIRE(refs.size() == 4u);
        for (const std::string& r : refs)
        {
            REQUIRE(r.empty());
        }

        std::error_code ec;
        std::filesystem::remove_all(tmpContent, ec);
    }
} // namespace

int main()
{
    Test_DefaultMapHasEmptyTextureRefs();
    if (g_failed == 0)
    {
        std::fprintf(stdout, "[OK] all world_editor_session_tests passed\n");
        return 0;
    }
    std::fprintf(stderr, "[FAIL] %d assertions failed\n", g_failed);
    return 1;
}
