// Test de la table de compat des statuts de quête persistés (SP1).
#include "src/shardd/gameplay/character/CharacterPersistenceQuestCompat.h"

#include <iostream>

using engine::server::QuestStatus;
using engine::server::MapPersistedQuestStatus;

int main()
{
    int failures = 0;
    auto expect = [&](QuestStatus got, QuestStatus want, const char* label) {
        if (got != want) { std::cerr << "[FAIL] " << label << "\n"; ++failures; }
    };

    // Ancien format (version 0) : 0=Locked, 1=Active, 2=Completed.
    expect(MapPersistedQuestStatus(0, 0), QuestStatus::Locked,    "old 0 -> Locked");
    expect(MapPersistedQuestStatus(1, 0), QuestStatus::Active,    "old 1 -> Active");
    expect(MapPersistedQuestStatus(2, 0), QuestStatus::Completed, "old 2 -> Completed");

    // Nouveau format (version 1) : valeurs 0..4 = enum direct.
    expect(MapPersistedQuestStatus(1, 1), QuestStatus::Offered,       "new 1 -> Offered");
    expect(MapPersistedQuestStatus(3, 1), QuestStatus::ReadyToTurnIn, "new 3 -> ReadyToTurnIn");
    expect(MapPersistedQuestStatus(4, 1), QuestStatus::Completed,     "new 4 -> Completed");

    // Valeur hors plage → Locked (dégradation sûre).
    expect(MapPersistedQuestStatus(99, 1), QuestStatus::Locked, "out of range -> Locked");

    if (failures) { std::cerr << failures << " échec(s)\n"; return 1; }
    std::cout << "OK\n";
    return 0;
}
