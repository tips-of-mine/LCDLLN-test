// Test de la table de compat des statuts de quête persistés (SP1) + du
// round-trip de persistance des quêtes, y compris EXT-2 (completed_at, format v2).
#include "src/shardd/gameplay/character/CharacterPersistenceQuestCompat.h"
#include "src/shardd/gameplay/character/CharacterPersistence.h"
#include "src/shared/core/Config.h"
#include "src/shared/platform/FileSystem.h"

#include <filesystem>
#include <iostream>
#include <random>
#include <string>

using engine::server::QuestStatus;
using engine::server::MapPersistedQuestStatus;
using engine::server::CharacterPersistenceStore;
using engine::server::PersistedCharacterState;
using engine::server::QuestState;

namespace
{
    int g_failures = 0;

    void Check(bool condition, const char* label)
    {
        if (!condition)
        {
            std::cerr << "[FAIL] " << label << "\n";
            ++g_failures;
        }
    }

    /// Construit un répertoire temporaire unique servant de content root isolé.
    /// Abort si la création échoue (test invalide sinon).
    std::filesystem::path MakeTempContentDir()
    {
        std::random_device rd;
        std::mt19937_64 rng(rd());
        const std::filesystem::path base = std::filesystem::temp_directory_path()
            / ("lcdlln_char_persist_test_" + std::to_string(rng()));
        std::error_code ec;
        std::filesystem::create_directories(base, ec);
        if (ec)
        {
            std::cerr << "[FATAL] cannot create temp dir " << base.string() << ": " << ec.message() << "\n";
            std::abort();
        }
        return base;
    }

    /// Construit une Config pointant sur un content root temporaire isolé et y
    /// écrit un fichier de schéma factice (Init() du store exige juste qu'il
    /// existe). Retourne la Config prête à instancier CharacterPersistenceStore.
    engine::core::Config MakeConfigWithSchema()
    {
        const std::filesystem::path contentRoot = MakeTempContentDir();
        engine::core::Config config;
        config.SetValue("paths.content", engine::core::Config::Value{ contentRoot.string() });

        const std::string schemaRel =
            config.GetString("server.persistence_schema_path",
                "persistence/db/migrations/0001_characters_inventory.sql");
        if (!engine::platform::FileSystem::WriteAllTextContent(config, schemaRel, "-- stub schema\n"))
        {
            std::cerr << "[FATAL] cannot write stub schema under " << contentRoot.string() << "\n";
            std::abort();
        }
        return config;
    }
}

