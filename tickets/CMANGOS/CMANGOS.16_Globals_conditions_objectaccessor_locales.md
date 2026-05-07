# CMANGOS.16_Globals_conditions_objectaccessor_locales

## Objectif

Mettre en place la **couche de globaux et de prédicats data-driven**
côté shard LCDLLN, inspirée de `src/game/Globals` cmangos. Quatre
piliers :

1. **`Conditions` data-driven** : un mini-DSL pour exprimer des
   prédicats (« a tel buff », « est en groupe », « niveau ≥ X ») avec
   composition AND/OR/NOT. Réutilisé par quêtes, loot, scripts,
   events. **Évite mille `if` en C++** disséminés dans le code métier.
2. **`ObjectAccessor`** : façade thread-safe pour
   `GetPlayer(guid)`/`GetCreature(guid)`/`GetGameObject(guid)`. Évite
   que chaque système maintienne sa propre map.
3. **`GraveyardManager`** : table de points de respawn liée aux zones +
   faction, requêtable par « plus proche graveyard valide pour ce
   joueur ».
4. **`Locales`** : strings localisées chargées en RAM par
   `(stringId, locale)`, fallback sur la locale par défaut. Pour les
   broadcasts serveur, system messages, noms de PNJ.

C'est un **P2 shard**, et le système Conditions est un game-changer pour
tout le contenu data-driven.

## Dépendances

- M00.1 (build base)
- CMANGOS.13 (Database — SQLStorage pour cache)
- Pré-requis pour : CMANGOS.17 (Loot), CMANGOS.23 (Quests), CMANGOS.14 (DBScripts), CMANGOS.07 (AI/EventAI), CMANGOS.26 (Spells)

## Livrables

### Côté shard (`engine/server/shard/globals/`)

- `Condition.h` — struct + enum :
  ```cpp
  enum class ConditionType : uint8 {
    None,
    HasAura,            // value1 = aura_spell_id
    LevelGE,            // value1 = min_level
    LevelLE,            // value1 = max_level
    InGroup,
    InRaid,
    InGuild,            // value1 = guild_id (0 = any)
    HasItem,            // value1 = item_id, value2 = count
    QuestState,         // value1 = quest_id, value2 = QuestState enum
    ZoneId,             // value1 = zone_id
    Reputation,         // value1 = faction_id, value2 = min_rank
    HealthPctBelow,     // value1 = pct
    NotInCombat,
    Custom,             // value1 = handler_id pour scripts spéciaux
  };
  struct Condition {
    uint32_t conditionId;
    ConditionType type;
    int32_t value1, value2, value3;
  };
  ```
- `ConditionGroup.h` — composition logique :
  ```cpp
  enum class ConditionLogic : uint8 { And, Or, Not };
  struct ConditionGroup {
    uint32_t groupId;
    ConditionLogic logic;
    std::vector<uint32_t> conditionIds;     // ou subgroup ids
  };
  ```
- `ConditionMgr.{h,cpp}` — singleton :
  - `Load(ConnectionPool&)` — charge `conditions` + `condition_groups`.
  - `bool Evaluate(uint32_t conditionId, Player const* source, WorldObject const* target = nullptr) const`.
- `ObjectAccessor.{h,cpp}` — façade :
  - `Player* GetPlayer(ObjectGuid)`
  - `Creature* GetCreature(MapId, ObjectGuid)`
  - Thread-safe via `std::shared_mutex` autour des registries.
- `GraveyardManager.{h,cpp}` — :
  - `Load(ConnectionPool&)`
  - `Vector3 ClosestGraveyard(MapId, Vector3 pos, FactionId faction)`
- `LocaleStrings.{h,cpp}` — cache `(stringId, localeId) → string`
  + `GetString(stringId, localeId)` avec fallback.

### Migration DB

```sql
CREATE TABLE conditions (
  condition_id  INT UNSIGNED NOT NULL,
  type          TINYINT UNSIGNED NOT NULL,
  value1        INT NOT NULL DEFAULT 0,
  value2        INT NOT NULL DEFAULT 0,
  value3        INT NOT NULL DEFAULT 0,
  description   VARCHAR(255),
  PRIMARY KEY (condition_id)
);

CREATE TABLE condition_groups (
  group_id      INT UNSIGNED NOT NULL,
  logic         TINYINT UNSIGNED NOT NULL,    -- And/Or/Not
  member_id     INT UNSIGNED NOT NULL,        -- condition_id ou autre group_id
  member_type   TINYINT UNSIGNED NOT NULL,    -- 0=condition, 1=group
  PRIMARY KEY (group_id, member_id, member_type)
);

CREATE TABLE graveyards (
  id          INT UNSIGNED NOT NULL,
  map_id      INT UNSIGNED NOT NULL,
  position_x  FLOAT NOT NULL,
  position_y  FLOAT NOT NULL,
  position_z  FLOAT NOT NULL,
  faction     TINYINT UNSIGNED NOT NULL DEFAULT 0,    -- 0 = neutral
  zone_id     INT UNSIGNED NOT NULL DEFAULT 0,
  PRIMARY KEY (id)
);

CREATE TABLE locale_strings (
  string_id   INT UNSIGNED NOT NULL,
  locale_id   TINYINT UNSIGNED NOT NULL,    -- 0=fr_FR (default LCDLLN), 1=en_US, ...
  text        TEXT NOT NULL,
  PRIMARY KEY (string_id, locale_id)
);
```

