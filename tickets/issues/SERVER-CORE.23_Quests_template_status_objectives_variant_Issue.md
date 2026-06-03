# Issue: SERVER-CORE.23

**Status:** Closed

_Verifie automatiquement le 2026-06-03 (analyse de code approfondie, reorganisation tickets)._

## Preuves d'implementation
- src/masterd/quests/QuestState.h
- src/shared/network/QuestPayloads.h

## Note
Quest state machine + payloads

---

## Contenu du ticket (SERVER-CORE.23)

# SERVER-CORE.23_Quests_template_status_objectives_variant

## Objectif

Mettre en place le **système de quêtes** côté master+shard LCDLLN,
inspiré de `src/game/Quests` server-core mais **modernisé C++20**. Cinq
piliers :

1. **Split static def / dynamic state** : `QuestTemplate` (immuable,
   chargé DB une fois côté master+shard via SQLStorage) vs
   `QuestStatusData` (par joueur, en RAM/DB). Pattern essentiel pour
   économiser la RAM sur 100k+ comptes × 1000 quêtes.
2. **Objectifs hétérogènes via `std::variant`** au lieu d'arrays
   parallèles old-school server-core (`RequiredItemId[4]`,
   `RequiredCreatureId[4]`, etc.). Plus C++20 idiomatique, type-safe.
3. **Flags bitmask pour variantes** : `Daily`, `Weekly`, `Repeatable`,
   `AutoComplete` cohabitent sur un même `Quest` → un seul type au
   lieu de classes dérivées.
4. **Reward avec choix client** : 6 « choice items » + 4 « given items ».
   Le client choisit ; le serveur valide.
5. **Chaînes via `prevQuestId / nextQuestId`** : graphe orienté résolu
   au load, pas de récursion runtime.

C'est un **P2 cross master+shard**, pré-requis dès qu'on veut du
contenu PvE narratif (quêtes ↔ kill credit ↔ items ↔ rewards).

## Dépendances

- M00.1 (build base)
- SERVER-CORE.13 (Database — SQLStorage)
- SERVER-CORE.16 (Globals/Conditions — `requiredCondition`)
- SERVER-CORE.14 (DBScripts — `quest_start_scripts`, `quest_end_scripts`)
- SERVER-CORE.17 (Loot — `kill credit` interagit avec drop)
- SERVER-CORE.18 (Mails — récompenses livrées par mail si offline)

## Livrables

### Côté master+shard (`engine/server/quests/`)

- `QuestObjective.h` — variant typé :
  ```cpp
  struct KillCreditObjective {
    uint32_t creatureEntry;
    uint32_t requiredCount;
  };
  struct CollectItemObjective {
    uint32_t itemEntry;
    uint32_t requiredCount;
  };
  struct InteractGameObjectObjective {
    uint32_t gameObjectEntry;
    uint32_t requiredCount;
  };
  struct VisitAreaObjective {
    uint32_t zoneId;
  };
  struct CastSpellObjective {
    uint32_t spellId;
    uint32_t requiredCount;
  };
  using QuestObjective = std::variant<
    KillCreditObjective,
    CollectItemObjective,
    InteractGameObjectObjective,
    VisitAreaObjective,
    CastSpellObjective
  >;
  ```
- `QuestTemplate.h` — struct immuable :
  ```cpp
  struct QuestTemplate {
    uint32_t questId;
    std::string title;
    std::string description;
    std::string objectiveText;
    int32_t  minLevel;
    int32_t  maxLevel;        // -1 = no max
    QuestFlags flags;          // bitmask
    std::vector<QuestObjective> objectives;
    QuestRewards rewards;
    uint32_t prevQuestId;
    uint32_t nextQuestId;
    uint32_t requiredConditionId;
    uint32_t startScriptId;    // DBScripts
    uint32_t endScriptId;
  };
  ```
- `QuestRewards.h` :
  ```cpp
  struct QuestRewards {
    std::vector<RewardItem> givenItems;            // tous reçus
    std::vector<RewardItem> choiceItems;           // un seul reçu (choix client)
    uint64_t copperGold;
    uint32_t experience;
    uint32_t reputationFactionId;
    int32_t reputationAmount;
  };
  ```
- `QuestStatusData.h` — état per-player :
  ```cpp
  enum class QuestState : uint8 {
    None,
    Available,        // visible mais pas accept
    Incomplete,       // accept en cours
    Complete,         // objectifs OK, reward pas pris
    Rewarded,         // terminé
    Failed,
  };
  struct QuestStatusData {
    uint32_t questId;
    QuestState state;
    int64_t  acceptedTs;
    std::vector<uint32_t> objectiveProgress;       // count par objectif
  };
  ```
