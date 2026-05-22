// src/client/character_creation/tests/RaceDefinitionTests.cpp
//
// Sous-projet C MVP : tests parsing de RaceDefinition.meshPath depuis
// races.json. Verifie que les 3 races MVP exposent un meshPath non vide
// et que les races hors-MVP exposent une string vide (fallback handle
// par Engine::GetRaceMesh a EnterWorld).

#include "src/client/character_creation/CharacterCreationUi.h"
#include "src/shared/core/Config.h"

#include <cstdio>
#include <string>

using engine::client::CharacterCreationPresenter;
using engine::client::RaceDefinition;
using engine::core::Config;

namespace
{
    int g_failed = 0;
    #define REQUIRE(cond) do { \
        if (!(cond)) { std::fprintf(stderr, "[FAIL] %s:%d %s\n", __FILE__, __LINE__, #cond); ++g_failed; } \
    } while (0)

    /// Construit un Config minimal pointant vers le races.json reel du
    /// repo. Les tests CTest tournent avec WORKING_DIRECTORY =
    /// CMAKE_SOURCE_DIR, donc "game/data" resoluable tel quel.
    Config MakeConfigPointingToRepoContent()
    {
        Config cfg;
        cfg.SetValue("paths.content", std::string("game/data"));
        cfg.SetValue("char_creation.races_path", std::string("races/races.json"));
        cfg.SetValue("char_creation.classes_path", std::string("races/classes.json"));
        return cfg;
    }

    /// Cherche une race par id dans la liste retournee par GetRaces().
    /// Renvoie nullptr si introuvable.
    const RaceDefinition* FindRace(const std::vector<RaceDefinition>& races, const std::string& id)
    {
        for (const auto& r : races) if (r.id == id) return &r;
        return nullptr;
    }

    /// Verifie que les 3 races MVP (humains, nains, orcs) ont un meshPath
    /// non vide pointant vers le chemin .glb attendu.
    void Test_MvpRaces_HaveMeshPath()
    {
        CharacterCreationPresenter p;
        Config cfg = MakeConfigPointingToRepoContent();
        REQUIRE(p.Init(cfg));

        const auto& races = p.GetRaces();
        REQUIRE(!races.empty());

        const RaceDefinition* humains = FindRace(races, "humains");
        REQUIRE(humains != nullptr);
        // Migration UE5 : humains pointe sur le corps modulaire UE5 (Male_Ranger).
        REQUIRE(humains->meshPath == "models/characters/humains/Male_Ranger/Male_Ranger.glb");

        const RaceDefinition* nains = FindRace(races, "nains");
        REQUIRE(nains != nullptr);
        REQUIRE(nains->meshPath == "models/avatars/nains/nains.glb");

        const RaceDefinition* orcs = FindRace(races, "orcs");
        REQUIRE(orcs != nullptr);
        REQUIRE(orcs->meshPath == "models/avatars/orc/orc.glb");
    }

    /// Verifie que les races hors-MVP ont meshPath vide (string par defaut
    /// "" quand la cle est absente du JSON). Engine fallback humains.
    void Test_NonMvpRaces_HaveEmptyMeshPath()
    {
        CharacterCreationPresenter p;
        Config cfg = MakeConfigPointingToRepoContent();
        REQUIRE(p.Init(cfg));

        const auto& races = p.GetRaces();

        const RaceDefinition* elfes = FindRace(races, "elfes");
        REQUIRE(elfes != nullptr);
        REQUIRE(elfes->meshPath.empty());

        const RaceDefinition* demons = FindRace(races, "demons");
        REQUIRE(demons != nullptr);
        REQUIRE(demons->meshPath.empty());
    }
}

int main()
{
    Test_MvpRaces_HaveMeshPath();
    Test_NonMvpRaces_HaveEmptyMeshPath();
    return g_failed == 0 ? 0 : 1;
}