### Configuration (`config.json`)

```json
"globals": {
  "default_locale": "fr_FR",
  "fallback_locale": "fr_FR",
  "graveyard_default_faction_neutral_radius_m": 500.0
}
```

### Tests

- `ConditionMgrTests.cpp` — chaque ConditionType simple ; combine via group AND/OR.
- `ObjectAccessorTests.cpp` — get/set, lookup multi-thread.
- `GraveyardManagerTests.cpp` — closest graveyard avec filtrage faction.
- `LocaleStringsTests.cpp` — fallback locale manquante.

## Structure & chemins (verrouillé)

- Code moteur : uniquement sous `/engine`
- ❌ Interdit : créer un dossier racine non autorisé

## Spécification technique

### 1. ConditionMgr usage

```cpp
// Loot drop conditionnel : un item ne tombe que si le joueur a la quête active.
auto* lootEntry = GetLootEntry(...);
if (lootEntry->condition_id != 0
 && !g_conditions.Evaluate(lootEntry->condition_id, looter)) {
  // skip
}
```

```cpp
// Quest accept : ne propose la quête que si conditions remplies.
if (!g_conditions.Evaluate(quest.required_condition_id, player)) {
  return false;  // not eligible
}
```

### 2. ObjectAccessor cache

```cpp
class ObjectAccessor {
public:
  Player* FindPlayer(ObjectGuid guid);  // par GUID
  Player* FindPlayerByName(std::string_view name);
  Creature* FindCreature(MapId mapId, ObjectGuid guid);
private:
  std::unordered_map<ObjectGuid, Player*> m_players;
  mutable std::shared_mutex m_mutex;
};
```

Inscription auto au login, désinscription au logout.

### 3. GraveyardManager

À la mort d'un joueur, on cherche le graveyard valide (faction
correspondante OU neutre dans un rayon) le plus proche.

```cpp
Vector3 ClosestGraveyard(MapId mapId, Vector3 pos, FactionId faction) {
  auto candidates = m_graveyards[mapId];
  // filter par faction (ou neutre)
  // tri par distance
  return closest;
}
```

### 4. Locales

Format de string avec placeholders :
```
"Bienvenue {0}, niveau {1}!"
```

Helper `Format(stringId, locale, args...)` interpolant les `{N}`.

## Étapes d'implémentation

1. Créer `engine/server/shard/globals/`.
2. Migration DB des 4 tables.
3. Implémenter `ConditionMgr` + 5 ConditionTypes initiaux.
4. Implémenter `ObjectAccessor`.
5. Implémenter `GraveyardManager`.
6. Implémenter `LocaleStrings`.
7. Tests : 4 fichiers.
8. Doc : section « Globals shard » dans `CODEBASE_MAP.md`.

## Definition of Done (DoD)

- [ ] Build Linux OK (shard)
- [ ] Tests passent
- [ ] Smoke test : créer condition `LevelGE 10`, l'évaluer pour un Player niveau 5 → false ; niveau 15 → true
- [ ] Smoke test composition : `(LevelGE 10) AND (HasAura 100)` filtre correctement
- [ ] `ObjectAccessor::FindPlayer(guid)` retourne le bon Player en multi-thread
- [ ] Closest graveyard sur 3 candidats donne le bon (test avec coordonnées connues)
- [ ] LocaleStrings : si `fr_FR` manquant, fallback `default_locale`
- [ ] Migrations idempotentes
- [ ] Aucun dossier racine non autorisé
- [ ] Rapport final

## Notes / pièges à éviter

- **Performance ConditionMgr** : `Evaluate` peut être appelé des milliers de fois par tick (loot, AoE). **Pas de virtual call** — switch sur ConditionType directement.
- **Chargement ConditionMgr** : 10k+ conditions OK, 1M = problème. Si LCDLLN scale, paginer/lazy-load.
- **Cache ObjectAccessor** : invalider au logout est critique. Sinon `FindPlayer` retourne un pointeur dangling.
- **Locale fallback chain** : ne pas faire `fr_FR → en_US → fail`. Faire `fr_FR → default_locale → string_id_as_text` (pour ne jamais retourner empty).
- **Conditions composables** : démarrer avec AND/OR/NOT simples. Si vous voulez `EXACTLY_N_OF (a, b, c, d) >= 2`, c'est un autre niveau de complexité — le reporter.
- **Custom condition handler** : pour les cas non prévus, `ConditionType::Custom + handler_id` permet d'enregistrer un foncteur en C++. À utiliser sparingly, sinon on perd le data-driven.
- **Thread safety** : `ObjectAccessor` partagé entre Map threads — `shared_mutex` (multiple readers, exclusive writer). `ConditionMgr` est read-only post-load, donc lock-free.

## Références

- `CMANGOS_ANALYSIS.md` § Globals (P2 shard)
- cmangos `src/game/Globals/ObjectMgr.cpp`, `ObjectAccessor.cpp`,
  `Conditions.cpp`, `GraveyardManager.cpp`, `Locales.cpp`