- `QuestManager.{h,cpp}` :
  - `Load(ConnectionPool&)` — charge `QuestTemplate` via SQLStorage.
  - `bool TryAccept(Player&, uint32_t questId)`
  - `void OnKill(Player&, Creature& killed)` — incrémente kill credits
  - `void OnItemAdded(Player&, ItemId, count)`
  - `void OnGameObjectInteract(Player&, GameObject&)`
  - `bool IsObjectiveComplete(QuestStatusData const&) const`
  - `bool TryComplete(Player&, uint32_t questId)` — vérifie + transitions
  - `bool TryReward(Player&, uint32_t questId, int choiceItemIdx)` — applique rewards
- `QuestGiver.{h,cpp}` — interface implémentée par Creature/GameObject pour offrir des quêtes (querelist).

### Migration DB

```sql
CREATE TABLE quest_template (
  quest_id              INT UNSIGNED NOT NULL,
  title                 VARCHAR(255) NOT NULL,
  description           TEXT,
  objective_text        TEXT,
  min_level             INT NOT NULL DEFAULT 1,
  max_level             INT NOT NULL DEFAULT -1,
  flags                 INT UNSIGNED NOT NULL DEFAULT 0,
  prev_quest_id         INT UNSIGNED NOT NULL DEFAULT 0,
  next_quest_id         INT UNSIGNED NOT NULL DEFAULT 0,
  required_condition_id INT UNSIGNED NOT NULL DEFAULT 0,
  start_script_id       INT UNSIGNED NOT NULL DEFAULT 0,
  end_script_id         INT UNSIGNED NOT NULL DEFAULT 0,
  reward_gold           BIGINT UNSIGNED NOT NULL DEFAULT 0,
  reward_xp             INT UNSIGNED NOT NULL DEFAULT 0,
  reward_rep_faction    INT UNSIGNED NOT NULL DEFAULT 0,
  reward_rep_amount     INT NOT NULL DEFAULT 0,
  PRIMARY KEY (quest_id)
);

CREATE TABLE quest_objectives (
  quest_id      INT UNSIGNED NOT NULL,
  objective_idx TINYINT UNSIGNED NOT NULL,
  type          TINYINT UNSIGNED NOT NULL,    -- enum (Kill, Collect, Interact, Visit, Cast)
  param1        INT NOT NULL,
  param2        INT NOT NULL DEFAULT 0,
  PRIMARY KEY (quest_id, objective_idx)
);

CREATE TABLE quest_rewards_items (
  quest_id      INT UNSIGNED NOT NULL,
  item_id       INT UNSIGNED NOT NULL,
  count         INT UNSIGNED NOT NULL DEFAULT 1,
  is_choice     TINYINT UNSIGNED NOT NULL DEFAULT 0,
  slot          TINYINT UNSIGNED NOT NULL,
  PRIMARY KEY (quest_id, slot)
);

CREATE TABLE character_quest_status (
  character_id          BIGINT UNSIGNED NOT NULL,
  quest_id              INT UNSIGNED NOT NULL,
  state                 TINYINT UNSIGNED NOT NULL,
  accepted_ts           BIGINT NOT NULL DEFAULT 0,
  objective_progress    BLOB,                          -- serialized vector<uint32>
  PRIMARY KEY (character_id, quest_id)
);
```

### Configuration (`config.json`)

```json
"quests": {
  "max_active_quests_per_player": 25,
  "daily_reset_hour_utc": 7,
  "weekly_reset_day": "Tuesday",
  "auto_complete_grace_sec": 5
}
```

### Tests

- `QuestManagerTests.cpp` — accept + progress + complete + reward.
- `QuestObjectiveVariantTests.cpp` — chaque type d'objectif progress correctement.
- `QuestStatusPersistenceTests.cpp` — load/save round-trip.
- `QuestChainTests.cpp` — `prevQuestId` non terminé → quête suivante invisible.

## Structure & chemins (verrouillé)

- Code moteur : uniquement sous `/engine`
- ❌ Interdit : créer un dossier racine non autorisé

## Spécification technique

### 1. Variant d'objectifs

Modernisation par rapport à server-core (qui utilise des arrays parallèles
`RequiredItemId[4]` / `RequiredItemCount[4]`). En C++20 `std::variant`
avec `std::visit` permet :

```cpp
void QuestManager::OnKill(Player& p, Creature& c) {
  for (auto& [questId, status] : p.GetQuestStatus()) {
    if (status.state != QuestState::Incomplete) continue;
    auto* tmpl = GetTemplate(questId);
    for (size_t i = 0; i < tmpl->objectives.size(); ++i) {
      std::visit([&](auto&& obj) {
        using T = std::decay_t<decltype(obj)>;
        if constexpr (std::is_same_v<T, KillCreditObjective>) {
          if (obj.creatureEntry == c.GetEntry() && status.objectiveProgress[i] < obj.requiredCount) {
            status.objectiveProgress[i]++;
            NotifyClient(p, questId, i, status.objectiveProgress[i]);
          }
        }
      }, tmpl->objectives[i]);
    }
  }
}
```