int main()
{
    // Ancien format (version 0) : 0=Locked, 1=Active, 2=Completed.
    Check(MapPersistedQuestStatus(0, 0) == QuestStatus::Locked,    "old 0 -> Locked");
    Check(MapPersistedQuestStatus(1, 0) == QuestStatus::Active,    "old 1 -> Active");
    Check(MapPersistedQuestStatus(2, 0) == QuestStatus::Completed, "old 2 -> Completed");

    // Nouveau format (version 1) : valeurs 0..4 = enum direct.
    Check(MapPersistedQuestStatus(1, 1) == QuestStatus::Offered,       "new 1 -> Offered");
    Check(MapPersistedQuestStatus(3, 1) == QuestStatus::ReadyToTurnIn, "new 3 -> ReadyToTurnIn");
    Check(MapPersistedQuestStatus(4, 1) == QuestStatus::Completed,     "new 4 -> Completed");

    // Valeur hors plage → Locked (dégradation sûre).
    Check(MapPersistedQuestStatus(99, 1) == QuestStatus::Locked, "out of range -> Locked");

    // EXT-2 — round-trip completed_at : sauvegarde puis rechargement d'un état de
    // quête Completed portant un completedAtEpochMs non nul.
    {
        engine::core::Config config = MakeConfigWithSchema();
        CharacterPersistenceStore store(config);
        Check(store.Init(), "EXT-2 store Init OK (schéma factice)");

        PersistedCharacterState saved{};
        saved.characterKey = 424242ULL;
        QuestState q{};
        q.questId = "daily_boars";
        q.status = QuestStatus::Completed;
        q.stepProgressCounts = { 5u, 0u };
        q.completedAtEpochMs = 1751500000000ULL; // ms UTC arbitraire non nulle
        saved.questStates.push_back(q);

        Check(store.SaveCharacter(saved), "EXT-2 SaveCharacter OK");

        PersistedCharacterState loaded{};
        Check(store.LoadCharacter(saved.characterKey, loaded), "EXT-2 LoadCharacter OK");
        Check(loaded.questStates.size() == 1, "EXT-2 une quête rechargée");
        if (loaded.questStates.size() == 1)
        {
            Check(loaded.questStates[0].questId == "daily_boars", "EXT-2 questId round-trip");
            Check(loaded.questStates[0].status == QuestStatus::Completed, "EXT-2 statut round-trip");
            Check(loaded.questStates[0].completedAtEpochMs == 1751500000000ULL,
                  "EXT-2 completed_at survit au round-trip");
            Check(loaded.questStates[0].stepProgressCounts.size() == 2
                  && loaded.questStates[0].stepProgressCounts[0] == 5u,
                  "EXT-2 step_count/progress round-trip");
        }
    }

    // EXT-2 — compat save v1 : un blob sans quests.completed_at (format_version=1)
    // charge avec completedAtEpochMs == 0.
    {
        engine::core::Config config = MakeConfigWithSchema();
        CharacterPersistenceStore store(config);
        Check(store.Init(), "EXT-2 store Init OK (v1 compat)");

        // Écrit à la main un fichier de personnage au format v1 (sans completed_at).
        const uint64_t key = 99ULL;
        const std::string rel =
            "persistence/characters/character_" + std::to_string(key) + ".ini";
        const std::string v1Blob =
            "character.zone_id=1\n"
            "quests.format_version=1\n"
            "quests.count=1\n"
            "quests.0.id=legacy_quest\n"
            "quests.0.status=4\n"
            "quests.0.step_count=1\n"
            "quests.0.step.0.progress=2\n";
        Check(engine::platform::FileSystem::WriteAllTextContent(config, rel, v1Blob),
              "EXT-2 écriture blob v1");

        PersistedCharacterState loaded{};
        Check(store.LoadCharacter(key, loaded), "EXT-2 LoadCharacter blob v1 OK");
        Check(loaded.questStates.size() == 1, "EXT-2 v1 : une quête chargée");
        if (loaded.questStates.size() == 1)
        {
            Check(loaded.questStates[0].status == QuestStatus::Completed,
                  "EXT-2 v1 : statut mappé (enum direct)");
            Check(loaded.questStates[0].completedAtEpochMs == 0ULL,
                  "EXT-2 v1 sans completed_at → 0 (défaut)");
        }
    }

    // EXT-2 — le fichier sérialisé doit porter format_version=2.
    {
        engine::core::Config config = MakeConfigWithSchema();
        CharacterPersistenceStore store(config);
        Check(store.Init(), "EXT-2 store Init OK (format v2)");

        PersistedCharacterState saved{};
        saved.characterKey = 7ULL;
        QuestState q{};
        q.questId = "q";
        q.status = QuestStatus::Active;
        q.stepProgressCounts = { 0u };
        saved.questStates.push_back(q);
        Check(store.SaveCharacter(saved), "EXT-2 SaveCharacter (format v2) OK");

        const std::string rel =
            "persistence/characters/character_" + std::to_string(saved.characterKey) + ".ini";
        engine::core::Config reread;
        const auto full = engine::platform::FileSystem::ResolveContentPath(config, rel);
        Check(reread.LoadFromFile(full.string()), "EXT-2 relecture fichier sérialisé");
        Check(reread.GetInt("quests.format_version", 0) == 2,
              "EXT-2 format_version=2 écrit");
    }

    if (g_failures) { std::cerr << g_failures << " échec(s)\n"; return 1; }
    std::cout << "OK\n";
    return 0;
}
