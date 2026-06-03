# SERVER-CORE.40_Tools_playerdump_cleaner_formulas_language

## Objectif

Mettre en place 4 utilitaires d'administration côté master+shard
LCDLLN, inspirés de `src/game/Tools` server-core. Quatre piliers :

1. **`PlayerDump` format texte** : sérialisation human-readable d'un
   perso entier (perso + items + quêtes + skills + …) → restorable sur
   un autre shard. Pattern de migration inter-shard très propre.
2. **`CharacterDatabaseCleaner` périodique** : scan des orphelins
   (item sans owner, mail sans expéditeur) en tâche async.
3. **`Formulas.h` centralisé** : XP per kill, money loot, level
   penalty… une seule source de vérité, facile à équilibrer.
4. **`Language.h` server-side** : i18n côté serveur (broadcasts,
   commandes GM messages).

C'est un **P3 cross**.

## Dépendances

- M00.1 (build base)
- SERVER-CORE.13 (Database)
- SERVER-CORE.16 (Globals — `LocaleStrings` est l'équivalent runtime)

## Livrables

### Côté master (`engine/server/tools/`)

- `PlayerDump.{h,cpp}` :
  - `std::string Dump(uint64_t characterId)` — sérialise tout en texte (JSON ou format custom).
  - `bool Restore(std::string_view dump, uint32_t newAccountId)` — applique sur DB.
- `CharacterDatabaseCleaner.{h,cpp}` :
  - `void RunFullScan()` — async, balaie les tables, identifie orphelins.
  - `void DeleteOrphans()` — applique le cleanup.
- `Formulas.h` (header-only) :
  ```cpp
  namespace lcdlln::formulas {
    int32_t XpForKill(int playerLevel, int targetLevel);
    int64_t MoneyDropAt(int targetLevel);
    int32_t LevelPenaltyXp(int playerLevel, int targetLevel);
    float CritChanceFromAgility(float agility);
    // ...
  }
  ```
- `ServerLanguage.{h,cpp}` :
  - Charge `server_string` (i18n côté serveur) depuis DB.
  - `std::string Get(uint32_t stringId, std::string_view locale)`.

### Outil offline (`tools/player_migration/`)

- `tools/player_migration/player_migration.cpp` — outil
  `lcdlln_player_migration` qui :
  - lit un dump JSON,
  - se connecte à un autre shard DB,
  - restaure le perso.

### Migration DB

```sql
CREATE TABLE server_string (
  string_id   INT UNSIGNED NOT NULL,
  locale_id   TINYINT UNSIGNED NOT NULL,
  text        TEXT NOT NULL,
  PRIMARY KEY (string_id, locale_id)
);
```

### Configuration (`config.json`)

```json
"tools": {
  "db_cleanup_interval_hours": 24,
  "player_dump_format_version": 1,
  "default_server_locale": "fr_FR"
}
```

### Tests

- `PlayerDumpTests.cpp` — round-trip dump/restore.
- `DbCleanerTests.cpp` — scan détecte orphelins synthétiques.
- `FormulasTests.cpp` — quelques cas : XP normal, level penalty.

## Structure & chemins (verrouillé)

- Code moteur : uniquement sous `/engine`
- Outils offline : uniquement sous `/tools` (`tools/player_migration/`)
- ❌ Interdit : créer un dossier racine non autorisé

## Spécification technique

### 1. PlayerDump format

JSON (ou JSONL) human-readable :

```json
{
  "version": 1,
  "exported_at": "2026-05-07T12:34:56Z",
  "character": { "id": 123, "name": "Toto", "level": 10, "x": ..., "y": ..., "z": ... },
  "items": [ { "guid": ..., "entry": ..., "slot": ..., "count": ... }, ... ],
  "quest_status": [ ... ],
  "spells_known": [ ... ],
  "social": { "friends": [...], "ignored": [...] },
  "reputation": [ ... ],
  "money": 12345
}
```

Restore = transactional : tout-ou-rien. En cas de conflit (charId déjà
présent), reject + log.

### 2. CharacterDatabaseCleaner

Detecting orphans :

- `mail_items` row sans `mail` correspondant.
- `character_inventory` row pointant vers un `item_instance` qui n'existe pas.
- `quest_status` pour un `character_id` supprimé.

Tâche async via SqlDelayThread (SERVER-CORE.13). Cron quotidien.

### 3. Formulas centralisé

**Pas** de magic numbers dispersés. Tout dans un header lu par toutes
les couches. Un changement d'équilibrage = 1 fichier modifié.

### 4. ServerLanguage

Pour les broadcasts type :
- "Le serveur va redémarrer dans 5 minutes" → multi-locale.
- Messages d'erreur GM commands en français.
- Messages système (welcome, MOTD).

## Étapes d'implémentation

1. Créer `engine/server/tools/`.
2. Implémenter `PlayerDump` (export/import).
3. Implémenter `CharacterDatabaseCleaner` (cron).
4. Créer `Formulas.h`.
5. Migration DB `server_string` + impl `ServerLanguage`.
6. Créer `tools/player_migration/`.
7. Tests : 3 fichiers.
8. Doc : section « Admin tools » dans `CODEBASE_MAP.md`.

## Definition of Done (DoD)

- [ ] Build Linux OK (master + outil player_migration)
- [ ] Tests passent
- [ ] Smoke test : dump perso → restore sur DB séparée → contenu identique
- [ ] Cleaner détecte orphelins synthétiques + delete
- [ ] Migrations idempotentes
- [ ] Aucun dossier racine non autorisé (sauf `/tools/player_migration/`)
- [ ] Rapport final

## Notes / pièges à éviter

- **Dump format versioning** : inclure version dans le JSON. Restore refuse les versions inconnues avec un message clair.
- **Item GUID conflict** : à la restore, soit re-issuer de nouveaux GUIDs (sécurisé), soit utiliser les GUIDs originaux (cohérent mais conflit possible). Choisir : **re-issue** par défaut.
- **Cleaner timing** : pas pendant peak hours. Cron à 4h du matin UTC.
- **ServerLanguage vs LocaleStrings** : les 2 patterns coexistent. Choisir clairement : LocaleStrings pour les noms d'items/PNJ, ServerLanguage pour les broadcasts/GM messages. Documenter.

## Références

- `SERVER-CORE_ANALYSIS.md` § Tools (P3 cross)
- server-core `src/game/Tools/PlayerDump.cpp`,
  `CharacterDatabaseCleaner.cpp`, `Formulas.h`, `Language.h`