Avantages :
- Type-safe : le compilateur garantit qu'on traite chaque variant case.
- Pas de switch/cast.
- Extensible : ajouter `EscortObjective` = ajouter un type au variant + une branche `if constexpr`.

### 2. Flags bitmask

```cpp
enum class QuestFlags : uint32 {
  None         = 0,
  Daily        = 1u << 0,
  Weekly       = 1u << 1,
  Repeatable   = 1u << 2,
  AutoComplete = 1u << 3,    // pas besoin de retour au quest giver
  PartyShared  = 1u << 4,    // tous les membres en groupe progress
  Sharable     = 1u << 5,    // partageable avec d'autres
};
```

### 3. Persistance

Sérialiser `objectiveProgress` (vector<uint32>) en BLOB (4 bytes × N
objectifs, max ~16 = 64 bytes) ou JSON. **BLOB** plus compact.

### 4. Reset Daily/Weekly

Tâche cron côté master :
- À `daily_reset_hour_utc` UTC : pour chaque `QuestStatusData` avec `Daily` et `state == Rewarded`, reset à `Available`.
- Le mardi à `daily_reset_hour_utc` : idem pour `Weekly`.

### 5. Master vs Shard

- `QuestTemplate` : chargé sur master ET shard (même DB).
- `QuestStatusData` : per-character, vit côté master (entre les sessions) MAIS doit être consulté côté shard pour les hooks `OnKill`/`OnItemAdded`. Solution : à `EnterWorld`, master push le QuestStatusData du joueur au shard. Modifications côté shard syncées avec master (write-through).

## Étapes d'implémentation

1. Créer `engine/server/quests/`.
2. Migrations DB.
3. Implémenter `QuestTemplate` + `QuestStatusData`.
4. Implémenter `QuestManager::Load` (master+shard).
5. Implémenter `TryAccept` / `TryComplete` / `TryReward`.
6. Implémenter hooks `OnKill` / `OnItemAdded` / `OnGameObjectInteract`.
7. Implémenter sync master ↔ shard (à coordonner avec SERVER-CORE.02).
8. Implémenter daily/weekly reset (master cron).
9. Tests : 4 fichiers.
10. Doc : section « Quests cross master+shard » dans `CODEBASE_MAP.md`.

## Definition of Done (DoD)

- [ ] Build Linux OK (master + shard)
- [ ] Tests passent
- [ ] Smoke test : accept quête "Tuer 5 sangliers" → kill 5 sangliers → état Complete → reward
- [ ] Daily quest re-disponible au reset 7h UTC
- [ ] Quest chain : prev non terminé → next invisible
- [ ] Persistance : status survit reboot shard et master
- [ ] Migrations idempotentes
- [ ] Aucun dossier racine non autorisé
- [ ] Rapport final

## Notes / pièges à éviter

- **Variant overhead** : `std::variant` a un coût léger (tag + payload). Si profilage montre une regression sur `OnKill` à haute charge, considérer un dispatch manuel via enum + struct uniforme.
- **Quest accept conditions** : 5 checks séquentiels (level, prev quest, condition, max_active, available). Tous au même endroit (`TryAccept`), pas dispersés.
- **Quest sharing** : un joueur en groupe peut partager une `Sharable` quest. Implique re-vérifier conditions pour chaque membre.
- **Auto-complete** : quêtes simples sans objectifs (« Visite le donjon ») se complètent automatiquement à l'acceptation. UX : l'animation "quest accepted" avant le "quest complete" doit être lisible côté client (délai `auto_complete_grace_sec`).
- **PartyShared progress** : kill credit pour tous les membres en groupe dans le rayon X. Hook côté shard : `OnKill` parcourt le groupe, pas juste le killer.
- **Quest giver questlist** : un PNJ peut avoir plusieurs quêtes. Le client demande la liste, le serveur retourne celles éligibles (level, prev, condition).
- **Reward inventory full** : si l'inventaire est plein au moment de prendre la reward, **ne pas** dropper l'item au sol. Refuser et demander de libérer de la place.
- **Choice item** : envoyer la liste au client, attendre `kOpcodeQuestChoiceReward(choiceIdx)`, valider l'index, donner UNIQUEMENT cet item. Sinon exploit (donner tous).

## Références

- `SERVER-CORE_ANALYSIS.md` § Quests (P2 cross)
- server-core `src/game/Quests/Quest.cpp`, `QuestHandler.cpp`,
  `QuestDef.h`
